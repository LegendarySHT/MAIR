#pragma once

#include "asan/asan_interceptors_memintrinsics.h"
#include "asan/asan_mapping.h"
#include "asan/orig/asan_internal.h"
#include "asan/orig/asan_report.h"
#include "asan/orig/asan_stack.h"
#include "asan/orig/asan_suppressions.h"
#include "asan/orig/asan_interface_internal.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "xsan_hooks.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"

/// TODO: Implement suppression & flags

DECLARE_REAL(void *, memcpy, void *to, const void *from, SIZE_T size)
DECLARE_REAL(void *, memset, void *block, int c, SIZE_T size)

namespace __tsan {
struct ThreadState;
}

namespace __xsan {

// struct XsanInterceptorContext {
//   const char *interceptor_name;
//   /// TODO: should use pointer or reference?
//   XsanContext xsan_ctx;
// };

#define CHECK_RANGES_OVERLAP(name, _offset1, length1, _offset2, length2)      \
  do {                                                                        \
    const char *offset1 = (const char *)_offset1;                             \
    const char *offset2 = (const char *)_offset2;                             \
    if (UNLIKELY(                                                             \
            __asan::AsanRangesOverlap(offset1, length1, offset2, length2))) { \
      GET_STACK_TRACE_FATAL_HERE;                                             \
      bool suppressed = __asan::IsInterceptorSuppressed(name);                \
      if (!suppressed && __asan::HaveStackTraceBasedSuppressions()) {         \
        suppressed = __asan::IsStackTraceSuppressed(&stack);                  \
      }                                                                       \
      if (!suppressed) {                                                      \
        __asan::ReportStringFunctionMemoryRangesOverlap(                      \
            name, offset1, length1, offset2, length2, &stack);                \
      }                                                                       \
    }                                                                         \
  } while (0)

// We implement ACCESS_MEMORY_RANGE, ASAN_READ_RANGE,
// and ASAN_WRITE_RANGE as macro instead of function so
// that no extra frames are created, and stack trace contains
// relevant information only.
// We check all shadow bytes.
#define ASAN_ACCESS_MEMORY_RANGE(ctx, offset, size, isWrite)                  \
  do {                                                                        \
    uptr __offset = (uptr)(offset);                                           \
    uptr __size = (uptr)(size);                                               \
    uptr __bad = 0;                                                           \
    if (UNLIKELY(__offset > __offset + __size)) {                             \
      GET_STACK_TRACE_FATAL_HERE;                                             \
      __asan::ReportStringFunctionSizeOverflow(__offset, __size, &stack);     \
    }                                                                         \
    if (UNLIKELY(                                                             \
            !__asan::AsanQuickCheckForUnpoisonedRegion(__offset, __size)) &&  \
        (__bad = __asan_region_is_poisoned(__offset, __size))) {              \
      XsanInterceptorContext *_ctx = (XsanInterceptorContext *)(ctx);         \
      bool suppressed = false;                                                \
      if (_ctx) {                                                             \
        suppressed = __asan::IsInterceptorSuppressed(_ctx->interceptor_name); \
        if (!suppressed && __asan::HaveStackTraceBasedSuppressions()) {       \
          GET_STACK_TRACE_FATAL_HERE;                                         \
          suppressed = __asan::IsStackTraceSuppressed(&stack);                \
        }                                                                     \
      }                                                                       \
      if (!suppressed) {                                                      \
        GET_CURRENT_PC_BP_SP;                                                 \
        __asan::ReportGenericError(pc, bp, sp, __bad, isWrite, __size, 0,     \
                                   false);                                    \
      }                                                                       \
    }                                                                         \
  } while (0)

/// TODO: mange this in a better way
#define TSAN_ACCESS_MEMORY_RANGE(ctx, offset, size, isWrite)                 \
  do {                                                                       \
    XsanInterceptorContext *_ctx = (XsanInterceptorContext *)(ctx);          \
    if (_ctx) {                                                              \
      auto [thr, pc] = _ctx->xsan_ctx.tsan;                                  \
      __tsan::MemoryAccessRange(thr, pc, (uptr)(offset), (size), (isWrite)); \
    } else {                                                                 \
      __tsan::MemoryAccessRange(__tsan::cur_thread_init(), GET_CURRENT_PC(), \
                                (uptr)(offset), (size), (isWrite));          \
    }                                                                        \
  } while (0)

#define XSAN_ACCESS_MEMORY_RANGE(ctx, offset, size, isWrite) \
  do {                                                       \
    ASAN_ACCESS_MEMORY_RANGE(ctx, offset, size, isWrite);    \
    TSAN_ACCESS_MEMORY_RANGE(ctx, offset, size, isWrite);    \
  } while (0)


#define XSAN_READ_RANGE(ctx, offset, size) \
  XSAN_ACCESS_MEMORY_RANGE(ctx, offset, size, false)
#define XSAN_WRITE_RANGE(ctx, offset, size) \
  XSAN_ACCESS_MEMORY_RANGE(ctx, offset, size, true)

}  // namespace __xsan
