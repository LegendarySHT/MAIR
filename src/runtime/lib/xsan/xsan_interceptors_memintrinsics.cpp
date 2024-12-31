//===-- xsan_interceptors_memintrinsics.cpp -------------------------------===//

#define SANITIZER_COMMON_NO_REDEFINE_BUILTINS

#include "xsan_interceptors_memintrinsics.h"

#include "asan/asan_report.h"
#include "asan/orig/asan_suppressions.h"
#include "xsan_interceptors.h"
#include "xsan_stack.h"

using namespace __xsan;

/// FIXME: is it OKay to use nullptr as ctx? Should __asan_memcpy be ignorable
/// by ScopedIgnoreInterceptors?

// memcpy is called during __xsan_init() from the internals of printf(...).
// We do not treat memcpy with to==from as a bug.
// See http://llvm.org/bugs/show_bug.cgi?id=11763.
#define XSAN_MEMCPY_IMPL(ctx, to, from, size)               \
  do {                                                        \
    if (LIKELY(replace_intrin_cached)) {                      \
      if (LIKELY(to != from)) {                               \
        CHECK_RANGES_OVERLAP("memcpy", to, size, from, size); \
      }                                                       \
      XSAN_READ_RANGE(ctx, from, size);                       \
      XSAN_WRITE_RANGE(ctx, to, size);                        \
    } else if (UNLIKELY(!XsanInited())) {                     \
      return internal_memcpy(to, from, size);                 \
    }                                                         \
    return REAL(memcpy)(to, from, size);                      \
  } while (0)

// memset is called inside Printf.
#define XSAN_MEMSET_IMPL(ctx, block, c, size) \
  do {                                        \
    if (LIKELY(replace_intrin_cached)) {      \
      XSAN_WRITE_RANGE(ctx, block, size);     \
    } else if (UNLIKELY(!XsanInited())) {     \
      return internal_memset(block, c, size); \
    }                                         \
    return REAL(memset)(block, c, size);      \
  } while (0)

#define XSAN_MEMMOVE_IMPL(ctx, to, from, size) \
  do {                                         \
    if (LIKELY(xsan_inited)) {                 \
      XSAN_READ_RANGE(ctx, from, size);        \
      XSAN_WRITE_RANGE(ctx, to, size);         \
    }                                          \
    return internal_memmove(to, from, size);   \
  } while (0)

#define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size) \
  do {                                                       \
    XSAN_INTERCEPTOR_ENTER(ctx, memmove, to, from, size);    \
    XSAN_MEMMOVE_IMPL(ctx, to, from, size);                  \
  } while (false)

#define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size) \
  do {                                                      \
    XSAN_INTERCEPTOR_ENTER(ctx, memcpy, to, from, size);    \
    XSAN_MEMCPY_IMPL(ctx, to, from, size);                  \
  } while (false)

#define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size) \
  do {                                                      \
    XSAN_INTERCEPTOR_ENTER(ctx, memset, block, c, size);    \
    XSAN_MEMSET_IMPL(ctx, block, c, size);                  \
  } while (false)

#if SANITIZER_FUCHSIA

// Fuchsia doesn't use sanitizer_common_interceptors.inc, but
// the only things there it wants are these three.  Just define them
// as aliases here rather than repeating the contents.

extern "C" decltype(__asan_memcpy) memcpy [[gnu::alias("__asan_memcpy")]];
extern "C" decltype(__asan_memmove) memmove [[gnu::alias("__asan_memmove")]];
extern "C" decltype(__asan_memset) memset [[gnu::alias("__asan_memset")]];

#else  // SANITIZER_FUCHSIA

#  include "sanitizer_common/sanitizer_common_interceptors_memintrinsics.inc"

extern "C" {

/// FIXME: migrate to memintrinsics.cpp, adhering
/// https://github.com/llvm/llvm-project/commit/c551c9c311b33a847390f6a57afda3b82d517675
void *__xsan_memcpy(void *dst, const void *src, uptr size) {
  void *ctx;
#  if PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size);
#  else
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
#  endif
}

void *__xsan_memset(void *dst, int c, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, c, size);
}

void *__xsan_memmove(void *dst, const void *src, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
}

/// XSan does own an intrinsic, and thus use ASan's implementation.
void *__asan_memcpy(void *dst, const void *src, uptr size) {
  void *ctx;
#  if PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size);
#  else
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
#  endif
}

void *__asan_memset(void *dst, int c, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, c, size);
}

void *__asan_memmove(void *dst, const void *src, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
}
}

#endif  // SANITIZER_FUCHSIA
