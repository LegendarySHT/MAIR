#include "tsan_rtl_extra.h"

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "tsan_rtl.h"
namespace __tsan {
ScopedIgnoreTsan::ScopedIgnoreTsan(bool enable) : enable_(enable) {
#if !SANITIZER_GO
  if (enable_) {
    ThreadState *thr = cur_thread();
    nomalloc_ = thr->nomalloc;
    thr->nomalloc = false;
    thr->ignore_sync++;
    thr->ignore_reads_and_writes++;
    atomic_store_relaxed(&thr->in_signal_handler, 0);
  }
#endif
}

ScopedIgnoreTsan::~ScopedIgnoreTsan() {
#if !SANITIZER_GO
  if (enable_) {
    ThreadState *thr = cur_thread();
    thr->nomalloc = nomalloc_;
    thr->ignore_sync--;
    thr->ignore_reads_and_writes--;
    atomic_store_relaxed(&thr->in_signal_handler, 0);
  }
#endif
}

bool ShouldIgnoreInterceptors(ThreadState *thr) {
  return !thr->is_inited || thr->ignore_interceptors || thr->in_ignored_lib;
}

bool ShouldIgnoreAllocFreeHook() {
  ThreadState *thr = cur_thread();
  return (ctx == 0 || !ctx->initialized || thr->ignore_interceptors);
}

void OnPthreadCreate() {
  MaybeSpawnBackgroundThread();

  if (ctx->after_multithreaded_fork) {
    if (flags()->die_after_fork) {
      Report("ThreadSanitizer: starting new threads after multi-threaded "
          "fork is not supported. Dying (set die_after_fork=0 to override)\n");
      Die();
    } else {
      VPrintf(1,
              "ThreadSanitizer: starting new threads after multi-threaded "
              "fork is not supported (pid %lu). Continuing because of "
              "die_after_fork=0, but you are on your own\n",
              internal_getpid());
    }
  }
}

THREADLOCAL bool disabled_tsan = false;

/// See the following comment in tsan_interceptors_posix.cpp:vfork for details.
// Some programs (e.g. openjdk) call close for all file descriptors
// in the child process. Under tsan it leads to false positives, because
// address space is shared, so the parent process also thinks that
// the descriptors are closed (while they are actually not).
// This leads to false positives due to missed synchronization.
// Strictly saying this is undefined behavior, because vfork child is not
// allowed to call any functions other than exec/exit. But this is what
// openjdk does, so we want to handle it.
// We could disable interceptors in the child process. But it's not possible
// to simply intercept and wrap vfork, because vfork child is not allowed
// to return from the function that calls vfork, and that's exactly what
// we would do. So this would require some assembly trickery as well.
// Instead we simply turn vfork into fork.
void DisableTsanForVfork() {
  /* VFORK is epected to call exit/exec soon, so we should not do TSan's sanity
   * checks for its child. */
  if (disabled_tsan)
    return;
  disabled_tsan = true;
  ThreadState *thr = cur_thread();
  thr->ignore_interceptors = true;
  // We've just forked a multi-threaded process. We cannot reasonably function
  // after that (some mutexes may be locked before fork). So just enable
  // ignores for everything in the hope that we will exec soon.
  thr->ignore_interceptors++;
  thr->suppress_reports++;
  thr->in_vfork_child = true;
  ThreadIgnoreBegin(thr, 0);
  ThreadIgnoreSyncBegin(thr, 0);
}

void RecoverTsanAfterVforkParent() {
  ThreadState *thr = cur_thread();
  // We've just forked a multi-threaded process. We cannot reasonably function
  // after that (some mutexes may be locked before fork). So just enable
  // ignores for everything in the hope that we will exec soon.
  thr->ignore_interceptors--;
  thr->suppress_reports--;
  thr->in_vfork_child = false;
  ThreadIgnoreEnd(thr);
  ThreadIgnoreSyncEnd(thr);
  disabled_tsan = false;
}

}  // namespace __tsan

extern "C" {

/// TODO: define these functions only if LSan is used.
/*
  TSan intercepts dl_iterate_phdr, and leads to unexpected crashes in LSan,
which uses dl_iterate_phdr. The ideal solution is that ignore the
interceptors after LSan's checks are activated. However, LSan indeed
provides no interface for us to instrument a ScopedIgnoreInterceptors.
Therefore, we need to use other approaches to sense whether LSan's checks
are active. Here are some ideas:
  1. (Intrusive) Modify LSan's source code to provide interface to query its
    status.
  2. (Intrusive & Incompatible) Modify LSan's source code to replace
    dl_iterate_phdr with internal_dl_iterate_phdr.
  3. (Intrusive & Incompatible) Modify LSan's source code and register a
    ScopedIgnoreInterceptors before checks.
  4. (Non-intrusive) Use -Wl,-wrap,<symbol> to replace the symbols.
  5. (Non-intrusive & tricky) Use entry 'hook' __lsan_is_turned_off to
    obtain the caller ra of its caller, which could be considered as exit.,
  6. (Non-intrusive & heary) Use -finstrument-functions to register a
    callback for each function call.
Excluding all intrusive approaches, the only non-tricky simple solution is
to use -Wl,-wrap,<symbolc> (approach 4).

  The only trouble is that we need to intercept all the exported symbols about
LSan's check, as this approach does not work for those calls that locate in
the same file defining the called symbols.
*/

#define LSAN_REAL(f) __real_##f
#define LSAN_WRAP(f) __wrap_##f

// __lsan::DoLeakCheck 
void LSAN_REAL(_ZN6__lsan11DoLeakCheckEv)();
// __lsan::DoRecoverableLeakCheck 
void LSAN_REAL(_ZN6__lsan26DoRecoverableLeakCheckVoidEv)();
void LSAN_REAL(__lsan_do_leak_check)();
void LSAN_REAL(__lsan_do_recoverable_leak_check)();
using namespace __asan;
// interceptor of __lsan::DoLeakCheck 
void LSAN_WRAP(_ZN6__lsan11DoLeakCheckEv)() {
  __tsan::ScopedIgnoreInterceptors ignore_interceptors;
  LSAN_REAL(_ZN6__lsan11DoLeakCheckEv)();
}

// interceptor of __lsan::DoRecoverableLeakCheck 
void LSAN_WRAP(_ZN6__lsan26DoRecoverableLeakCheckVoidEv)() {
  __tsan::ScopedIgnoreInterceptors ignore_interceptors;
  LSAN_REAL(_ZN6__lsan26DoRecoverableLeakCheckVoidEv)();
}

// interceptor of __lsan_do_leak_check 
void LSAN_WRAP(__lsan_do_leak_check)() {
  __tsan::ScopedIgnoreInterceptors ignore_interceptors;
  LSAN_REAL(__lsan_do_leak_check)();
}

// interceptor of __lsan_do_recoverable_leak_check 
void LSAN_WRAP(__lsan_do_recoverable_leak_check)() {
  __tsan::ScopedIgnoreInterceptors ignore_interceptors;
  LSAN_REAL(__lsan_do_recoverable_leak_check)();
}

#undef LSAN_WRAP
#undef LSAN_REAL
}