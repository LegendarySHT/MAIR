
/// The following interceptions replace the original __xxx_init functions with
/// __xsan_init. This is performed in link-time by passing -Wl,-wrap,<symbol>.
#include "xsan_interface_internal.h"

extern "C" {

/// Use -Wl,-wrap,<symbol> to intercept the __xxx_init functions and delegate
/// them to __xsan_init.
#define RTL_WRAP(f) __wrap_##f

void RTL_WRAP(__asan_init)() { __xsan_init(); }

void RTL_WRAP(__tsan_init)() { __xsan_init(); }

#undef RTL_WRAP
}
