/*
 *  This file provide the implementations of some symbols defined in
 * tsan_interceptors_posix.cpp.
 *    - libignore
 *    - ProcessPendingSignalsImpl
 *    - InitializeInterceptors : this is provided by xsan_disability_dummy.cpp
 *    - InitializeLibIgnore
 *    - __tsan_setjmp
 */
#include "tsan_rtl_extra.h"

#include "tsan/tsan_interceptors.h"

using namespace __tsan;

#if SANITIZER_FREEBSD || SANITIZER_APPLE
#  define stdout __stdoutp
#  define stderr __stderrp
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

#ifdef __mips__
struct ucontext_t {
  u64 opaque[768 / sizeof(u64) + 1];
};
#else
struct ucontext_t {
  // The size is determined by looking at sizeof of real ucontext_t on linux.
  u64 opaque[936 / sizeof(u64) + 1];
};
#endif

#if defined(__x86_64__) || defined(__mips__) || SANITIZER_PPC64V1 || \
    defined(__s390x__)
#  define PTHREAD_ABI_BASE "GLIBC_2.3.2"
#elif defined(__aarch64__) || SANITIZER_PPC64V2
#  define PTHREAD_ABI_BASE "GLIBC_2.17"
#endif

// extern "C" int pthread_attr_init(void *attr);
// extern "C" int pthread_attr_destroy(void *attr);
// DECLARE_REAL(int, pthread_attr_getdetachstate, void *, void *)
// extern "C" int pthread_attr_setstacksize(void *attr, uptr stacksize);
// extern "C" int pthread_atfork(void (*prepare)(void), void (*parent)(void),
//                               void (*child)(void));
// extern "C" int pthread_key_create(unsigned *key, void (*destructor)(void*
// v)); extern "C" int pthread_setspecific(unsigned key, const void *v);
// DECLARE_REAL(int, pthread_mutexattr_gettype, void *, void *)
// DECLARE_REAL(int, fflush, __sanitizer_FILE *fp)
// DECLARE_REAL_AND_INTERCEPTOR(void *, malloc, uptr size)
// DECLARE_REAL_AND_INTERCEPTOR(void, free, void *ptr)
// extern "C" int pthread_equal(void *t1, void *t2);
// extern "C" void *pthread_self();
// extern "C" void _exit(int status);
// #if !SANITIZER_NETBSD
// extern "C" int fileno_unlocked(void *stream);
// extern "C" int dirfd(void *dirp);
// #endif
// #if SANITIZER_NETBSD
// extern __sanitizer_FILE __sF[];
// #else
// extern __sanitizer_FILE *stdout, *stderr;
// #endif
#if !SANITIZER_FREEBSD && !SANITIZER_APPLE && !SANITIZER_NETBSD
const int PTHREAD_MUTEX_RECURSIVE = 1;
const int PTHREAD_MUTEX_RECURSIVE_NP = 1;
#else
const int PTHREAD_MUTEX_RECURSIVE = 2;
const int PTHREAD_MUTEX_RECURSIVE_NP = 2;
#endif
#if !SANITIZER_FREEBSD && !SANITIZER_APPLE && !SANITIZER_NETBSD
const int EPOLL_CTL_ADD = 1;
#endif
const int SIGILL = 4;
const int SIGTRAP = 5;
const int SIGABRT = 6;
const int SIGFPE = 8;
const int SIGSEGV = 11;
const int SIGPIPE = 13;
const int SIGTERM = 15;
#if defined(__mips__) || SANITIZER_FREEBSD || SANITIZER_APPLE || \
    SANITIZER_NETBSD
const int SIGBUS = 10;
const int SIGSYS = 12;
#else
const int SIGBUS = 7;
const int SIGSYS = 31;
#endif
void *const MAP_FAILED = (void *)-1;
#if SANITIZER_NETBSD
const int PTHREAD_BARRIER_SERIAL_THREAD = 1234567;
#elif !SANITIZER_APPLE
const int PTHREAD_BARRIER_SERIAL_THREAD = -1;
#endif
const int MAP_FIXED = 0x10;
typedef long long_t;
typedef __sanitizer::u16 mode_t;

// From /usr/include/unistd.h
#define F_ULOCK 0 /* Unlock a previously locked region.  */
#define F_LOCK 1  /* Lock a region for exclusive use.  */
#define F_TLOCK 2 /* Test and lock a region for exclusive use.  */
#define F_TEST 3  /* Test a region for other processes locks.  */

#if SANITIZER_FREEBSD || SANITIZER_APPLE || SANITIZER_NETBSD
const int SA_SIGINFO = 0x40;
const int SIG_SETMASK = 3;
#elif defined(__mips__)
const int SA_SIGINFO = 8;
const int SIG_SETMASK = 3;
#else
const int SA_SIGINFO = 4;
const int SIG_SETMASK = 2;
#endif

// #define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED \
//   (!cur_thread_init()->is_inited)

DECLARE_REAL(int, pthread_sigmask, int how, const __sanitizer_sigset_t *set,
             __sanitizer_sigset_t *oldset);

namespace __tsan {
struct SignalDesc {
  bool armed;
  __sanitizer_siginfo siginfo;
  ucontext_t ctx;
};

struct ThreadSignalContext {
  int int_signal_send;
  atomic_uintptr_t in_blocking_func;
  SignalDesc pending_signals[kSigCount];
  // emptyset and oldset are too big for stack.
  __sanitizer_sigset_t emptyset;
  __sanitizer_sigset_t oldset;
};

}  // namespace __tsan

static ThreadSignalContext *SigCtx(ThreadState *thr) {
  ThreadSignalContext *ctx = (ThreadSignalContext *)thr->signal_ctx;
  if (ctx == 0 && !thr->is_dead) {
    ctx = (ThreadSignalContext *)MmapOrDie(sizeof(*ctx), "ThreadSignalContext");
    MemoryResetRange(thr, (uptr)&SigCtx, (uptr)ctx, sizeof(*ctx));
    thr->signal_ctx = ctx;
  }
  return ctx;
}

// Cleanup old bufs.
static void JmpBufGarbageCollect(ThreadState *thr, uptr sp) {
  for (uptr i = 0; i < thr->jmp_bufs.Size(); i++) {
    JmpBuf *buf = &thr->jmp_bufs[i];
    if (buf->sp <= sp) {
      uptr sz = thr->jmp_bufs.Size();
      internal_memcpy(buf, &thr->jmp_bufs[sz - 1], sizeof(*buf));
      thr->jmp_bufs.PopBack();
      i--;
    }
  }
}

static void SetJmp(ThreadState *thr, uptr sp) {
  if (!thr->is_inited)  // called from libc guts during bootstrap
    return;
  // Cleanup old bufs.
  JmpBufGarbageCollect(thr, sp);
  // Remember the buf.
  JmpBuf *buf = thr->jmp_bufs.PushBack();
  buf->sp = sp;
  buf->shadow_stack_pos = thr->shadow_stack_pos;
  ThreadSignalContext *sctx = SigCtx(thr);
  buf->int_signal_send = sctx ? sctx->int_signal_send : 0;
  buf->in_blocking_func =
      sctx ? atomic_load(&sctx->in_blocking_func, memory_order_relaxed) : false;
  buf->in_signal_handler =
      atomic_load(&thr->in_signal_handler, memory_order_relaxed);
}

extern "C" void __tsan_setjmp(uptr sp) { SetJmp(cur_thread_init(), sp); }

namespace __tsan {

static ALIGNED(64) char interceptor_placeholder[sizeof(InterceptorContext)];
InterceptorContext *interceptor_ctx() {
  return reinterpret_cast<InterceptorContext *>(&interceptor_placeholder[0]);
}

LibIgnore *libignore() { return &interceptor_ctx()->libignore; }

void InitializeLibIgnore() {
  const SuppressionContext &supp = *Suppressions();
  const uptr n = supp.SuppressionCount();
  for (uptr i = 0; i < n; i++) {
    const Suppression *s = supp.SuppressionAt(i);
    if (0 == internal_strcmp(s->type, kSuppressionLib))
      libignore()->AddIgnoredLibrary(s->templ);
  }
  if (flags()->ignore_noninstrumented_modules)
    libignore()->IgnoreNoninstrumentedModules(true);
  libignore()->OnLibraryLoaded(0);
}

void PlatformCleanUpThreadState(ThreadState *thr) {
  ThreadSignalContext *sctx = thr->signal_ctx;
  if (sctx) {
    thr->signal_ctx = 0;
    UnmapOrDie(sctx, sizeof(*sctx));
  }
}

static void ReportErrnoSpoiling(ThreadState *thr, uptr pc, int sig) {
  VarSizeStackTrace stack;
  // StackTrace::GetNestInstructionPc(pc) is used because return address is
  // expected, OutputReport() will undo this.
  ObtainCurrentStack(thr, StackTrace::GetNextInstructionPc(pc), &stack);
  ThreadRegistryLock l(&ctx->thread_registry);
  ScopedReport rep(ReportTypeErrnoInSignal);
  rep.SetSigNum(sig);
  if (!IsFiredSuppression(ctx, ReportTypeErrnoInSignal, stack)) {
    rep.AddStack(stack, true);
    OutputReport(thr, rep);
  }
}

static void CallUserSignalHandler(ThreadState *thr, bool sync, bool acquire,
                                  int sig, __sanitizer_siginfo *info,
                                  void *uctx) {
  CHECK(thr->slot);
  __sanitizer_sigaction *sigactions = interceptor_ctx()->sigactions;
  if (acquire)
    Acquire(thr, 0, (uptr)&sigactions[sig]);
  // Signals are generally asynchronous, so if we receive a signals when
  // ignores are enabled we should disable ignores. This is critical for sync
  // and interceptors, because otherwise we can miss synchronization and report
  // false races.
  int ignore_reads_and_writes = thr->ignore_reads_and_writes;
  int ignore_interceptors = thr->ignore_interceptors;
  int ignore_sync = thr->ignore_sync;
  // For symbolizer we only process SIGSEGVs synchronously
  // (bug in symbolizer or in tsan). But we want to reset
  // in_symbolizer to fail gracefully. Symbolizer and user code
  // use different memory allocators, so if we don't reset
  // in_symbolizer we can get memory allocated with one being
  // feed with another, which can cause more crashes.
  int in_symbolizer = thr->in_symbolizer;
  if (!ctx->after_multithreaded_fork) {
    thr->ignore_reads_and_writes = 0;
    thr->fast_state.ClearIgnoreBit();
    thr->ignore_interceptors = 0;
    thr->ignore_sync = 0;
    thr->in_symbolizer = 0;
  }
  // Ensure that the handler does not spoil errno.
  const int saved_errno = errno;
  errno = 99;
  // This code races with sigaction. Be careful to not read sa_sigaction twice.
  // Also need to remember pc for reporting before the call,
  // because the handler can reset it.
  volatile uptr pc = (sigactions[sig].sa_flags & SA_SIGINFO)
                         ? (uptr)sigactions[sig].sigaction
                         : (uptr)sigactions[sig].handler;
  if (pc != sig_dfl && pc != sig_ign) {
    // The callback can be either sa_handler or sa_sigaction.
    // They have different signatures, but we assume that passing
    // additional arguments to sa_handler works and is harmless.
    ((__sanitizer_sigactionhandler_ptr)pc)(sig, info, uctx);
  }
  if (!ctx->after_multithreaded_fork) {
    thr->ignore_reads_and_writes = ignore_reads_and_writes;
    if (ignore_reads_and_writes)
      thr->fast_state.SetIgnoreBit();
    thr->ignore_interceptors = ignore_interceptors;
    thr->ignore_sync = ignore_sync;
    thr->in_symbolizer = in_symbolizer;
  }
  // We do not detect errno spoiling for SIGTERM,
  // because some SIGTERM handlers do spoil errno but reraise SIGTERM,
  // tsan reports false positive in such case.
  // It's difficult to properly detect this situation (reraise),
  // because in async signal processing case (when handler is called directly
  // from rtl_generic_sighandler) we have not yet received the reraised
  // signal; and it looks too fragile to intercept all ways to reraise a signal.
  if (ShouldReport(thr, ReportTypeErrnoInSignal) && !sync && sig != SIGTERM &&
      errno != 99)
    ReportErrnoSpoiling(thr, pc, sig);
  errno = saved_errno;
}

void ProcessPendingSignalsImpl(ThreadState *thr) {
  atomic_store(&thr->pending_signals, 0, memory_order_relaxed);
  ThreadSignalContext *sctx = SigCtx(thr);
  if (sctx == 0)
    return;
  atomic_fetch_add(&thr->in_signal_handler, 1, memory_order_relaxed);
  internal_sigfillset(&sctx->emptyset);
  int res = REAL(pthread_sigmask)(SIG_SETMASK, &sctx->emptyset, &sctx->oldset);
  CHECK_EQ(res, 0);
  for (int sig = 0; sig < kSigCount; sig++) {
    SignalDesc *signal = &sctx->pending_signals[sig];
    if (signal->armed) {
      signal->armed = false;
      CallUserSignalHandler(thr, false, true, sig, &signal->siginfo,
                            &signal->ctx);
    }
  }
  res = REAL(pthread_sigmask)(SIG_SETMASK, &sctx->oldset, 0);
  CHECK_EQ(res, 0);
  atomic_fetch_add(&thr->in_signal_handler, -1, memory_order_relaxed);
}

}  // namespace __tsan

using namespace __tsan;

ScopedInterceptor::ScopedInterceptor(ThreadState *thr, const char *fname,
                                     uptr pc)
    : thr_(thr), in_ignored_lib_(false), ignoring_(false) {
  LazyInitialize(thr);
  if (!thr_->is_inited)
    return;
  if (!thr_->ignore_interceptors)
    FuncEntry(thr, pc);
  DPrintf("#%d: intercept %s()\n", thr_->tid, fname);
  ignoring_ =
      !thr_->in_ignored_lib && (flags()->ignore_interceptors_accesses ||
                                libignore()->IsIgnored(pc, &in_ignored_lib_));
  EnableIgnores();
}

ScopedInterceptor::~ScopedInterceptor() {
  if (!thr_->is_inited)
    return;
  DisableIgnores();
  if (!thr_->ignore_interceptors) {
    ProcessPendingSignals(thr_);
    FuncExit(thr_);
    CheckedMutex::CheckNoLocks();
  }
}

NOINLINE
void ScopedInterceptor::EnableIgnoresImpl() {
  ThreadIgnoreBegin(thr_, 0);
  if (flags()->ignore_noninstrumented_modules)
    thr_->suppress_reports++;
  if (in_ignored_lib_) {
    DCHECK(!thr_->in_ignored_lib);
    thr_->in_ignored_lib = true;
  }
}

NOINLINE
void ScopedInterceptor::DisableIgnoresImpl() {
  ThreadIgnoreEnd(thr_);
  if (flags()->ignore_noninstrumented_modules)
    thr_->suppress_reports--;
  if (in_ignored_lib_) {
    DCHECK(thr_->in_ignored_lib);
    thr_->in_ignored_lib = false;
  }
}
