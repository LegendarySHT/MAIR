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