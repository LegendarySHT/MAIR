#include "tsan_hooks.h"

#include "tsan_fd.h"
#include "tsan_interceptors.h"
#include "tsan_interceptors_common.inc"
#include "tsan_rtl.h"
#include "xsan_internal.h"
#include "xsan_thread.h"

namespace __tsan {
void SetCommonFlags(CommonFlags &cf);
void ValidateFlags();
void SetTsanThreadName(const char *name);
void SetTsanThreadNameByUserId(uptr uid, const char *name);
void InitializeFlags();
void DisableTsanForVfork();
void RecoverTsanAfterVforkParent();
void atfork_prepare();
void atfork_parent();
void atfork_child();
}  // namespace __tsan

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

// ---------------------- Special Function Hooks -----------------
TsanHooks::ScopedAtExitWrapper::ScopedAtExitWrapper(uptr pc, const void *ctx) {
  ThreadState *thr = __tsan::cur_thread();
  if (!__xsan::xsan_in_init) {
    Release(thr, pc, (uptr)ctx);
  }
  // Memory allocation in __cxa_atexit will race with free during exit,
  // because we do not see synchronization around atexit callback list.
  ThreadIgnoreBegin(thr, pc);
}
TsanHooks::ScopedAtExitWrapper::~ScopedAtExitWrapper() {
  ThreadState *thr = __tsan::cur_thread();
  ThreadIgnoreEnd(thr);
}
TsanHooks::ScopedAtExitHandler::ScopedAtExitHandler(uptr pc, const void *ctx) {
  // Stop init order checking to avoid false positives in the
  // initialization code, adhering the logic of ASan.
  ThreadState *thr = cur_thread();
  Acquire(thr, pc, (uptr)ctx);
  FuncEntry(thr, pc);
}
TsanHooks::ScopedAtExitHandler::~ScopedAtExitHandler() {
  FuncExit(cur_thread());
}
void *TsanHooks::vfork_before_and_after() {
  __tsan::DisableTsanForVfork();
  return nullptr;
}
void TsanHooks::vfork_parent_after(void *sp) {
  __tsan::RecoverTsanAfterVforkParent();
}
void TsanHooks::OnForkBefore() { atfork_prepare(); }
void TsanHooks::OnForkAfter(bool is_child) {
  if (is_child) {
    atfork_child();
  } else {
    atfork_parent();
  }
}
void TsanHooks::OnLibraryLoaded(const char *filename, void *handle) {
  __tsan::libignore()->OnLibraryLoaded(filename);
}
void TsanHooks::OnLibraryUnloaded() {
  __tsan::libignore()->OnLibraryUnloaded();
}
void TsanHooks::OnLongjmp(void *env, const char *fn_name, uptr pc) {
  __tsan::handle_longjmp(env, fn_name, pc);
}

// ---------------------- Flags Registration Hooks ---------------
void TsanHooks::InitializeFlags() { __tsan::InitializeFlags(); }
void TsanHooks::SetCommonFlags(CommonFlags &cf) { __tsan::SetCommonFlags(cf); }
void TsanHooks::ValidateFlags() { __tsan::ValidateFlags(); }

// ---------- Thread-Related Hooks --------------------------
void TsanHooks::SetThreadName(const char *name) {
  __tsan::SetTsanThreadName(name);
}
void TsanHooks::SetThreadNameByUserId(uptr uid, const char *name) {
  __tsan::SetTsanThreadNameByUserId(uid, name);
}

auto TsanHooks::CreateMainThread() -> Thread {
  Thread thread;
  // __tsan::ThreadCreate calls internal malloc, which needs proc to be
  // initialized.
  thread.tsan_thread = __tsan::cur_thread_init();
  __tsan::Processor *proc = __tsan::ProcCreate();
  __tsan::ProcWire(proc, thread.tsan_thread);
  thread.tid = __tsan::ThreadCreate(nullptr, 0, 0, true);
  return thread;
}

auto TsanHooks::CreateThread(u32 parent_tid, uptr child_uid, StackTrace *stack,
                             const void *data, uptr data_size, bool detached)
    -> Thread {
  Thread thread;
  thread.tid = __tsan::ThreadCreate(__tsan::cur_thread_init(), stack->trace[0],
                                    child_uid, detached);
  CHECK_NE(thread.tid, kMainTid);
  return thread;
}

void TsanHooks::ChildThreadInit(Thread &thread, tid_t os_id) {
  if (LIKELY(!__xsan::XsanThread::isMainThread())) {
    thread.tsan_thread = __tsan::SetCurrentThread();
    Processor *proc = __tsan::ProcCreate();
    ProcWire(proc, thread.tsan_thread);
  }
}

void TsanHooks::ChildThreadStartReal(Thread &thread, tid_t os_id) {
  // ThreadStart will call ThreadState's constructor, which will overwrite
  // the query key. So initialize the query key at the last.
  ThreadStart(thread.tsan_thread, thread.tid, os_id, ThreadType::Regular);
  __xsan::XsanThread::SetQueryKey(thread.tsan_thread->xsan_key);
}

void TsanHooks::DestroyThread(Thread &thread) {
  thread.tsan_thread->DestroyThreadState();
}

// ---------- Synchronization and File-Related Hooks ------------------------
void TsanHooks::AfterMmap(const Context &ctx, void *res, uptr size, int fd) {
  if (fd > 0)
    FdAccess(ctx.thr_, ctx.pc_, fd);
  MemoryRangeImitateWriteOrResetRange(ctx.thr_, ctx.pc_, (uptr)res, size);
}
void TsanHooks::BeforeMunmap(const Context &ctx, void *addr, uptr size) {
  UnmapShadow(ctx.thr_, (uptr)addr, size);
}

}  // namespace __tsan
