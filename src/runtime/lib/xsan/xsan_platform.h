#pragma once

#if !defined(__LP64__) && !defined(_WIN64)
#  error "Only 64-bit is supported"
#endif

#include <sanitizer_common/sanitizer_common.h>

namespace __xsan {

enum {
  // App memory is not mapped onto shadow memory range.
  kBrokenMapping = 1 << 0,
  // Mapping app memory and back does not produce the same address,
  // this can lead to wrong addresses in reports and potentially
  // other bad consequences.
  kBrokenReverseMapping = 1 << 1,
  // Mapping is non-linear for linear user range.
  // This is bad and can lead to unpredictable memory corruptions, etc
  // because range access functions assume linearity.
  kBrokenLinearity = 1 << 2,
};

template <typename Mapping>
constexpr uptr mem_to_asan_shadow(const uptr mem) {
  return (mem >> Mapping::kAsanShadowScale) + Mapping::kAsanShadowOffset;
}

template <typename Mapping>
struct AsanMappingBase {
  static constexpr const uptr kAsanLowShadowBeg =
      mem_to_asan_shadow<Mapping>(0);
  static constexpr const uptr kAsanLowShadowEnd =
      mem_to_asan_shadow<Mapping>(Mapping::kLoAppMemEnd);
  static constexpr const uptr kAsanHighShadowEnd =
      mem_to_asan_shadow<Mapping>(Mapping::kHiAppMemEnd);
  static constexpr const uptr kAsanHighShadowBeg =
      mem_to_asan_shadow<Mapping>(kAsanHighShadowEnd);
  static constexpr const uptr kAsanShadowGapBeg = kAsanLowShadowEnd;
  static constexpr const uptr kAsanShadowGapEnd = kAsanHighShadowBeg;
};

struct MsanMappingDesc {
  uptr start;
  uptr end;
  enum Type {
    INVALID = 1,
    ALLOCATOR = 2,
    APP = 4,
    SHADOW = 8,
    ORIGIN = 16,
  } type;
  const char *name;
};

/*
C/C++ on linux/x86_64 and freebsd/x86_64
0000 0000 1000 - 0000 7fff 8000: main binary and/or MAP_32BIT mappings (low app)
0000 7fff 8000 - 1000 7fff 8000: ASan shadow
1000 7fff 8000 - 1200 0000 0000: -
1200 0000 0000 - 3000 0000 0000: TSan shadow
3000 0000 0000 - 3100 0000 0000: MSan shadow (low app)
3100 0000 0000 - 3800 0000 0000: -
3800 0000 0000 - 4000 0000 0000: TSan metainfo
4000 0000 0000 - 4100 0000 0000: MSan origin (low app)
4100 0000 0000 - 4200 0000 0000: -
4200 0000 0000 - 4400 0000 0000: MSan shadow (heap)
4400 0000 0000 - 4a00 0000 0000: -
4a00 0000 0000 - 5000 0000 0000: Msan shadow (high app)
5000 0000 0000 - 5200 0000 0000: -
5200 0000 0000 - 5400 0000 0000: MSan origin (heap)
5400 0000 0000 - 5500 0000 0000: -
5500 0000 0000 - 5a00 0000 0000: pie binaries without ASLR or on 4.1+ kernels (5TB) (mid app)
5a00 0000 0000 - 6000 0000 0000: MSan origin (high app)
6000 0000 0000 - 6500 0000 0000: -
6500 0000 0000 - 6a00 0000 0000: MSan shadow (mid app)
6a00 0000 0000 - 7200 0000 0000: -
7200 0000 0000 - 7400 0000 0000: heap (2TB)
7400 0000 0000 - 7500 0000 0000: -
7500 0000 0000 - 7a00 0000 0000: MSan origin (mid app)
7a00 0000 0000 - 8000 0000 0000: modules and main thread stack (6TB) (high app)
C/C++ on netbsd/amd64 can reuse the same mapping:
 * The address space starts from 0x1000 (option with 0x0) and ends with
   0x7f7ffffff000.
 * LoAppMem-kHeapMemEnd can be reused as it is.
 * No VDSO support.
 * No MidAppMem region.
 * No additional HeapMem region.
 * HiAppMem contains the stack, loader, shared libraries and heap.
 * Stack on NetBSD/amd64 has prereserved 128MB.
 * Heap grows downwards (top-down).
 * ASLR must be disabled per-process or globally.
*/
/// Modified: enlarge the heap size from 1T to 2T to fit ASan's needs.
struct Mapping48AddressSpace : public AsanMappingBase<Mapping48AddressSpace> {
  static constexpr const uptr kHeapMemBeg = 0x720000000000ull;
  static constexpr const uptr kHeapMemEnd = 0x740000000000ull;

  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x00007fff8000ull;
  static constexpr const uptr kMidAppMemBeg = 0x550000000000ull;
  static constexpr const uptr kMidAppMemEnd = 0x5a0000000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x7a0000000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x800000000000ull;
  static constexpr const uptr kVdsoBeg = 0xf000000000000000ull;

  /// TSan's Shadow & MetaInfo Shadow parameters
  static constexpr const uptr kTsanShadowBeg = 0x120000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x300000000000ull;
  static constexpr const uptr kTsanMetaShadowBeg = 0x380000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x400000000000ull;
  static constexpr const uptr kTsanShadowMsk = 0x700000000000ull;
  static constexpr const uptr kTsanShadowXor = 0x0b0000000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x120000000000ull;

  /// ASan's Shadow parameters
  static constexpr const uptr kAsanShadowOffset = 0x000000007fff8000ull;
  static constexpr const uptr kAsanShadowScale = 3;

  /// MSan's Shadow parameters
  static constexpr uptr kMSanShadowXor = 0x300000000000ull;
  static constexpr uptr kMSanShadowAdd = 0x100000000000ull;

  static constexpr uptr kMSanLoShadowBeg   = 0x300000000000ull;
  static constexpr uptr kMSanLoShadowEnd   = 0x310000000000ull;
  static constexpr uptr kMSanMidShadowBeg  = 0x650000000000ull;
  static constexpr uptr kMSanMidShadowEnd  = 0x6a0000000000ull;
  static constexpr uptr kMSanHiShadowBeg   = 0x4a0000000000ull;
  static constexpr uptr kMSanHiShadowEnd   = 0x500000000000ull;
  static constexpr uptr kMSanHeapShadowBeg = 0x420000000000ull;
  static constexpr uptr kMSanHeapShadowEnd = 0x440000000000ull;
  static constexpr MsanMappingDesc kMsanMemoryLayout[] = {
    {kLoAppMemBeg,  kLoAppMemEnd,  MsanMappingDesc::APP, "app-1"},
    {kMidAppMemBeg, kMidAppMemEnd, MsanMappingDesc::APP, "app-2"},
    {kHiAppMemBeg,  kHiAppMemEnd,  MsanMappingDesc::APP, "app-3"},
    {kHeapMemBeg,   kHeapMemEnd,   MsanMappingDesc::ALLOCATOR, "allocator"},
    {kMSanLoShadowBeg,   kMSanLoShadowEnd,   MsanMappingDesc::SHADOW, "shadow-1"},
    {kMSanMidShadowBeg,  kMSanMidShadowEnd,  MsanMappingDesc::SHADOW, "shadow-2"},
    {kMSanHiShadowBeg,   kMSanHiShadowEnd,   MsanMappingDesc::SHADOW, "shadow-3"},
    {kMSanHeapShadowBeg, kMSanHeapShadowEnd, MsanMappingDesc::SHADOW, "shadow-alloctor"},
    {kMSanLoShadowBeg   + kMSanShadowAdd, kMSanLoShadowEnd   + kMSanShadowAdd, MsanMappingDesc::ORIGIN, "origin-1"},
    {kMSanMidShadowBeg  + kMSanShadowAdd, kMSanMidShadowEnd  + kMSanShadowAdd, MsanMappingDesc::ORIGIN, "origin-2"},
    {kMSanHiShadowBeg   + kMSanShadowAdd, kMSanHiShadowEnd   + kMSanShadowAdd, MsanMappingDesc::ORIGIN, "origin-3"},
    {kMSanHeapShadowBeg + kMSanShadowAdd, kMSanHeapShadowEnd + kMSanShadowAdd, MsanMappingDesc::ORIGIN, "origin-allocator"},
  };
};

/*
C/C++ on linux/mips64 (40-bit VMA)
0000 0000 00 - 0100 0000 00: -                                           (4 GB)
0100 0000 00 - 0200 0000 00: main binary                                 (4 GB)
0200 0000 00 - 1200 0000 00: -                                          (64 GB)
1200 0000 00 - 2200 0000 00: shadow                                     (64 GB)
2200 0000 00 - 4000 0000 00: -                                         (120 GB)
4000 0000 00 - 5000 0000 00: metainfo (memory blocks and sync objects)  (64 GB)
5000 0000 00 - aa00 0000 00: -                                         (360 GB)
aa00 0000 00 - ab00 0000 00: main binary (PIE)                           (4 GB)
ab00 0000 00 - fe00 0000 00: -                                         (332 GB)
fe00 0000 00 - ff00 0000 00: heap                                        (4 GB)
ff00 0000 00 - ff80 0000 00: -                                           (2 GB)
ff80 0000 00 - ffff ffff ff: modules and main thread stack              (<2 GB)
*/
struct MappingMips64_40 {
  static constexpr const uptr kTsanMetaShadowBeg = 0x4000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x5000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x1200000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x2200000000ull;
  static constexpr const uptr kHeapMemBeg = 0xfe00000000ull;
  static constexpr const uptr kHeapMemEnd = 0xff00000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x0100000000ull;
  static constexpr const uptr kLoAppMemEnd = 0x0200000000ull;
  static constexpr const uptr kMidAppMemBeg = 0xaa00000000ull;
  static constexpr const uptr kMidAppMemEnd = 0xab00000000ull;
  static constexpr const uptr kHiAppMemBeg = 0xff80000000ull;
  static constexpr const uptr kHiAppMemEnd = 0xffffffffffull;
  static constexpr const uptr kTsanShadowMsk = 0xf800000000ull;
  static constexpr const uptr kTsanShadowXor = 0x0800000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x0000000000ull;
  static constexpr const uptr kVdsoBeg = 0xfffff00000ull;
};

/*
C/C++ on Darwin/iOS/ARM64 (36-bit VMA, 64 GB VM)
0000 0000 00 - 0100 0000 00: -                                    (4 GB)
0100 0000 00 - 0200 0000 00: main binary, modules, thread stacks  (4 GB)
0200 0000 00 - 0300 0000 00: heap                                 (4 GB)
0300 0000 00 - 0400 0000 00: -                                    (4 GB)
0400 0000 00 - 0800 0000 00: shadow memory                       (16 GB)
0800 0000 00 - 0d00 0000 00: -                                   (20 GB)
0d00 0000 00 - 0e00 0000 00: metainfo                             (4 GB)
0e00 0000 00 - 1000 0000 00: -
*/
struct MappingAppleAarch64 {
  static constexpr const uptr kLoAppMemBeg = 0x0100000000ull;
  static constexpr const uptr kLoAppMemEnd = 0x0200000000ull;
  static constexpr const uptr kHeapMemBeg = 0x0200000000ull;
  static constexpr const uptr kHeapMemEnd = 0x0300000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x0400000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x0800000000ull;
  static constexpr const uptr kTsanMetaShadowBeg = 0x0d00000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x0e00000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x0fc0000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x0fc0000000ull;
  static constexpr const uptr kTsanShadowMsk = 0x0ull;
  static constexpr const uptr kTsanShadowXor = 0x0ull;
  static constexpr const uptr kTsanShadowAdd = 0x0200000000ull;
  static constexpr const uptr kVdsoBeg = 0x7000000000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
};

/*
C/C++ on linux/aarch64 (39-bit VMA)
0000 0010 00 - 0100 0000 00: main binary
0100 0000 00 - 0400 0000 00: -
0400 0000 00 - 1000 0000 00: shadow memory
2000 0000 00 - 3100 0000 00: -
3100 0000 00 - 3400 0000 00: metainfo
3400 0000 00 - 5500 0000 00: -
5500 0000 00 - 5600 0000 00: main binary (PIE)
5600 0000 00 - 7c00 0000 00: -
7c00 0000 00 - 7d00 0000 00: heap
7d00 0000 00 - 7fff ffff ff: modules and main thread stack
*/
struct MappingAarch64_39 {
  static constexpr const uptr kLoAppMemBeg = 0x0000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x0100000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x0400000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x1000000000ull;
  static constexpr const uptr kTsanMetaShadowBeg = 0x3100000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x3400000000ull;
  static constexpr const uptr kMidAppMemBeg = 0x5500000000ull;
  static constexpr const uptr kMidAppMemEnd = 0x5600000000ull;
  static constexpr const uptr kHeapMemBeg = 0x7c00000000ull;
  static constexpr const uptr kHeapMemEnd = 0x7d00000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x7e00000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x7fffffffffull;
  static constexpr const uptr kTsanShadowMsk = 0x7800000000ull;
  static constexpr const uptr kTsanShadowXor = 0x0200000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x0000000000ull;
  static constexpr const uptr kVdsoBeg = 0x7f00000000ull;
};

/*
C/C++ on linux/aarch64 (42-bit VMA)
00000 0010 00 - 01000 0000 00: main binary
01000 0000 00 - 08000 0000 00: -
08000 0000 00 - 10000 0000 00: shadow memory
10000 0000 00 - 26000 0000 00: -
26000 0000 00 - 28000 0000 00: metainfo
28000 0000 00 - 2aa00 0000 00: -
2aa00 0000 00 - 2ab00 0000 00: main binary (PIE)
2ab00 0000 00 - 3e000 0000 00: -
3e000 0000 00 - 3f000 0000 00: heap
3f000 0000 00 - 3ffff ffff ff: modules and main thread stack
*/
struct MappingAarch64_42 {
  static constexpr const uptr kBroken = kBrokenReverseMapping;
  static constexpr const uptr kLoAppMemBeg = 0x00000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x01000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x08000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x10000000000ull;
  static constexpr const uptr kTsanMetaShadowBeg = 0x26000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x28000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0x2aa00000000ull;
  static constexpr const uptr kMidAppMemEnd = 0x2ab00000000ull;
  static constexpr const uptr kHeapMemBeg = 0x3e000000000ull;
  static constexpr const uptr kHeapMemEnd = 0x3f000000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x3f000000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x3ffffffffffull;
  static constexpr const uptr kTsanShadowMsk = 0x3c000000000ull;
  static constexpr const uptr kTsanShadowXor = 0x04000000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x00000000000ull;
  static constexpr const uptr kVdsoBeg = 0x37f00000000ull;
};

struct MappingAarch64_48 {
  static constexpr const uptr kLoAppMemBeg = 0x0000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x0000200000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x0001000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x0002000000000ull;
  static constexpr const uptr kTsanMetaShadowBeg = 0x0005000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x0006000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0x0aaaa00000000ull;
  static constexpr const uptr kMidAppMemEnd = 0x0aaaf00000000ull;
  static constexpr const uptr kHeapMemBeg = 0x0ffff00000000ull;
  static constexpr const uptr kHeapMemEnd = 0x0ffff00000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x0ffff00000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x1000000000000ull;
  static constexpr const uptr kTsanShadowMsk = 0x0fff800000000ull;
  static constexpr const uptr kTsanShadowXor = 0x0000800000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x0000000000000ull;
  static constexpr const uptr kVdsoBeg = 0xffff000000000ull;
};

/*
C/C++ on linux/powerpc64 (44-bit VMA)
0000 0000 0100 - 0001 0000 0000: main binary
0001 0000 0000 - 0001 0000 0000: -
0001 0000 0000 - 0b00 0000 0000: shadow
0b00 0000 0000 - 0b00 0000 0000: -
0b00 0000 0000 - 0d00 0000 0000: metainfo (memory blocks and sync objects)
0d00 0000 0000 - 0f00 0000 0000: -
0f00 0000 0000 - 0f50 0000 0000: heap
0f50 0000 0000 - 0f60 0000 0000: -
0f60 0000 0000 - 1000 0000 0000: modules and main thread stack
*/
struct MappingPPC64_44 {
  static constexpr const uptr kBroken =
      kBrokenMapping | kBrokenReverseMapping | kBrokenLinearity;
  static constexpr const uptr kTsanMetaShadowBeg = 0x0b0000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x0d0000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x000100000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x0b0000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000000100ull;
  static constexpr const uptr kLoAppMemEnd = 0x000100000000ull;
  static constexpr const uptr kHeapMemBeg = 0x0f0000000000ull;
  static constexpr const uptr kHeapMemEnd = 0x0f5000000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x0f6000000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x100000000000ull;  // 44 bits
  static constexpr const uptr kTsanShadowMsk = 0x0f0000000000ull;
  static constexpr const uptr kTsanShadowXor = 0x002100000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x000000000000ull;
  static constexpr const uptr kVdsoBeg = 0x3c0000000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
};

/*
C/C++ on linux/powerpc64 (46-bit VMA)
0000 0000 1000 - 0100 0000 0000: main binary
0100 0000 0000 - 0200 0000 0000: -
0100 0000 0000 - 0800 0000 0000: shadow
0800 0000 0000 - 1000 0000 0000: -
1000 0000 0000 - 1200 0000 0000: metainfo (memory blocks and sync objects)
1200 0000 0000 - 3d00 0000 0000: -
3d00 0000 0000 - 3e00 0000 0000: heap
3e00 0000 0000 - 3e80 0000 0000: -
3e80 0000 0000 - 4000 0000 0000: modules and main thread stack
*/
struct MappingPPC64_46 {
  static constexpr const uptr kTsanMetaShadowBeg = 0x100000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x120000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x010000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x080000000000ull;
  static constexpr const uptr kHeapMemBeg = 0x3d0000000000ull;
  static constexpr const uptr kHeapMemEnd = 0x3e0000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x010000000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x3e8000000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x400000000000ull;  // 46 bits
  static constexpr const uptr kTsanShadowMsk = 0x3c0000000000ull;
  static constexpr const uptr kTsanShadowXor = 0x020000000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x000000000000ull;
  static constexpr const uptr kVdsoBeg = 0x7800000000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
};

/*
C/C++ on linux/powerpc64 (47-bit VMA)
0000 0000 1000 - 0100 0000 0000: main binary
0100 0000 0000 - 0200 0000 0000: -
0100 0000 0000 - 0800 0000 0000: shadow
0800 0000 0000 - 1000 0000 0000: -
1000 0000 0000 - 1200 0000 0000: metainfo (memory blocks and sync objects)
1200 0000 0000 - 7d00 0000 0000: -
7d00 0000 0000 - 7e00 0000 0000: heap
7e00 0000 0000 - 7e80 0000 0000: -
7e80 0000 0000 - 8000 0000 0000: modules and main thread stack
*/
struct MappingPPC64_47 {
  static constexpr const uptr kTsanMetaShadowBeg = 0x100000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x120000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x010000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x080000000000ull;
  static constexpr const uptr kHeapMemBeg = 0x7d0000000000ull;
  static constexpr const uptr kHeapMemEnd = 0x7e0000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x010000000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x7e8000000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x800000000000ull;  // 47 bits
  static constexpr const uptr kTsanShadowMsk = 0x7c0000000000ull;
  static constexpr const uptr kTsanShadowXor = 0x020000000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x000000000000ull;
  static constexpr const uptr kVdsoBeg = 0x7800000000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
};

/*
C/C++ on linux/s390x
While the kernel provides a 64-bit address space, we have to restrict ourselves
to 48 bits due to how e.g. SyncVar::GetId() works.
0000 0000 1000 - 0e00 0000 0000: binary, modules, stacks - 14 TiB
0e00 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 4000 0000 0000: shadow - 32TiB (2 * app)
4000 0000 0000 - 9000 0000 0000: -
9000 0000 0000 - 9800 0000 0000: metainfo - 8TiB (0.5 * app)
9800 0000 0000 - be00 0000 0000: -
be00 0000 0000 - c000 0000 0000: heap - 2TiB (max supported by the allocator)
*/
struct MappingS390x {
  static constexpr const uptr kTsanMetaShadowBeg = 0x900000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x980000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x200000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x400000000000ull;
  static constexpr const uptr kHeapMemBeg = 0xbe0000000000ull;
  static constexpr const uptr kHeapMemEnd = 0xc00000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x0e0000000000ull;
  static constexpr const uptr kHiAppMemBeg = 0xc00000004000ull;
  static constexpr const uptr kHiAppMemEnd = 0xc00000004000ull;
  static constexpr const uptr kTsanShadowMsk = 0xb00000000000ull;
  static constexpr const uptr kTsanShadowXor = 0x100000000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x000000000000ull;
  static constexpr const uptr kVdsoBeg = 0xfffffffff000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
};

/* Go on linux, darwin and freebsd on x86_64
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 21c0 0000 0000: shadow
21c0 0000 0000 - 3000 0000 0000: -
3000 0000 0000 - 4000 0000 0000: metainfo (memory blocks and sync objects)
4000 0000 0000 - 8000 0000 0000: -
*/

struct MappingGo48 {
  static constexpr const uptr kTsanMetaShadowBeg = 0x300000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x400000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x200000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x21c000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x00e000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
  static constexpr const uptr kHiAppMemBeg = 0;
  static constexpr const uptr kHiAppMemEnd = 0;
  static constexpr const uptr kHeapMemBeg = 0;
  static constexpr const uptr kHeapMemEnd = 0;
  static constexpr const uptr kVdsoBeg = 0;
  static constexpr const uptr kTsanShadowMsk = 0;
  static constexpr const uptr kTsanShadowXor = 0;
  static constexpr const uptr kTsanShadowAdd = 0x200000000000ull;
};

/* Go on windows
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00f8 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 0100 0000 0000: -
0100 0000 0000 - 0300 0000 0000: shadow
0300 0000 0000 - 0700 0000 0000: -
0700 0000 0000 - 0770 0000 0000: metainfo (memory blocks and sync objects)
07d0 0000 0000 - 8000 0000 0000: -
PIE binaries currently not supported, but it should be theoretically possible.
*/

struct MappingGoWindows {
  static constexpr const uptr kTsanMetaShadowBeg = 0x070000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x077000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x010000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x030000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x00e000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
  static constexpr const uptr kHiAppMemBeg = 0;
  static constexpr const uptr kHiAppMemEnd = 0;
  static constexpr const uptr kHeapMemBeg = 0;
  static constexpr const uptr kHeapMemEnd = 0;
  static constexpr const uptr kVdsoBeg = 0;
  static constexpr const uptr kTsanShadowMsk = 0;
  static constexpr const uptr kTsanShadowXor = 0;
  static constexpr const uptr kTsanShadowAdd = 0x010000000000ull;
};

/* Go on linux/powerpc64 (46-bit VMA)
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 21c0 0000 0000: shadow
21c0 0000 0000 - 2400 0000 0000: -
2400 0000 0000 - 2470 0000 0000: metainfo (memory blocks and sync objects)
2470 0000 0000 - 4000 0000 0000: -
*/

struct MappingGoPPC64_46 {
  static constexpr const uptr kTsanMetaShadowBeg = 0x240000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x247000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x200000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x21c000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x00e000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
  static constexpr const uptr kHiAppMemBeg = 0;
  static constexpr const uptr kHiAppMemEnd = 0;
  static constexpr const uptr kHeapMemBeg = 0;
  static constexpr const uptr kHeapMemEnd = 0;
  static constexpr const uptr kVdsoBeg = 0;
  static constexpr const uptr kTsanShadowMsk = 0;
  static constexpr const uptr kTsanShadowXor = 0;
  static constexpr const uptr kTsanShadowAdd = 0x200000000000ull;
};

/* Go on linux/powerpc64 (47-bit VMA)
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 2800 0000 0000: shadow
2800 0000 0000 - 3000 0000 0000: -
3000 0000 0000 - 3200 0000 0000: metainfo (memory blocks and sync objects)
3200 0000 0000 - 8000 0000 0000: -
*/

struct MappingGoPPC64_47 {
  static constexpr const uptr kTsanMetaShadowBeg = 0x300000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x320000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x200000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x280000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x00e000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
  static constexpr const uptr kHiAppMemBeg = 0;
  static constexpr const uptr kHiAppMemEnd = 0;
  static constexpr const uptr kHeapMemBeg = 0;
  static constexpr const uptr kHeapMemEnd = 0;
  static constexpr const uptr kVdsoBeg = 0;
  static constexpr const uptr kTsanShadowMsk = 0;
  static constexpr const uptr kTsanShadowXor = 0;
  static constexpr const uptr kTsanShadowAdd = 0x200000000000ull;
};

/* Go on linux/aarch64 (48-bit VMA) and darwin/aarch64 (47-bit VMA)
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 2800 0000 0000: shadow
2800 0000 0000 - 3000 0000 0000: -
3000 0000 0000 - 3200 0000 0000: metainfo (memory blocks and sync objects)
3200 0000 0000 - 8000 0000 0000: -
*/
struct MappingGoAarch64 {
  static constexpr const uptr kTsanMetaShadowBeg = 0x300000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x320000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x200000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x280000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x00e000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
  static constexpr const uptr kHiAppMemBeg = 0;
  static constexpr const uptr kHiAppMemEnd = 0;
  static constexpr const uptr kHeapMemBeg = 0;
  static constexpr const uptr kHeapMemEnd = 0;
  static constexpr const uptr kVdsoBeg = 0;
  static constexpr const uptr kTsanShadowMsk = 0;
  static constexpr const uptr kTsanShadowXor = 0;
  static constexpr const uptr kTsanShadowAdd = 0x200000000000ull;
};

/*
Go on linux/mips64 (47-bit VMA)
0000 0000 1000 - 0000 1000 0000: executable
0000 1000 0000 - 00c0 0000 0000: -
00c0 0000 0000 - 00e0 0000 0000: heap
00e0 0000 0000 - 2000 0000 0000: -
2000 0000 0000 - 2800 0000 0000: shadow
2800 0000 0000 - 3000 0000 0000: -
3000 0000 0000 - 3200 0000 0000: metainfo (memory blocks and sync objects)
3200 0000 0000 - 8000 0000 0000: -
*/
struct MappingGoMips64_47 {
  static constexpr const uptr kTsanMetaShadowBeg = 0x300000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x320000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x200000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x280000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x00e000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
  static constexpr const uptr kHiAppMemBeg = 0;
  static constexpr const uptr kHiAppMemEnd = 0;
  static constexpr const uptr kHeapMemBeg = 0;
  static constexpr const uptr kHeapMemEnd = 0;
  static constexpr const uptr kVdsoBeg = 0;
  static constexpr const uptr kTsanShadowMsk = 0;
  static constexpr const uptr kTsanShadowXor = 0;
  static constexpr const uptr kTsanShadowAdd = 0x200000000000ull;
};

/*
Go on linux/s390x
0000 0000 1000 - 1000 0000 0000: executable and heap - 16 TiB
1000 0000 0000 - 4000 0000 0000: -
4000 0000 0000 - 6000 0000 0000: shadow - 64TiB (4 * app)
6000 0000 0000 - 9000 0000 0000: -
9000 0000 0000 - 9800 0000 0000: metainfo - 8TiB (0.5 * app)
*/
struct MappingGoS390x {
  static constexpr const uptr kTsanMetaShadowBeg = 0x900000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x980000000000ull;
  static constexpr const uptr kTsanShadowBeg = 0x400000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x600000000000ull;
  static constexpr const uptr kLoAppMemBeg = 0x000000001000ull;
  static constexpr const uptr kLoAppMemEnd = 0x100000000000ull;
  static constexpr const uptr kMidAppMemBeg = 0;
  static constexpr const uptr kMidAppMemEnd = 0;
  static constexpr const uptr kHiAppMemBeg = 0;
  static constexpr const uptr kHiAppMemEnd = 0;
  static constexpr const uptr kHeapMemBeg = 0;
  static constexpr const uptr kHeapMemEnd = 0;
  static constexpr const uptr kVdsoBeg = 0;
  static constexpr const uptr kTsanShadowMsk = 0;
  static constexpr const uptr kTsanShadowXor = 0;
  static constexpr const uptr kTsanShadowAdd = 0x400000000000ull;
};

extern uptr vmaSize;

template <typename Func, typename Arg>
ALWAYS_INLINE auto SelectMapping(Arg arg) {
#if SANITIZER_GO
#  if defined(__powerpc64__)
  switch (vmaSize) {
    case 46:
      return Func::template Apply<MappingGoPPC64_46>(arg);
    case 47:
      return Func::template Apply<MappingGoPPC64_47>(arg);
  }
#  elif defined(__mips64)
  return Func::template Apply<MappingGoMips64_47>(arg);
#  elif defined(__s390x__)
  return Func::template Apply<MappingGoS390x>(arg);
#  elif defined(__aarch64__)
  return Func::template Apply<MappingGoAarch64>(arg);
#  elif SANITIZER_WINDOWS
  return Func::template Apply<MappingGoWindows>(arg);
#  else
  return Func::template Apply<MappingGo48>(arg);
#  endif
#else  // SANITIZER_GO
#  if SANITIZER_IOS && !SANITIZER_IOSSIM
  return Func::template Apply<MappingAppleAarch64>(arg);
#  elif defined(__x86_64__) || SANITIZER_APPLE
  return Func::template Apply<Mapping48AddressSpace>(arg);
#  elif defined(__aarch64__)
  switch (vmaSize) {
    case 39:
      return Func::template Apply<MappingAarch64_39>(arg);
    case 42:
      return Func::template Apply<MappingAarch64_42>(arg);
    case 48:
      return Func::template Apply<MappingAarch64_48>(arg);
  }
#  elif defined(__powerpc64__)
  switch (vmaSize) {
    case 44:
      return Func::template Apply<MappingPPC64_44>(arg);
    case 46:
      return Func::template Apply<MappingPPC64_46>(arg);
    case 47:
      return Func::template Apply<MappingPPC64_47>(arg);
  }
#  elif defined(__mips64)
  return Func::template Apply<MappingMips64_40>(arg);
#  elif defined(__s390x__)
  return Func::template Apply<MappingS390x>(arg);
#  else
#    error "unsupported platform"
#  endif
#endif
  Die();
}

template <typename Func>
void ForEachMapping() {
  Func::template Apply<Mapping48AddressSpace>();
  Func::template Apply<MappingMips64_40>();
  Func::template Apply<MappingAppleAarch64>();
  Func::template Apply<MappingAarch64_39>();
  Func::template Apply<MappingAarch64_42>();
  Func::template Apply<MappingAarch64_48>();
  Func::template Apply<MappingPPC64_44>();
  Func::template Apply<MappingPPC64_46>();
  Func::template Apply<MappingPPC64_47>();
  Func::template Apply<MappingS390x>();
  Func::template Apply<MappingGo48>();
  Func::template Apply<MappingGoWindows>();
  Func::template Apply<MappingGoPPC64_46>();
  Func::template Apply<MappingGoPPC64_47>();
  Func::template Apply<MappingGoAarch64>();
  Func::template Apply<MappingGoMips64_47>();
  Func::template Apply<MappingGoS390x>();
}

enum MappingType {
  kLoAppMemBeg,
  kLoAppMemEnd,
  kHiAppMemBeg,
  kHiAppMemEnd,
  kMidAppMemBeg,
  kMidAppMemEnd,
  kHeapMemBeg,
  kHeapMemEnd,
  kVdsoBeg,

  kAsanLowShadowBeg,
  kAsanLowShadowEnd,
  kAsanHighShadowBeg,
  kAsanHighShadowEnd,
  kAsanShadowGapBeg,
  kAsanShadowGapEnd,
  kTsanShadowBeg,
  kTsanShadowEnd,
  kTsanMetaShadowBeg,
  kTsanMetaShadowEnd,
};

struct MappingField {
  template <typename Mapping>
  static uptr Apply(MappingType type) {
    switch (type) {
      case kLoAppMemBeg:
        return Mapping::kLoAppMemBeg;
      case kLoAppMemEnd:
        return Mapping::kLoAppMemEnd;
      case kMidAppMemBeg:
        return Mapping::kMidAppMemBeg;
      case kMidAppMemEnd:
        return Mapping::kMidAppMemEnd;
      case kHiAppMemBeg:
        return Mapping::kHiAppMemBeg;
      case kHiAppMemEnd:
        return Mapping::kHiAppMemEnd;
      case kHeapMemBeg:
        return Mapping::kHeapMemBeg;
      case kHeapMemEnd:
        return Mapping::kHeapMemEnd;
      case kVdsoBeg:
        return Mapping::kVdsoBeg;

      case kAsanLowShadowBeg:
        return Mapping::kAsanLowShadowBeg;
      case kAsanLowShadowEnd:
        return Mapping::kAsanLowShadowEnd;
      case kAsanHighShadowBeg:
        return Mapping::kAsanHighShadowBeg;
      case kAsanHighShadowEnd:
        return Mapping::kAsanHighShadowEnd;
      case kAsanShadowGapBeg:
        return Mapping::kAsanShadowGapBeg;
      case kAsanShadowGapEnd:
        return Mapping::kAsanShadowGapEnd;
      case kTsanShadowBeg:
        return Mapping::kTsanShadowBeg;
      case kTsanShadowEnd:
        return Mapping::kTsanShadowEnd;
      case kTsanMetaShadowBeg:
        return Mapping::kTsanMetaShadowBeg;
      case kTsanMetaShadowEnd:
        return Mapping::kTsanMetaShadowEnd;
    }
    Die();
  }
};

ALWAYS_INLINE
uptr LoAppMemBeg(void) { return SelectMapping<MappingField>(kLoAppMemBeg); }
ALWAYS_INLINE
uptr LoAppMemEnd(void) { return SelectMapping<MappingField>(kLoAppMemEnd); }

ALWAYS_INLINE
uptr MidAppMemBeg(void) { return SelectMapping<MappingField>(kMidAppMemBeg); }
ALWAYS_INLINE
uptr MidAppMemEnd(void) { return SelectMapping<MappingField>(kMidAppMemEnd); }

ALWAYS_INLINE
uptr HeapMemBeg(void) { return SelectMapping<MappingField>(kHeapMemBeg); }
ALWAYS_INLINE
uptr HeapMemEnd(void) { return SelectMapping<MappingField>(kHeapMemEnd); }

ALWAYS_INLINE
uptr HiAppMemBeg(void) { return SelectMapping<MappingField>(kHiAppMemBeg); }
ALWAYS_INLINE
uptr HiAppMemEnd(void) { return SelectMapping<MappingField>(kHiAppMemEnd); }

ALWAYS_INLINE
uptr VdsoBeg(void) { return SelectMapping<MappingField>(kVdsoBeg); }

ALWAYS_INLINE
uptr AsanLowShadowBeg(void) {
  return SelectMapping<MappingField>(kAsanLowShadowBeg);
}
ALWAYS_INLINE
uptr AsanLowShadowEnd(void) {
  return SelectMapping<MappingField>(kAsanLowShadowEnd);
}
ALWAYS_INLINE
uptr AsanHighShadowBeg(void) {
  return SelectMapping<MappingField>(kAsanHighShadowBeg);
}
ALWAYS_INLINE
uptr AsanHighShadowEnd(void) {
  return SelectMapping<MappingField>(kAsanHighShadowEnd);
}
ALWAYS_INLINE
uptr AsanShadowGapBeg(void) {
  return SelectMapping<MappingField>(kAsanShadowGapBeg);
}
ALWAYS_INLINE
uptr AsanShadowGapEnd(void) {
  return SelectMapping<MappingField>(kAsanShadowGapEnd);
}
ALWAYS_INLINE
uptr TsanShadowBeg(void) { return SelectMapping<MappingField>(kTsanShadowBeg); }
ALWAYS_INLINE
uptr TsanShadowEnd(void) { return SelectMapping<MappingField>(kTsanShadowEnd); }
ALWAYS_INLINE
uptr TsanMetaShadowBeg(void) {
  return SelectMapping<MappingField>(kTsanMetaShadowBeg);
}
ALWAYS_INLINE
uptr TsanMetaShadowEnd(void) {
  return SelectMapping<MappingField>(kTsanMetaShadowEnd);
}

struct IsAppMemImpl {
  template <typename Mapping>
  static bool Apply(uptr mem) {
    return (mem >= Mapping::kHeapMemBeg && mem < Mapping::kHeapMemEnd) ||
           (mem >= Mapping::kMidAppMemBeg && mem < Mapping::kMidAppMemEnd) ||
           (mem >= Mapping::kLoAppMemBeg && mem < Mapping::kLoAppMemEnd) ||
           (mem >= Mapping::kHiAppMemBeg && mem < Mapping::kHiAppMemEnd);
  }
};

ALWAYS_INLINE
bool IsAppMem(uptr mem) { return SelectMapping<IsAppMemImpl>(mem); }
ALWAYS_INLINE
bool IsAppMem(const void* mem) { return IsAppMem((uptr)mem); }

struct IsTsanShadowMemImpl {
  template <typename Mapping>
  static bool Apply(uptr mem) {
    return mem >= Mapping::kTsanShadowBeg && mem <= Mapping::kTsanShadowEnd;
  }
};

ALWAYS_INLINE
bool IsTsanShadowMem(uptr p) { return SelectMapping<IsTsanShadowMemImpl>(p); }

struct IsTsanMetaMemImpl {
  template <typename Mapping>
  static bool Apply(uptr mem) {
    return mem >= Mapping::kTsanMetaShadowBeg &&
           mem <= Mapping::kTsanMetaShadowEnd;
  }
};

ALWAYS_INLINE
bool IsTsanMetaMem(uptr p) { return SelectMapping<IsTsanMetaMemImpl>(p); }

struct IsAsanLowShadowMemImpl {
  template <typename Mapping>
  static bool Apply(uptr mem) {
    return mem >= Mapping::kAsanLowShadowBeg &&
           mem <= Mapping::kAsanLowShadowEnd;
  }
};

ALWAYS_INLINE
bool IsAsanLowShadowMem(uptr p) {
  return SelectMapping<IsAsanLowShadowMemImpl>(p);
}

struct IsAsanHighShadowMemImpl {
  template <typename Mapping>
  static bool Apply(uptr mem) {
    return mem >= Mapping::kAsanHighShadowBeg &&
           mem <= Mapping::kAsanHighShadowEnd;
  }
};

ALWAYS_INLINE
bool IsAsanHighShadowMem(uptr p) {
  return SelectMapping<IsAsanHighShadowMemImpl>(p);
}

struct IsAsanShadowGapMemImpl {
  template <typename Mapping>
  static bool Apply(uptr mem) {
    return mem >= Mapping::kAsanShadowGapBeg &&
           mem <= Mapping::kAsanShadowGapEnd;
  }
};

ALWAYS_INLINE
bool IsAsanShadowGapMem(uptr p) {
  return SelectMapping<IsAsanShadowGapMemImpl>(p);
}

ALWAYS_INLINE
bool IsAsanShadowMem(uptr p) {
  return IsAsanHighShadowMem(p) || IsAsanLowShadowMem(p);
}

ALWAYS_INLINE
bool IsAsanPrivateMem(uptr p) {
  return IsAsanShadowMem(p) || IsAsanShadowGapMem(p);
}

ALWAYS_INLINE
bool IsTsanPrivateMem(uptr p) { return IsTsanShadowMem(p) || IsTsanMetaMem(p); }

ALWAYS_INLINE
bool IsSanitizerShadowMem(uptr p) {
  return IsAsanShadowMem(p) || IsTsanShadowMem(p) || IsTsanMetaMem(p);
}

ALWAYS_INLINE
bool IsSanitizerPrivateMem(uptr p) {
  return IsAsanPrivateMem(p) || IsTsanPrivateMem(p);
}

// struct MemToShadowImpl {
//   template <typename Mapping>
//   static uptr Apply(uptr x) {
//     DCHECK(IsAppMemImpl::Apply<Mapping>(x));
//     return (((x) & ~(Mapping::kTsanShadowMsk | (kShadowCell - 1))) ^
//             Mapping::kTsanShadowXor) *
//                kShadowMultiplier +
//            Mapping::kTsanShadowAdd;
//   }
// };

// ALWAYS_INLINE
// RawShadow *MemToShadow(uptr x) {
//   return reinterpret_cast<RawShadow *>(SelectMapping<MemToShadowImpl>(x));
// }

// struct MemToMetaImpl {
//   template <typename Mapping>
//   static u32 *Apply(uptr x) {
//     DCHECK(IsAppMemImpl::Apply<Mapping>(x));
//     return (u32 *)(((((x) & ~(Mapping::kTsanShadowMsk | (kMetaShadowCell -
//     1))))
//     /
//                     kMetaShadowCell * kMetaShadowSize) |
//                    Mapping::kTsanMetaShadowBeg);
//   }
// };

// ALWAYS_INLINE
// u32 *MemToMeta(uptr x) { return SelectMapping<MemToMetaImpl>(x); }

// struct ShadowToMemImpl {
//   template <typename Mapping>
//   static uptr Apply(uptr sp) {
//     if (!IsShadowMemImpl::Apply<Mapping>(sp))
//       return 0;
//     // The shadow mapping is non-linear and we've lost some bits, so we don't
//     // have an easy way to restore the original app address. But the mapping
//     is
//     // a bijection, so we try to restore the address as belonging to
//     // low/mid/high range consecutively and see if shadow->app->shadow
//     mapping
//     // gives us the same address.
//     uptr p =
//         ((sp - Mapping::kTsanShadowAdd) / kShadowMultiplier) ^
//         Mapping::kTsanShadowXor;
//     if (p >= Mapping::kLoAppMemBeg && p < Mapping::kLoAppMemEnd &&
//         MemToShadowImpl::Apply<Mapping>(p) == sp)
//       return p;
//     if (Mapping::kMidAppMemBeg) {
//       uptr p_mid = p + (Mapping::kMidAppMemBeg & Mapping::kTsanShadowMsk);
//       if (p_mid >= Mapping::kMidAppMemBeg && p_mid < Mapping::kMidAppMemEnd
//       &&
//           MemToShadowImpl::Apply<Mapping>(p_mid) == sp)
//         return p_mid;
//     }
//     return p | Mapping::kTsanShadowMsk;
//   }
// };

// ALWAYS_INLINE
// uptr ShadowToMem(RawShadow *s) {
//   return SelectMapping<ShadowToMemImpl>(reinterpret_cast<uptr>(s));
// }

// // Compresses addr to kCompressedAddrBits stored in least significant bits.
// ALWAYS_INLINE uptr CompressAddr(uptr addr) {
//   return addr & ((1ull << kCompressedAddrBits) - 1);
// }

// struct RestoreAddrImpl {
//   typedef uptr Result;
//   template <typename Mapping>
//   static Result Apply(uptr addr) {
//     // To restore the address we go over all app memory ranges and check if
//     top
//     // 3 bits of the compressed addr match that of the app range. If yes, we
//     // assume that the compressed address come from that range and restore
//     the
//     // missing top bits to match the app range address.
//     const uptr ranges[] = {
//         Mapping::kLoAppMemBeg,  Mapping::kLoAppMemEnd,
//         Mapping::kMidAppMemBeg, Mapping::kMidAppMemEnd,
//         Mapping::kHiAppMemBeg, Mapping::kHiAppMemEnd, Mapping::kHeapMemBeg,
//         Mapping::kHeapMemEnd,
//     };
//     const uptr indicator = 0x0e0000000000ull;
//     const uptr ind_lsb = 1ull << LeastSignificantSetBitIndex(indicator);
//     for (uptr i = 0; i < ARRAY_SIZE(ranges); i += 2) {
//       uptr beg = ranges[i];
//       uptr end = ranges[i + 1];
//       if (beg == end)
//         continue;

//       for (uptr p = beg; p < end; p = RoundDownTo(p + ind_lsb, ind_lsb)) {
//         if ((addr & indicator) == (p & indicator))
//           return addr | (p & ~(ind_lsb - 1));
//       }
//     }
//     Printf("ThreadSanitizer: failed to restore address 0x%zx\n", addr);
//     Die();
//   }
// };

// // Restores compressed addr from kCompressedAddrBits to full representation.
// // This is called only during reporting and is not performance-critical.
// inline uptr RestoreAddr(uptr addr) {
//   return SelectMapping<RestoreAddrImpl>(addr);
// }

// void InitializePlatform();
// void InitializePlatformEarly();
// void CheckAndProtect();
// void InitializeShadowMemoryPlatform();
// void WriteMemoryProfile(char *buf, uptr buf_size, u64 uptime_ns);
// int ExtractResolvFDs(void *state, int *fds, int nfd);
// int ExtractRecvmsgFDs(void *msg, int *fds, int nfd);
// uptr ExtractLongJmpSp(uptr *env);
// void ImitateTlsWrite(ThreadState *thr, uptr tls_addr, uptr tls_size);

// int call_pthread_cancel_with_cleanup(int (*fn)(void *arg),
//                                      void (*cleanup)(void *arg), void *arg);

// void DestroyThreadState();
// void PlatformCleanUpThreadState(ThreadState *thr);

}  // namespace __xsan
