//===-- xsan_hooks.cpp ------------------------------------------*- C++
//-*-===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is used to manuplate the hooks for coordination between multiple
// sanitizers.
//===----------------------------------------------------------------------===//

#include "xsan_hooks.h"

#include <sanitizer_common/sanitizer_common.h>

#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_thread.h"

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

}  // namespace __xsan

// ---------- End of Memory Management Hooks -------------------

// ---------------------- Special Function Hooks -----------------
namespace __asan {
/// ASan 1) checks the correctness of main thread ID, 2) checks the init orders.
void OnPthreadCreate();
}  // namespace __asan

namespace __tsan {
/// TSan may spawn a background thread to recycle resource in pthread_create.
/// What's more, TSan does not support starting new threads after multi-threaded
/// fork.
void OnPthreadCreate();
}  // namespace __tsan

namespace __xsan {
void OnPthreadCreate() {
  __asan::OnPthreadCreate();
  __tsan::OnPthreadCreate();
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

void AfterMmap(void *ctx, void *res, uptr size, int fd) {
  XsanInterceptorContext *ctx_ = (XsanInterceptorContext *)ctx;
  auto [thr, pc] = ctx_->xsan_thr->getTsanArgs();
  if (fd > 0)
    OnFdAccess(ctx, fd);
  __tsan::MemoryRangeImitateWriteOrResetRange(thr, pc, (uptr)res, size);
}

}  // namespace __xsan

// ---------- End of Synchronization and File-Related Hooks ----------------

// ---------- Ignoration Hooks -----------------------------------------------
/*
 The sub-sanitizers implement the following ignore predicates to ignore
  - Interceptors
  - Allocation/Free Hooks
 which are shared by all sub-sanitizers.
 */
namespace __tsan {
SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool ShouldIgnoreInterceptors(ThreadState *thr) { return false; }
SANITIZER_WEAK_CXX_DEFAULT_IMPL
bool ShouldIgnoreAllocFreeHook() { return false; }
}  // namespace __tsan

namespace __xsan {
bool ShouldSanitzerIgnoreInterceptors(XsanThread *xsan_thr) {
  /// TODO: to support libignore, we plan to migrate it to Xsan.
  /// xsan_suppressions.cpp is required accordingly.
  return __tsan::ShouldIgnoreInterceptors(xsan_thr->tsan_thread_);
}

bool ShouldSanitzerIgnoreAllocFreeHook() {
  return __tsan::ShouldIgnoreAllocFreeHook();
}
}  // namespace __xsan

// ---------- End of Ignoration Hooks -------------------------------