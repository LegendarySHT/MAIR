#!/usr/bin/env python3
"""
Extensible Automatic Shadow Memory Allocation for Multiple Sanitizers using Z3 SMT Solver
Supports flexible sanitizer and platform configuration.

Enhanced with CLI (--platform, --mode, --output, --outdir, --align, --max-solutions)
and a header-emission mode that writes a C++ header with generated constants.

The header contains a timestamp and the exact command line used to generate it,
plus comment blocks automatically listing the computed memory layout.
"""

from z3 import *
import z3
import sys
from dataclasses import dataclass, field
from typing import List, Dict, Tuple, Optional, Callable
from abc import ABC, abstractmethod
import argparse
import os
from pathlib import Path
from datetime import datetime
from itertools import combinations
import shlex


# Monkey-patch Z3 BitVecRef for convenience
if not hasattr(z3.BitVecRef, "__floordiv__"):

    def _bitvec_floordiv(self, other):
        if isinstance(other, int):
            other = z3.BitVecVal(other, self.size())
        return z3.UDiv(self, other)

    z3.BitVecRef.__floordiv__ = _bitvec_floordiv

z3.BitVecRef.__lt__ = lambda self, other: ULT(self, other)
z3.BitVecRef.__le__ = lambda self, other: ULE(self, other)
z3.BitVecRef.__gt__ = lambda self, other: UGT(self, other)
z3.BitVecRef.__ge__ = lambda self, other: UGE(self, other)


@dataclass
class SanitizerPlatformConfig:
    """Platform-specific configuration for a sanitizer"""

    pass


@dataclass
class ASanPlatformConfig(SanitizerPlatformConfig):
    """ASan platform-specific configuration"""

    shadow_offset: int
    shadow_scale: int = 3


@dataclass
class MSanPlatformConfig(SanitizerPlatformConfig):
    """MSan platform-specific configuration"""

    xor_alignment: int = 0x1000_0000_0000


@dataclass
class TSanPlatformConfig(SanitizerPlatformConfig):
    """TSan platform-specific configuration"""

    shadow_mask: int
    # That many user bytes are mapped onto a single shadow cell.
    shadow_cell: int = 8
    # Count of shadow values in a shadow cell.
    shadow_count: int = 4
    # Size of a single shadow value (u32/RawShadow).
    shadow_size: int = 4
    # Shadow memory is kShadowMultiplier times larger than user memory.
    # Default value = 2
    shadow_multiplier: int = shadow_count * shadow_size // shadow_cell
    # That many user bytes are mapped onto a single meta shadow cell.
    # Must be less or equal to minimal memory allocator alignment.
    meta_shadow_cell: int = 8
    # Size of a single meta shadow value (u32).
    meta_shadow_size: int = 4
    # Meta shadow memory's alignment.
    meta_alignment: int = 0x1000_0000_0000


@dataclass
class PlatformConfig:
    """Platform-specific configuration"""

    # name is always required
    name: str

    # These fields must be specified (no default)
    lo_app_mem_beg: int
    lo_app_mem_end: int
    # Use a loose app mem end for shadow calculation.
    lo_app_mem_end_loose: int  # Used for sanitizers except ASan
    # Related to PIE address, must be specified
    mid_app_beg: int
    hi_app_end: int
    vdso_beg: int

    # hi_app_beg and hi_app_beg_hint: exactly one must be set (not -1)
    hi_app_beg: int = -1
    # Auto-calc hi_app_beg with hi_app_beg >= hi_app_beg_hint
    hi_app_beg_hint: int = 0

    # Optional/defaulted fields
    mid_app_end: int = -1
    heap_beg: int = -1
    heap_end: int = -1
    alignment: int = 0x0100_0000_0000
    min_mid_app_size: int = 0
    min_hi_app_size: int = 0
    min_heap_size: int = 0

    # Sanitizers' Mappers
    sanitizer_mappers: List["ShadowMapper"] = field(default_factory=list)

    def __post_init__(self):
        # hi_app_beg 与 hi_app_beg_hint 必须且只能有一个显式设置（非-1）
        hi_app_beg_is_set = self.hi_app_beg != -1
        hi_app_beg_hint_is_set = self.hi_app_beg_hint != 0
        if (
            hi_app_beg_is_set == hi_app_beg_hint_is_set
            and self.hi_app_beg < self.hi_app_beg_hint
        ) or (not hi_app_beg_is_set and not hi_app_beg_hint_is_set):
            raise ValueError(
                f'Exactly one of hi_app_beg or hi_app_beg_hint must be set explicitly for platform "{self.name}" '
                f"(current: hi_app_beg={self.hi_app_beg}, hi_app_beg_hint={self.hi_app_beg_hint})"
            )
        # Check if the manually specified app regions are large enough to contain the minimum size
        app_regions = [
            (
                getattr(self, "mid_app_beg", -1),
                getattr(self, "mid_app_end", -1),
                getattr(self, "min_mid_app_size", 0),
                "mid_app",
            ),
            (
                self.hi_app_beg,
                getattr(self, "hi_app_end", -1),
                getattr(self, "min_hi_app_size", 0),
                "hi_app",
            ),
            (
                getattr(self, "heap_beg", -1),
                getattr(self, "heap_end", -1),
                getattr(self, "min_heap_size", 0),
                "heap",
            ),
        ]
        for beg, end, min_size, region_name in app_regions:
            is_fixed = beg != -1 and end != -1
            if not is_fixed or min_size == 0:
                continue
            region_size = end - beg
            if min_size > region_size:
                raise ValueError(
                    f"[{self.name}] Both segment {region_name} and min_{region_name}_size are manually specified, but conflicts:\n"
                    f"\treal_{region_name}_size={region_size:#x} < min_{region_name}_size={min_size:#x} "
                    f"({region_name}_beg=0x{beg:x}, {region_name}_end=0x{end:x})"
                )
        if hi_app_beg_is_set:
            self.hi_app_beg_hint = self.hi_app_beg


def format_cpp_int(val):
    """Format integer as C++ style, split every 4 hex digits with _."""
    s = f"{val:x}".rjust(12, "0")
    segments = []
    while s:
        segments.insert(0, s[-4:])
        s = s[:-4]
    return "'".join(segments)


@dataclass
class ShadowRegion:
    """Represents a shadow memory region"""

    name: str
    sanitizer: str
    region_type: str  # "shadow", "origin", "meta", etc.
    app_region_name: str  # Which app region this shadows
    beg: any  # Can be BitVec or int
    end: any

    def __repr__(self):
        return f"{self.sanitizer} {self.region_type} for {self.app_region_name}"


class ShadowMapper(ABC):
    """Abstract base class for shadow mapping strategies"""

    def __init__(
        self,
        platform_config: SanitizerPlatformConfig,
        app_regions: List[Tuple[any, any, str]],
    ):
        self.platform_config = platform_config
        self.app_regions = app_regions

    def get_all_regions(self) -> List[ShadowRegion]:
        """Generate all shadow regions for the platform"""
        all_regions = []
        for app_beg, app_end, app_name in self.app_regions:
            regions = self.get_regions(app_beg, app_end, app_name)
            all_regions.extend(regions)
        return all_regions

    @abstractmethod
    def get_regions(self, app_beg, app_end, app_name: str) -> List[ShadowRegion]:
        """Generate shadow regions for the given app memory region"""
        pass

    @abstractmethod
    def get_parameters(self) -> Dict[str, any]:
        """Get the sanitizer-specific parameters (variables or constants)"""
        pass

    @abstractmethod
    def add_constraints(self, optimizer: Optimize, platform: PlatformConfig):
        """Add sanitizer-specific constraints"""
        pass

    def get_parameters_as_cpp_code(self, model, in_header_file=False) -> str:
        """Print sanitizer-specific parameters as C++ code"""
        params = self.format_parameters(model)
        san_name = type(self).__name__.replace("Mapper", "")
        indent = "  " if in_header_file else ""
        code = f"{indent}// {san_name} Parameters:\n"
        for key, value in params.items():
            if isinstance(value, int):
                code += f"{indent}static constexpr const uintptr {key} = 0x{format_cpp_int(value)}ull;\n"
        return code

    def format_parameters(self, model) -> Dict[str, int]:
        """Extract and format parameters from Z3 model"""
        params = self.get_parameters()
        if isinstance(model, dict):
            # Model is a dictionary
            return {key: model[key] for key, _ in params.items()}
        else:
            # Model is a Z3 Solver's model
            return {
                key: val if isinstance(val, int) else model.eval(val).as_long()
                for key, val in params.items()
            }


class ASanMapper(ShadowMapper):
    """AddressSanitizer shadow mapper"""

    def __init__(
        self, config: ASanPlatformConfig, app_regions: List[Tuple[any, any, str]]
    ):
        super().__init__(config, app_regions)
        self.config = config

    def get_regions(self, app_beg, app_end, app_name: str) -> List[ShadowRegion]:
        # ASan uses a fixed global shadow, not per-region
        return []

    def _mem_to_shadow(self, mem):
        return (mem >> self.config.shadow_scale) + self.config.shadow_offset

    def get_global_regions(self) -> List[ShadowRegion]:
        """ASan has a single global shadow region (not corresponding to each app region)"""
        hiapp_region = next(
            filter(lambda item: item[2] == "HiApp", self.app_regions), None
        )
        assert hiapp_region is not None, "HiApp region is not found"
        _, hiapp_end, _ = hiapp_region
        lo_shadow_beg = self.config.shadow_offset
        lo_shadow_end = self._mem_to_shadow(lo_shadow_beg)
        rest_shadow_end = self._mem_to_shadow(hiapp_end)
        rest_shadow_beg = self._mem_to_shadow(rest_shadow_end)
        return [
            ShadowRegion(
                name="ASan Shadow (LoApp)",
                sanitizer="ASan",
                region_type="shadow",
                app_region_name="LoApp",
                beg=lo_shadow_beg,
                end=lo_shadow_end,
            ),
            ShadowRegion(
                name="ASan Shadow (Rest)",
                sanitizer="ASan",
                region_type="shadow",
                app_region_name="Rest",
                beg=rest_shadow_beg,
                end=rest_shadow_end,
            ),
        ]

    def get_parameters(self) -> Dict[str, any]:
        return {
            "kAsanShadowOffset": self.config.shadow_offset,
            "kAsanShadowScale": self.config.shadow_scale,
        }

    def add_constraints(self, optimizer: Optimize, platform: PlatformConfig):
        # ASan uses fixed values, no constraints needed
        pass

    def get_parameters_as_cpp_code(self, model, in_header_file=False) -> str:
        """Print sanitizer-specific parameters as C++ code"""
        params = self.format_parameters(model)
        align = "  " if in_header_file else ""
        code = f"\n{align}// ASan Parameters:\n"
        for key, value in params.items():
            """
            Overwrite print_parameters_as_cpp_code for custom print
            """
            if key == "kAsanShadowScale":
                code += f"{align}static constexpr const uintptr {key} = {value};\n"
                continue
            if isinstance(value, int):
                code += f"{align}static constexpr const uintptr {key} = 0x{format_cpp_int(value)}ull;\n"
        return code


class MSanMapper(ShadowMapper):
    """MemorySanitizer shadow mapper"""

    def __init__(
        self, config: MSanPlatformConfig, app_regions: List[Tuple[any, any, str]]
    ):
        super().__init__(config, app_regions)
        self.config = config
        self.kMSanShadowXor = BitVec("kMSanShadowXor", 64)
        self.kMSanShadowAdd = BitVec("kMSanShadowAdd", 64)

    def _mem_to_shadow(self, mem, is_end: bool = False):
        if is_end:
            mem = mem - 1 if isinstance(mem, int) else mem - 1
        shadow = mem ^ self.kMSanShadowXor
        if is_end:
            shadow = shadow + 1 if isinstance(shadow, int) else shadow + 1
        return shadow

    def _mem_to_origin(self, mem, is_end: bool = False):
        return self._mem_to_shadow(mem, is_end) + self.kMSanShadowAdd

    def get_regions(self, app_beg, app_end, app_name: str) -> List[ShadowRegion]:
        shadow_beg = self._mem_to_shadow(app_beg, is_end=False)
        shadow_end = self._mem_to_shadow(app_end, is_end=True)

        origin_beg = self._mem_to_origin(app_beg, is_end=False)
        origin_end = self._mem_to_origin(app_end, is_end=True)

        return [
            ShadowRegion(
                name=f"MSan Shadow ({app_name})",
                sanitizer="MSan",
                region_type="shadow",
                app_region_name=app_name,
                beg=shadow_beg,
                end=shadow_end,
            ),
            ShadowRegion(
                name=f"MSan Origin ({app_name})",
                sanitizer="MSan",
                region_type="origin",
                app_region_name=app_name,
                beg=origin_beg,
                end=origin_end,
            ),
        ]

    def get_parameters(self) -> Dict[str, any]:
        return {
            "kMSanShadowXor": self.kMSanShadowXor,
            "kMSanShadowAdd": self.kMSanShadowAdd,
        }

    def add_constraints(self, optimizer: Optimize, platform: PlatformConfig):
        optimizer.add(self.kMSanShadowAdd < platform.hi_app_beg_hint)
        optimizer.add(self.kMSanShadowXor % self.config.xor_alignment == 0)
        optimizer.add(self.kMSanShadowAdd % platform.alignment == 0)


class TSanMapper(ShadowMapper):
    """ThreadSanitizer shadow mapper"""

    def __init__(
        self, config: TSanPlatformConfig, app_regions: List[Tuple[any, any, str]]
    ):
        super().__init__(config, app_regions)
        self.config = config

        # TODO: reduce such implicit constraints via change RestoreAddr
        # TSan uses bits [41:44] to restore a compressed address to the original address.
        # That means, the app region must be distinguishable by the [41:44] bits.
        # Unlike shadow_mask, which only requires the app region distinguishable by the
        # [0:ctz(shadow_mask)] bits, e.g., here, [0:44] or [0:43] or [0:45] or [0:46]
        self.indicator = 0x0E00_0000_0000
        self.kTsanShadowXor = BitVec("kTsanShadowXor", 64)
        self.kTsanShadowAdd = BitVec("kTsanShadowAdd", 64)
        self.kTsanMetaShadowBeg = BitVec("kTsanMetaShadowBeg", 64)

        # Get the bit length of shadow_mask
        width = self.config.shadow_mask.bit_length()
        self.available_user_space_size = ((~self.config.shadow_mask) + 1) & (
            (1 << width) - 1
        )
        self.kTsanMetaShadowEnd = (
            self.kTsanMetaShadowBeg
            + self.available_user_space_size
            // self.config.meta_shadow_cell
            * self.config.meta_shadow_size
        )

        self.shadow_regions = {}
        self.meta_regions = {}

    def _mem_to_shadow(self, mem, is_end: bool = False):
        if is_end:
            mem = mem - 1 if isinstance(mem, int) else mem - 1
        shadow = (
            (mem & ~(self.config.shadow_mask | (self.config.shadow_cell - 1)))
            ^ self.kTsanShadowXor
        ) * self.config.shadow_multiplier + self.kTsanShadowAdd
        if is_end:
            shadow = shadow + self.config.shadow_cell * self.config.shadow_multiplier
        return shadow

    def _mem_to_meta(self, mem, is_end: bool = False):
        if is_end:
            mem = mem - 1 if isinstance(mem, int) else mem - 1
        meta = (
            (mem & ~(self.config.shadow_mask | (self.config.meta_shadow_cell - 1)))
            // self.config.meta_shadow_cell
            * self.config.meta_shadow_size
        ) | self.kTsanMetaShadowBeg
        if is_end:
            meta = meta + self.config.meta_shadow_size
        return meta

    def get_regions(self, app_beg, app_end, app_name: str) -> List[ShadowRegion]:
        shadow_beg = self._mem_to_shadow(app_beg, is_end=False)
        shadow_end = self._mem_to_shadow(app_end, is_end=True)

        meta_beg = self._mem_to_meta(app_beg, is_end=False)
        meta_end = self._mem_to_meta(app_end, is_end=True)

        shadow_region = ShadowRegion(
            name=f"TSan Shadow ({app_name})",
            sanitizer="TSan",
            region_type="shadow",
            app_region_name=app_name,
            beg=shadow_beg,
            end=shadow_end,
        )
        self.shadow_regions[app_name] = shadow_region

        meta_region = ShadowRegion(
            name=f"TSan Meta ({app_name})",
            sanitizer="TSan",
            region_type="meta",
            app_region_name=app_name,
            beg=meta_beg,
            end=meta_end,
        )
        self.meta_regions[app_name] = meta_region

        return [shadow_region, meta_region]

    def get_parameters(self) -> Dict[str, any]:
        shadow_beg = 0xFFFFFFFFFFFFFFFF
        shadow_end = 0x0000000000000000
        for _, shadow_region in self.shadow_regions.items():
            sbeg = shadow_region.beg
            send = shadow_region.end
            shadow_beg = If(sbeg < shadow_beg, sbeg, shadow_beg)
            shadow_end = If(send > shadow_end, send, shadow_end)
        return {
            "kTsanShadowXor": self.kTsanShadowXor,
            "kTsanShadowAdd": self.kTsanShadowAdd,
            "kTsanShadowMsk": self.config.shadow_mask,
            "kTsanMetaShadowBeg": self.kTsanMetaShadowBeg,
            "kTsanMetaShadowEnd": self.kTsanMetaShadowEnd,
            "kTsanShadowBeg": shadow_beg,
            "kTsanShadowEnd": shadow_end,
        }

    def _add_app_distinguishable_constraints(self, optimizer):
        """
        Add app region distinguishable constraints for all region pairs
        Relevant Code : src/runtime/lib/xsan/tsan/tsan_platform.h
        Relevant Commit:
        https://github.com/llvm/llvm-project/commit/b1338d1e3a8c4b1b4c7364696852f67401fa40ca
        TODO: eliminate this constraint via modify the relevant code in tsan_platform.h
        """
        for (r1_beg, r1_end, _), (r2_beg, r2_end, _) in combinations(
            self.app_regions, 2
        ):
            r1_beg_ind = r1_beg & self.indicator
            r1_end_ind = r1_end & self.indicator
            r2_beg_ind = r2_beg & self.indicator
            r2_end_ind = r2_end & self.indicator
            optimizer.add(Or(r1_end_ind <= r2_beg_ind, r2_end_ind <= r1_beg_ind))

    def add_constraints(self, optimizer: Optimize, platform: PlatformConfig):
        optimizer.add(self.kTsanShadowAdd < platform.hi_app_beg_hint)
        optimizer.add(self.kTsanShadowXor == 0x0000_0000_0000)
        optimizer.add(self.kTsanShadowAdd % platform.alignment == 0)

        optimizer.add(self.kTsanMetaShadowBeg >= platform.lo_app_mem_end_loose)
        optimizer.add(self.kTsanMetaShadowBeg % self.config.meta_alignment == 0)

        # TSan requires the [41:44] bits of the app region to be distinguishable.
        self._add_app_distinguishable_constraints(optimizer)
        # There is no solution under x64_48 with such implicit constraints
        # We specialize the relevant template to support such comment out.
        # self._add_heap_constraints(optimizer)


# Platform configurations
"""
=============================================================================
This section provides the unified XSan platform configuration for all sanitizers.
It covers the division of distinct address-space memory regions alongside
various platform and sanitizer hyperparameters.
When explicit region boundaries are not provided, an SMT solver will be invoked
to automatically solve for address ranges that satisfy all constraints.
The configurable (app) memory regions include:
  - lo_app   : Low address application memory
  - mid_app  : Mid address application memory (often used for PIE or shared libs)
  - hi_app   : High address application memory (stacks, high-PAGE binaries, etc.)
  - heap     : Heap memory regions
For each sanitizer's shadow memory layout, the *SanMapper objects are responsible
for describing the app-region-to-shadow mapping. Concrete per-sanitizer mappers
maintain this mapping and provide any required parameters to the solver. The SMT
solver will then find suitable shadow memory locations and configurations.
As a guiding principle, we should prefer to respect (and thus explicitly configure)
the strictest app memory region boundaries required among all sanitizers to maximize
compatibility. Automatic solving (making certain region boundaries symbolic) is only
attempted for regions that are otherwise unsatisfiable. The general policy follows:
  - *(Must be explicitly configured)*: ASan enforces the strictest requirements for 
    lo_app (low app memory) and protects the last page of lo_app to prevent 
    overwriting its shadow. For uniformity, we set 
       lo_app_end = ASan’s low_app_end - 0x1000 (page_size).
  
  - TSan has the strictest requirements on mid_app and hi_app. Our default is to
    respect these regions as fixed where possible.
  - The heap region is best solved by the SMT; hard-coding it to a sanitizer's
    configuration may otherwise guarantee unsatisfiability.
NOTE: If you encounter unsolvable constraints (e.g. heap overlapping or no
available space after reserving all regions), the solver will attempt "relaxation"
for specific region boundaries, allowing a feasible result when possible.
=============================================================================
"""
PAGE_SIZE = 0x1000
PLATFORMS = {
    "x64_48": PlatformConfig(
        name="MappingX64_48",  # Used for classname in generated header filename
        alignment=0x0100_0000_0000,
        lo_app_mem_beg=0x0000_0000_0000,
        lo_app_mem_end=0x0000_7FFF_8000 - PAGE_SIZE,
        # lo_app_mem_end_loose=0x0000_7fff_8000,
        lo_app_mem_end_loose=0x0100_0000_0000,
        mid_app_beg=0x5500_0000_0000,
        mid_app_end=0x5A00_0000_0000,
        hi_app_beg=0x7A00_0000_0000,
        hi_app_end=0x8000_0000_0000,
        # heap_beg=0x6200_0000_0000,
        # heap_end=0x6400_0000_0000,
        vdso_beg=0xF000_0000_0000_0000,
        min_mid_app_size=0x0500_0000_0000,
        min_hi_app_size=0x0600_0000_0000,
        min_heap_size=0x0200_0000_0000,
        sanitizer_mappers=[
            (
                ASanMapper,
                ASanPlatformConfig(
                    shadow_offset=0x0000_0000_7FFF_8000,
                    shadow_scale=3,
                ),
            ),
            (MSanMapper, MSanPlatformConfig(xor_alignment=0x1000_0000_0000)),
            (
                TSanMapper,
                TSanPlatformConfig(
                    shadow_mask=0x7000_0000_0000,
                    shadow_cell=8,
                    shadow_multiplier=2,
                    meta_shadow_cell=8,
                    meta_shadow_size=4,
                    meta_alignment=0x1000_0000_0000,
                ),
            ),
        ],
    ),
    "aarch64_48": PlatformConfig(
        name="MappingAarch64_48",
        alignment=0x0100_0000_0000,  # 1TB alignment
        lo_app_mem_beg=0x0000_0000_0000,
        lo_app_mem_end=0x0010_0000_0000 - PAGE_SIZE,
        lo_app_mem_end_loose=0x0100_0000_0000,
        mid_app_beg=0xAAAA_0000_0000,
        mid_app_end=0xAC00_0000_0000,
        hi_app_beg=0xFC00_0000_0000,
        hi_app_end=0x1_0000_0000_0000,
        vdso_beg=0x000F_FFF0_0000_0000,
        min_heap_size=0x0200_0000_0000,
        sanitizer_mappers=[
            (
                ASanMapper,
                ASanPlatformConfig(
                    shadow_offset=0x0010_0000_0000,
                    shadow_scale=3,
                ),
            ),
            (MSanMapper, MSanPlatformConfig(xor_alignment=0x1000_0000_0000)),
            (
                TSanMapper,
                TSanPlatformConfig(
                    shadow_mask=0xF000_0000_0000,
                    shadow_cell=8,
                    shadow_multiplier=2,
                    meta_shadow_cell=8,
                    meta_shadow_size=4,
                    meta_alignment=0x1000_0000_0000,
                ),
            ),
        ],
    ),
}


class SanitizerShadowAllocator:
    """Main allocator class with extensible sanitizer support"""

    def __init__(self, platform_name: str, align_hint: Optional[int] = None):
        if platform_name not in PLATFORMS:
            raise KeyError(
                f"Unknown platform '{platform_name}'. Available: {list(PLATFORMS.keys())}"
            )
        self.platform_key = platform_name
        self.platform = PLATFORMS[platform_name]
        if align_hint is not None:
            self.platform.alignment = align_hint
        self.optimizer = Optimize()

        # Application memory regions (variables)
        self.kLoAppMemBeg = BitVec("kLoAppMemBeg", 64)
        self.kLoAppMemEnd = BitVec("kLoAppMemEnd", 64)
        self.kMidAppMemBeg = BitVec("kMidAppMemBeg", 64)
        self.kMidAppMemEnd = BitVec("kMidAppMemEnd", 64)
        self.kHiAppMemBeg = BitVec("kHiAppMemBeg", 64)
        self.kHiAppMemEnd = BitVec("kHiAppMemEnd", 64)
        self.kHeapMemBeg = BitVec("kHeapMemBeg", 64)
        self.kHeapMemEnd = BitVec("kHeapMemEnd", 64)

        # Define app regions
        self.app_regions = [
            (self.kLoAppMemBeg, self.kLoAppMemEnd, "LoApp"),
            (self.kMidAppMemBeg, self.kMidAppMemEnd, "MidApp"),
            (self.kHiAppMemBeg, self.kHiAppMemEnd, "HiApp"),
            (self.kHeapMemBeg, self.kHeapMemEnd, "Heap"),
        ]

        # Sanitizer mappers (extensible list)
        self.sanitizers: List[ShadowMapper] = [
            GenMapper(config, self.app_regions)
            for GenMapper, config in self.platform.sanitizer_mappers
        ]

        # All regions (app + shadow): (beg, end, name, is_app)
        self.all_regions: List[Tuple[any, any, str, bool]] = []
        self.shadow_regions: List[ShadowRegion] = []

    def setup_regions(self):
        """Setup all memory regions including shadows"""
        self.all_regions = [
            (beg, end, name, True) for beg, end, name in self.app_regions
        ]

        # Add global regions (like ASan's fixed shadow)
        for sanitizer in self.sanitizers:
            if hasattr(sanitizer, "get_global_regions"):
                for region in sanitizer.get_global_regions():
                    self.all_regions.append(
                        (region.beg, region.end, region.name, False)
                    )
                    self.shadow_regions.append(region)

        # Generate shadow regions for each app region
        for sanitizer in self.sanitizers:
            regions = sanitizer.get_all_regions()
            for region in regions:
                # only add min/max constraints for Z3 vars (BitVec)
                self._add_min_max_constraints(region.beg, region.end)
                self.all_regions.append((region.beg, region.end, region.name, False))
                self.shadow_regions.append(region)

    def _add_min_max_constraints(self, region_beg, region_end):
        """Add min and max constraints for the given address"""
        opt = self.optimizer
        # if platform.lo_app_mem_end is int, ensure region_beg is larger
        opt.add(self.kLoAppMemEnd < region_beg)
        opt.add(region_beg < region_end)
        opt.add(region_end < self.kHiAppMemBeg)

    def add_basic_constraints(self):
        """Add basic ordering and boundary constraints"""
        opt = self.optimizer

        # Basic ordering
        opt.add(self.kLoAppMemEnd < self.kMidAppMemBeg)
        opt.add(self.kMidAppMemBeg < self.kMidAppMemEnd)
        opt.add(self.kMidAppMemEnd < self.kHiAppMemBeg)
        opt.add(self.kHiAppMemBeg < self.kHiAppMemEnd)

        # Heap constraints
        self._add_min_max_constraints(self.kHeapMemBeg, self.kHeapMemEnd)
        opt.add(self.kHeapMemBeg > self.kMidAppMemEnd)

        # Platform-specific constraints
        opt.add(self.kHiAppMemBeg >= self.platform.hi_app_beg_hint)

        # Optional app region specification
        if self.platform.lo_app_mem_beg != -1:
            opt.add(self.kLoAppMemBeg == self.platform.lo_app_mem_beg)
        if self.platform.lo_app_mem_end_loose != -1:
            opt.add(self.kLoAppMemEnd == self.platform.lo_app_mem_end_loose)
        if self.platform.mid_app_beg != -1:
            opt.add(self.kMidAppMemBeg == self.platform.mid_app_beg)
        if self.platform.mid_app_end != -1:
            opt.add(self.kMidAppMemEnd == self.platform.mid_app_end)
        if self.platform.hi_app_beg != -1:
            opt.add(self.kHiAppMemBeg == self.platform.hi_app_beg)
        if self.platform.hi_app_end != -1:
            opt.add(self.kHiAppMemEnd == self.platform.hi_app_end)
        if self.platform.heap_beg != -1:
            opt.add(self.kHeapMemBeg == self.platform.heap_beg)
        if self.platform.heap_end != -1:
            opt.add(self.kHeapMemEnd == self.platform.heap_end)

    def add_size_constraints(self):
        """Add size constraints"""
        opt = self.optimizer
        opt.add(
            self.kMidAppMemEnd - self.kMidAppMemBeg >= self.platform.min_mid_app_size
        )
        opt.add(self.kHiAppMemEnd - self.kHiAppMemBeg >= self.platform.min_hi_app_size)
        opt.add(self.kHeapMemEnd - self.kHeapMemBeg >= self.platform.min_heap_size)

    def add_alignment_constraints(self):
        """Add alignment constraints"""
        opt = self.optimizer
        for var in [
            self.kMidAppMemBeg,
            self.kMidAppMemEnd,
            self.kHiAppMemBeg,
            self.kHeapMemBeg,
            self.kHeapMemEnd,
        ]:
            remainder = var % self.platform.alignment
            opt.minimize(remainder)

    def add_non_overlap_constraints(self):
        """Add non-overlapping constraints for all region pairs"""
        # LoApp is included in ASan's shadow
        regions_excluded = [
            region for region in self.all_regions if "LoApp" != region[2]
        ]
        for (r1_beg, r1_end, _, _), (r2_beg, r2_end, _, _) in combinations(
            regions_excluded, 2
        ):
            self.optimizer.add(Or(r1_end <= r2_beg, r2_end <= r1_beg))

    def solve(self, max_solutions: int = 1):
        """Solve the constraint system"""
        print(f"Platform: {self.platform_key}")
        print(f"Sanitizers: {', '.join([type(s).__name__ for s in self.sanitizers])}")
        print("Setting up constraints...")

        self.setup_regions()
        self.add_basic_constraints()
        self.add_size_constraints()
        self.add_alignment_constraints()

        # Add sanitizer-specific constraints
        for sanitizer in self.sanitizers:
            sanitizer.add_constraints(self.optimizer, self.platform)

        self.add_non_overlap_constraints()

        print("Solving...")
        results = []
        res = self.optimizer.check()

        if res == sat:
            print("Solution found!")
            model = self.optimizer.model()
            results.append(self.extract_solution(model))
            # optionally ask for additional models (not implemented fully because Optimize())
            return results
        elif res == unsat:
            print("No solution found! Constraints are unsatisfiable.")
            return []
        else:
            print("Solver returned unknown")
            return []

    def extract_solution(self, model):
        """Extract and format the solution"""
        solution = {
            "kLoAppMemBeg": model[self.kLoAppMemBeg].as_long(),
            "kAsanLoAppMemEnd": self.platform.lo_app_mem_end,
            "kLoAppMemEnd": model[self.kLoAppMemEnd].as_long(),
            "kMidAppMemBeg": model[self.kMidAppMemBeg].as_long(),
            "kMidAppMemEnd": model[self.kMidAppMemEnd].as_long(),
            "kHiAppMemBeg": model[self.kHiAppMemBeg].as_long(),
            "kHiAppMemEnd": model[self.kHiAppMemEnd].as_long(),
            "kHeapMemBeg": model[self.kHeapMemBeg].as_long(),
            "kHeapMemEnd": model[self.kHeapMemEnd].as_long(),
        }

        # Extract sanitizer parameters
        for sanitizer in self.sanitizers:
            solution.update(sanitizer.format_parameters(model))

        # Extract all regions
        regions = []
        for region_beg, region_end, region_name, is_app in self.all_regions:
            beg = (
                region_beg
                if isinstance(region_beg, int)
                else model.eval(region_beg).as_long()
            )
            end = (
                region_end
                if isinstance(region_end, int)
                else model.eval(region_end).as_long()
            )
            regions.append((beg, end, region_name, is_app))
        regions.append(
            (
                self.platform.lo_app_mem_beg,
                self.platform.lo_app_mem_end,
                "LoApp (for ASan)",
                True,
            )
        )
        # sort regions by address
        regions_sorted = sorted(regions, key=lambda x: x[1])

        return solution, regions_sorted

    def format_size(self, size):
        """Format size in human-readable format (show integer if divisible, otherwise 2 decimal places)"""
        units = [(1024**4, "TB"), (1024**3, "GB"), (1024**2, "MB"), (1024, "KB")]
        for factor, label in units:
            if size >= factor:
                value = size / factor
                if size % factor == 0:
                    return f"{int(value)} {label}"
                else:
                    return f"{value:.2f} {label}"
        return f"{size} B"

    def print_solution(self, solution, regions):
        """Print the solution in a readable format"""
        if not solution:
            print("No solution to display")
            return

        print("\n" + "=" * 80)
        print(f"SOLUTION - Platform: {self.platform_key}")
        print("=" * 80)
        print(
            f"static constexpr const uintptr kVdsoBeg = 0x{format_cpp_int(self.platform.vdso_beg)}ull;"
        )

        print("\n// App Memory Regions:")

        print(
            f"static constexpr const uintptr kLoAppMemBeg = 0x{format_cpp_int(solution['kLoAppMemBeg'])}ull;"
        )
        print(
            f"static constexpr const uintptr kLoAppMemEnd = 0x{format_cpp_int(solution['kLoAppMemEnd'])}ull;"
        )
        print(
            "// Used only for ASan's shadow calculation\n"
            f"static constexpr const uintptr kAsanLoAppMemEnd = 0x{format_cpp_int(solution['kAsanLoAppMemEnd'])}ull;"
        )
        print(
            f"static constexpr const uintptr kMidAppMemBeg = 0x{format_cpp_int(solution['kMidAppMemBeg'])}ull;"
        )
        print(
            f"static constexpr const uintptr kMidAppMemEnd = 0x{format_cpp_int(solution['kMidAppMemEnd'])}ull;"
        )
        print(
            f"static constexpr const uintptr kHiAppMemBeg = 0x{format_cpp_int(solution['kHiAppMemBeg'])}ull;"
        )
        print(
            f"static constexpr const uintptr kHiAppMemEnd = 0x{format_cpp_int(solution['kHiAppMemEnd'])}ull;"
        )
        print(
            f"static constexpr const uintptr kHeapMemBeg = 0x{format_cpp_int(solution['kHeapMemBeg'])}ull;"
        )
        print(
            f"static constexpr const uintptr kHeapMemEnd = 0x{format_cpp_int(solution['kHeapMemEnd'])}ull;"
        )
        # Print sanitizer parameters
        for sanitizer in self.sanitizers:
            code = sanitizer.get_parameters_as_cpp_code(solution)
            print(code)

        print("\n// Complete Memory Layout:")
        desc = self.get_memory_layout_desc(regions)
        print(desc)

    def get_memory_layout_desc(self, regions) -> str:
        """Print the complete memory layout"""
        layout = regions
        desc = "Complete Memory Layout (sorted by address):\n"
        desc += "-" * 80 + "\n"
        prev_end = None
        for start, end, name, is_app in layout:
            if prev_end is not None and start > prev_end:
                gap_size = start - prev_end
                desc += f"{prev_end:012x} - {start:012x}: - gap ({self.format_size(gap_size)})\n"

            size = end - start
            display_name = name if is_app else f"-- {name}"
            desc += f"{start:012x} - {end:012x}: {display_name} ({self.format_size(size)})\n"
            prev_end = end
        return desc

    def get_regions_cppcode(self, regions) -> str:
        """Get the C++ code for the regions"""
        lines = []
        AppMemMap = {
            # 'LoApp (for ASan)': '{kLoAppMemBeg, kAsanLoAppMemEnd, RegionType::App, "LoApp (for ASan)"},',
            # 'LoApp': '{kLoAppMemBeg, kLoAppMemEnd, RegionType::App, "LoApp"},',
            # 'MidApp': '{kMidAppMemBeg, kMidAppMemEnd, RegionType::App, "MidApp"},',
            # 'HiApp': '{kHiAppMemBeg, kHiAppMemEnd, RegionType::App, "HiApp"},',
            # 'Heap': '{kHeapMemBeg, kHeapMemEnd, RegionType::App, "Heap"},',
        }
        indent = " " * 6
        for beg, end, name, is_app in regions:
            if name in AppMemMap:
                lines.append(f"{indent}{AppMemMap[name]}")
            else:
                lines.append(
                    f"{indent}{{0x{format_cpp_int(beg)}ull, 0x{format_cpp_int(end)}ull, RegionType::{'App' if is_app else 'Shadow'}, \"{name}\"}},"
                )
        code = "\n".join(lines)
        regions_code = f"""  // All Memory Regions to Map (just for reference as sanitizer might change the mapping dynamically)
  static constexpr const MemRegion kRegions[] = {{
{code}
  }};
"""
        return regions_code

    def emit_cpp_header(
        self,
        solution: Dict[str, int],
        regions: List[Tuple[int, int, str, bool]],
        outdir: str,
        cmdline: str,
    ):
        """Emit a C++ header file representing the layout."""
        HEADER_COMMENT_LEN = 80
        # sanitize platform key for filename
        filename = f"xsan_platform_{self.platform_key}.h"
        header_comment = f"//===-- {filename}: Auto-generated by SMT solver --"
        header_comment += "-" * (
            HEADER_COMMENT_LEN - len(header_comment) - len("===//")
        )
        header_comment += "===//"
        path = os.path.join(outdir, filename)
        classname = self.platform.name

        header_comments = f"""{header_comment}
//
// Generated Time: {datetime.now().isoformat()}Z
// Platform: {self.platform.name}
// Generated by:
//
// {cmdline}
//
//==={'-' * (HEADER_COMMENT_LEN - len("//===") *2)}===/
//
// NOTE: This file was generated by an SMT solver; do not edit manually unless
// you know what you are doing.
//
//==={'-' * (HEADER_COMMENT_LEN - len("//===") *2)}===/
"""

        # comment block with human-readable layout
        mapping_desc = self.get_memory_layout_desc(regions)
        mapping_comments = f"""
/*\nC/C++ on {self.platform.name} Memory Layout:

{mapping_desc}
*/"""
        parameters = []
        # app regions

        def append_const(name, val, comment=None):
            if comment is not None:
                parameters.append(f"  // {comment}")
            parameters.append(
                f"  static constexpr const uintptr {name} = 0x{format_cpp_int(val)}ull;"
            )

        append_const("kHeapMemBeg", solution["kHeapMemBeg"])
        append_const("kHeapMemEnd", solution["kHeapMemEnd"])
        append_const("kLoAppMemBeg", solution["kLoAppMemBeg"])
        append_const("kLoAppMemEnd", solution["kLoAppMemEnd"])
        append_const(
            "kAsanLoAppMemEnd",
            solution["kAsanLoAppMemEnd"],
            "Used only for ASan's shadow calculation",
        )
        append_const("kMidAppMemBeg", solution["kMidAppMemBeg"])
        append_const("kMidAppMemEnd", solution["kMidAppMemEnd"])
        append_const("kHiAppMemBeg", solution["kHiAppMemBeg"])
        append_const("kHiAppMemEnd", solution["kHiAppMemEnd"])
        append_const("kVdsoBeg", self.platform.vdso_beg)
        # sanitizer parameters (TSan/ASan/MSan...)

        # Output sanitizer parameters as C++ code
        for sanitizer in self.sanitizers:
            code = sanitizer.get_parameters_as_cpp_code(solution, in_header_file=True)
            parameters.append(code)

        parameters_code = "\n".join(parameters)
        regions_code = self.get_regions_cppcode(regions)
        header_code = f"""{header_comments}
#pragma once
{mapping_comments}
struct {classname} {{
{parameters_code}
{regions_code}
}};
"""

        # write file
        os.makedirs(outdir, exist_ok=True)
        with open(path, "w") as f:
            f.write(header_code)

        print(f"[+] Header file written: {path}")
        return path


def parse_args(argv=None):
    p = argparse.ArgumentParser(
        description="XSan Shadow Memory Auto-Allocator (Z3-based)"
    )
    p.add_argument(
        "--platform",
        default="x64_48",
        choices=list(PLATFORMS.keys()),
        help="Platform key to use (one of the PLATFORMS keys)",
    )
    p.add_argument(
        "--mode",
        default="default",
        choices=["default", "conservative", "aggressive"],
        help="Solver mode (semantic only)",
    )
    p.add_argument(
        "--output",
        default="print",
        choices=["print", "header"],
        help="Output mode: print to stdout or emit C++ header",
    )
    default_outdir = (
        Path(__file__).resolve().parent.parent / "src" / "include" / "platforms"
    )
    p.add_argument(
        "--outdir",
        default=str(default_outdir),
        help=f"Output directory for headers (default: {default_outdir})",
    )
    p.add_argument(
        "--align",
        type=lambda x: int(x, 0),
        default=None,
        help="Optional override for platform alignment (e.g. 0x100000000000)",
    )
    p.add_argument(
        "--max-solutions",
        type=int,
        default=1,
        help="Max number of solutions to request (not fully used with Optimize)",
    )
    return p.parse_args(argv)


def main(argv=None):
    args = parse_args(argv)

    # Create allocator
    allocator = SanitizerShadowAllocator(
        platform_name=args.platform, align_hint=args.align
    )

    # Solve
    solutions = allocator.solve(max_solutions=args.max_solutions)

    if not solutions:
        print("\n✗ Failed to find a valid memory layout")
        return 1

    # use first solution for header generation
    sol, regions = solutions[0]

    if args.output == "print":
        allocator.print_solution(sol, regions)
    elif args.output == "header":
        cmdline = " ".join(shlex.quote(a) for a in sys.argv)
        path = allocator.emit_cpp_header(
            sol, regions, outdir=args.outdir, cmdline=cmdline
        )

    print("\n✓ Successfully found non-overlapping memory layout!")
    return 0


if __name__ == "__main__":
    sys.exit(main())
