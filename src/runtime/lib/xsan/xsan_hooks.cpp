//===-- xsan_hooks.cpp ---------------------------------------------------===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is used to manuplate the hooks for coordination between multiple
// sanitizers.
//===----------------------------------------------------------------------===//

#include "xsan_hooks.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

#include "asan/asan_thread.h"
#include "asan/orig/asan_internal.h"
#include "lsan/lsan_common.h"
#include "tsan/tsan_rtl.h"
#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_thread.h"

using namespace __xsan;
// ---------------------- State/Ignoration Management Hooks --------------------
/*
 Manage and notify the following states:
  - if XSan is in internal
  - if XSan is in symbolizer
  - if XSan should ignore interceptors
  - the exit code of XSan
*/

namespace __tsan {
void EnterSymbolizer();
void ExitSymbolizer();

/*
 The sub-sanitizers implement the following ignore predicates to ignore
  - Interceptors
  - Allocation/Free Hooks
 which are shared by all sub-sanitizers.
 */
SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool ShouldIgnoreInterceptors(ThreadState *thr) { return false; }
SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool ShouldIgnoreInterceptors() { return false; }
SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool ShouldIgnoreAllocFreeHook() { return false; }
}  // namespace __tsan

namespace __xsan {
THREADLOCAL int xsan_in_intenal = 0;

bool IsInXsanInternal() { return xsan_in_intenal != 0; }

ScopedXsanInternal::ScopedXsanInternal() { xsan_in_intenal++; }

ScopedXsanInternal::~ScopedXsanInternal() { xsan_in_intenal--; }

THREADLOCAL int is_in_symbolizer;
void EnterSymbolizer() {
  ++is_in_symbolizer;
  __tsan::EnterSymbolizer();
}

void ExitSymbolizer() {
  --is_in_symbolizer;
  __tsan::ExitSymbolizer();
}

bool ShouldSanitzerIgnoreInterceptors(const XsanContext &xsan_ctx) {
  /// Avoid sanity checks in XSan internal.
  if (IsInXsanInternal()) {
    return true;
  }

  bool should_ignore = false;

  /// TODO: to support libignore, we plan to migrate it to Xsan.
  /// xsan_suppressions.cpp is required accordingly.
  should_ignore = __tsan::ShouldIgnoreInterceptors(xsan_ctx.tsan_ctx_.thr_);

  return should_ignore;
}

bool ShouldSanitzerIgnoreAllocFreeHook() {
  return __tsan::ShouldIgnoreAllocFreeHook();
}

int get_exit_code(const void *ctx) {
  int exit_code = 0;
  if (CAN_SANITIZE_LEAKS && common_flags()->detect_leaks &&
      __lsan::HasReportedLeaks()) {
    return common_flags()->exitcode;
  }

  auto *tsan_thr =
      ctx == nullptr
          ? __tsan::cur_thread()
          : ((const XsanInterceptorContext *)ctx)->xsan_ctx.tsan_ctx_.thr_;
  exit_code = __tsan::Finalize(tsan_thr);
  return exit_code;
}

}  // namespace __xsan

// ---------- End of State Management Hooks -----------------

// ---------------------- Memory Management Hooks -------------------
/// As XSan uses ASan's heap allocator and fake stack directly, hence we don't
/// need to invoke ASan's hooks here.
namespace __tsan {

void OnAllocatorUnmap(uptr p, uptr size);

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnXsanAllocHook(uptr ptr, uptr size, bool write, uptr pc) {}

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnXsanFreeHook(uptr ptr, bool write, uptr pc) {}

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnXsanAllocFreeTailHook(uptr pc) {}

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnFakeStackDestory(uptr addr, uptr size) {}
}  // namespace __tsan

namespace __asan {

SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool GetASanMellocStackTrace(u32 &stack_trace_id, uptr addr,
                             bool set_stack_trace_id) {
  return false;
}

}  // namespace __asan

namespace __xsan {

void OnAllocatorMap(uptr p, uptr size) {}

void OnAllocatorMapSecondary(uptr p, uptr size, uptr user_begin,
                             uptr user_size) {}

void OnAllocatorUnmap(uptr p, uptr size) { __tsan::OnAllocatorUnmap(p, size); }

void XsanAllocHook(uptr ptr, uptr size, bool write, uptr pc) {
  __tsan::OnXsanAllocHook(ptr, size, write, pc);
}

void XsanFreeHook(uptr ptr, bool write, uptr pc) {
  __tsan::OnXsanFreeHook(ptr, write, pc);
}

void XsanAllocFreeTailHook(uptr pc) { __tsan::OnXsanAllocFreeTailHook(pc); }

void OnFakeStackDestory(uptr addr, uptr size) {
  __tsan::OnFakeStackDestory(addr, size);
}

bool IsInFakeStack(const XsanThread *thr, uptr addr) {
  __asan::FakeStack *fake_stack = thr->asan_thread_->get_fake_stack();
  if (fake_stack) {
    return fake_stack->AddrIsInFakeStack((uptr)addr);
  }
  return false;
}

bool GetMellocStackTrace(u32 &stack_trace_id, uptr addr,
                         bool set_stack_trace_id) {
  return __asan::GetASanMellocStackTrace(stack_trace_id, addr,
                                         set_stack_trace_id);
}

}  // namespace __xsan

// ---------- End of Memory Management Hooks -------------------

// ---------------------- pthread-related hooks -----------------
/* pthread_create, pthread_join, pthread_detach, pthread_tryjoin_np, ... */

namespace __tsan {
/// TSan may spawn a background thread to recycle resource in pthread_create.
/// What's more, TSan does not support starting new threads after multi-threaded
/// fork.
void OnPthreadCreate();
Tid ThreadConsumeTid(ThreadState *thr, uptr pc, uptr uid);
ScopedPthreadJoin::ScopedPthreadJoin(const int &res,
                                     const XsanContext &xsan_ctx,
                                     const void *th)
    : res_(res), tsan_ctx_(xsan_ctx.tsan_ctx_) {
  auto [thr, pc] = tsan_ctx_;
  tid_ = ThreadConsumeTid(thr, pc, (uptr)th);
  ThreadIgnoreBegin(thr, pc);
}

ScopedPthreadJoin::~ScopedPthreadJoin() {
  auto [thr, pc] = tsan_ctx_;
  ThreadIgnoreEnd(thr);
  if (res_ == 0) {
    ThreadJoin(thr, pc, tid_);
  }
}

ScopedPthreadDetach::ScopedPthreadDetach(const int &res,
                                         const XsanContext &xsan_ctx,
                                         const void *th)
    : res_(res), tsan_ctx_(xsan_ctx.tsan_ctx_) {
  auto [thr, pc] = tsan_ctx_;
  tid_ = ThreadConsumeTid(thr, pc, (uptr)th);
}

ScopedPthreadDetach::~ScopedPthreadDetach() {
  if (res_ != 0) return;
  auto [thr, pc] = tsan_ctx_;
  ThreadDetach(thr, pc, tid_);
}

ScopedPthreadTryJoin::ScopedPthreadTryJoin(const int &res,
                                           const XsanContext &xsan_ctx,
                                           const void *th)
    : th_((uptr)th), res_(res), tsan_ctx_(xsan_ctx.tsan_ctx_) {
  auto [thr, pc] = tsan_ctx_;
  tid_ = ThreadConsumeTid(thr, pc, th_);
  ThreadIgnoreBegin(thr, pc);
}

ScopedPthreadTryJoin::~ScopedPthreadTryJoin() {
  auto [thr, pc] = tsan_ctx_;
  ThreadIgnoreEnd(thr);
  if (res_ == 0) {
    ThreadJoin(thr, pc, tid_);
  } else {
    ThreadNotJoined(thr, pc, tid_, th_);
  }
}
}

namespace __asan {
/// ASan 1) checks the correctness of main thread ID, 2) checks the init orders.
void OnPthreadCreate();
}

namespace __xsan {
void OnPthreadCreate() {
  __asan::OnPthreadCreate();
  __tsan::OnPthreadCreate();
}
}

// ---------------- End of pthread-related hooks -----------------


// ---------------------- Special Function Hooks -----------------
extern "C" {
void *__asan_extra_spill_area();
void __asan_handle_vfork(void *sp);
}
namespace __asan {
void __asan_handle_no_return();
void StopInitOrderChecking();

/// Comes from BeforeFork and AfterFork in asan_posix.cpp
/// We don't want to modify the asan_posix.cpp only for such a small change.
void OnForkBefore() {
  VReport(2, "BeforeFork tid: %llu\n", GetTid());
  if (CAN_SANITIZE_LEAKS) {
    __lsan::LockGlobal();
  }
  // `_lsan` functions defined regardless of `CAN_SANITIZE_LEAKS` and lock the
  // stuff we need.
#if !XSAN_CONTAINS_TSAN
  /// 1. TSan restrictifys the internal lock checks.
  /// 2. This LockThreads() locks ThreadRegistry, which conflicts with TSan's
  ///    lock restrictions, as defined in `mutex_meta` in tsan_rtl.cpp.
  /// 3. In details, TSan does not support multi-lock of type ThreadRegistry.
  /// Therefore, we forbid this lock while TSan is enabled.
  __lsan::LockThreads();
#endif
  /// Shared resources managed by XSan
  // __lsan::LockAllocator();
  // StackDepotLockBeforeFork();
}
void OnForkAfter() {
  /// Shared resources managed by XSan
  // StackDepotUnlockAfterFork(fork_child);
  // // `_lsan` functions defined regardless of `CAN_SANITIZE_LEAKS` and unlock
  // // the stuff we need.
  // __lsan::UnlockAllocator();
#if !XSAN_CONTAINS_TSAN
  /// 1. TSan restrictifys the internal lock checks.
  /// 2. This LockThreads() locks ThreadRegistry, which conflicts with TSan's
  ///    lock restrictions, as defined in `mutex_meta` in tsan_rtl.cpp.
  /// 3. In details, TSan does not support multi-lock of type ThreadRegistry.
  /// Therefore, we forbid this lock while TSan is enabled.
  __lsan::UnlockThreads();
#endif
  if (CAN_SANITIZE_LEAKS) {
    __lsan::UnlockGlobal();
  }
  VReport(2, "AfterFork tid: %llu\n", GetTid());
}
}  // namespace __asan

namespace __tsan {

void Release(ThreadState *thr, uptr pc, uptr addr);

void ThreadIgnoreBegin(ThreadState *thr, uptr pc);
void ThreadIgnoreEnd(ThreadState *thr);

void DisableTsanForVfork();
void RecoverTsanAfterVforkParent();

LibIgnore *libignore();
void handle_longjmp(void *env, const char *fname, uptr caller_pc);

void atfork_prepare();
void atfork_parent();
void atfork_child();
}  // namespace __tsan

namespace __xsan {
/*
Provides hooks for special functions, such as
  - atexit / on_exit / _cxa_atexit
  - longjmp / siglongjmp / _longjmp / _siglongjmp
  - dlopen / dlclose
  - vfork / fork
*/

ScopedAtExitWrapper::ScopedAtExitWrapper(uptr pc, const void *ctx) {
  __tsan::ThreadState *thr = __tsan::cur_thread();
  if (!xsan_in_init) {
    __tsan::Release(thr, pc, (uptr)ctx);
  }
  // Memory allocation in __cxa_atexit will race with free during exit,
  // because we do not see synchronization around atexit callback list.
  __tsan::ThreadIgnoreBegin(thr, pc);
}

ScopedAtExitWrapper::~ScopedAtExitWrapper() {
  __tsan::ThreadState *thr = __tsan::cur_thread();
  __tsan::ThreadIgnoreEnd(thr);
}

ScopedAtExitHandler::ScopedAtExitHandler(uptr pc, const void *ctx) {
  /// Stop init order checking to avoid false positives in the
  /// initialization code, adhering the logic of ASan.
  __asan::StopInitOrderChecking();

  __tsan::ThreadState *thr = __tsan::cur_thread();
  __tsan::Acquire(thr, pc, (uptr)ctx);
  __tsan::FuncEntry(thr, pc);
}

ScopedAtExitHandler::~ScopedAtExitHandler() {
  __tsan::FuncExit(__tsan::cur_thread());
}

/// To implement macro COMMON_INTERCEPTOR_SPILL_AREA in *vfork.S
/// Notably, this function is called TWICE at the attitude per process.
extern "C" void *__xsan_vfork_before_and_after() {
  __tsan::DisableTsanForVfork();
  /// Invoked TRIPLE totally, once before vfork to store the sp, twice after
  /// vfork child/parent to restore the sp.
  return __asan_extra_spill_area();
}

/// To implement macro COMMON_INTERCEPTOR_HANDLE_VFORK in *vfork.S
extern "C" void __xsan_vfork_parent_after(void *sp) {
  __tsan::RecoverTsanAfterVforkParent();
  /// Unpoison vfork child's new stack space : [stack_bottom, sp]
  __asan_handle_vfork(sp);
}

/// Used to lock before fork
void OnForkBefore() {
  __asan::OnForkBefore();
  __tsan::atfork_prepare();
}

/// Used to unlock after fork
void OnForkAfter(bool is_child) {
  if (is_child) {
    __tsan::atfork_child();
  } else {
    __tsan::atfork_parent();
  }
  __asan::OnForkAfter();
}

// Ignore interceptors in OnLibraryLoaded()/Unloaded().  These hooks use code
// (ListOfModules::init, MemoryMappingLayout::DumpListOfModules) that make
// intercepted calls, which can cause deadlockes with ReportRace() which also
// uses this code.
void OnLibraryLoaded(const char *filename, void *handle) {
  __xsan::ScopedIgnoreInterceptors ignore;
  __tsan::libignore()->OnLibraryLoaded(filename);
}

void OnLibraryUnloaded() { 
  __xsan::ScopedIgnoreInterceptors ignore;
  __tsan::libignore()->OnLibraryUnloaded(); 
}

void OnLongjmp(void *env, const char *fn_name, uptr pc) {
  __tsan::handle_longjmp(env, fn_name, pc);
  __asan_handle_no_return();
}
}  // namespace __xsan

// --------------- End of Special Function Hooks -----------------

// ---------------------- Flags Registration Hooks ---------------
namespace __asan {
void InitializeFlags();
void SetCommonFlags(CommonFlags &cf);
void ValidateFlags();
}  // namespace __asan

namespace __tsan {
void InitializeFlags();
void SetCommonFlags(CommonFlags &cf);
void ValidateFlags();
}  // namespace __tsan

namespace __xsan {
void InitializeSanitizerFlags() {
  {
    ScopedSanitizerToolName tool_name("AddressSanitizer");
    // Initialize flags. This must be done early, because most of the
    // initialization steps look at flags().
    __asan::InitializeFlags();
  }
  {
    ScopedSanitizerToolName tool_name("ThreadSanitizer");
    __tsan::InitializeFlags();
  }
}

void SetSanitizerCommonFlags(CommonFlags &cf) {
  __asan::SetCommonFlags(cf);
  __tsan::SetCommonFlags(cf);
}

void ValidateSanitizerFlags() {
  __asan::ValidateFlags();
  __tsan::ValidateFlags();
}
}  // namespace __xsan

// ---------- End of Flags Registration Hooks ---------------

// ---------- Thread-Related Hooks --------------------------
namespace __asan {
SANITIZER_WEAK_CXX_DEFAULT_IMPL
void SetAsanThreadName(const char *name) {}
void SetAsanThreadNameByUserId(uptr uid, const char *name) {}
}  // namespace __asan

namespace __tsan {
SANITIZER_WEAK_CXX_DEFAULT_IMPL
void SetTsanThreadName(const char *name) {}
SANITIZER_WEAK_CXX_DEFAULT_IMPL
void SetTsanThreadNameByUserId(uptr uid, const char *name) {}
}  // namespace __tsan
namespace __xsan {
void SetSanitizerThreadName(const char *name) {
  __asan::SetAsanThreadName(name);
  __tsan::SetTsanThreadName(name);
}

void SetSanitizerThreadNameByUserId(uptr uid, const char *name) {
  /// Should be asanThreadRegistry().SetThreadNameByUserId(thread, name)
  /// But asan does not remember UserId's for threads (pthread_t);
  /// and remembers all ever existed threads, so the linear search by UserId
  /// can be slow.
  // __asan::SetAsanThreadNameByUserId(uid, name);
  __tsan::SetTsanThreadNameByUserId(uid, name);
}

/*
The following events happen in order while `pthread_create` is executed:

- [parent-thread] : pthread_create
  - [parent-thread] : XsanThread::Create
    - [parent-thread] : (**HOOK**) XsanThread::OnThreadCreate
  - [parent-thread] : (**HOOK**) XsanThread::PostNonMainThreadCreate
  - [child-thread] : xsan_thread_start
    - [child-thread] : SetCurrentThread
      - [child-thread] : (**HOOK**) OnSetCurrentThread
    - [child-thread] : XsanThread::ThreadStart
      - [child-thread] : (**HOOK**) XsanThread::BeforeThreadStart
      - [child-thread] : start_routine_
      - [child-thread] : (**HOOK**) XsanThread::AfterThreadStart

- [child-thread] : TSD destroy / Active destroy 
  - [child-thread] : XsanThread::Destroy
    - [child-thread] : (**HOOK**) XsanThread::OnThreadDestroy
*/

void XsanThread::OnThreadCreate(const void *start_data, uptr data_size,
                                u32 parent_tid, StackTrace *stack,
                                bool detached) {
  auto *asan_thread = __asan::AsanThread::Create(
      /* start_data */ start_data, /* data_size */ data_size,
      /* parent_tid */ parent_tid, /* stack */ stack, /* detached */ detached);
  this->asan_thread_ = asan_thread;
  this->tid_ = asan_thread->tid();

  if (this->is_main_thread_) {
    // Main thread.
    auto *tsan_thread = __tsan::cur_thread_init();
    __tsan::Processor *proc = __tsan::ProcCreate();
    __tsan::ProcWire(proc, tsan_thread);
    this->tsan_tid_ = __tsan::ThreadCreate(nullptr, 0, 0, true);
    this->tsan_thread_ = tsan_thread;
    tsan_thread->xsan_thread = this;
  } else {
    // Other thread create for TSan is called in CreateTsanThread.
  }
}

Tid XsanThread::PostNonMainThreadCreate(uptr pc, uptr uid) {
  /// TODO: merge ASan's ThreadContext and TSan's ThreadContext.
  Tid tsan_tid = __tsan::ThreadCreate(__tsan::cur_thread_init(), pc, uid,
                                      IsStateDetached(detached_));
  CHECK_NE(tsan_tid, kMainTid);
  tsan_tid_ = tsan_tid;

  return tsan_tid;
}

void XsanThread::OnThreadDestroy() {
  this->tsan_thread_->DestroyThreadState();
  // Common resource, must be managed by the XSan
  this->asan_thread_->Destroy();
}

void XsanThread::BeforeThreadStart(tid_t os_id) {
  /* TSan's logic */
  __tsan::ThreadState *thr = tsan_thread_;
  if (isMainThread()) {
    __tsan::ThreadStart(thr, tsan_tid_, os_id, ThreadType::Regular);
  } else {
    __tsan::Processor *proc = __tsan::ProcCreate();
    __tsan::ProcWire(proc, thr);
    __tsan::ThreadStart(thr, tsan_tid_, os_id, ThreadType::Regular);
  }

  /* ASan's logic */
  this->asan_thread_->BeforeThreadStart(os_id);
}

void XsanThread::AfterThreadStart() { this->asan_thread_->AfterThreadStart(); }

void OnSetCurrentThread(XsanThread *t) {
  __asan::SetCurrentThread(t->asan_thread_);

  auto *tsan_thread = __tsan::SetCurrentThread();
  t->tsan_thread_ = tsan_thread;
  tsan_thread->xsan_thread = t;
}

}  // namespace __xsan

// ---------- End of Thread-Related Hooks --------------------------

// ---------- Synchronization and File-Related Hooks ------------------------
extern "C" int fileno_unlocked(void *stream);

namespace __tsan {
SANITIZER_WEAK_CXX_DEFAULT_IMPL
uptr Dir2addr(const char *path);
uptr File2addr(const char *path);
void Acquire(ThreadState *thr, uptr pc, uptr addr);
void Release(ThreadState *thr, uptr pc, uptr addr);

void FdAcquire(ThreadState *thr, uptr pc, int fd);
void FdRelease(ThreadState *thr, uptr pc, int fd);
void FdAccess(ThreadState *thr, uptr pc, int fd);
void FdClose(ThreadState *thr, uptr pc, int fd, bool write = true);
void FdFileCreate(ThreadState *thr, uptr pc, int fd);
void FdSocketAccept(ThreadState *thr, uptr pc, int fd, int newfd);
void MemoryRangeImitateWriteOrResetRange(ThreadState *thr, uptr pc, uptr addr,
                                         uptr size);

void HandleRecvmsg(ThreadState *thr, uptr pc, __sanitizer_msghdr *msg);
void AfterMmap(const XsanInterceptorContext &ctx, void *res, uptr size, int fd)  {
  auto [thr, pc] = ctx.xsan_ctx.tsan_ctx_;
  if (fd > 0)
    __tsan::FdAccess(thr, pc, fd);
  __tsan::MemoryRangeImitateWriteOrResetRange(thr, pc, (uptr)res, size);
}
void BeforeMunmap(const XsanInterceptorContext &ctx, void *addr, uptr size) {
  auto [thr, pc] = ctx.xsan_ctx.tsan_ctx_;
  UnmapShadow(thr, (uptr)addr, size);
}
}  // namespace __tsan

namespace __asan {

void PoisonShadow(uptr addr, uptr size, u8 value);

void AfterMmap(void *res, uptr size, int fd)  {
  if (!size || res == (void *)-1) {
    return;
  }
  const uptr beg = reinterpret_cast<uptr>(res);
  DCHECK(IsAligned(beg, GetPageSize()));
  SIZE_T rounded_length = RoundUpTo(size, GetPageSize());
  // Only unpoison shadow if it's an ASAN managed address.
  if (__asan::AddrIsInMem(beg) && __asan::AddrIsInMem(beg + rounded_length - 1))
    __asan::PoisonShadow(beg, RoundUpTo(size, GetPageSize()), 0);
}

void BeforeMunmap(void *addr, uptr size) {
  // We should not tag if munmap fail, but it's to late to tag after
  // real_munmap, as the pages could be mmaped by another thread.
  const uptr beg = reinterpret_cast<uptr>(addr);
  if (size && IsAligned(beg, GetPageSize())) {
    SIZE_T rounded_length = RoundUpTo(size, GetPageSize());
    // Protect from unmapping the shadow.
    if (__asan::AddrIsInMem(beg) && __asan::AddrIsInMem(beg + rounded_length - 1))
      __asan::PoisonShadow(beg, rounded_length, 0);
  }
}
}

namespace __xsan {
void OnAcquire(const void *ctx, uptr addr) {
  const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
  __tsan::Acquire(thr, pc, addr);
}

void OnDirAcquire(const void *ctx, const char *path) {
  OnAcquire(ctx, __tsan::Dir2addr(path));
}

void OnRelease(const void *ctx, uptr addr) {
  const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
  __tsan::Release(thr, pc, addr);
}

void OnFdAcquire(const void *ctx, int fd) {
  const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
  __tsan::FdAcquire(thr, pc, fd);
}

void OnFdRelease(const void *ctx, int fd) {
  const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
  __tsan::FdRelease(thr, pc, fd);
}

void OnFdAccess(const void *ctx, int fd) {
  const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
  __tsan::FdAccess(thr, pc, fd);
}

void OnFdSocketAccept(const void *ctx, int fd, int newfd) {
  const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
  __tsan::FdSocketAccept(thr, pc, fd, newfd);
}

void OnFileOpen(const void *ctx, void *file, const char *path) {
  const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
  if (path) {
    __tsan::Acquire(thr, pc, __tsan::File2addr(path));
  }
  if (file) {
    int fd = fileno_unlocked(file);
    if (fd >= 0) {
      __tsan::FdFileCreate(thr, pc, fd);
    }
  }
}

void OnFileClose(const void *ctx, void *file) {
  if (file) {
    int fd = fileno_unlocked(file);
    const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
    auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
    __tsan::FdClose(thr, pc, fd);
  }
}

#if !SANITIZER_APPLE
void OnHandleRecvmsg(const void *ctx, __sanitizer_msghdr *msg) {
  const XsanInterceptorContext *ctx_ = (const XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_ctx.tsan_ctx_;
  __tsan::HandleRecvmsg(thr, pc, msg);
}
#endif

void AfterMmap(const XsanInterceptorContext& ctx, void *res, uptr size, int fd) {
  __tsan::AfterMmap(ctx, res, size, fd);
  __asan::AfterMmap(res, size, fd);
}

void BeforeMunmap(const XsanInterceptorContext &ctx, void *addr, uptr size) {
  /// Size too big can cause problems with the shadow memory.
  /// unmap on NULL is not allowed.
  if ((sptr)size < 0 || addr == nullptr)
    return;

  __tsan::BeforeMunmap(ctx, addr, size);
  __asan::BeforeMunmap(addr, size);
}
}  // namespace __xsan

// ---------- End of Synchronization and File-Related Hooks ----------------
