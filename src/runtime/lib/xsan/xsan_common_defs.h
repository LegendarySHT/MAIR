#pragma once

#include <sanitizer_common/sanitizer_internal_defs.h>
#include <sanitizer_common/sanitizer_libc.h>
#include <sanitizer_common/sanitizer_mutex.h>
#include <sanitizer_common/sanitizer_platform.h>
#include <ubsan/ubsan_platform.h>

#include "sanitizer_common/sanitizer_platform_limits_netbsd.h"
#include "sanitizer_common/sanitizer_platform_limits_posix.h"

// We have already defined and assigned values to the XSAN_CONTAINS_XXX macros in
// CMakeLists.txt, here we are just dealing with special cases.
#if !CAN_SANITIZE_UB || SANITIZER_GO
#  define XSAN_CONTAINS_UBSAN 0
#endif

#if SANITIZER_GO
#  define XSAN_CONTAINS_TSAN 0
#endif

#define XSAN_REAL(f) __real_##f
#define XSAN_WRAP(f) __wrap_##f

#define XSAN_DECLARE_REAL(ret_ty, f, ...) \
  extern "C" ret_ty XSAN_REAL(f)(__VA_ARGS__);

#define XSAN_DECLARE_WRAPPER(ret_ty, f, ...) \
  extern "C" ret_ty XSAN_WRAP(f)(__VA_ARGS__);

#define XSAN_WRAPPER(ret_ty, f, ...)        \
  XSAN_DECLARE_REAL(ret_ty, f, __VA_ARGS__) \
  extern "C" ret_ty XSAN_WRAP(f)(__VA_ARGS__)

#define INTERCEPT_AND_IGNORE_VOID(ret, f)                 \
  XSAN_WRAPPER(ret, f, void) {                            \
    __xsan::ScopedIgnoreInterceptors ignore_interceptors; \
    return XSAN_REAL(f)();                                \
  }

#define INTERCEPT_AND_IGNORE(ret, f, params, args)        \
  extern "C" ret XSAN_REAL(f) params;                     \
  extern "C" ret XSAN_WRAP(f) params {                    \
    __xsan::ScopedIgnoreInterceptors ignore_interceptors; \
    return XSAN_REAL(f) args;                             \
  }

namespace __xsan {

/// The default alignment for heap allocations.
/// ASan's original alignment is 8 while TSan requires 16.
/// XSan is backward compatible to 16
const uptr kDefaultAlignment = 16;
}  // namespace __xsan
