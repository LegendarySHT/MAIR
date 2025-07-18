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
1200 0000 0000 - 1400 0000 0000: MSan shadow (heap)
1400 0000 0000 - 1500 0000 0000: -
1500 0000 0000 - 1a00 0000 0000: MSan shadow (mid app)
1a00 0000 0000 - 3a00 0000 0000: TSan shadow
3a00 0000 0000 - 4000 0000 0000: MSan shadow (high app)
4000 0000 0000 - 4100 0000 0000: MSan shadow (low app)
4100 0000 0000 - 4200 0000 0000: -
4200 0000 0000 - 4400 0000 0000: MSan origin (heap)
4400 0000 0000 - 4500 0000 0000: -
4500 0000 0000 - 4a00 0000 0000: MSan origin (mid app)
4a00 0000 0000 - 5200 0000 0000: -
5200 0000 0000 - 5400 0000 0000: heap (2TB)
5400 0000 0000 - 5500 0000 0000: -
5500 0000 0000 - 5a00 0000 0000: pie binaries without ASLR or on 4.1+ kernels (5TB) (mid app)
5a00 0000 0000 - 6000 0000 0000: -
6000 0000 0000 - 6800 0000 0000: TSan metainfo
6800 0000 0000 - 6a00 0000 0000: -
6a00 0000 0000 - 7000 0000 0000: MSan origin (high app)
7000 0000 0000 - 7100 0000 0000: MSan origin (low app)
7100 0000 0000 - 7a00 0000 0000: -
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
struct Mapping48AddressSpace {
  static constexpr const uptr kHeapMemBeg = 0x520000000000ull;
  static constexpr const uptr kHeapMemEnd = 0x540000000000ull;

  static constexpr const uptr kLoAppMemBeg = 0x000000000000ull;
  // ASan set this 0x00007fff8000ull but it protects the last page. So we
  // only use 0x00007fff7000ull.
  static constexpr const uptr kLoAppMemEnd = 0x00007fff7000ull;
  static constexpr const uptr kMidAppMemBeg = 0x550000000000ull;
  static constexpr const uptr kMidAppMemEnd = 0x5a0000000000ull;
  static constexpr const uptr kHiAppMemBeg = 0x7a0000000000ull;
  static constexpr const uptr kHiAppMemEnd = 0x800000000000ull;
  static constexpr const uptr kVdsoBeg = 0xf000000000000000ull;

  /// TSan's Shadow & MetaInfo Shadow parameters
  static constexpr const uptr kTsanShadowBeg = 0x1a0000000000ull;
  static constexpr const uptr kTsanShadowEnd = 0x3a0000000000ull;
  static constexpr const uptr kTsanMetaShadowBeg = 0x600000000000ull;
  static constexpr const uptr kTsanMetaShadowEnd = 0x680000000000ull;
  static constexpr const uptr kTsanShadowMsk = 0x700000000000ull;
  static constexpr const uptr kTsanShadowXor = 0x000000000000ull;
  static constexpr const uptr kTsanShadowAdd = 0x1a0000000000ull;

  /// ASan's Shadow parameters
  static constexpr const uptr kAsanShadowOffset = 0x000000007fff8000ull;
  static constexpr const uptr kAsanShadowScale = 3;

  /// MSan's Shadow parameters
  static constexpr uptr kMSanShadowXor = 0x400000000000ull;
  static constexpr uptr kMSanShadowAdd = 0x300000000000ull;

  static constexpr uptr kMSanLoShadowBeg   = 0x400000000000ull;
  static constexpr uptr kMSanLoShadowEnd   = 0x410000000000ull;
  static constexpr uptr kMSanMidShadowBeg  = 0x150000000000ull;
  static constexpr uptr kMSanMidShadowEnd  = 0x1a0000000000ull;
  static constexpr uptr kMSanHiShadowBeg   = 0x3a0000000000ull;
  static constexpr uptr kMSanHiShadowEnd   = 0x400000000000ull;
  static constexpr uptr kMSanHeapShadowBeg = 0x120000000000ull;
  static constexpr uptr kMSanHeapShadowEnd = 0x140000000000ull;
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
  // Keep same with MappingAarch64_39 since ASan allocator needs to know the
  // heap memory range at compile time.
  static constexpr uptr kHeapMemBeg    = 0x0f20000000000ull;
  static constexpr uptr kHeapMemEnd    = 0x0f40000000000ull;
  
  static constexpr uptr kLoAppMemBeg   = 0x0000000000000ull;
  static constexpr uptr kLoAppMemEnd   = 0x0000ffffff000ull;
  static constexpr uptr kMidAppMemBeg  = 0x0aaaa00000000ull;
  static constexpr uptr kMidAppMemEnd  = 0x0ac0000000000ull;
  static constexpr uptr kHiAppMemBeg   = 0x0fc0000000000ull;
  static constexpr uptr kHiAppMemEnd   = 0x1000000000000ull;
  static constexpr uptr kVdsoBeg       = 0xffff000000000ull;

  /// TSan's Shadow & MetaInfo Shadow parameters
  static constexpr uptr kTsanShadowAdd = 0x0000000000000ull;
  static constexpr uptr kTsanShadowBeg = 0x0255400000000ull;
  static constexpr uptr kTsanShadowEnd = 0x0380000000000ull;
  static constexpr uptr kTsanMetaShadowBeg = 0x0380000000000ull;
  static constexpr uptr kTsanMetaShadowEnd = 0x0400000000000ull;
  static constexpr uptr kTsanShadowMsk = 0x0f00000000000ull;
  static constexpr uptr kTsanShadowXor = 0x0180000000000ull;

  /// ASan's Shadow parameters
  static constexpr const uptr kAsanShadowOffset = 0x0000001000000000ull;
  static constexpr const uptr kAsanShadowScale = 3;

  /// MSan's Shadow parameters
  static constexpr uptr kMSanShadowXor = 0x0600000000000ull;
  static constexpr uptr kMSanShadowAdd = 0x0040000000000ull;

  static constexpr uptr kMSanLoShadowBeg   = 0x0600000000000ull;
  static constexpr uptr kMSanLoShadowEnd   = 0x0610000000000ull;
  static constexpr uptr kMSanMidShadowBeg  = 0x0ca0000000000ull;
  static constexpr uptr kMSanMidShadowEnd  = 0x0cc0000000000ull;
  static constexpr uptr kMSanHiShadowBeg   = 0x09c0000000000ull;
  static constexpr uptr kMSanHiShadowEnd   = 0x0a00000000000ull;
  static constexpr uptr kMSanHeapShadowBeg = 0x0920000000000ull;
  static constexpr uptr kMSanHeapShadowEnd = 0x0940000000000ull;
  static constexpr MsanMappingDesc kMsanMemoryLayout[] = {
    {kLoAppMemBeg,  kLoAppMemEnd,  MsanMappingDesc::APP, "app-1"},
    {kMidAppMemBeg, kMidAppMemEnd, MsanMappingDesc::APP, "app-2"},
    {kHiAppMemBeg,  kHiAppMemEnd,  MsanMappingDesc::APP, "app-3"},
    {kHeapMemBeg,   kHeapMemEnd,   MsanMappingDesc::ALLOCATOR, "allocator"},
    {kMSanLoShadowBeg,   kMSanLoShadowEnd,   MsanMappingDesc::SHADOW, "shadow-1"},
    {kMSanMidShadowBeg,  kMSanMidShadowEnd,  MsanMappingDesc::SHADOW, "shadow-2"},
    {kMSanHiShadowBeg,   kMSanHiShadowEnd,   MsanMappingDesc::SHADOW, "shadow-3"},
    {kMSanHeapShadowBeg, kMSanHeapShadowEnd, MsanMappingDesc::SHADOW, "shadow-allocator"},
    {kMSanLoShadowBeg   + kMSanShadowAdd, kMSanLoShadowEnd   + kMSanShadowAdd, MsanMappingDesc::ORIGIN, "origin-1"},
    {kMSanMidShadowBeg  + kMSanShadowAdd, kMSanMidShadowEnd  + kMSanShadowAdd, MsanMappingDesc::ORIGIN, "origin-2"},
    {kMSanHiShadowBeg   + kMSanShadowAdd, kMSanHiShadowEnd   + kMSanShadowAdd, MsanMappingDesc::ORIGIN, "origin-3"},
    {kMSanHeapShadowBeg + kMSanShadowAdd, kMSanHeapShadowEnd + kMSanShadowAdd, MsanMappingDesc::ORIGIN, "origin-allocator"},
  };
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

template <typename Func, typename... Args>
ALWAYS_INLINE auto SelectMapping(Args... args) {
#if SANITIZER_GO
#  if defined(__powerpc64__)
  switch (vmaSize) {
    case 46:
      return Func::template Apply<MappingGoPPC64_46>(args...);
    case 47:
      return Func::template Apply<MappingGoPPC64_47>(args...);
  }
#  elif defined(__mips64)
  return Func::template Apply<MappingGoMips64_47>(args...);
#  elif defined(__s390x__)
  return Func::template Apply<MappingGoS390x>(args...);
#  elif defined(__aarch64__)
  return Func::template Apply<MappingGoAarch64>(args...);
#  elif SANITIZER_WINDOWS
  return Func::template Apply<MappingGoWindows>(args...);
#  else
  return Func::template Apply<MappingGo48>(args...);
#  endif
#else  // SANITIZER_GO
#  if SANITIZER_IOS && !SANITIZER_IOSSIM
  return Func::template Apply<MappingAppleAarch64>(args...);
#  elif defined(__x86_64__) || SANITIZER_APPLE
  return Func::template Apply<Mapping48AddressSpace>(args...);
#  elif defined(__aarch64__)
  switch (vmaSize) {
    // case 39:
    //   return Func::template Apply<MappingAarch64_39>(args...);
    // case 42:
    //   return Func::template Apply<MappingAarch64_42>(args...);
    case 48:
      return Func::template Apply<MappingAarch64_48>(args...);
  }
#  elif defined(__powerpc64__)
  switch (vmaSize) {
    case 44:
      return Func::template Apply<MappingPPC64_44>(args...);
    case 46:
      return Func::template Apply<MappingPPC64_46>(args...);
    case 47:
      return Func::template Apply<MappingPPC64_47>(args...);
  }
#  elif defined(__mips64)
  return Func::template Apply<MappingMips64_40>(args...);
#  elif defined(__s390x__)
  return Func::template Apply<MappingS390x>(args...);
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

#define XSAN_MAP_FUNC(ret, func, parameters, arguments)                 \
  namespace mapping_impl {                                              \
  struct func##Impl {                                                   \
    template <typename Mapping>                                         \
    static ret Apply parameters;                                        \
  };                                                                    \
  }                                                                     \
  ALWAYS_INLINE ret func parameters {                                   \
    return ::__xsan::SelectMapping<mapping_impl::func##Impl> arguments; \
  }                                                                     \
  template <typename Mapping>                                           \
  ALWAYS_INLINE ret mapping_impl::func##Impl::Apply parameters

#define MAP_FIELD(field) Mapping::field

#define XSAN_MAP_FUNC_VOID(ret, func) XSAN_MAP_FUNC(ret, func, (), ())
#define XSAN_MAP_FIELD_FUNC(func, field) \
  XSAN_MAP_FUNC_VOID(uptr, func) { return MAP_FIELD(field); }

XSAN_MAP_FIELD_FUNC(LoAppMemBeg, kLoAppMemBeg)
XSAN_MAP_FIELD_FUNC(LoAppMemEnd, kLoAppMemEnd)
XSAN_MAP_FIELD_FUNC(MidAppMemBeg, kMidAppMemBeg)
XSAN_MAP_FIELD_FUNC(MidAppMemEnd, kMidAppMemEnd)
XSAN_MAP_FIELD_FUNC(HeapMemBeg, kHeapMemBeg)
XSAN_MAP_FIELD_FUNC(HeapMemEnd, kHeapMemEnd)
XSAN_MAP_FIELD_FUNC(HiAppMemBeg, kHiAppMemBeg)
XSAN_MAP_FIELD_FUNC(HiAppMemEnd, kHiAppMemEnd)
XSAN_MAP_FIELD_FUNC(VdsoBeg, kVdsoBeg)

XSAN_MAP_FUNC(bool, IsAppMem, (uptr mem), (mem)) {
  return (mem >= MAP_FIELD(kHeapMemBeg) && mem < MAP_FIELD(kHeapMemEnd)) ||
         (mem >= MAP_FIELD(kMidAppMemBeg) && mem < MAP_FIELD(kMidAppMemEnd)) ||
         (mem >= MAP_FIELD(kLoAppMemBeg) && mem < MAP_FIELD(kLoAppMemEnd)) ||
         (mem >= MAP_FIELD(kHiAppMemBeg) && mem < MAP_FIELD(kHiAppMemEnd));
}

template <typename T>
bool IsAppMem(T ptr) {
  return IsAppMem((uptr)ptr);
}

}  // namespace __xsan
