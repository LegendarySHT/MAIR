#include "asan_report.h"
#include "asan_stack.h"
#include "asan_suppressions.h"

#include "xsan_interceptors.h"


using namespace __xsan;

void *__xsan_memcpy(void *to, const void *from, uptr size) {
  XSAN_MEMCPY_IMPL(nullptr, to, from, size);
}

void *__xsan_memset(void *block, int c, uptr size) {
  XSAN_MEMSET_IMPL(nullptr, block, c, size);
}

void *__xsan_memmove(void *to, const void *from, uptr size) {
  XSAN_MEMMOVE_IMPL(nullptr, to, from, size);
}

#if SANITIZER_FUCHSIA

// Fuchsia doesn't use sanitizer_common_interceptors.inc, but
// the only things there it wants are these three.  Just define them
// as aliases here rather than repeating the contents.

extern "C" decltype(__xsan_memcpy) memcpy[[gnu::alias("__xsan_memcpy")]];
extern "C" decltype(__xsan_memmove) memmove[[gnu::alias("__xsan_memmove")]];
extern "C" decltype(__xsan_memset) memset[[gnu::alias("__xsan_memset")]];

#endif  // SANITIZER_FUCHSIA
