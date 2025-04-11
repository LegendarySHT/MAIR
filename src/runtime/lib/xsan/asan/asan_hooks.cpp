#include "asan_hooks.h"

#include "asan_poisoning.h"
#include "asan_thread.h"
#include "lsan/lsan_common.h"

namespace __asan {
void SetCommonFlags(CommonFlags &cf);
void ValidateFlags();
void SetAsanThreadName(const char *name);
}  // namespace __asan

namespace __asan {

void AsanHooks::OnPthreadCreate() {
  EnsureMainThreadIDIsCorrect();

  // Strict init-order checking is thread-hostile.
  if (__asan::flags()->strict_init_order)
    __asan::StopInitOrderChecking();
}

// ---------------------- Special Function Hooks -----------------
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
AsanHooks::ScopedAtExitHandler::ScopedAtExitHandler(uptr pc, const void *ctx) {
  __asan::StopInitOrderChecking();
}
AsanHooks::ScopedAtExitHandler::~ScopedAtExitHandler() {}
void AsanHooks::OnForkBefore() { __asan::OnForkBefore(); }
void AsanHooks::OnForkAfter(bool is_child) { __asan::OnForkAfter(); }
// ---------------------- Flags Registration Hooks ---------------
void AsanHooks::SetCommonFlags(CommonFlags &cf) { __asan::SetCommonFlags(cf); }
void AsanHooks::ValidateFlags() { __asan::ValidateFlags(); }
void AsanHooks::InitializeFlags() { __asan::InitializeFlags(); }
// ---------- Thread-Related Hooks --------------------------
void AsanHooks::SetThreadName(const char *name) {
  __asan::SetAsanThreadName(name);
}
// void AsanHooks::OnSetCurrentThread(__xsan::XsanThread *t) {
//   __asan::SetCurrentThread(t->asan_thread_);
// }
// void AsanHooks::OnThreadCreate(__xsan::XsanThread *xsan_thread,
//                                const void *start_data, uptr data_size,
//                                u32 parent_tid, StackTrace *stack,
//                                bool detached) {
//   // auto *asan_thread = __asan::AsanThread::Create(
//   //     /* start_data */ start_data, /* data_size */ data_size,
//   //     /* parent_tid */ parent_tid, /* stack */ stack, /* detached */
//   detached);
//   // xsan_thread->asan_thread_ = asan_thread;
//   // xsan_thread->tid_ = asan_thread->tid();
//                                }
// ---------- Synchronization and File-Related Hooks ------------------------
void AsanHooks::AfterMmap(const Context &ctx, void *res, uptr size, int fd) {
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

void AsanHooks::BeforeMunmap(const Context &ctx, void *addr, uptr size) {
  // We should not tag if munmap fail, but it's to late to tag after
  // real_munmap, as the pages could be mmaped by another thread.
  const uptr beg = reinterpret_cast<uptr>(addr);
  if (size && IsAligned(beg, GetPageSize())) {
    SIZE_T rounded_length = RoundUpTo(size, GetPageSize());
    // Protect from unmapping the shadow.
    if (__asan::AddrIsInMem(beg) &&
        __asan::AddrIsInMem(beg + rounded_length - 1))
      __asan::PoisonShadow(beg, rounded_length, 0);
  }
}

}  // namespace __asan
