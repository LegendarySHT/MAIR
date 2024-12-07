#include "asan/orig/asan_report.h"
#include "asan/orig/asan_stack.h"
#include "asan/orig/asan_suppressions.h"
#include "xsan_interceptors.h"
#include "xsan_thread.h"
#include "xsan_interceptors.h"


using namespace __xsan;

/// FIXME: is it OKay to use nullptr as ctx? Should __asan_memcpy be ignorable
/// by ScopedIgnoreInterceptors?


#if SANITIZER_FUCHSIA

// Fuchsia doesn't use sanitizer_common_interceptors.inc, but
// the only things there it wants are these three.  Just define them
// as aliases here rather than repeating the contents.

extern "C" decltype(__xsan_memcpy) memcpy [[gnu::alias("__xsan_memcpy")]];
extern "C" decltype(__xsan_memmove) memmove [[gnu::alias("__xsan_memmove")]];
extern "C" decltype(__xsan_memset) memset [[gnu::alias("__xsan_memset")]];

#endif  // SANITIZER_FUCHSIA
