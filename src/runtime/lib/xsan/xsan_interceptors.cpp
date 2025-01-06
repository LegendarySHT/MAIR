#include "xsan_interceptors.h"

#include "asan/orig/asan_flags.h"
#include "asan/orig/asan_internal.h"
#include "asan/orig/asan_poisoning.h"
#include "asan/orig/asan_report.h"
#include "asan/orig/asan_suppressions.h"
#include "lsan/lsan_common.h"
#include "tsan/tsan_interceptors.h"
#include "tsan/tsan_rtl.h"

#include "xsan_hooks.h"
#include "xsan_interceptors_memintrinsics.h"
#include "xsan_internal.h"
#include "xsan_stack.h"
#include "xsan_thread.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_libc.h"


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

extern "C" int pthread_attr_init(void *attr);
extern "C" int pthread_attr_destroy(void *attr);
extern "C" int pthread_equal(void *t1, void *t2);
extern "C" void *pthread_self();


#if SANITIZER_FREEBSD || SANITIZER_APPLE
#  define stdout __stdoutp
#  define stderr __stderrp
#endif
#if !SANITIZER_NETBSD
extern "C" int fileno_unlocked(void *stream);
extern "C" int dirfd(void *dirp);
#endif
#if SANITIZER_NETBSD
#  define dirfd(dirp) (*(int *)(dirp))
#  define fileno_unlocked(fp)              \
    (((__sanitizer_FILE *)fp)->_file == -1 \
         ? -1                              \
         : (int)(unsigned short)(((__sanitizer_FILE *)fp)->_file))

#  define stdout ((__sanitizer_FILE *)&__sF[1])
#  define stderr ((__sanitizer_FILE *)&__sF[2])

#  define nanosleep __nanosleep50
#  define vfork __vfork14
#endif



namespace __xsan {

THREADLOCAL uptr xsan_ignore_interceptors = 0;

ScopedIgnoreInterceptors::ScopedIgnoreInterceptors(bool in_report)
    : tsan_sii(), sit(in_report) {
  xsan_ignore_interceptors++;
}

ScopedIgnoreInterceptors::~ScopedIgnoreInterceptors() {
  xsan_ignore_interceptors--;
}

ScopedIgnoreChecks::ScopedIgnoreChecks() : ScopedIgnoreChecks(GET_CALLER_PC()) {}

ScopedIgnoreChecks::ScopedIgnoreChecks(uptr pc) {
  __tsan::ThreadIgnoreBegin(__tsan::cur_thread(), pc);
}

ScopedIgnoreChecks::~ScopedIgnoreChecks() {
  __tsan::ThreadIgnoreEnd(__tsan::cur_thread());
}

ScopedInterceptor::ScopedInterceptor(const XsanContext &xsan_ctx,
                                     const char *func, uptr caller_pc)
    : tsan_si(xsan_ctx.tsan_ctx_.thr_, func, caller_pc) {}

bool ShouldXsanIgnoreInterceptor(const XsanContext &xsan_ctx) {
  if (xsan_ignore_interceptors || !XsanInited()) {
    return true;
  }
  XsanThread *thread = __xsan::GetCurrentThread();
  return (thread && (!thread->is_inited_ || thread->in_ignored_lib_)) ||
         __xsan::ShouldSanitzerIgnoreInterceptors(xsan_ctx);
}


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

}  // namespace __xsan

// ---------------------- Wrappers ---------------- {{{1
using namespace __xsan;

extern __sanitizer_FILE *stdout, *stderr;

DECLARE_REAL(int, fflush, __sanitizer_FILE *fp)

static void FlushStreams() {
  REAL(fflush)(stdout);
  REAL(fflush)(stderr);
}

/// Changes exit code.
static int OnExit(void *ctx) {
  int exit_code = get_exit_code(ctx);
  FlushStreams();

  // FIXME: ask frontend whether we need to return failure.
  return exit_code;
}

/// Changes exit code.
static void finalize(void *arg) {
  int exit_code = get_exit_code();
  // Make sure the output is not lost.
  FlushStreams();
  if (exit_code)
    Die();
}

#  if XSAN_CONTAINS_TSAN
#    include "sanitizer_common/sanitizer_platform_interceptors.h"
// Causes interceptor recursion (getaddrinfo() and fopen())
/// TSan needs to ignore memory accesses in getaddrinfo()
#    undef SANITIZER_INTERCEPT_GETADDRINFO
// We define our own.
#    undef SANITIZER_INTERCEPT_TLS_GET_ADDR
#    define SANITIZER_INTERCEPT_TLS_GET_OFFSET 1
#  endif

DECLARE_REAL_AND_INTERCEPTOR(void *, malloc, usize)
DECLARE_REAL_AND_INTERCEPTOR(void, free, void *)


#  define XSAN_BEFORE_DLOPEN(filename, flag) \
    if (__asan::flags()->strict_init_order)  \
      __asan::StopInitOrderChecking();


#  define COMMON_INTERCEPT_FUNCTION_VER(name, ver) \
    XSAN_INTERCEPT_FUNC_VER(name, ver)
#  define COMMON_INTERCEPT_FUNCTION_VER_UNVERSIONED_FALLBACK(name, ver) \
    XSAN_INTERCEPT_FUNC_VER_UNVERSIONED_FALLBACK(name, ver)
#  define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size) \
    XSAN_WRITE_RANGE(ctx, ptr, size)
#  define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size) \
    XSAN_READ_RANGE(ctx, ptr, size)
#  define COMMON_INTERCEPTOR_ENTER(ctx, func, ...)  \
    XSAN_INTERCEPTOR_ENTER(ctx, func, __VA_ARGS__); \
    do {                                            \
      if constexpr (SANITIZER_APPLE) {              \
        if (UNLIKELY(!XsanInited()))                \
          return REAL(func)(__VA_ARGS__);           \
      } else {                                      \
        if (!TryXsanInitFromRtl())                  \
          return REAL(func)(__VA_ARGS__);           \
      }                                             \
    } while (false)
#  define COMMON_INTERCEPTOR_ENTER_NOIGNORE(ctx, func, ...)   \
    XSAN_INTERCEPTOR_ENTER_NO_IGNORE(ctx, func, __VA_ARGS__); \
    do {                                                      \
      XsanInitFromRtl();                                   \
    } while (false)

#  define COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name) \
    __xsan::SetSanitizerThreadName(name);
// Should be asanThreadRegistry().SetThreadNameByUserId(thread, name)
// But asan does not remember UserId's for threads (pthread_t);
// and remembers all ever existed threads, so the linear search by UserId
// can be slow.
#  define COMMON_INTERCEPTOR_SET_PTHREAD_NAME(ctx, thread, name)           \
    if (pthread_equal(pthread_self(), reinterpret_cast<void *>(thread))) { \
      COMMON_INTERCEPTOR_SET_THREAD_NAME(ctx, name);                       \
    } else {                                                               \
      __xsan::SetSanitizerThreadNameByUserId((uptr)(thread), (name));      \
    }
// Strict init-order checking is dlopen-hostile:
// https://github.com/google/sanitizers/issues/178
#  define COMMON_INTERCEPTOR_DLOPEN(filename, flag) \
    ({                                              \
      XSAN_BEFORE_DLOPEN(filename, flag);           \
      CheckNoDeepBind(filename, flag);              \
      __xsan::ScopedIgnoreChecks ignore_checks;     \
      REAL(dlopen)(filename, flag);                 \
    })
#  define COMMON_INTERCEPTOR_ON_EXIT(ctx) OnExit(ctx)

// Zero out addr if it points into shadow memory and was provided as a hint
// only, i.e., MAP_FIXED is not set.
[[maybe_unused]]
static bool fix_mmap_addr(void **addr, uptr sz, int flags) {
  const int MAP_FIXED = 0x10;
  if (*addr) {
    if (!IsAppMem((uptr)*addr) || !IsAppMem((uptr)*addr + sz - 1)) {
      if (flags & MAP_FIXED) {
        errno = errno_EINVAL;
        return false;
      } else {
        *addr = 0;
      }
    }
  }
  return true;
}

template <class Mmap>
static void *mmap_interceptor(void *ctx, Mmap real_mmap,
                              void *addr, SIZE_T sz, int prot, int flags,
                              int fd, OFF64_T off) {
  void *const MAP_FAILED = (void *)-1;
  /// FIXME: conflicts with cuda_test.cpp. Should we really return -1 when addr
  /// is not in app memory?
  // if (!fix_mmap_addr(&addr, sz, flags)) return MAP_FAILED;
  void *res = real_mmap(addr, sz, prot, flags, fd, off);
  if (res != MAP_FAILED) {
    // if (!IsAppMem((uptr)res) || !IsAppMem((uptr)res + sz - 1)) {
    //   Report("XSan: mmap at bad address: addr=%p size=%p res=%p\n",
    //          addr, (void*)sz, res);
    //   Die();
    // }
    if (IsAppMem((uptr)res) && IsAppMem((uptr)res + sz - 1)) {
      const XsanInterceptorContext *ctx_ =
          reinterpret_cast<const XsanInterceptorContext *>(ctx);
      AfterMmap(*ctx_, res, sz, fd);
    }
  }

  return res;
}

template <class Munmap>
static int munmap_interceptor(void *ctx, Munmap real_munmap, void *addr,
                              SIZE_T length) {
  const XsanInterceptorContext *ctx_ =
      reinterpret_cast<const XsanInterceptorContext *>(ctx);
  BeforeMunmap(*ctx_, addr, length);
  return real_munmap(addr, length);
}

#  define COMMON_INTERCEPTOR_MMAP_IMPL(ctx, mmap, addr, sz, prot, flags, fd, \
                                       off)                                  \
    do {                                                                     \
      (void)(ctx);                                                           \
      return mmap_interceptor(ctx, REAL(mmap), addr, sz, prot, flags, fd,    \
                              off);                                          \
    } while (false)

#  define COMMON_INTERCEPTOR_MUNMAP_IMPL(ctx, addr, length)   \
    do {                                                      \
      (void)(ctx);                                            \
      return munmap_interceptor(ctx, REAL(munmap), addr, sz); \
    } while (false)

#  define COMMON_INTERCEPTOR_LIBRARY_LOADED(filename, handle) \
    __xsan::OnLibraryLoaded(filename, handle)

#  define COMMON_INTERCEPTOR_LIBRARY_UNLOADED() __xsan::OnLibraryUnloaded()

#  define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED (!XsanInited())

#  define COMMON_INTERCEPTOR_GET_TLS_RANGE(begin, end) \
    if (XsanThread *t = GetCurrentThread()) {          \
      *begin = t->tls_begin();                         \
      *end = t->tls_end();                             \
    } else {                                           \
      *begin = *end = 0;                               \
    }

#  if CAN_SANITIZE_LEAKS
#    define COMMON_INTERCEPTOR_STRERROR() \
      __lsan::ScopedInterceptorDisabler disabler
#  endif

#  if XSAN_CONTAINS_TSAN
/* These MACROs are only defined by TSan,
   hence we directly use their definitions in TSan.
      COMMON_INTERCEPTOR_BLOCK_REAL
      COMMON_INTERCEPTOR_FILE_OPEN
      COMMON_INTERCEPTOR_FILE_CLOSE
      COMMON_INTERCEPTOR_ACQUIRE
      COMMON_INTERCEPTOR_RELEASE
      COMMON_INTERCEPTOR_DIR_ACQUIRE
      COMMON_INTERCEPTOR_FD_ACQUIRE
      COMMON_INTERCEPTOR_FD_RELEASE
      COMMON_INTERCEPTOR_FD_ACCESS
      COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT
      COMMON_INTERCEPTOR_USER_CALLBACK_START
      COMMON_INTERCEPTOR_USER_CALLBACK_END
      COMMON_INTERCEPTOR_HANDLE_RECVMSG
 */

// #    define COMMON_INTERCEPTOR_BLOCK_REAL(name) \
//       (__tsan::BlockingCall(xsan_ctx.tsan_ctx_.thr_), REAL(name))
// #    define COMMON_INTERCEPTOR_FILE_OPEN(ctx, file, path) \
//       __xsan::OnFileOpen(ctx, file, path)
// #    define COMMON_INTERCEPTOR_FILE_CLOSE(ctx, file) \
//       __xsan::OnFileClose(ctx, file)
// #    define COMMON_INTERCEPTOR_ACQUIRE(ctx, u) __xsan::OnAcquire(ctx, u)
// #    define COMMON_INTERCEPTOR_RELEASE(ctx, u) __xsan::OnRelease(ctx, u)
// #    define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path) \
//       __xsan::OnDirAcquire(ctx, path)
// #    define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd) __xsan::OnFdAcquire(ctx, fd)
// #    define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd) __xsan::OnFdRelease(ctx, fd)
// #    define COMMON_INTERCEPTOR_FD_ACCESS(ctx, fd) __xsan::OnFdAccess(ctx, fd)
// #    define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd) \
//       __xsan::OnFdSocketAccept(ctx, fd, newfd)
// #    define COMMON_INTERCEPTOR_USER_CALLBACK_START() xsi.DisableIgnores();
// #    define COMMON_INTERCEPTOR_USER_CALLBACK_END() xsi.EnableIgnores();
// #    if !SANITIZER_APPLE
// #      define COMMON_INTERCEPTOR_HANDLE_RECVMSG(ctx, msg) \
//         __xsan::OnHandleRecvmsg((ctx), (msg));
// #    endif
#    include "tsan/tsan_interceptors_common.inc"

#  else
#    define COMMON_INTERCEPTOR_BLOCK_REAL(name) REAL(name)

#    define COMMON_INTERCEPTOR_DIR_ACQUIRE(ctx, path) \
      do {                                            \
      } while (false)
#    define COMMON_INTERCEPTOR_FD_ACQUIRE(ctx, fd) \
      do {                                         \
      } while (false)
#    define COMMON_INTERCEPTOR_FD_RELEASE(ctx, fd) \
      do {                                         \
      } while (false)
#    define COMMON_INTERCEPTOR_FD_SOCKET_ACCEPT(ctx, fd, newfd) \
      do {                                                      \
      } while (false)
#  endif

#  include <sanitizer_common/sanitizer_common_interceptors.inc>
#  if !XSAN_CONTAINS_TSAN
#    include <sanitizer_common/sanitizer_signal_interceptors.inc>
#  endif

/// Following the logic of TSan, treat syscall in a special way.
#  define XSAN_SYSCALL()                                       \
    __xsan::XsanThread *xsan_thr = __xsan::GetCurrentThread(); \
    if (xsan_ignore_interceptors)                              \
      return;                                                  \
    ScopedSyscall scoped_syscall(xsan_thr)

struct ScopedSyscall {
  XsanThread *thr;

  explicit ScopedSyscall(XsanThread *thr) : thr(thr) { XsanInitFromRtl(); }

  ~ScopedSyscall() {
    /// FIXME: migrate handling of pending signals to XSan
    __tsan::ProcessPendingSignals(thr->tsan_thread_);
  }
};

#  if !SANITIZER_FREEBSD && !SANITIZER_APPLE
[[gnu::always_inline]] static void syscall_access_range(uptr pc, uptr offset,
                                                        uptr size, bool write) {
  XSAN_SYSCALL();
  ASAN_ACCESS_MEMORY_RANGE(nullptr, offset, size, write);
  __tsan::MemoryAccessRange(__tsan::cur_thread(), pc, offset, size, write);
}

#  endif

// Syscall interceptors don't have contexts, we don't support suppressions
// for them.
#  define COMMON_SYSCALL_PRE_READ_RANGE(p, s) \
    syscall_access_range(GET_CALLER_PC(), (uptr)(p), (uptr)(s), false)

#  define COMMON_SYSCALL_PRE_WRITE_RANGE(p, s) \
    syscall_access_range(GET_CALLER_PC(), (uptr)(p), (uptr)(s), true)

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

#  if XSAN_CONTAINS_TSAN

/*
 Only TSan defines the following macros:
  COMMON_SYSCALL_ACQUIRE
  COMMON_SYSCALL_RELEASE
  COMMON_SYSCALL_FD_CLOSE
  COMMON_SYSCALL_FD_ACQUIRE
  COMMON_SYSCALL_FD_RELEASE
  COMMON_SYSCALL_PRE_FORK
  COMMON_SYSCALL_POST_FORK
*/
#    include "tsan/tsan_interceptors_syscall.inc"

#  endif

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
  auto self = GetThreadSelf();
  auto args = xsanThreadArgRetval().GetArgs(self);

  /// Semaphore: comes from TSan, controlling the thread create event.
  created.Wait();
  t->ThreadStart(GetTid());

#    if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
        SANITIZER_SOLARIS
  __sanitizer_sigset_t sigset;
  t->GetStartData(sigset);
  SetSigProcMask(&sigset, nullptr);
#    endif

  // ThreadParam p is now not needed anymore, notify the parent thread.
  started.Post();

  thread_return_t retval = (*args.routine)(args.arg_retval);
  xsanThreadArgRetval().Finish(self, retval);

  return retval;
}

INTERCEPTOR(int, pthread_create, void *thread, void *attr,
            void *(*start_routine)(void *), void *arg) {
  GET_STACK_TRACE_THREAD;

  const uptr pc = stack.trace_buffer[0];
  XsanContext xsan_ctx(pc);
  ScopedInterceptor si(xsan_ctx, "pthread_create", stack.trace_buffer[1]);


  __xsan::OnPthreadCreate();

  __sanitizer_pthread_attr_t myattr;
  if (!attr) {
    pthread_attr_init(&myattr);
    attr = &myattr;
  }

  /// Ensure that we have enough stack space to store TLS.
  AdjustStackSize(attr);

  int detached = 0;
  if (attr)
    REAL(pthread_attr_getdetachstate)(attr, &detached);

  u32 current_tid = GetCurrentTidOrInvalid();

  __sanitizer_sigset_t sigset = {};
#    if SANITIZER_FREEBSD || SANITIZER_LINUX || SANITIZER_NETBSD || \
        SANITIZER_SOLARIS
  ScopedBlockSignals block(&sigset);
#    endif

  /// Note that sub_thread's recycle is delegated to sub thread.
  /// Hence, we could not use it after pthread_create in the parent thread.
  XsanThread *sub_thread =
      XsanThread::Create(sigset, current_tid, &stack, detached);

  ThreadParam p;
  p.t = sub_thread;

  int result;
  {
    ScopedIgnoreInterceptors sii;
    /// Sanitizers should not intervene the internality ptrhead_create,
    /// elminating FP in TLS handlings,
    ScopedIgnoreChecks sic(pc);
    // Ignore all allocations made by pthread_create: thread stack/TLS may be
    // stored by pthread for future reuse even after thread destruction, and
    // the linked list it's stored in doesn't even hold valid pointers to the
    // objects, the latter are calculated by obscure pointer arithmetic.
#    if CAN_SANITIZE_LEAKS
    __lsan::ScopedInterceptorDisabler disabler;
#    endif
    xsanThreadArgRetval().Create(detached, {start_routine, arg}, [&]() -> uptr {
      result = REAL(pthread_create)(thread, attr, xsan_thread_start, &p);
      return result ? 0 : *(uptr *)(thread);
    });
  }

  if (result == 0) {
    // sub_thread must live, as sub_thread waits for `created_.Post()`
    sub_thread->PostNonMainThreadCreate(pc, *(uptr *)thread);
    // Synchronization on p.tid serves two purposes:
    // 1. ThreadCreate must finish before the new thread starts.
    //    Otherwise the new thread can call pthread_detach, but the pthread_t
    //    identifier is not yet registered in ThreadRegistry by ThreadCreate.
    // 2. ThreadStart must finish before this thread continues.
    //    Otherwise, this thread can call pthread_detach and reset thr->sync
    //    before the new thread got a chance to acquire from it in ThreadStart.
    p.created.Post();
    // Wait for ThreadParam *p to be free.
    p.started.Wait();
  } else {
    // If the thread didn't start delete the AsanThread to avoid leaking it.
    // Note AsanThreadContexts never get destroyed so the AsanThreadContext
    // that was just created for the AsanThread is wasted.
    sub_thread->Destroy();
  }
  if (attr == &myattr)
    pthread_attr_destroy(&myattr);
  return result;
}

INTERCEPTOR(int, pthread_join, void *thread, void **retval) {
  SCOPED_XSAN_INTERCEPTOR_RAW(pthread_join, t, arg);
  int result;
  ScopedPthreadJoin scoped_pthread_join(result, xsan_ctx, thread);
  xsanThreadArgRetval().Join((uptr)thread, [&]() {
    result = COMMON_INTERCEPTOR_BLOCK_REAL(pthread_join)(thread, retval);
    return !result;
  });
  return result;
}

INTERCEPTOR(int, pthread_detach, void *thread) {
  SCOPED_XSAN_INTERCEPTOR_RAW(pthread_detach, thread);
  int result;
  ScopedPthreadDetach scoped_pthread_detach(result, xsan_ctx, thread);
  xsanThreadArgRetval().Detach((uptr)thread, [&]() {
    result = REAL(pthread_detach)(thread);
    return !result;
  });
  return result;
}

INTERCEPTOR(int, pthread_exit, void *retval) {
  SCOPED_XSAN_INTERCEPTOR_RAW(pthread_exit, retval);
  xsanThreadArgRetval().Finish(GetThreadSelf(), retval);
  return REAL(pthread_exit)(retval);
}

#    if XSAN_INTERCEPT_TRYJOIN
INTERCEPTOR(int, pthread_tryjoin_np, void *thread, void **ret) {
  SCOPED_XSAN_INTERCEPTOR_RAW(pthread_tryjoin_np, thread, ret);
  int result;
  ScopedPthreadTryJoin scoped_pthread_tryjoin(result, xsan_ctx, thread);
  xsanThreadArgRetval().Join((uptr)thread, [&]() {
    result = REAL(pthread_tryjoin_np)(thread, ret);
    return !result;
  });
  return result;
}
#    endif

#    if XSAN_INTERCEPT_TIMEDJOIN
INTERCEPTOR(int, pthread_timedjoin_np, void *thread, void **ret,
            const struct timespec *abstime) {
  SCOPED_XSAN_INTERCEPTOR_RAW(pthread_timedjoin_np, thread, ret, abstime);
  int result;
  ScopedPthreadTryJoin scoped_pthread_tryjoin(result, xsan_ctx, thread);
  xsanThreadArgRetval().Join((uptr)thread, [&]() {
    result = COMMON_INTERCEPTOR_BLOCK_REAL(pthread_timedjoin_np)(thread, ret,
                                                                 abstime);
    return !result;
  });
  return result;
}
#    endif

// DEFINE_INTERNAL_PTHREAD_FUNCTIONS
namespace __sanitizer {
int internal_pthread_create(void *th, void *attr, void *(*callback)(void *),
                            void *param) {
  ScopedIgnoreInterceptors ignore;
  return REAL(pthread_create)(th, attr, callback, param);
}
int internal_pthread_join(void *th, void **ret) {
  ScopedIgnoreInterceptors ignore;
  return REAL(pthread_join)(th, ret);
}
}  // namespace __sanitizer
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

// Since Solaris 10/SPARC, ucp->uc_stack.ss_sp refers to the stack base address
// as on other targets.  For binary compatibility, the new version uses a
// different external name, so we intercept that.
#    if SANITIZER_SOLARIS && defined(__sparc__)
INTERCEPTOR(void, __makecontext_v2, struct ucontext_t *ucp, void (*func)(),
            int argc, ...) {
#    else
INTERCEPTOR(void, makecontext, struct ucontext_t *ucp, void (*func)(), int argc,
            ...) {
#    endif
  va_list ap;
  uptr args[64];
  // We don't know a better way to forward ... into REAL function. We can
  // increase args size if necessary.
  CHECK_LE(argc, ARRAY_SIZE(args));
  internal_memset(args, 0, sizeof(args));
  va_start(ap, argc);
  for (int i = 0; i < argc; ++i) args[i] = va_arg(ap, uptr);
  va_end(ap);

#    define ENUMERATE_ARRAY_4(start) \
      args[start], args[start + 1], args[start + 2], args[start + 3]
#    define ENUMERATE_ARRAY_16(start)                         \
      ENUMERATE_ARRAY_4(start), ENUMERATE_ARRAY_4(start + 4), \
          ENUMERATE_ARRAY_4(start + 8), ENUMERATE_ARRAY_4(start + 12)
#    define ENUMERATE_ARRAY_64()                                             \
      ENUMERATE_ARRAY_16(0), ENUMERATE_ARRAY_16(16), ENUMERATE_ARRAY_16(32), \
          ENUMERATE_ARRAY_16(48)

#    if SANITIZER_SOLARIS && defined(__sparc__)
  REAL(__makecontext_v2)
#    else
  REAL(makecontext)
#    endif
  ((struct ucontext_t *)ucp, func, argc, ENUMERATE_ARRAY_64());

#    undef ENUMERATE_ARRAY_4
#    undef ENUMERATE_ARRAY_16
#    undef ENUMERATE_ARRAY_64

  // Sign the stack so we can identify it for unpoisoning.
  SignContextStack(ucp);
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
  OnLongjmp(env, "longjmp", GET_CALLER_PC());
  REAL(longjmp)(env, val);
}

#  if XSAN_INTERCEPT__LONGJMP
INTERCEPTOR(void, _longjmp, void *env, int val) {
  OnLongjmp(env, "_longjmp", GET_CALLER_PC());
  REAL(_longjmp)(env, val);
}
#  endif

#  if XSAN_INTERCEPT___LONGJMP_CHK
INTERCEPTOR(void, __longjmp_chk, void *env, int val) {
  OnLongjmp(env, "__longjmp_chk", GET_CALLER_PC());
  REAL(__longjmp_chk)(env, val);
}
#  endif

#  if XSAN_INTERCEPT_SIGLONGJMP
INTERCEPTOR(void, siglongjmp, void *env, int val) {
  OnLongjmp(env, "siglongjmp", GET_CALLER_PC());
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
  ALIAS(WRAP(strchr));
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
  XsanInitFromRtl();
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

INTERCEPTOR(char *, strncat, char *to, const char *from, usize size) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strncat, to, from, size);
  XsanInitFromRtl();
  if (__asan::flags()->replace_str) {
    uptr from_length = MaybeRealStrnlen(from, size);
    uptr copy_length = Min<uptr>(size, from_length + 1);
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
  if constexpr (SANITIZER_APPLE) {
    // strcpy is called from malloc_default_purgeable_zone()
    // in __asan::ReplaceSystemAlloc() on Mac.
    if (UNLIKELY(!XsanInited()))
      return REAL(strcpy)(to, from);
  } else {
    if (!TryXsanInitFromRtl())
      return REAL(strcpy)(to, from);
  }

  if (__asan::flags()->replace_str) {
    uptr from_size = internal_strlen(from) + 1;
    CHECK_RANGES_OVERLAP("strcpy", to, from_size, from, from_size);
    XSAN_READ_RANGE(ctx, from, from_size);
    XSAN_WRITE_RANGE(ctx, to, from_size);
  }
  return REAL(strcpy)(to, from);
}

// Windows doesn't always define the strdup identifier,
// and when it does it's a macro defined to either _strdup
// or _strdup_dbg, _strdup_dbg ends up calling _strdup, so
// we want to intercept that. push/pop_macro are used to avoid problems
// if this file ends up including <string.h> in the future.
#  if SANITIZER_WINDOWS
#    pragma push_macro("strdup")
#    undef strdup
#    define strdup _strdup
#  endif

INTERCEPTOR(char *, strdup, const char *s) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strdup, s);
  if (UNLIKELY(!TryXsanInitFromRtl()))
    return internal_strdup(s);
  uptr length = internal_strlen(s);
  if (__asan::flags()->replace_str) {
    XSAN_READ_RANGE(ctx, s, length + 1);
  }
  GET_STACK_TRACE_MALLOC;
  void *new_mem = xsan_malloc(length + 1, &stack);
  if (new_mem) {
    REAL(memcpy)(new_mem, s, length + 1);
  }
  return reinterpret_cast<char *>(new_mem);
}

#  if XSAN_INTERCEPT___STRDUP
INTERCEPTOR(char *, __strdup, const char *s) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strdup, s);
  if (UNLIKELY(!TryXsanInitFromRtl()))
    return internal_strdup(s);
  uptr length = internal_strlen(s);
  if (__asan::flags()->replace_str) {
    XSAN_READ_RANGE(ctx, s, length + 1);
  }
  GET_STACK_TRACE_MALLOC;
  void *new_mem = xsan_malloc(length + 1, &stack);
  if (new_mem) {
    REAL(memcpy)(new_mem, s, length + 1);
  }
  return reinterpret_cast<char *>(new_mem);
}
#  endif  // XSAN_INTERCEPT___STRDUP

INTERCEPTOR(char *, strncpy, char *to, const char *from, usize size) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strncpy, to, from, size);
  XsanInitFromRtl();
  if (__asan::flags()->replace_str) {
    uptr from_size = Min<uptr>(size, MaybeRealStrnlen(from, size) + 1);
    CHECK_RANGES_OVERLAP("strncpy", to, from_size, from, from_size);
    XSAN_READ_RANGE(ctx, from, from_size);
    XSAN_WRITE_RANGE(ctx, to, size);
  }
  return REAL(strncpy)(to, from, size);
}

template <typename Fn>
static ALWAYS_INLINE auto StrtolImpl(void *ctx, Fn real, const char *nptr,
                                     char **endptr, int base)
    -> decltype(real(nullptr, nullptr, 0)) {
  if (!__asan::flags()->replace_str)
    return real(nptr, endptr, base);
  char *real_endptr;
  auto res = real(nptr, &real_endptr, base);
  StrtolFixAndCheck(ctx, nptr, endptr, real_endptr, base);
  return res;
}

#  define INTERCEPTOR_STRTO_BASE(ret_type, func)                             \
    INTERCEPTOR(ret_type, func, const char *nptr, char **endptr, int base) { \
      void *ctx;                                                             \
      XSAN_INTERCEPTOR_ENTER(ctx, func, nptr, endptr, base);                 \
      XsanInitFromRtl();                                                     \
      return StrtolImpl(ctx, REAL(func), nptr, endptr, base);                \
    }

INTERCEPTOR_STRTO_BASE(long long, strtoll)

#  if SANITIZER_WINDOWS
INTERCEPTOR(long, strtol, const char *nptr, char **endptr, int base) {
  // REAL(strtol) may be ntdll!strtol, which doesn't set errno. Instead,
  // call REAL(strtoll) and do the range check ourselves.
  COMPILER_CHECK(sizeof(long) == sizeof(u32));

  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, strtol);
  XsanInitFromRtl();

  long long result = StrtolImpl(ctx, REAL(strtoll), nptr, endptr, base);

  if (result > INT32_MAX) {
    errno = errno_ERANGE;
    return INT32_MAX;
  }
  if (result < INT32_MIN) {
    errno = errno_ERANGE;
    return INT32_MIN;
  }
  return (long)result;
}
#  else
INTERCEPTOR_STRTO_BASE(long, strtol)
#  endif

#  if SANITIZER_GLIBC
INTERCEPTOR_STRTO_BASE(long, __isoc23_strtol)
INTERCEPTOR_STRTO_BASE(long long, __isoc23_strtoll)
#  endif

INTERCEPTOR(int, atoi, const char *nptr) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, atoi, nptr);
  if (SANITIZER_APPLE && UNLIKELY(!XsanInited()))
    return REAL(atoi)(nptr);
  XsanInitFromRtl();
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
  if (SANITIZER_APPLE && UNLIKELY(!XsanInited()))
    return REAL(atol)(nptr);
  XsanInitFromRtl();
  if (!__asan::flags()->replace_str) {
    return REAL(atol)(nptr);
  }
  char *real_endptr;
  long result = REAL(strtol)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  XSAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}

INTERCEPTOR(long long, atoll, const char *nptr) {
  void *ctx;
  XSAN_INTERCEPTOR_ENTER(ctx, atoll, nptr);
  XsanInitFromRtl();
  if (!__asan::flags()->replace_str) {
    return REAL(atoll)(nptr);
  }
  char *real_endptr;
  long long result = REAL(strtoll)(nptr, &real_endptr, 10);
  FixRealStrtolEndptr(nptr, &real_endptr);
  XSAN_READ_STRING(ctx, nptr, (real_endptr - nptr) + 1);
  return result;
}

static int setup_at_exit_wrapper(uptr pc, AtExitFuncTy f,
                                 bool is_on_exit = false, void *arg = nullptr,
                                 void *dso = nullptr);

#  if XSAN_INTERCEPT___CXA_ATEXIT
INTERCEPTOR(int, __cxa_atexit, void (*func)(void *), void *arg,
            void *dso_handle) {
  if (__xsan::in_symbolizer())
    return 0;
  if (SANITIZER_APPLE && UNLIKELY(!XsanInited()))
    return REAL(__cxa_atexit)(func, arg, dso_handle);
  XsanInitFromRtl();
#    if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#    endif
  SCOPED_XSAN_INTERCEPTOR(__cxa_atexit, func, arg, dso_handle);
  // int res = REAL(__cxa_atexit)(func, arg, dso_handle);
  // REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  // return res;
  return setup_at_exit_wrapper(GET_CALLER_PC(), (AtExitFuncTy)func, false, arg,
                               dso_handle);
}
#  endif  // XSAN_INTERCEPT___CXA_ATEXIT

#  if XSAN_INTERCEPT_ATEXIT
INTERCEPTOR(int, atexit, void (*func)()) {
  if (__xsan::in_symbolizer())
    return 0;

  XsanInitFromRtl();
#    if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#    endif
  SCOPED_XSAN_INTERCEPTOR_RAW(atexit, func);
  // Avoid calling real atexit as it is unreachable on at least on Linux.
  // int res = REAL(__cxa_atexit)((void (*)(void *a))func, nullptr, nullptr);
  // REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  // return res;
  return setup_at_exit_wrapper(GET_CALLER_PC(), (AtExitFuncTy)func);
}
#  endif

#  if XSAN_INTERCEPT_ON_EXIT
INTERCEPTOR(int, on_exit, void (*func)(int, void *), void *arg) {
  if (__xsan::in_symbolizer())
    return 0;

  XsanInitFromRtl();
#    if CAN_SANITIZE_LEAKS
  __lsan::ScopedInterceptorDisabler disabler;
#    endif
  SCOPED_XSAN_INTERCEPTOR(on_exit, func, arg);
  // Avoid calling real atexit as it is unreachable on at least on Linux.
  // int res = REAL(__cxa_atexit)((void (*)(void *a))func, nullptr, nullptr);
  // REAL(__cxa_atexit)(AtCxaAtexit, nullptr, nullptr);
  // return res;
  return setup_at_exit_wrapper(GET_CALLER_PC(), (AtExitFuncTy)func, true, arg);
}
#  endif

#  if XSAN_INTERCEPT___CXA_ATEXIT || XSAN_INTERCEPT_ATEXIT || \
      XSAN_INTERCEPT_ON_EXIT

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

  {
    ScopedAtExitHandler saeh(ctx->pc, ctx);
    ((void (*)())ctx->f)();
  }
  Free(ctx);
}

static void XSanCxaAtExitWrapper(void *arg) {
  AtExitCtx *ctx = (AtExitCtx *)arg;

  {
    ScopedAtExitHandler saeh(ctx->pc, ctx);
    ((void (*)(void *arg))ctx->f)(ctx->arg);
  }
  Free(ctx);
}

static void XSanOnExitWrapper(int status, void *arg) {
  AtExitCtx *ctx = (AtExitCtx *)arg;

  {
    ScopedAtExitHandler saeh(ctx->pc, ctx);
    ((void (*)(int status, void *arg))ctx->f)(status, ctx->arg);
  }
  Free(ctx);
}

#  endif

static int setup_at_exit_wrapper(uptr pc, AtExitFuncTy f, bool is_on_exit,
                                 void *arg, void *dso) {
  auto *ctx = New<AtExitCtx>();
  ctx->f = f;
  ctx->arg = arg;
  ctx->pc = pc;
  ScopedAtExitWrapper saew(pc, ctx);
  int res;
#  if XSAN_INTERCEPT_ON_EXIT
  if (is_on_exit) {
    res = REAL(on_exit)(XSanOnExitWrapper, ctx);
  } else
#  endif
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

  return res;
}

#  if XSAN_INTERCEPT_PTHREAD_ATFORK
extern "C" {
extern int _pthread_atfork(void (*prepare)(), void (*parent)(),
                           void (*child)());
}

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
DEFINE_REAL(int, vfork,)
DECLARE_EXTERN_INTERCEPTOR_AND_WRAPPER(int, vfork,)
#  endif

// ---------------------- InitializeXsanInterceptors ---------------- {{{1
namespace __xsan {

#  if !SANITIZER_APPLE && !SANITIZER_ANDROID
static void unreachable() {
  Report("FATAL: XSan: unreachable called\n");
  Die();
}
#  endif

void InitializeXsanInterceptors() {
  static bool was_called_once;
  CHECK(!was_called_once);
  was_called_once = true;
  InitializePlatformInterceptors();
  InitializeCommonInterceptors();
#  if !XSAN_CONTAINS_TSAN
  InitializeSignalInterceptors();
#  endif
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
  ASAN_INTERCEPT_FUNC(atoll);
  XSAN_INTERCEPT_FUNC(strtol);
  XSAN_INTERCEPT_FUNC(strtoll);
#  if SANITIZER_GLIBC
  XSAN_INTERCEPT_FUNC(__isoc23_strtol);
  XSAN_INTERCEPT_FUNC(__isoc23_strtoll);
#  endif
  // Intecept jump-related functions.
  XSAN_INTERCEPT_FUNC(longjmp);

#  if XSAN_INTERCEPT_SWAPCONTEXT
  XSAN_INTERCEPT_FUNC(swapcontext);
  // See the makecontext interceptor above for an explanation.
#    if SANITIZER_SOLARIS && defined(__sparc__)
  ASAN_INTERCEPT_FUNC(__makecontext_v2);
#    else
  ASAN_INTERCEPT_FUNC(makecontext);
#    endif
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
  XSAN_INTERCEPT_FUNC(_Unwind_RaiseException);
#  endif
  // Indirectly intercept std::rethrow_exception.
#  if XSAN_INTERCEPT__UNWIND_SJLJ_RAISEEXCEPTION
  XSAN_INTERCEPT_FUNC(_Unwind_SjLj_RaiseException);
#  endif

  // Intercept threading-related functions
#  if XSAN_INTERCEPT_PTHREAD_CREATE
// TODO: this should probably have an unversioned fallback for newer arches?
#    if defined(XSAN_PTHREAD_CREATE_VERSION)
  XSAN_INTERCEPT_FUNC_VER(pthread_create, XSAN_PTHREAD_CREATE_VERSION);
#    else
  XSAN_INTERCEPT_FUNC(pthread_create);
#    endif
  /// If not compose TSan, intercept join here
  XSAN_INTERCEPT_FUNC(pthread_join);
  XSAN_INTERCEPT_FUNC(pthread_detach);
  XSAN_INTERCEPT_FUNC(pthread_exit);
#    if XSAN_INTERCEPT_TIMEDJOIN
  XSAN_INTERCEPT_FUNC(pthread_timedjoin_np);
#    endif

#    if XSAN_INTERCEPT_TRYJOIN
  XSAN_INTERCEPT_FUNC(pthread_tryjoin_np);
#    endif

#  endif

  // Intercept atexit function.
#  if XSAN_INTERCEPT___CXA_ATEXIT
  XSAN_INTERCEPT_FUNC(__cxa_atexit);
#  endif

#  if XSAN_INTERCEPT_ON_EXIT
  XSAN_INTERCEPT_FUNC(on_exit);
#  endif

  // #  if XSAN_INTERCEPT_ATEXIT
  //   XSAN_INTERCEPT_FUNC(atexit);
  // #  endif


#  if XSAN_INTERCEPT_PTHREAD_ATFORK
  XSAN_INTERCEPT_FUNC(pthread_atfork);
#  endif

#  if XSAN_INTERCEPT_VFORK
  XSAN_INTERCEPT_FUNC(vfork);
#  endif

#  if !SANITIZER_APPLE && !SANITIZER_ANDROID
  // Need to setup it, because interceptors check that the function is resolved.
  // But atexit is emitted directly into the module, so can't be resolved.
  REAL(atexit) = (int (*)(void (*)()))unreachable;
#  endif

  /// FIXME: bug in clone_test.cpp
  /// Multiple Die() cause Asan_Die dead loop
  /// Original TSan also report the similar problem
  /// The root cause is that TSan cannot handle clone with CLONE_VM flag,
  /// which is similar to vfork/pthread_create, sharing the same address space
  /// with the parent process.
  if (REAL(__cxa_atexit)(&finalize, 0, 0)) {
    Printf("XSan: failed to setup atexit callback\n");
    Die();
  }

  __tsan::InitializeInterceptors();

  VReport(1, "AddressSanitizer: libc interceptors initialized\n");
}

#  if SANITIZER_WINDOWS
#    pragma pop_macro("strdup")
#  endif

}  // namespace __xsan

#endif  // !SANITIZER_FUCHSIA
