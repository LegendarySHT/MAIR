//===-- asan_interceptors_memintrinsics.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan versions of memcpy, memmove, and memset.
//===---------------------------------------------------------------------===//

#define SANITIZER_COMMON_NO_REDEFINE_BUILTINS

#include "asan_interceptors_memintrinsics.h"

#include "asan_interceptors.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_suppressions.h"

using namespace __asan;

namespace __asan {

/// The code in this namespace is moved from asan_interceptors_memintrinsics.h

// Return true if we can quickly decide that the region is unpoisoned.
// We assume that a redzone is at least 16 bytes.
static inline bool QuickCheckForUnpoisonedRegion(uptr beg, uptr size) {
  if (UNLIKELY(size == 0 || size > sizeof(uptr) * ASAN_SHADOW_GRANULARITY))
    return !size;

  uptr last = beg + size - 1;
  uptr shadow_first = MEM_TO_SHADOW(beg);
  uptr shadow_last = MEM_TO_SHADOW(last);
  uptr uptr_first = RoundDownTo(shadow_first, sizeof(uptr));
  uptr uptr_last = RoundDownTo(shadow_last, sizeof(uptr));
  if (LIKELY(((*reinterpret_cast<const uptr *>(uptr_first) |
               *reinterpret_cast<const uptr *>(uptr_last)) == 0)))
    return true;
  u8 shadow = AddressIsPoisoned(last);
  for (; shadow_first < shadow_last; ++shadow_first)
    shadow |= *((u8 *)shadow_first);
  return !shadow;
}

struct AsanInterceptorContext {
  const char *interceptor_name;
};

// We implement ACCESS_MEMORY_RANGE, ASAN_READ_RANGE,
// and ASAN_WRITE_RANGE as macro instead of function so
// that no extra frames are created, and stack trace contains
// relevant information only.
// We check all shadow bytes.
#define ACCESS_MEMORY_RANGE(ctx, offset, size, isWrite)                   \
  do {                                                                    \
    uptr __offset = (uptr)(offset);                                       \
    uptr __size = (uptr)(size);                                           \
    uptr __bad = 0;                                                       \
    if (UNLIKELY(__offset > __offset + __size)) {                         \
      GET_STACK_TRACE_FATAL_HERE;                                         \
      ReportStringFunctionSizeOverflow(__offset, __size, &stack);         \
    }                                                                     \
    if (UNLIKELY(!QuickCheckForUnpoisonedRegion(__offset, __size)) &&     \
        (__bad = __asan_region_is_poisoned(__offset, __size))) {          \
      AsanInterceptorContext *_ctx = (AsanInterceptorContext *)ctx;       \
      bool suppressed = false;                                            \
      if (_ctx) {                                                         \
        suppressed = IsInterceptorSuppressed(_ctx->interceptor_name);     \
        if (!suppressed && HaveStackTraceBasedSuppressions()) {           \
          GET_STACK_TRACE_FATAL_HERE;                                     \
          suppressed = IsStackTraceSuppressed(&stack);                    \
        }                                                                 \
      }                                                                   \
      if (!suppressed) {                                                  \
        GET_CURRENT_PC_BP_SP;                                             \
        ReportGenericError(pc, bp, sp, __bad, isWrite, __size, 0, false); \
      }                                                                   \
    }                                                                     \
  } while (0)

#define ASAN_READ_RANGE(ctx, offset, size) \
  ACCESS_MEMORY_RANGE(ctx, offset, size, false)
#define ASAN_WRITE_RANGE(ctx, offset, size) \
  ACCESS_MEMORY_RANGE(ctx, offset, size, true)

// Behavior of functions like "memcpy" or "strcpy" is undefined
// if memory intervals overlap. We report error in this case.
// Macro is used to avoid creation of new frames.
static inline bool RangesOverlap(const char *offset1, uptr length1,
                                 const char *offset2, uptr length2) {
  return !((offset1 + length1 <= offset2) || (offset2 + length2 <= offset1));
}

#define CHECK_RANGES_OVERLAP(name, _offset1, length1, _offset2, length2)   \
  do {                                                                     \
    const char *offset1 = (const char *)_offset1;                          \
    const char *offset2 = (const char *)_offset2;                          \
    if (UNLIKELY(RangesOverlap(offset1, length1, offset2, length2))) {     \
      GET_STACK_TRACE_FATAL_HERE;                                          \
      bool suppressed = IsInterceptorSuppressed(name);                     \
      if (!suppressed && HaveStackTraceBasedSuppressions()) {              \
        suppressed = IsStackTraceSuppressed(&stack);                       \
      }                                                                    \
      if (!suppressed) {                                                   \
        ReportStringFunctionMemoryRangesOverlap(name, offset1, length1,    \
                                                offset2, length2, &stack); \
      }                                                                    \
    }                                                                      \
  } while (0)

}  // namespace __asan

// memcpy is called during __asan_init() from the internals of printf(...).
// We do not treat memcpy with to==from as a bug.
// See http://llvm.org/bugs/show_bug.cgi?id=11763.
#define ASAN_MEMCPY_IMPL(ctx, to, from, size)                 \
  do {                                                        \
    if (LIKELY(replace_intrin_cached)) {                      \
      if (LIKELY(to != from)) {                               \
        CHECK_RANGES_OVERLAP("memcpy", to, size, from, size); \
      }                                                       \
      ASAN_READ_RANGE(ctx, from, size);                       \
      ASAN_WRITE_RANGE(ctx, to, size);                        \
    } else if (UNLIKELY(!AsanInited())) {                     \
      return internal_memcpy(to, from, size);                 \
    }                                                         \
    return REAL(memcpy)(to, from, size);                      \
  } while (0)

// memset is called inside Printf.
#define ASAN_MEMSET_IMPL(ctx, block, c, size) \
  do {                                        \
    if (LIKELY(replace_intrin_cached)) {      \
      ASAN_WRITE_RANGE(ctx, block, size);     \
    } else if (UNLIKELY(!AsanInited())) {     \
      return internal_memset(block, c, size); \
    }                                         \
    return REAL(memset)(block, c, size);      \
  } while (0)

#define ASAN_MEMMOVE_IMPL(ctx, to, from, size) \
  do {                                         \
    if (LIKELY(replace_intrin_cached)) {       \
      ASAN_READ_RANGE(ctx, from, size);        \
      ASAN_WRITE_RANGE(ctx, to, size);         \
    }                                          \
    return internal_memmove(to, from, size);   \
  } while (0)

void *__asan_memcpy(void *to, const void *from, uptr size) {
  ASAN_MEMCPY_IMPL(nullptr, to, from, size);
}

void *__asan_memset(void *block, int c, uptr size) {
  ASAN_MEMSET_IMPL(nullptr, block, c, size);
}

void *__asan_memmove(void *to, const void *from, uptr size) {
  ASAN_MEMMOVE_IMPL(nullptr, to, from, size);
}

#if SANITIZER_FUCHSIA

// Fuchsia doesn't use sanitizer_common_interceptors.inc, but
// the only things there it wants are these three.  Just define them
// as aliases here rather than repeating the contents.

extern "C" decltype(__asan_memcpy) memcpy[[gnu::alias("__asan_memcpy")]];
extern "C" decltype(__asan_memmove) memmove[[gnu::alias("__asan_memmove")]];
extern "C" decltype(__asan_memset) memset[[gnu::alias("__asan_memset")]];

#else  // SANITIZER_FUCHSIA

// #define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size) \
//   do {                                                       \
//     ASAN_INTERCEPTOR_ENTER(ctx, memmove);                    \
//     ASAN_MEMMOVE_IMPL(ctx, to, from, size);                  \
//   } while (false)

// #define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size) \
//   do {                                                      \
//     ASAN_INTERCEPTOR_ENTER(ctx, memcpy);                    \
//     ASAN_MEMCPY_IMPL(ctx, to, from, size);                  \
//   } while (false)

// #define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size) \
//   do {                                                      \
//     ASAN_INTERCEPTOR_ENTER(ctx, memset);                    \
//     ASAN_MEMSET_IMPL(ctx, block, c, size);                  \
//   } while (false)

// #include "sanitizer_common/sanitizer_common_interceptors_memintrinsics.inc"

#endif  // SANITIZER_FUCHSIA
