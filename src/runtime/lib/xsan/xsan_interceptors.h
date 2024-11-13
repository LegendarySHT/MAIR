#pragma once

#include <sanitizer_common/sanitizer_platform.h>
#include <sanitizer_common/sanitizer_platform_interceptors.h>

#include "interception/interception.h"
#include "tsan/tsan_interceptors.h"
#include "tsan/tsan_rtl_extra.h"
#include "xsan_interceptors_memintrinsics.h"
#include "xsan_internal.h"
namespace __xsan {

void InitializeXsanInterceptors();
void InitializePlatformInterceptors();

#define ENSURE_XSAN_INITED()      \
  do {                            \
    CHECK(!xsan_init_is_running); \
    if (UNLIKELY(!xsan_inited)) { \
      XsanInitFromRtl();          \
    }                             \
  } while (0)

}  // namespace __xsan

// There is no general interception at all on Fuchsia.
// Only the functions in xsan_interceptors_memintrinsics.h are
// really defined to replace libc functions.
#if !SANITIZER_FUCHSIA

// Use macro to describe if specific function should be
// intercepted on a given platform.
#  if !SANITIZER_WINDOWS
#    define XSAN_INTERCEPT_ATOLL_AND_STRTOLL 1
#    define XSAN_INTERCEPT__LONGJMP 1
#    define XSAN_INTERCEPT_INDEX 1
#    define XSAN_INTERCEPT_PTHREAD_CREATE 1
#  else
#    define XSAN_INTERCEPT_ATOLL_AND_STRTOLL 0
#    define XSAN_INTERCEPT__LONGJMP 0
#    define XSAN_INTERCEPT_INDEX 0
#    define XSAN_INTERCEPT_PTHREAD_CREATE 0
#  endif

#  if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
      SANITIZER_SOLARIS
#    define XSAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX 1
#  else
#    define XSAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX 0
#  endif

#  if SANITIZER_GLIBC || SANITIZER_SOLARIS
#    define XSAN_INTERCEPT_SWAPCONTEXT 1
#  else
#    define XSAN_INTERCEPT_SWAPCONTEXT 0
#  endif

#  if !SANITIZER_WINDOWS
#    define XSAN_INTERCEPT_SIGLONGJMP 1
#  else
#    define XSAN_INTERCEPT_SIGLONGJMP 0
#  endif

#  if SANITIZER_GLIBC
#    define XSAN_INTERCEPT___LONGJMP_CHK 1
#  else
#    define XSAN_INTERCEPT___LONGJMP_CHK 0
#  endif

#  if XSAN_HAS_EXCEPTIONS && !SANITIZER_WINDOWS && !SANITIZER_SOLARIS && \
      !SANITIZER_NETBSD
#    define XSAN_INTERCEPT___CXA_THROW 1
#    define XSAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION 1
#    if defined(_GLIBCXX_SJLJ_EXCEPTIONS) || (SANITIZER_IOS && defined(__xrm__))
#      define XSAN_INTERCEPT__UNWIND_SJLJ_RAISEEXCEPTION 1
#    else
#      define XSAN_INTERCEPT__UNWIND_RAISEEXCEPTION 1
#    endif
#  else
#    define XSAN_INTERCEPT___CXA_THROW 0
#    define XSAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION 0
#    define XSAN_INTERCEPT__UNWIND_RAISEEXCEPTION 0
#    define XSAN_INTERCEPT__UNWIND_SJLJ_RAISEEXCEPTION 0
#  endif

#  if !SANITIZER_WINDOWS
#    define XSAN_INTERCEPT___CXA_ATEXIT 1
#  else
#    define XSAN_INTERCEPT___CXA_ATEXIT 0
#  endif

#  if SANITIZER_NETBSD
#    define XSAN_INTERCEPT_ATEXIT 1
#  else
#    define XSAN_INTERCEPT_ATEXIT 0
#  endif

#  if SANITIZER_GLIBC
#    define XSAN_INTERCEPT___STRDUP 1
#  else
#    define XSAN_INTERCEPT___STRDUP 0
#  endif

#  if SANITIZER_LINUX &&                                                \
      (defined(__xrm__) || defined(__xarch64__) || defined(__i386__) || \
       defined(__x86_64__) || SANITIZER_RISCV64)
#    define XSAN_INTERCEPT_VFORK 1
#  else
#    define XSAN_INTERCEPT_VFORK 0
#  endif

#  if SANITIZER_NETBSD
#    define XSAN_INTERCEPT_PTHREAD_ATFORK 1
#  else
#    define XSAN_INTERCEPT_PTHREAD_ATFORK 0
#  endif

DECLARE_REAL(int, memcmp, const void *a1, const void *a2, uptr size)
DECLARE_REAL(char *, strchr, const char *str, int c)
DECLARE_REAL(SIZE_T, strlen, const char *s)
DECLARE_REAL(char *, strncpy, char *to, const char *from, uptr size)
DECLARE_REAL(uptr, strnlen, const char *s, uptr maxlen)
DECLARE_REAL(char *, strstr, const char *s1, const char *s2)

#  if !SANITIZER_APPLE
#    define XSAN_INTERCEPT_FUNC(name)                                  \
      do {                                                             \
        if (!INTERCEPT_FUNCTION(name))                                 \
          VReport(1, "XSanitizer: failed to intercept '%s'\n", #name); \
      } while (0)
#    define XSAN_INTERCEPT_FUNC_VER(name, ver)                            \
      do {                                                                \
        if (!INTERCEPT_FUNCTION_VER(name, ver))                           \
          VReport(1, "XSanitizer: failed to intercept '%s@@%s'\n", #name, \
                  ver);                                                   \
      } while (0)
#    define XSAN_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)          \
      do {                                                                   \
        if (!INTERCEPT_FUNCTION_VER(name, ver) && !INTERCEPT_FUNCTION(name)) \
          VReport(1, "XSanitizer: failed to intercept '%s@@%s' or '%s'\n",   \
                  #name, ver, #name);                                        \
      } while (0)

#  else
// OS X interceptors don't need to be initialized with INTERCEPT_FUNCTION.
#    define XSAN_INTERCEPT_FUNC(name)
#  endif  // SANITIZER_APPLE

#endif  // !SANITIZER_FUCHSIA

namespace __xsan {
/// Represents the extra arguments for alloc. series APIs
/// - ASan needs:
///     - BufferredStackTrace *stack
/// - TSan needs:
///     - ThreadState *thr
///     - uptr pc
struct TsanArgs {
  __tsan::ThreadState *thr_;
  uptr pc_;
};

class ScopedIgnoreInterceptors {
 public:
  ScopedIgnoreInterceptors(bool in_report = false) : sit(in_report) {}
  ~ScopedIgnoreInterceptors() {}

 private:
  __tsan::ScopedIgnoreInterceptors tsan_sii;
  __tsan::ScopedIgnoreTsan sit;
};

}  // namespace __xsan

#define XSAN_EXTRA_ALLOC_ARG(func, ...)                        \
  GET_STACK_TRACE_MALLOC;                                      \
  __xsan::XsanThread *xsan_thr = __xsan::GetCurrentThread();   \
  __tsan::ScopedInterceptor tsi(xsan_thr->tsan_thread_, #func, \
                                stack.trace_buffer[1]);        \
  xsan_thr->setTsanArgs(stack.trace_buffer[0]);
