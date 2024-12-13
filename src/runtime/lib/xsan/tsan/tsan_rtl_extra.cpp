#include "tsan_rtl_extra.h"

#include "tsan_rtl.h"
namespace __tsan {
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

}  // namespace __tsan