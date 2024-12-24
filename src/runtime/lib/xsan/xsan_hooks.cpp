//===-- xsan_hooks.cpp ---------------------------------------------------===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is used to manuplate the hooks for coordination between multiple
// sanitizers.
//===----------------------------------------------------------------------===//

#include "xsan_hooks.h"

#include <sanitizer_common/sanitizer_common.h>

#include "lsan/lsan_common.h"
#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_thread.h"

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

bool ShouldSanitzerIgnoreInterceptors(XsanThread *xsan_thr) {
  /// Avoid sanity checks in XSan internal.
  if (IsInXsanInternal()) {
    return true;
  }
  /// TODO: to support libignore, we plan to migrate it to Xsan.
  /// xsan_suppressions.cpp is required accordingly.
  if (xsan_thr == nullptr) {
    return __tsan::ShouldIgnoreInterceptors();
  } else {
    return __tsan::ShouldIgnoreInterceptors(xsan_thr->tsan_thread_);
  }
}

bool ShouldSanitzerIgnoreAllocFreeHook() {
  return __tsan::ShouldIgnoreAllocFreeHook();
}

int get_exit_code(void *ctx) {
  int exit_code = 0;
  if (CAN_SANITIZE_LEAKS && common_flags()->detect_leaks &&
      __lsan::HasReportedLeaks()) {
    return common_flags()->exitcode;
  }

  auto *tsan_thr =
      ctx == nullptr ? __tsan::cur_thread()
                     : ((XsanInterceptorContext *)ctx)->xsan_thr->tsan_thread_;
  exit_code = __tsan::Finalize(tsan_thr);
  return exit_code;
}

}  // namespace __xsan

// ---------- End of State Management Hooks -----------------

// ---------------------- Memory Management Hooks -------------------
/// As XSan uses ASan's heap allocator and fake stack directly, hence we don't
/// need to invoke ASan's hooks here.
namespace __tsan {

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
bool GetASanMellocStackTrace(u32 &stack_trace_id, uptr addr, bool set_stack_trace_id){return false;}

}// namespace __asan

namespace __xsan {
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

bool GetMellocStackTrace(u32 &stack_trace_id, uptr addr, bool set_stack_trace_id){
return __asan::GetASanMellocStackTrace(stack_trace_id, addr, set_stack_trace_id);
}

}  // namespace __xsan

// ---------- End of Memory Management Hooks -------------------

// ---------------------- Special Function Hooks -----------------
extern "C" {
void *__asan_extra_spill_area();
void __asan_handle_vfork(void *sp);
}
namespace __asan {
/// ASan 1) checks the correctness of main thread ID, 2) checks the init orders.
void OnPthreadCreate();
void __asan_handle_no_return();
void StopInitOrderChecking();
}  // namespace __asan

namespace __tsan {
/// TSan may spawn a background thread to recycle resource in pthread_create.
/// What's more, TSan does not support starting new threads after multi-threaded
/// fork.
void OnPthreadCreate();

void Release(ThreadState *thr, uptr pc, uptr addr);

void ThreadIgnoreBegin(ThreadState *thr, uptr pc);
void ThreadIgnoreEnd(ThreadState *thr);

void DisableTsanForVfork();
void RecoverTsanAfterVforkParent();

LibIgnore *libignore();
void handle_longjmp(void *env, const char *fname, uptr caller_pc);
}  // namespace __tsan

namespace __xsan {
/*
Provides hooks for special functions, such as
  - pthread_create
  - atexit / on_exit / _cxa_atexit
  - longjmp / siglongjmp / _longjmp / _siglongjmp
  - dlopen / dlclose
  - vfork
*/

void OnPthreadCreate() {
  __asan::OnPthreadCreate();
  __tsan::OnPthreadCreate();
}

ScopedAtExitWrapper::ScopedAtExitWrapper(uptr pc, void *ctx) {
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

ScopedAtExitHandler::ScopedAtExitHandler(uptr pc, void *ctx) {
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

void OnLibraryLoaded(const char *filename, void *handle) {
  __tsan::libignore()->OnLibraryLoaded(filename);
}

void OnLibraryUnloaded() {
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
}  // namespace __tsan

namespace __xsan {
void OnAcquire(void *ctx, uptr addr) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  __tsan::Acquire(thr, pc, addr);
}

void OnDirAcquire(void *ctx, const char *path) {
  OnAcquire(ctx, __tsan::Dir2addr(path));
}

void OnRelease(void *ctx, uptr addr) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  __tsan::Release(thr, pc, addr);
}

void OnFdAcquire(void *ctx, int fd) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  __tsan::FdAcquire(thr, pc, fd);
}

void OnFdRelease(void *ctx, int fd) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  __tsan::FdRelease(thr, pc, fd);
}

void OnFdAccess(void *ctx, int fd) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  __tsan::FdAccess(thr, pc, fd);
}

void OnFdSocketAccept(void *ctx, int fd, int newfd) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  __tsan::FdSocketAccept(thr, pc, fd, newfd);
}

void OnFileOpen(void *ctx, void *file, const char *path) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
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

void OnFileClose(void *ctx, void *file) {
  if (file) {
    int fd = fileno_unlocked(file);
    XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
    auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
    __tsan::FdClose(thr, pc, fd);
  }
}

#if !SANITIZER_APPLE
void OnHandleRecvmsg(void *ctx, __sanitizer_msghdr *msg) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  __tsan::HandleRecvmsg(thr, pc, msg);
}
#endif

void AfterMmap(void *ctx, void *res, uptr size, int fd) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  if (fd > 0)
    OnFdAccess(ctx, fd);
  __tsan::MemoryRangeImitateWriteOrResetRange(thr, pc, (uptr)res, size);
}

}  // namespace __xsan

// ---------- End of Synchronization and File-Related Hooks ----------------
