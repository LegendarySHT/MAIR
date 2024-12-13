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
}  // namespace __tsan