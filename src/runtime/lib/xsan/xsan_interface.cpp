//===-- xsan_interface.cpp ------------------------------------------------===//
//
// This file is a part of XSan, a composition of different Sanitizers.
//
// This file is used to provide some uniform interface for XSan runtime.
//===----------------------------------------------------------------------===//

#include <sanitizer_common/sanitizer_atomic.h>
#include <sanitizer_common/sanitizer_flags.h>
#include <sanitizer_common/sanitizer_interface_internal.h>
#include <sanitizer_common/sanitizer_libc.h>
#include <sanitizer_common/sanitizer_ptrauth.h>

#include "asan/asan_mapping.h"
#include "tsan/orig/tsan_interface.h"
#include "xsan_internal.h"

using namespace __xsan;

/// TODO: use a more generic way to perform checks.
/// For example, provide some hooks for events, and integrate the checks into these hooks.
///   - OnAllocation:
///   - OnFree:
///   - OnMemoryAccess:
///     ....

#define CHECK_SMALL_REGION(p, size, isWrite)                      \
  do {                                                            \
    uptr __p = reinterpret_cast<uptr>(p);                         \
    uptr __size = size;                                           \
    if (UNLIKELY(__asan::AddressIsPoisoned(__p) ||                \
                 __asan::AddressIsPoisoned(__p + __size - 1))) {  \
      GET_CURRENT_PC_BP_SP;                                       \
      uptr __bad = __asan_region_is_poisoned(__p, __size);        \
      __asan_report_error(pc, bp, sp, __bad, isWrite, __size, 0); \
    }                                                             \
  } while (false)

extern "C" {

SANITIZER_INTERFACE_ATTRIBUTE
u16 __sanitizer_unaligned_load16(const uu16 *addr) {
  CHECK_SMALL_REGION(addr, sizeof(*addr), false);
  __tsan_unaligned_read2(addr);
  return *addr;
}

SANITIZER_INTERFACE_ATTRIBUTE
u32 __sanitizer_unaligned_load32(const uu32 *addr) {
  CHECK_SMALL_REGION(addr, sizeof(*addr), false);
  __tsan_unaligned_read4(addr);
  return *addr;
}

SANITIZER_INTERFACE_ATTRIBUTE
u64 __sanitizer_unaligned_load64(const uu64 *addr) {
  CHECK_SMALL_REGION(addr, sizeof(*addr), false);
  __tsan_unaligned_read8(addr);
  return *addr;
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store16(uu16 *addr, u16 v) {
  CHECK_SMALL_REGION(addr, sizeof(*addr), true);
  *addr = v;
  __tsan_unaligned_write2(addr);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store32(uu32 *addr, u32 v) {
  CHECK_SMALL_REGION(addr, sizeof(*addr), true);
  *addr = v;
  __tsan_unaligned_write4(addr);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_unaligned_store64(uu64 *addr, u64 v) {
  CHECK_SMALL_REGION(addr, sizeof(*addr), true);
  *addr = v;
  __tsan_unaligned_write8(addr);
}
}
