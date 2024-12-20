
/// The following interceptions replace the original __xxx_init functions with
/// __xsan_init. This is performed in link-time by passing -Wl,-wrap,<symbol>.
#include "xsan_interface_internal.h"

extern "C" {

/// Use -Wl,-wrap,<symbol> to intercept the __xxx_init functions and delegate
/// them to __xsan_init.
#define RTL_WRAP(f) __wrap_##f

#define INTERCEPT_AND_REDIRECT(ret, f, ...) \
  ret RTL_WRAP(f)(__VA_ARGS__) { __xsan_init(); }

INTERCEPT_AND_REDIRECT(void, __asan_init)
INTERCEPT_AND_REDIRECT(void, __tsan_init)

#undef INTERCEPT_AND_REDIRECT
#undef RTL_WRAP
}
