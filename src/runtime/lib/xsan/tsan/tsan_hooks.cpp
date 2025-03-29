#include "tsan_hooks.h"

#include "tsan_rtl.h"

namespace __tsan {

bool TsanHooks::ShouldIgnoreInterceptors(const Context &ctx) {
  return !ctx.thr_->is_inited || ctx.thr_->ignore_interceptors ||
         ctx.thr_->in_ignored_lib;
}

bool TsanHooks::ShouldIgnoreAllocFreeHook() {
  ThreadState *thr = cur_thread();
  return (ctx == 0 || !ctx->initialized || thr->ignore_interceptors);
}

Tid ThreadConsumeTid(ThreadState *thr, uptr pc, uptr uid);
TsanHooks::ScopedPthreadJoin::ScopedPthreadJoin(const int &res,
                                                const Context &ctx,
                                                const void *th)
    : res_(res), ctx_(ctx) {
  auto [thr, pc] = ctx_;
  tid_ = ThreadConsumeTid(thr, pc, (uptr)th);
  ThreadIgnoreBegin(thr, pc);
}

TsanHooks::ScopedPthreadJoin::~ScopedPthreadJoin() {
  auto [thr, pc] = ctx_;
  ThreadIgnoreEnd(thr);
  if (res_ == 0) {
    ThreadJoin(thr, pc, tid_);
  }
}

TsanHooks::ScopedPthreadDetach::ScopedPthreadDetach(const int &res,
                                                    const Context &ctx,
                                                    const void *th)
    : res_(res), ctx_(ctx) {
  auto [thr, pc] = ctx_;
  tid_ = ThreadConsumeTid(thr, pc, (uptr)th);
}

TsanHooks::ScopedPthreadDetach::~ScopedPthreadDetach() {
  if (res_ != 0)
    return;
  auto [thr, pc] = ctx_;
  ThreadDetach(thr, pc, tid_);
}

TsanHooks::ScopedPthreadTryJoin::ScopedPthreadTryJoin(const int &res,
                                                      const Context &ctx,
                                                      const void *th)
    : th_((uptr)th), res_(res), ctx_(ctx) {
  auto [thr, pc] = ctx_;
  tid_ = ThreadConsumeTid(thr, pc, th_);
  ThreadIgnoreBegin(thr, pc);
}

TsanHooks::ScopedPthreadTryJoin::~ScopedPthreadTryJoin() {
  auto [thr, pc] = ctx_;
  ThreadIgnoreEnd(thr);
  if (res_ == 0) {
    ThreadJoin(thr, pc, tid_);
  } else {
    ThreadNotJoined(thr, pc, tid_, th_);
  }
}

void TsanHooks::OnPthreadCreate() {
  MaybeSpawnBackgroundThread();

  if (ctx->after_multithreaded_fork) {
    if (flags()->die_after_fork) {
      Report(
          "ThreadSanitizer: starting new threads after multi-threaded "
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
