#pragma once

#include <sanitizer_common/sanitizer_internal_defs.h>
#include <sanitizer_common/sanitizer_libc.h>
#include <sanitizer_common/sanitizer_mutex.h>
#include <ubsan/ubsan_platform.h>

#include "xsan_internal.h"

#ifndef XSAN_CONTAINS_UBSAN
# if CAN_SANITIZE_UB && !SANITIZER_GO
#  define XSAN_CONTAINS_UBSAN 1
# else
#  define XSAN_CONTAINS_UBSAN 0
# endif
#endif


#ifndef XSAN_CONTAINS_TSAN
# if !SANITIZER_GO
#  define XSAN_CONTAINS_TSAN 1
# endif
#endif



namespace __xsan {

/// The default alignment for heap allocations.
/// ASan's original alignment is 8 while TSan requires 16.
/// XSan is backward compatible to 16
const uptr kDefaultAlignment = 16;
}  // namespace __xsan