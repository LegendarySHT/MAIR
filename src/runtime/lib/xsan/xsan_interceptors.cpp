#include "xsan_interceptors.h"

#include <lsan/lsan_common.h>
#include <sanitizer_common/sanitizer_libc.h>

#include "asan/orig/asan_flags.h"
#include "asan/orig/asan_internal.h"
#include "asan/orig/asan_poisoning.h"
#include "asan/orig/asan_report.h"
#include "asan/orig/asan_suppressions.h"
#include "tsan/tsan_interceptors.h"
#include "xsan_internal.h"
#include "xsan_stack.h"
#include "xsan_thread.h"

// There is no general interception at all on Fuchsia.
// Only the functions in xsan_interceptors_memintrinsics.cpp are
// really defined to replace libc functions.
#if !SANITIZER_FUCHSIA

#  if SANITIZER_POSIX
#    include <sanitizer_common/sanitizer_posix.h>
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

// The sole reason tsan wraps atexit callbacks is to establish synchronization
// between callback setup and callback execution.
using AtExitFuncTy = void (*)();
struct AtExitCtx {
  AtExitFuncTy f;
  void *arg;
  uptr pc;
};

/// This context comes from TSan's and MSan's xxx_interceptors.cpp
// InterceptorContext holds all global data required for interceptors.
// It's explicitly constructed in InitializeInterceptors with placement new
// and is never destroyed. This allows usage of members with non-trivial
// constructors and destructors.
struct InterceptorContext {
  /// FIXME: provide interface for TSan to register its own implementations like
  /// the following..
  //   // The object is 64-byte aligned, because we want hot data to be located
  //   // in a single cache line if possible (it's accessed in every
  //   interceptor). ALIGNED(64) LibIgnore libignore;
  //   __sanitizer_sigaction sigactions[kSigCount];
  // #if !SANITIZER_APPLE && !SANITIZER_NETBSD
  //   unsigned finalize_key;
  // #endif

  Mutex atexit_mu;
  Vector<struct AtExitCtx *> AtExitStack;
  /// FIXME: TSan use MutexTypeAtExit, but we don't have it. But MSan use
  /// MutexUnchecked.
  InterceptorContext() : atexit_mu(MutexUnchecked), AtExitStack() {}
};

static ALIGNED(64) char interceptor_placeholder[sizeof(InterceptorContext)];
InterceptorContext *interceptor_ctx() {
  return reinterpret_cast<InterceptorContext *>(&interceptor_placeholder[0]);
}

// void InitializeLibIgnore() {
//   const SuppressionContext &supp = *Suppressions();
//   const uptr n = supp.SuppressionCount();
//   for (uptr i = 0; i < n; i++) {
//     const Suppression *s = supp.SuppressionAt(i);
//     if (0 == internal_strcmp(s->type, kSuppressionLib))
//       libignore()->AddIgnoredLibrary(s->templ);
//   }
//   if (flags()->ignore_noninstrumented_modules)
//     libignore()->IgnoreNoninstrumentedModules(true);
//   libignore()->OnLibraryLoaded(0);
// }

#  define XSAN_READ_STRING_OF_LEN(ctx, s, len, n) \
    XSAN_READ_RANGE((ctx), (s),                   \
                    common_flags()->strict_string_checks ? (len) + 1 : (n))

#  define XSAN_READ_STRING(ctx, s, n) \
    XSAN_READ_STRING_OF_LEN((ctx), (s), internal_strlen(s), (n))

static inline uptr MaybeRealStrnlen(const char *s, uptr maxlen) {
#  if SANITIZER_INTERCEPT_STRNLEN
  if (REAL(strnlen)) {
    return REAL(strnlen)(s, maxlen);
  }
#  endif
  return internal_strnlen(s, maxlen);
}

void SetThreadName(const char *name) {
  XsanThread *t = GetCurrentThread();
  if (t) {
    t->setThreadName(name);
  }
}

int OnExit() {
  if (CAN_SANITIZE_LEAKS && common_flags()->detect_leaks &&
      __lsan::HasReportedLeaks()) {
    return common_flags()->exitcode;
  }
  // FIXME: ask frontend whether we need to return failure.
  return 0;
}

}  // namespace __xsan

// ---------------------- Wrappers ---------------- {{{1
using namespace __xsan;

#if XSAN_CONTAINS_TSAN
#include "sanitizer_common/sanitizer_platform_interceptors.h"
// Causes interceptor recursion (getaddrinfo() and fopen())
/// TSan needs to ignore memory accesses in getaddrinfo()
#undef SANITIZER_INTERCEPT_GETADDRINFO
// We define our own.
#undef SANITIZER_INTERCEPT_TLS_GET_ADDR
#define SANITIZER_INTERCEPT_TLS_GET_OFFSET 1
#endif

DECLARE_REAL_AND_INTERCEPTOR(void *, malloc, uptr)
DECLARE_REAL_AND_INTERCEPTOR(void, free, void *)

/// TODO: use a better approach to use SCOPED_TSAN_INTERCEPTOR
#  define XSAN_INTERCEPTOR_ENTER(ctx, func, ...)    \
    SCOPED_TSAN_INTERCEPTOR(func, __VA_ARGS__);     \
    XsanInterceptorContext _ctx = {#func, thr, pc}; \
    ctx = (void *)&_ctx;                            \
    (void)ctx;

#  define XSAN_BEFORE_DLOPEN(filename, flag) \
    if (__asan::flags()->strict_init_order)  \
      __asan::StopInitOrderChecking();

#  define COMMON_INTERCEPT_FUNCTION(name) XSAN_INTERCEPT_FUNC(name)
#  define COMMON_INTERCEPT_FUNCTION_VER(name, ver) \
    XSAN_INTERCEPT_FUNC_VER(name, ver)
#  define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver) \
    XSAN_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)
#  define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size) \
    XSAN_WRITE_RANGE(ctx, ptr, size)
#  define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size) \
    XSAN_READ_RANGE(ctx, ptr, size)
#  define COMMON_INTERCEPTOR_ENTER(ctx, func, ...)   \
    XSAN_INTERCEPTOR_ENTER(ctx, func, __VA_ARGS__);  \
    do {                                             \
      if (xsan_init_is_running)                      \
        return REAL(func)(__VA_ARGS__);              \
      if (SANITIZER_APPLE && UNLIKELY(!xsan_inited)) \
        return REAL(func)(__VA_ARGS__);              \
      ENSURE_XSAN_INITED();                          \
    } while (false)
#  define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path) \
    do {                                            \
    } while (false)
#  define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd) \
    do {                                         \
    } while (false)
#  define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd) \
    do {                                         \
    } while (false)
#  define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd) \
    do {                                                      \
    } while (false)
#  define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name) SetThreadName(name)
// Should be asanThreadRegistry().SetThreadNameByUserId(thread, name)
// But asan does not remember UserId's for threads (pthread_t);
// and remembers all ever existed threads, so the linear search by UserId
// can be slow.
#  define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name) \
    do {                                                         \
    } while (false)
#  define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)
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

#  define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end) \
    if (XsanThread *t = GetCurrentThread()) {          \
      *begin = t->tls_begin();                         \
      *end = t->tls_end();                             \
    } else {                                           \
      *begin = *end = 0;                               \
    }

#  define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, to, from, size) \
    do {                                                       \
      XSAN_INTERCEPTOR_ENTER(ctx, memmove, to, from, size);    \
      XSAN_MEMMOVE_IMPL(ctx, to, from, size);                  \
    } while (false)

#  define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, to, from, size) \
    do {                                                      \
      XSAN_INTERCEPTOR_ENTER(ctx, memcpy, to, from, size);    \
      XSAN_MEMCPY_IMPL(ctx, to, from, size);                  \
    } while (false)

#  define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, block, c, size) \
    do {                                                      \
      XSAN_INTERCEPTOR_ENTER(ctx, memset, block, c, size);    \
      XSAN_MEMSET_IMPL(ctx, block, c, size);                  \
    } while (false)

#  if CAN_SANITIZE_LEAKS
#    define COMMON_INTERCEPTOR_STRERROR() \
      __lsan::ScopedInterceptorDisabler disabler
#  endif

#  include <sanitizer_common/sanitizer_common_interceptors.inc>
#  include <sanitizer_common/sanitizer_signal_interceptors.inc>

// Syscall interceptors don't have contexts, we don't support suppressions
// for them.
#  define COMMON_SYSCALL_PRE_READ_RANGE(p, s) XSAN_READ_RANGE(nullptr, p, s)
#  define COMMON_SYSCALL_PRE_WRITE_RANGE(p, s) XSAN_WRITE_RANGE(nullptr, p, s)
#  define COMMON_SYSCALL_POST_READ_RANGE(p, s) \
    do {                                       \
      (void)(p);                               \
      (void)(s);                               \
    } while (false)
#  define COMMON_SYSCALL_POST_WRITE_RANGE(p, s) \
    do {                                        \
      (void)(p);                                \
      (void)(s);                                \
    } while (false)
#  include <sanitizer_common/sanitizer_common_syscalls.inc>
#  include <sanitizer_common/sanitizer_syscalls_netbsd.inc>

#  if XSAN_INTERCEPT_PTHREAD_CREATE

struct ThreadParam {
  XsanThread *t;
  Semaphore created;
  Semaphore started;
};

static thread_return_t THREAD_CALLING_CONV xsan_thread_start(void *arg) {
  ThreadParam *p = (ThreadParam *)arg;
  auto &[t, created, started] = *p;
  SetCurrentThread(t);
  return t->ThreadStart(GetTid(), &created, &started);
}

INTERCEPTOR(int, pthread_create, void *thread, void *attr,
            void *(*start_routine)(void *), void *arg) {
  /// TODO: managed by XSan
  SCOPED_INTERCEPTOR_RAW(pthread_create, thread, attr, start_routine, arg);
  EnsureMainThreadIDIsCorrect();

  /// TODO: Extract this to a separate function?
  // Strict init-order checking is thread-hostile.
  if (__asan::flags()->strict_init_order)
    __asan::StopInitOrderChecking();

  GET_STACK_TRACE_THREAD;
  int detached = 0;
  if (attr)
    REAL(pthread_attr_getdetachstate)(attr, &detached);

  u32 current_tid = GetCurrentTidOrInvalid();

  /// Note that sub_thread's recycle is delegated to sub thread.
  /// Hence, we could not use it after pthread_create in the parent thread.
  XsanThread *sub_thread =
      XsanThread::Create(start_routine, arg, current_tid, &stack, detached);

  ThreadParam p;
  p.t = sub_thread;

  int result;
  {
    // Ignore all allocations made by pthread_create: thread stack/TLS may be
    // stored by pthread for future reuse even after thread destruction, and
    // the linked list it's stored in doesn't even hold valid pointers to the
    // objects, the latter are calculated by obscure pointer arithmetic.
#    if CAN_SANITIZE_LEAKS
    __lsan::ScopedInterceptorDisabler disabler;
#    endif
    result = REAL(pthread_create)(thread, attr, xsan_thread_start, &p);
  }

  if (result == 0) {
    // sub_thread must live, as sub_thread waits for `created_.Post()`
    sub_thread->PostCreateTsanThread(pc, *(uptr *)thread);
    // Synchronization on p.tid serves two purposes:
    // 1. ThreadCreate must finish before the new thread starts.
    //    Otherwise the new thread can call pthread_detach, but the pthread_t
    //    identifier is not yet registered in ThreadRegistry by ThreadCreate.
    // 2. ThreadStart must finish before this thread continues.
    //    Otherwise, this thread can call pthread_detach and reset thr->sync
    //    before the new thread got a chance to acquire from it in ThreadStart.
    p.created.Post();
    p.started.Wait();
  } else {
    // If the thread didn't start delete the AsanThread to avoid leaking it.
    // Note AsanThreadContexts never get destroyed so the AsanThreadContext
    // that was just created for the AsanThread is wasted.
    sub_thread->Destroy();
  }
  return result;
}

/// TODO: If not compose TSan, intercept join here
// INTERCEPTOR(int, pthread_join, void *t, void **arg) {
//   return real_pthread_join(t, arg);
// }

// DEFINE_REAL_PTHREAD_FUNCTIONS
#  endif  // XSAN_INTERCEPT_PTHREAD_CREATE

#  if XSAN_INTERCEPT_SWAPCONTEXT
static void ClearShadowMemoryForContextStack(uptr stack, uptr ssize) {
  // Only clear if we know the stack. This should be true only for contexts
  // created with makecontext().
  if (!ssize)
    return;
  // Align to page size.
  uptr PageSize = GetPageSizeCached();
  uptr bottom = RoundDownTo(stack, PageSize);
  if (!__asan::AddrIsInMem(bottom))
    return;
  ssize += stack - bottom;
  ssize = RoundUpTo(ssize, PageSize);
  __asan::PoisonShadow(bottom, ssize, 0);
}

INTERCEPTOR(int, getcontext, struct ucontext_t *ucp) {
  // API does not requires to have ucp clean, and sets only part of fields. We
  // use ucp->uc_stack to unpoison new stack. We prefer to have zeroes then
  // uninitialized bytes.
  ResetContextStack(ucp);
  return REAL(getcontext)(ucp);
}

INTERCEPTOR(int, swapcontext, struct ucontext_t *oucp, struct ucontext_t *ucp) {
  // --------------------- ASan part ----------------------------
  static bool reported_warning = false;
  if (!reported_warning) {
    Report(
        "WARNING: ASan doesn't fully support makecontext/swapcontext "
        "functions and may produce false positives in some cases!\n");
    reported_warning = true;
  }
  // Clear shadow memory for new context (it may share stack
  // with current context).
  uptr stack, ssize;
  ReadContextStack(ucp, &stack, &ssize);
  ClearShadowMemoryForContextStack(stack, ssize);

  // See getcontext interceptor.
  ResetContextStack(oucp);

  // -----------------------------------------------------------

#    if __has_attribute(__indirect_return__) && \
        (defined(__x86_64__) || defined(__i386__))
  int (*real_swapcontext)(struct ucontext_t *, struct ucontext_t *)
      __attribute__((__indirect_return__)) = REAL(swapcontext);
  int res = real_swapcontext(oucp, ucp);
#    else
  int res = REAL(swapcontext)(oucp, ucp);
#    endif

  // --------------------- ASan part ----------------------------

  // swapcontext technically does not return, but program may swap context to
  // "oucp" later, that would look as if swapcontext() returned 0.
  // We need to clear shadow for ucp once again, as it may be in arbitrary
  // state.
  ClearShadowMemoryForContextStack(stack, ssize);

  // -----------------------------------------------------------

  return res;
}
#  endif  // XSAN_INTERCEPT_SWAPCONTEXT

#  if SANITIZER_NETBSD
#    define longjmp __longjmp14
#    define siglongjmp __siglongjmp14
#  endif

INTERCEPTOR(void, longjmp, void *env, int val) {
  __tsan::handle_longjmp(env, "longjmp", GET_CALLER_PC());
  __xsan_handle_no_return();
  REAL(longjmp)(env, val);
}

#  if XSAN_INTERCEPT__LONGJMP
INTERCEPTOR(void, _longjmp, void *env, int val) {
  __tsan::handle_longjmp(env, "_longjmp", GET_CALLER_PC());
  __xsan_handle_no_return();
  REAL(_longjmp)(env, val);
}
#  endif

#  if XSAN_INTERCEPT___LONGJMP_CHK
INTERCEPTOR(void, __longjmp_chk, void *env, int val) {
  __tsan::handle_longjmp(env, "__longjmp_chk", GET_CALLER_PC());
  __xsan_handle_no_return();
  REAL(__longjmp_chk)(env, val);
}
#  endif

#  if XSAN_INTERCEPT_SIGLONGJMP
INTERCEPTOR(void, siglongjmp, void *env, int val) {
  __tsan::handle_longjmp(env, "siglongjmp", GET_CALLER_PC());
  __xsan_handle_no_return();
  REAL(siglongjmp)(env, val);
}
#  endif

#  if XSAN_INTERCEPT___CXA_THROW
INTERCEPTOR(void, __cxa_throw, void *a, void *b, void *c) {
  CHECK(REAL(__cxa_throw));
  __xsan_handle_no_return();
  REAL(__cxa_throw)(a, b, c);
}
#  endif

#  if XSAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION
INTERCEPTOR(void, __cxa_rethrow_primary_exception, void *a) {
  CHECK(REAL(__cxa_rethrow_primary_exception));
  __xsan_handle_no_return();
  REAL(__cxa_rethrow_primary_exception)(a);
}
#  endif

#  if XSAN_INTERCEPT__UNWIND_RAISEEXCEPTION
INTERCEPTOR(_Unwind_Reason_Code, _Unwind_RaiseException,
            _Unwind_Exception *object) {
  CHECK(REAL(_Unwind_RaiseException));
  __xsan_handle_no_return();
  return REAL(_Unwind_RaiseException)(object);
}
#  endif

#  if XSAN_INTERCEPT__SJLJ_UNWIND_RAISEEXCEPTION
INTERCEPTOR(_Unwind_Reason_Code, _Unwind_SjLj_RaiseException,
            _Unwind_Exception *object) {
  CHECK(REAL(_Unwind_SjLj_RaiseException));
  __xsan_handle_no_return();
  return REAL(_Unwind_SjLj_RaiseException)(object);
}
#  endif

#  if XSAN_INTERCEPT_INDEX
#    if XSAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX
INTERCEPTOR(char *, index, const char *string, int c)
ALIAS(WRAPPER_NAME(strchr));
#    else
#      if SANITIZER_APPLE
DECLARE_REAL(char *, index, const char *string, int c)
OVERRIDE_FUNCTION(index, strchr);
#      else
DEFINE_REAL(char *, index, const char *string, int c)
#      endif
#    endif
#  endif  // XSAN_INTERCEPT_INDEX

// For both strcat() and strncat() we need to check the validity of |to|
// argument irrespective of the |from| length.
INTERCEPTOR(char *, strcat, char *to, const char *from) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strcat, to, from);
  ENSURE_XSAN_INITED();
  if (__asan::flags()->replace_str) {
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
  }
  return REAL(strcat)(to, from);
}

INTERCEPTOR(char *, strncat, char *to, const char *from, uptr size) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strncat, to, from, size);
  ENSURE_XSAN_INITED();
  if (__asan::flags()->replace_str) {
    uptr from_length = MaybeRealStrnlen(from, size);
    uptr copy_length = Min(size, from_length + 1);
    XSAN_READ_RANGE(ctx, from, copy_length);
    uptr to_length = internal_strlen(to);
    XSAN_READ_STRING_OF_LEN(ctx, to, to_length, to_length);
    XSAN_WRITE_RANGE(ctx, to + to_length, from_length + 1);
    if (from_length > 0) {
      CHECK_RANGES_OVERLAP("strncat", to, to_length + copy_length + 1, from,
                           copy_length);
    }
  }
  return REAL(strncat)(to, from, size);
}

INTERCEPTOR(char *, strcpy, char *to, const char *from) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strcpy, to, from);
#  if SANITIZER_APPLE
  if (UNLIKELY(!xsan_inited))
    return REAL(strcpy)(to, from);
#  endif
  // strcpy is called from malloc_default_purgeable_zone()
  // in __xsan::ReplaceSystemAlloc() on Mac.
  if (xsan_init_is_running) {
    return REAL(strcpy)(to, from);
  }
  ENSURE_XSAN_INITED();
  if (__asan::flags()->replace_str) {
    uptr from_size = internal_strlen(from) + 1;
    CHECK_RANGES_OVERLAP("strcpy", to, from_size, from, from_size);
    XSAN_READ_RANGE(ctx, from, from_size);
    XSAN_WRITE_RANGE(ctx, to, from_size);
  }
  return REAL(strcpy)(to, from);
}

INTERCEPTOR(char *, strdup, const char *s) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strdup, s);
  if (UNLIKELY(!xsan_inited))
    return internal_strdup(s);
  ENSURE_XSAN_INITED();
  uptr length = internal_strlen(s);
  if (__asan::flags()->replace_str) {
    XSAN_READ_RANGE(ctx, s, length + 1);
  }
  XSAN_EXTRA_ALLOC_ARG(strdup, s);
  void *new_mem = xsan_malloc(length + 1, &stack);
  REAL(memcpy)(new_mem, s, length + 1);
  return reinterpret_cast<char *>(new_mem);
  return REAL(strdup)(s);
}

#  if XSAN_INTERCEPT___STRDUP
INTERCEPTOR(char *, __strdup, const char *s) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strdup, s);
  if (UNLIKELY(!xsan_inited))
    return internal_strdup(s);
  ENSURE_XSAN_INITED();
  uptr length = internal_strlen(s);
  if (__asan::flags()->replace_str) {
    XSAN_READ_RANGE(ctx, s, length + 1);
  }
  XSAN_EXTRA_ALLOC_ARG(__strdup, s);
  void *new_mem = xsan_malloc(length + 1, &stack);
  REAL(memcpy)(new_mem, s, length + 1);
  return reinterpret_cast<char *>(new_mem);
  return REAL(__strdup)(s);
}
#  endif  // XSAN_INTERCEPT___STRDUP

INTERCEPTOR(char *, strncpy, char *to, const char *from, uptr size) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strncpy, to, from, size);
  ENSURE_XSAN_INITED();
  if (__asan::flags()->replace_str) {
    uptr from_size = Min(size, MaybeRealStrnlen(from, size) + 1);
    CHECK_RANGES_OVERLAP("strncpy", to, from_size, from, from_size);
    XSAN_READ_RANGE(ctx, from, from_size);
    XSAN_WRITE_RANGE(ctx, to, size);
  }
  return REAL(strncpy)(to, from, size);
}

INTERCEPTOR(long, strtol, const char *nptr, char **endptr, int base) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strtol, nptr, endptr, base);
  ENSURE_XSAN_INITED();
  if (!__asan::flags()->replace_str) {
    return REAL(strtol)(nptr, endptr, base);
  }
  char *real_endptr;
  long result = REAL(strtol)(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return result;
}

INTERCEPTOR(int, atoi, const char *nptr) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, atoi, nptr);
#  if SANITIZER_APPLE
  if (UNLIKELY(!xsan_inited))
    return REAL(atoi)(nptr);
#  endif
  ENSURE_XSAN_INITED();
  if (!__asan::flags()->replace_str) {
    return REAL(atoi)(nptr);
  }
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
  XSAN_INTERCEPTOR_ENTER(ctx, atol, nptr);
#  if SANITIZER_APPLE
  if (UNLIKELY(!xsan_inited))
    return REAL(atol)(nptr);
#  endif
  ENSURE_XSAN_INITED();
  if (!__asan::flags()->replace_str) {
    return REAL(atol)(nptr);
  }
  char *real_endptr;
  long result = REAL(strtol)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  XSAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}

#  if XSAN_INTERCEPT_ATOLL_AND_STRTOLL
INTERCEPTOR(long long, strtoll, const char *nptr, char **endptr, int base) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strtoll, nptr, endptr, base);
  ENSURE_XSAN_INITED();
  if (!__asan::flags()->replace_str) {
    return REAL(strtoll)(nptr, endptr, base);
  }
  char *real_endptr;
  long long result = REAL(strtoll)(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return result;
}

INTERCEPTOR(long long, atoll, const char *nptr) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, atoll, nptr);
  ENSURE_XSAN_INITED();
  if (!__asan::flags()->replace_str) {
    return REAL(atoll)(nptr);
  }
  char *real_endptr;
  long long result = REAL(strtoll)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  XSAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}
#  endif  // XSAN_INTERCEPT_ATOLL_AND_STRTOLL

static int setup_at_exit_wrapper(uptr pc, AtExitFuncTy f, void *arg,
                                 void *dso);

#  if XSAN_INTERCEPT___CXA_ATEXIT
/// TODO: support on_exit interceptor
INTERCEPTOR(int, __cxa_atexit, void (*func)(void *), void *arg,
            void *dso_handle) {
  if (__tsan::in_symbolizer())
    return 0;
#    if SANITIZER_APPLE
  if (UNLIKELY(!xsan_inited))
    return REAL(__cxa_atexit)(func, arg, dso_handle);
#    endif
  ENSURE_XSAN_INITED();
#    if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#    endif
  // int res = REAL(__cxa_atexit)(func, arg, dso_handle);
  // REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  // return res;
  return setup_at_exit_wrapper(GET_CALLER_PC(), (AtExitFuncTy)func, arg, dso_handle);
}
#  endif  // XSAN_INTERCEPT___CXA_ATEXIT

#  if XSAN_INTERCEPT_ATEXIT
INTERCEPTOR(int, atexit, void (*func)()) {
  /// TODO: add if (in_symbolizer()) && support TSan
  ENSURE_XSAN_INITED();
#    if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#    endif
  // Avoid calling real atexit as it is unreachable on at least on Linux.
  // int res = REAL(__cxa_atexit)((void (*)(void *a))func, nullptr, nullptr);
  // REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  // return res;
  return setup_at_exit_wrapper(GET_CALLER_PC(), (AtExitFuncTy)func, nullptr, nullptr);
}
#  endif

#  if XSAN_INTERCEPT___CXA_ATEXIT || XSAN_INTERCEPT_ATEXIT

static void XsanBeforeAtExitHandler(AtExitCtx *ctx) {
  /// Stop init order checking to avoid false positives in the
  /// initialization code, adhering the logic of ASan.
  __asan::StopInitOrderChecking();

  /// TODO: use a more generic way 
  __tsan::ThreadState *thr = __tsan::cur_thread();
  __tsan::Acquire(thr, ctx->pc, (uptr)ctx);
  __tsan::FuncEntry(thr, ctx->pc);
}

static void XsanPostAtExitHandler(AtExitCtx *ctx) {
  (void)ctx;
  /// TODO: use a more generic way 
  __tsan::FuncExit(__tsan::cur_thread());
}


static void XSanAtExitWrapper() {
  AtExitCtx *ctx;
  {
    // Ensure thread-safety.
    Lock l(&interceptor_ctx()->atexit_mu);

    // Pop AtExitCtx from the top of the stack of callback functions
    uptr element = interceptor_ctx()->AtExitStack.Size() - 1;
    ctx = interceptor_ctx()->AtExitStack[element];
    interceptor_ctx()->AtExitStack.PopBack();
  }

  XsanBeforeAtExitHandler(ctx);
  ((void (*)())ctx->f)();
  XsanPostAtExitHandler(ctx);
  Free(ctx);
}

static void XSanCxaAtExitWrapper(void *arg) {
  AtExitCtx *ctx = (AtExitCtx *)arg;

  XsanBeforeAtExitHandler(ctx); 
  ((void (*)(void *arg))ctx->f)(ctx->arg);
  XsanPostAtExitHandler(ctx);
  Free(ctx);
}
#  endif

static int setup_at_exit_wrapper(uptr pc, AtExitFuncTy f, void *arg, void *dso) {
  auto *ctx = New<AtExitCtx>();
  ctx->f = f;
  ctx->arg = arg;
  ctx->pc = pc;
  // Release(thr, pc, (uptr)ctx);
  // Memory allocation in __cxa_atexit will race with free during exit,
  // because we do not see synchronization around atexit callback list.
  // ThreadIgnoreBegin(thr, pc);
  int res;
  if (!dso) {
    // NetBSD does not preserve the 2nd argument if dso is equal to 0
    // Store ctx in a local stack-like structure

    // Ensure thread-safety.
    Lock l(&interceptor_ctx()->atexit_mu);
    // __cxa_atexit calls calloc. If we don't ignore interceptors, we will fail
    // due to atexit_mu held on exit from the calloc interceptor.
    ScopedIgnoreInterceptors ignore;

    res = REAL(__cxa_atexit)((void (*)(void *a))XSanAtExitWrapper, 0, 0);
    // Push AtExitCtx on the top of the stack of callback functions
    if (!res) {
      interceptor_ctx()->AtExitStack.PushBack(ctx);
    }
  } else {
    res = REAL(__cxa_atexit)(XSanCxaAtExitWrapper, ctx, dso);
  }
  // ThreadIgnoreEnd(thr);
  return res;
}

#  if XSAN_INTERCEPT_PTHREAD_ATFORK
extern "C" {
extern int _pthread_atfork(void (*prepare)(), void (*parent)(),
                           void (*child)());
};

INTERCEPTOR(int, pthread_atfork, void (*prepare)(), void (*parent)(),
            void (*child)()) {
#    if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#    endif
  // REAL(pthread_atfork) cannot be called due to symbol indirections at least
  // on NetBSD
  return _pthread_atfork(prepare, parent, child);
}
#  endif

#  if XSAN_INTERCEPT_VFORK
DEFINE_REAL(int, vfork)
DECLARE_EXTERN_INTERCEPTOR_AND_WRAPPER(int, vfork)
#  endif

// ---------------------- InitializeXsanInterceptors ---------------- {{{1
namespace __xsan {

#if !SANITIZER_APPLE && !SANITIZER_ANDROID
static void unreachable() {
  Report("FATAL: XSan: unreachable called\n");
  Die();
}
#endif

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
#  if XSAN_INTERCEPT___STRDUP
  XSAN_INTERCEPT_FUNC(__strdup);
#  endif
#  if XSAN_INTERCEPT_INDEX && XSAN_USE_ALIAS_ATTRIBUTE_FOR_INDEX
  XSAN_INTERCEPT_FUNC(index);
#  endif

  XSAN_INTERCEPT_FUNC(atoi);
  XSAN_INTERCEPT_FUNC(atol);
  XSAN_INTERCEPT_FUNC(strtol);
#  if XSAN_INTERCEPT_ATOLL_AND_STRTOLL
  XSAN_INTERCEPT_FUNC(atoll);
  XSAN_INTERCEPT_FUNC(strtoll);
#  endif

  // Intecept jump-related functions.
  XSAN_INTERCEPT_FUNC(longjmp);

#  if XSAN_INTERCEPT_SWAPCONTEXT
  XSAN_INTERCEPT_FUNC(getcontext);
  XSAN_INTERCEPT_FUNC(swapcontext);
#  endif
#  if XSAN_INTERCEPT__LONGJMP
  XSAN_INTERCEPT_FUNC(_longjmp);
#  endif
#  if XSAN_INTERCEPT___LONGJMP_CHK
  XSAN_INTERCEPT_FUNC(__longjmp_chk);
#  endif
#  if XSAN_INTERCEPT_SIGLONGJMP
  XSAN_INTERCEPT_FUNC(siglongjmp);
#  endif

  // Intercept exception handling functions.
#  if XSAN_INTERCEPT___CXA_THROW
  XSAN_INTERCEPT_FUNC(__cxa_throw);
#  endif
#  if XSAN_INTERCEPT___CXA_RETHROW_PRIMARY_EXCEPTION
  XSAN_INTERCEPT_FUNC(__cxa_rethrow_primary_exception);
#  endif
  // Indirectly intercept std::rethrow_exception.
#  if XSAN_INTERCEPT__UNWIND_RAISEEXCEPTION
  INTERCEPT_FUNCTION(_Unwind_RaiseException);
#  endif
  // Indirectly intercept std::rethrow_exception.
#  if XSAN_INTERCEPT__UNWIND_SJLJ_RAISEEXCEPTION
  INTERCEPT_FUNCTION(_Unwind_SjLj_RaiseException);
#  endif

  // Intercept threading-related functions
#  if XSAN_INTERCEPT_PTHREAD_CREATE
// TODO: this should probably have an unversioned fallback for newer arches?
#    if defined(XSAN_PTHREAD_CREATE_VERSION)
  XSAN_INTERCEPT_FUNC_VER(pthread_create, XSAN_PTHREAD_CREATE_VERSION);
#    else
  XSAN_INTERCEPT_FUNC(pthread_create);
#    endif
  /// TODO: If not compose TSan, intercept join here
  // XSAN_INTERCEPT_FUNC(pthread_join);
#  endif

  // Intercept atexit function.
#  if XSAN_INTERCEPT___CXA_ATEXIT
  XSAN_INTERCEPT_FUNC(__cxa_atexit);
#  endif

// #  if XSAN_INTERCEPT_ATEXIT
//   XSAN_INTERCEPT_FUNC(atexit);
// #  endif

# if !SANITIZER_APPLE && !SANITIZER_ANDROID
  // Need to setup it, because interceptors check that the function is resolved.
  // But atexit is emitted directly into the module, so can't be resolved.
  REAL(atexit) = (int(*)(void(*)()))unreachable;
# endif

#  if XSAN_INTERCEPT_PTHREAD_ATFORK
  XSAN_INTERCEPT_FUNC(pthread_atfork);
#  endif

#  if XSAN_INTERCEPT_VFORK
  XSAN_INTERCEPT_FUNC(vfork);
#  endif

  __tsan::InitializeInterceptors();

  InitializePlatformInterceptors();

  VReport(1, "AddressSanitizer: libc interceptors initialized\n");
}

}  // namespace __xsan

#endif  // !SANITIZER_FUCHSIA
