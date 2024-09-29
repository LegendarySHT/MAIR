#include "asan/orig/asan_internal.h"
#include "asan/orig/asan_report.h"
#include "asan/orig/asan_suppressions.h"

#include "xsan_interceptors.h"
#include "xsan_internal.h"
#include "xsan_stack.h"
#include "lsan/lsan_common.h"
#include "sanitizer_common/sanitizer_libc.h"

// There is no general interception at all on Fuchsia.
// Only the functions in xsan_interceptors_memintrinsics.cpp are
// really defined to replace libc functions.
#if !SANITIZER_FUCHSIA

#  if SANITIZER_POSIX
#    include "sanitizer_common/sanitizer_posix.h"
#  endif

#  if XSAN_INTERCEPT__UNWIND_RAISEEXCEPTION || \
      XSAN_INTERCEPT__SJLJ_UNWIND_RAISEEXCEPTION
#    include <unwind.h>
#  endif

#  if defined(__i386) && SANITIZER_LINUX
#    define XSAN_PTHREAD_CREATE_VERSION "GLIBC_2.1"
#  elif defined(__mips__) && SANITIZER_LINUX
#    define XSAN_PTHREAD_CREATE_VERSION "GLIBC_2.2"
#  endif

namespace __xsan {

#define XSAN_READ_STRING_OF_LEN(ctx, s, len, n)                 \
  XSAN_READ_RANGE((ctx), (s),                                   \
    common_flags()->strict_string_checks ? (len) + 1 : (n))

#  define XSAN_READ_STRING(ctx, s, n) \
    XSAN_READ_STRING_OF_LEN((ctx), (s), internal_strlen(s), (n))

static inline uptr MaybeRealStrnlen(const char *s, uptr maxlen) {
#if SANITIZER_INTERCEPT_STRNLEN
  if (REAL(strnlen)) {
    return REAL(strnlen)(s, maxlen);
  }
#endif
  return internal_strnlen(s, maxlen);
}

/// FIXME: does Xsan need this?
void SetThreadName(const char *name) {
//   AsanThread *t = GetCurrentThread();
//   if (t)
//     asanThreadRegistry().SetThreadName(t->tid(), name);
}

int OnExit() {
  if (CAN_SANITIZE_LEAKS && common_flags()->detect_leaks &&
      __lsan::HasReportedLeaks()) {
    return common_flags()->exitcode;
  }
  // FIXME: ask frontend whether we need to return failure.
  return 0;
}

} // namespace __xsan

// ---------------------- Wrappers ---------------- {{{1
using namespace __xsan;

DECLARE_REAL_AND_INTERCEPTOR(void *, malloc, uptr)
DECLARE_REAL_AND_INTERCEPTOR(void, free, void *)

#define XSAN_INTERCEPTOR_ENTER(ctx, func)                                      \
  XsanInterceptorContext _ctx = {#func};                                       \
  ctx = (void *)&_ctx;                                                         \
  (void) ctx;                                                                  \

/// FIXME: ASan:
//    if (flags()->strict_init_order)               
//         StopInitOrderChecking();                    
#define XSAN_BEFORE_DLOPEN(filename, flag) \
    ({                                              \
        (void)filename; \
    })

#define COMMON_INTERCEPT_FUNCTION(name) XSAN_INTERCEPT_FUNC(name)
#define COMMON_INTERCEPT_FUNCTION_VER(name, ver) \
  XSAN_INTERCEPT_FUNC_VER(name, ver)
#define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver) \
  XSAN_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)
#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size) \
  XSAN_WRITE_RANGE(ctx, ptr, size)
#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size) \
  XSAN_READ_RANGE(ctx, ptr, size)
#define COMMON_INTERCEPTOR_ENTER(ctx, func, ...)                               \
  XSAN_INTERCEPTOR_ENTER(ctx, func);                                           \
  do {                                                                         \
    if (xsan_init_is_running)                                                  \
      return REAL(func)(__VA_ARGS__);                                          \
    if (SANITIZER_APPLE && UNLIKELY(!xsan_inited))                               \
      return REAL(func)(__VA_ARGS__);                                          \
    ENSURE_XSAN_INITED();                                                      \
  } while (false)
#define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path) \
  do {                                            \
  } while (false)
#define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd) \
  do {                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd) \
  do {                                         \
  } while (false)
#define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd) \
  do {                                                      \
  } while (false)
#define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name) SetThreadName(name)
// Should be asanThreadRegistry().SetThreadNameByUserId(thread, name)
// But asan does not remember UserId's for threads (pthread_t);
// and remembers all ever existed threads, so the linear search by UserId
// can be slow.
#define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name) \
  do {                                                         \
  } while (false)
#define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)
// Strict init-order checking is dlopen-hostile:
// https://github.com/google/sanitizers/issues/178
#  define COMMON_INTERCEPTOR_DLOPEN(filename, flag) \
    ({                                              \
      XSAN_BEFORE_DLOPEN(filename, flag);           \
      CheckNoDeepBind(filename, flag);              \
      REAL(dlopen)(filename, flag);                 \
    })
#  define COMMON_INTERCEPTOR_ON_EXIT(ctx) OnExit()
#  define COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, handle)
#  define COMMON_INTERCEPTOR_LIBRARY_UNLOADED()
#  define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED (!xsan_inited)

/// FIXME:
// #  define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end) \
//     if (AsanThread *t = GetCurrentThread()) {          \
//       *begin = t->tls_begin();                         \
//       *end = t->tls_end();                             \
//     } else {                                           \
//       *begin = *end = 0;                               \
//     }

#define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size) \
  do {                                                       \
    XSAN_INTERCEPTOR_ENTER(ctx, memmove);                    \
    XSAN_MEMMOVE_IMPL(ctx, to, from, size);                  \
  } while (false)

#define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size) \
  do {                                                      \
    XSAN_INTERCEPTOR_ENTER(ctx, memcpy);                    \
    XSAN_MEMCPY_IMPL(ctx, to, from, size);                  \
  } while (false)

#define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size) \
  do {                                                      \
    XSAN_INTERCEPTOR_ENTER(ctx, memset);                    \
    XSAN_MEMSET_IMPL(ctx, block, c, size);                  \
  } while (false)

#if CAN_SANITIZE_LEAKS
#define COMMON_INTERCEPTOR_STRERROR()                       \
  __lsan::ScopedInterceptorDisabler disabler
#endif

#include "sanitizer_common/sanitizer_common_interceptors.inc"
#include "sanitizer_common/sanitizer_signal_interceptors.inc"

// Syscall interceptors don't have contexts, we don't support suppressions
// for them.
#define COMMON_SYSCALL_PRE_READ_RANGE(p, s) XSAN_READ_RANGE(nullptr, p, s)
#define COMMON_SYSCALL_PRE_WRITE_RANGE(p, s) XSAN_WRITE_RANGE(nullptr, p, s)
#define COMMON_SYSCALL_POST_READ_RANGE(p, s) \
  do {                                       \
    (void)(p);                               \
    (void)(s);                               \
  } while (false)
#define COMMON_SYSCALL_POST_WRITE_RANGE(p, s) \
  do {                                        \
    (void)(p);                                \
    (void)(s);                                \
  } while (false)
#include "sanitizer_common/sanitizer_common_syscalls.inc"
#include "sanitizer_common/sanitizer_syscalls_netbsd.inc"

#if XSAN_INTERCEPT_PTHREAD_CREATE
// static thread_return_t THREAD_CALLING_CONV xsan_thread_start(void *arg) {
//   AsanThread *t = (AsanThread *)arg;
//   SetCurrentThread(t);
//   return t->ThreadStart(GetTid());
// }

INTERCEPTOR(int, pthread_create, void *thread,
    void *attr, void *(*start_routine)(void*), void *arg) {
  int result;
  /// FIXME: scope needed for __lsan::ScopedInterceptorDisabler
  /// FIXME: param modified!
  {
    result = REAL(pthread_create)(thread, attr, start_routine, arg);
  }
  return result;
}

INTERCEPTOR(int, pthread_join, void *t, void **arg) {
  return real_pthread_join(t, arg);
}

DEFINE_REAL_PTHREAD_FUNCTIONS
#endif  // XSAN_INTERCEPT_PTHREAD_CREATE

#if XSAN_INTERCEPT_SWAPCONTEXT
// static void ClearShadowMemoryForContextStack(uptr stack, uptr ssize) {
//   // Only clear if we know the stack. This should be true only for contexts
//   // created with makecontext().
//   if (!ssize)
//     return;
//   // Align to page size.
//   uptr PageSize = GetPageSizeCached();
//   uptr bottom = RoundDownTo(stack, PageSize);
//   if (!AddrIsInMem(bottom))
//     return;
//   ssize += stack - bottom;
//   ssize = RoundUpTo(ssize, PageSize);
//   PoisonShadow(bottom, ssize, 0);
// }

INTERCEPTOR(int, getcontext, struct ucontext_t *ucp) {
  // API does not requires to have ucp clean, and sets only part of fields. We
  // use ucp->uc_stack to unpoison new stack. We prefer to have zeroes then
  // uninitialized bytes.
  ResetContextStack(ucp);
  return REAL(getcontext)(ucp);
}

INTERCEPTOR(int, swapcontext, struct ucontext_t *oucp,
            struct ucontext_t *ucp) {
#    if __has_attribute(__indirect_return__) && \
        (defined(__x86_64__) || defined(__i386__))
  int (*real_swapcontext)(struct ucontext_t *, struct ucontext_t *)
      __attribute__((__indirect_return__)) = REAL(swapcontext);
  int res = real_swapcontext(oucp, ucp);
#    else
  int res = REAL(swapcontext)(oucp, ucp);
#    endif
  return res;
}
#endif  // XSAN_INTERCEPT_SWAPCONTEXT

#if SANITIZER_NETBSD
#define longjmp __longjmp14
#define siglongjmp __siglongjmp14
#endif

INTERCEPTOR(void, longjmp, void *env, int val) {
  __xsan_handle_no_return();
  REAL(longjmp)(env, val);
}

#if XSAN_INTERCEPT__LONGJMP
INTERCEPTOR(void, _longjmp, void *env, int val) {
  __xsan_handle_no_return();
  REAL(_longjmp)(env, val);
}
#endif

#if XSAN_INTERCEPT___LONGJMP_CHK
INTERCEPTOR(void, __longjmp_chk, void *env, int val) {
  __xsan_handle_no_return();
  REAL(__longjmp_chk)(env, val);
}
#endif

#if XSAN_INTERCEPT_SIGLONGJMP
INTERCEPTOR(void, siglongjmp, void *env, int val) {
  __xsan_handle_no_return();
  REAL(siglongjmp)(env, val);
}
#endif

#if XSAN_INTERCEPT___CXA_THROW
INTERCEPTOR(void, __cxa_throw, void *a, void *b, void *c) {
  CHECK(REAL(__cxa_throw));
  __xsan_handle_no_return();
  REAL(__cxa_throw)(a, b, c);
}
#endif

#if XSAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION
INTERCEPTOR(void, __cxa_rethrow_primary_exception, void *a) {
  CHECK(REAL(__cxa_rethrow_primary_exception));
  __xsan_handle_no_return();
  REAL(__cxa_rethrow_primary_exception)(a);
}
#endif

#if XSAN_INTERCEPT__UNWIND_RAISEEXCEPTION
INTERCEPTOR(_Unwind_Reason_Code, _Unwind_RaiseException,
            _Unwind_Exception *object) {
  CHECK(REAL(_Unwind_RaiseException));
  __xsan_handle_no_return();
  return REAL(_Unwind_RaiseException)(object);
}
#endif

#if XSAN_INTERCEPT__SJLJ_UNWIND_RAISEEXCEPTION
INTERCEPTOR(_Unwind_Reason_Code, _Unwind_SjLj_RaiseException,
            _Unwind_Exception *object) {
  CHECK(REAL(_Unwind_SjLj_RaiseException));
  __xsan_handle_no_return();
  return REAL(_Unwind_SjLj_RaiseException)(object);
}
#endif

#if XSAN_INTERCEPT_INDEX
# if XSAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX
INTERCEPTOR(char*, index, const char *string, int c)
  ALIAS(WRAPPER_NAME(strchr));
# else
#  if SANITIZER_APPLE
DECLARE_REAL(char*, index, const char *string, int c)
OVERRIDE_FUNCTION(index, strchr);
#  else
DEFINE_REAL(char*, index, const char *string, int c)
#  endif
# endif
#endif  // XSAN_INTERCEPT_INDEX

// For both strcat() and strncat() we need to check the validity of |to|
// argument irrespective of the |from| length.
INTERCEPTOR(char *, strcat, char *to, const char *from) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strcat);
  ENSURE_XSAN_INITED();  

  uptr from_length = internal_strlen(from);
  XSAN_READ_RANGE(ctx, from, from_length + 1);
  uptr to_length = internal_strlen(to);
  XSAN_READ_STRING_OF_LEN(ctx, to, to_length, to_length);
  XSAN_WRITE_RANGE(ctx, to + to_length, from_length + 1);
    // If the copying actually happens, the |from| string should not overlap
    // with the resulting string starting at |to|, which has a length of
    // to_length + from_length + 1.
  if (from_length > 0) {
    CHECK_RANGES_OVERLAP("strcat", to, from_length + to_length + 1, from,
                            from_length + 1);
  }
    
  return REAL(strcat)(to, from);
}

INTERCEPTOR(char*, strncat, char *to, const char *from, uptr size) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strncat);
  ENSURE_XSAN_INITED();
  uptr from_length = MaybeRealStrnlen(from, size);
  uptr copy_length = Min(size, from_length + 1);
  XSAN_READ_RANGE(ctx, from, copy_length);
  uptr to_length = internal_strlen(to);
  XSAN_READ_STRING_OF_LEN(ctx, to, to_length, to_length);
  XSAN_WRITE_RANGE(ctx, to + to_length, from_length + 1);
  if (from_length > 0) {
    CHECK_RANGES_OVERLAP("strncat", to, to_length + copy_length + 1,
                           from, copy_length);
  }
  return REAL(strncat)(to, from, size);
}

INTERCEPTOR(char *, strcpy, char *to, const char *from) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strcpy);
#if SANITIZER_APPLE
  if (UNLIKELY(!xsan_inited))
    return REAL(strcpy)(to, from);
#endif
  // strcpy is called from malloc_default_purgeable_zone()
  // in __xsan::ReplaceSystemAlloc() on Mac.
  if (xsan_init_is_running) {
    return REAL(strcpy)(to, from);
  }
  ENSURE_XSAN_INITED();
  uptr from_size = internal_strlen(from) + 1;
  CHECK_RANGES_OVERLAP("strcpy", to, from_size, from, from_size);
  XSAN_READ_RANGE(ctx, from, from_size);
  XSAN_WRITE_RANGE(ctx, to, from_size);
  return REAL(strcpy)(to, from);
}

INTERCEPTOR(char*, strdup, const char *s) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strdup);
  if (UNLIKELY(!xsan_inited)) return internal_strdup(s);
  ENSURE_XSAN_INITED();
  uptr length = internal_strlen(s);
  XSAN_READ_RANGE(ctx, s, length + 1);
  /// TODO: FIX this
//   GET_STACK_TRACE_MALLOC;
//   void *new_mem = xsan_malloc(length + 1, &stack);
//   REAL(memcpy)(new_mem, s, length + 1);
//   return reinterpret_cast<char*>(new_mem);
  return REAL(strdup)(s);
}

#if XSAN_INTERCEPT___STRDUP
INTERCEPTOR(char*, __strdup, const char *s) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strdup);
  if (UNLIKELY(!xsan_inited)) return internal_strdup(s);
  ENSURE_XSAN_INITED();
  uptr length = internal_strlen(s);
  XSAN_READ_RANGE(ctx, s, length + 1);
  /// TODO: FIX this
//   GET_STACK_TRACE_MALLOC;
//   void *new_mem = xsan_malloc(length + 1, &stack);
//   REAL(memcpy)(new_mem, s, length + 1);
//   return reinterpret_cast<char*>(new_mem);
  return REAL(__strdup)(s);
}
#endif // XSAN_INTERCEPT___STRDUP

INTERCEPTOR(char*, strncpy, char *to, const char *from, uptr size) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strncpy);
  ENSURE_XSAN_INITED();
  uptr from_size = Min(size, MaybeRealStrnlen(from, size) + 1);
  CHECK_RANGES_OVERLAP("strncpy", to, from_size, from, from_size);
  XSAN_READ_RANGE(ctx, from, from_size);
  XSAN_WRITE_RANGE(ctx, to, size);
  return REAL(strncpy)(to, from, size);
}

INTERCEPTOR(long, strtol, const char *nptr, char **endptr, int base) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strtol);
  ENSURE_XSAN_INITED();
//   if (!flags()->replace_str) {
//     return REAL(strtol)(nptr, endptr, base);
//   }
  char *real_endptr;
  long result = REAL(strtol)(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return result;
}

INTERCEPTOR(int, atoi, const char *nptr) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, atoi);
#if SANITIZER_APPLE
  if (UNLIKELY(!xsan_inited)) return REAL(atoi)(nptr);
#endif
  ENSURE_XSAN_INITED();
//   if (!flags()->replace_str) {
//     return REAL(atoi)(nptr);
//   }
  char *real_endptr;
  // "man atoi" tells that behavior of atoi(nptr) is the same as
  // strtol(nptr, 0, 10), i.e. it sets errno to ERANGE if the
  // parsed integer can't be stored in *long* type (even if it's
  // different from int). So, we just imitate this behavior.
  int result = REAL(strtol)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  XSAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}

INTERCEPTOR(long, atol, const char *nptr) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, atol);
#if SANITIZER_APPLE
  if (UNLIKELY(!xsan_inited)) return REAL(atol)(nptr);
#endif
  ENSURE_XSAN_INITED();
//   if (!flags()->replace_str) {
//     return REAL(atol)(nptr);
//   }
  char *real_endptr;
  long result = REAL(strtol)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  XSAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}

#if XSAN_INTERCEPT_ATOLL_AND_STRTOLL
INTERCEPTOR(long long, strtoll, const char *nptr, char **endptr, int base) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strtoll);
  ENSURE_XSAN_INITED();
//   if (!flags()->replace_str) {
//     return REAL(strtoll)(nptr, endptr, base);
//   }
  char *real_endptr;
  long long result = REAL(strtoll)(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return result;
}

INTERCEPTOR(long long, atoll, const char *nptr) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, atoll);
  ENSURE_XSAN_INITED();
//   if (!flags()->replace_str) {
//     return REAL(atoll)(nptr);
//   }
  char *real_endptr;
  long long result = REAL(strtoll)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  XSAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}
#endif  // XSAN_INTERCEPT_ATOLL_AND_STRTOLL

#if XSAN_INTERCEPT___CXA_ATEXIT || XSAN_INTERCEPT_ATEXIT
static void AtCxaAtexit(void *unused) {
  (void)unused;
  /// TODO: do ASan's check in a more generic way
  __asan::StopInitOrderChecking();
}
#endif

#if XSAN_INTERCEPT___CXA_ATEXIT
INTERCEPTOR(int, __cxa_atexit, void (*func)(void *), void *arg,
            void *dso_handle) {
#if SANITIZER_APPLE
  if (UNLIKELY(!xsan_inited)) return REAL(__cxa_atexit)(func, arg, dso_handle);
#endif
  ENSURE_XSAN_INITED();
#if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#endif
  int res = REAL(__cxa_atexit)(func, arg, dso_handle);
  REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  return res;
}
#endif  // XSAN_INTERCEPT___CXA_ATEXIT

#if XSAN_INTERCEPT_ATEXIT
INTERCEPTOR(int, atexit, void (*func)()) {
  ENSURE_XSAN_INITED();
#if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#endif
  // Avoid calling real atexit as it is unreachable on at least on Linux.
  int res = REAL(__cxa_atexit)((void (*)(void *a))func, nullptr, nullptr);
  REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  return res;
}
#endif

#if XSAN_INTERCEPT_PTHREAD_ATFORK
extern "C" {
extern int _pthread_atfork(void (*prepare)(), void (*parent)(),
                           void (*child)());
};

INTERCEPTOR(int, pthread_atfork, void (*prepare)(), void (*parent)(),
            void (*child)()) {
#if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#endif
  // REAL(pthread_atfork) cannot be called due to symbol indirections at least
  // on NetBSD
  return _pthread_atfork(prepare, parent, child);
}
#endif

#if XSAN_INTERCEPT_VFORK
DEFINE_REAL(int, vfork)
DECLARE_EXTERN_INTERCEPTOR_AND_WRAPPER(int, vfork)
#endif

// ---------------------- InitializeXsanInterceptors ---------------- {{{1
namespace __xsan {
void InitializeXsanInterceptors() {
  static bool was_called_once;
  CHECK(!was_called_once);
  was_called_once = true;
  InitializeCommonInterceptors();
  InitializeSignalInterceptors();

  // Intercept str* functions.
  XSAN_INTERCEPT_FUNC(strcat);
  XSAN_INTERCEPT_FUNC(strcpy);
  XSAN_INTERCEPT_FUNC(strncat);
  XSAN_INTERCEPT_FUNC(strncpy);
  XSAN_INTERCEPT_FUNC(strdup);
#if XSAN_INTERCEPT___STRDUP
  XSAN_INTERCEPT_FUNC(__strdup);
#endif
#if XSAN_INTERCEPT_INDEX && XSAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX
  XSAN_INTERCEPT_FUNC(index);
#endif

  XSAN_INTERCEPT_FUNC(atoi);
  XSAN_INTERCEPT_FUNC(atol);
  XSAN_INTERCEPT_FUNC(strtol);
#if XSAN_INTERCEPT_ATOLL_AND_STRTOLL
  XSAN_INTERCEPT_FUNC(atoll);
  XSAN_INTERCEPT_FUNC(strtoll);
#endif

  // Intecept jump-related functions.
  XSAN_INTERCEPT_FUNC(longjmp);

#if XSAN_INTERCEPT_SWAPCONTEXT
  XSAN_INTERCEPT_FUNC(getcontext);
  XSAN_INTERCEPT_FUNC(swapcontext);
#endif
#if XSAN_INTERCEPT__LONGJMP
  XSAN_INTERCEPT_FUNC(_longjmp);
#endif
#if XSAN_INTERCEPT___LONGJMP_CHK
  XSAN_INTERCEPT_FUNC(__longjmp_chk);
#endif
#if XSAN_INTERCEPT_SIGLONGJMP
  XSAN_INTERCEPT_FUNC(siglongjmp);
#endif

  // Intercept exception handling functions.
#if XSAN_INTERCEPT___CXA_THROW
  XSAN_INTERCEPT_FUNC(__cxa_throw);
#endif
#if XSAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION
  XSAN_INTERCEPT_FUNC(__cxa_rethrow_primary_exception);
#endif
  // Indirectly intercept std::rethrow_exception.
#if XSAN_INTERCEPT__UNWIND_RAISEEXCEPTION
  INTERCEPT_FUNCTION(_Unwind_RaiseException);
#endif
  // Indirectly intercept std::rethrow_exception.
#if XSAN_INTERCEPT__UNWIND_SJLJ_RAISEEXCEPTION
  INTERCEPT_FUNCTION(_Unwind_SjLj_RaiseException);
#endif

  // Intercept threading-related functions
#if XSAN_INTERCEPT_PTHREAD_CREATE
// TODO: this should probably have an unversioned fallback for newer arches?
#if defined(XSAN_PTHREAD_CREATE_VERSION)
  XSAN_INTERCEPT_FUNC_VER(pthread_create, XSAN_PTHREAD_CREATE_VERSION);
#else
  XSAN_INTERCEPT_FUNC(pthread_create);
#endif
  XSAN_INTERCEPT_FUNC(pthread_join);
#endif

  // Intercept atexit function.
#if XSAN_INTERCEPT___CXA_ATEXIT
  XSAN_INTERCEPT_FUNC(__cxa_atexit);
#endif

#if XSAN_INTERCEPT_ATEXIT
  XSAN_INTERCEPT_FUNC(atexit);
#endif

#if XSAN_INTERCEPT_PTHREAD_ATFORK
  XSAN_INTERCEPT_FUNC(pthread_atfork);
#endif

#if XSAN_INTERCEPT_VFORK
  XSAN_INTERCEPT_FUNC(vfork);
#endif

  InitializePlatformInterceptors();

  VReport(1, "AddressSanitizer: libc interceptors initialized\n");
}

} // namespace __xsan

#endif  // !SANITIZER_FUCHSIA
