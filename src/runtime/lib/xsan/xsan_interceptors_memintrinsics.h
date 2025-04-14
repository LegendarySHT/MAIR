#pragma once

#include "asan/asan_interface_xsan.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "xsan_hooks.h"

/// TODO: Implement suppression & flags

DECLARE_REAL(void *, memcpy, void *to, const void *from, SIZE_T size)
DECLARE_REAL(void *, memset, void *block, int c, SIZE_T size)

namespace __xsan {

#define CHECK_RANGES_OVERLAP(name, _offset1, length1, _offset2, length2)       \
  do {                                                                         \
    const char *offset1 = (const char *)_offset1;                              \
    const char *offset2 = (const char *)_offset2;                              \
    if (UNLIKELY(                                                              \
            __asan::AsanRangesOverlap_(offset1, length1, offset2, length2))) { \
      UNINITIALIZED BufferedStackTrace stack;                                  \
      GetStackTraceFatalHere(stack);                                           \
      bool suppressed = __asan::IsInterceptorSuppressed(name);                 \
      if (!suppressed && __asan::HaveStackTraceBasedSuppressions()) {          \
        suppressed = __asan::IsStackTraceSuppressed(&stack);                   \
      }                                                                        \
      if (!suppressed) {                                                       \
        __asan::ReportStringFunctionMemoryRangesOverlap(                       \
            name, offset1, length1, offset2, length2, &stack);                 \
      }                                                                        \
    }                                                                          \
  } while (0)

#define XSAN_READ_RANGE(ctx, offset, size) \
  ::__xsan::ReadRange(ctx, offset, size)
#define XSAN_WRITE_RANGE(ctx, offset, size) \
  ::__xsan::WriteRange(ctx, offset, size)

// --------------------- For sanitizer_common ---------------------

#define XSAN_COMMON_READ_RANGE(ctx, offset, size) \
  ::__xsan::CommonReadRange(ctx, offset, size)

#define XSAN_COMMON_WRITE_RANGE(ctx, offset, size) \
  ::__xsan::CommonWriteRange(ctx, offset, size)

}  // namespace __xsan
