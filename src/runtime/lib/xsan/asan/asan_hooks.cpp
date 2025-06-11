#include "asan_hooks.h"

#include "asan_interface_internal.h"
#include "asan_thread.h"
#include "lsan/lsan_common.h"

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

// ---------- Thread-Related Hooks --------------------------
auto AsanHooks::CreateMainThread() -> Thread {
  Thread thread;
  thread.asan_thread = AsanThread::Create(
      /* start_data */ nullptr, /* data_size */ 0,
      /* parent_tid */ kMainTid, /* stack */ nullptr, /* detached */ true);
  return thread;
}

auto AsanHooks::CreateThread(u32 parent_tid, uptr child_uid, StackTrace *stack,
                             const void *data, uptr data_size, bool detached)
    -> Thread {
  Thread thread;
  thread.asan_thread = AsanThread::Create(
      /* start_data */ data, /* data_size */ data_size,
      /* parent_tid */ parent_tid, /* stack */ stack, /* detached */ detached);
  return thread;
}

void AsanHooks::ChildThreadInit(Thread &thread, tid_t os_id) {
  SetCurrentThread(thread.asan_thread);
}

void AsanHooks::ChildThreadStart(Thread &thread, tid_t os_id) {
  thread.asan_thread->BeforeThreadStart(os_id);
}

void AsanHooks::DestroyThreadReal(Thread &thread) {
  thread.asan_thread->Destroy();
}

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

#define ASAN_INTERFACE_HOOK(size, operation, asan_operation) \
  template <>                                                \
  void AsanHooks::__xsan_##operation<size>(uptr p) {         \
    __asan_##asan_operation##size(p);                        \
  }

ASAN_INTERFACE_HOOK(2, unaligned_read, unaligned_load)
ASAN_INTERFACE_HOOK(4, unaligned_read, unaligned_load)
ASAN_INTERFACE_HOOK(8, unaligned_read, unaligned_load)

ASAN_INTERFACE_HOOK(2, unaligned_write, unaligned_store)
ASAN_INTERFACE_HOOK(4, unaligned_write, unaligned_store)
ASAN_INTERFACE_HOOK(8, unaligned_write, unaligned_store)

ASAN_INTERFACE_HOOK(1, read, load)
ASAN_INTERFACE_HOOK(2, read, load)
ASAN_INTERFACE_HOOK(4, read, load)
ASAN_INTERFACE_HOOK(8, read, load)
ASAN_INTERFACE_HOOK(16, read, load)

ASAN_INTERFACE_HOOK(1, write, store)
ASAN_INTERFACE_HOOK(2, write, store)
ASAN_INTERFACE_HOOK(4, write, store)
ASAN_INTERFACE_HOOK(8, write, store)
ASAN_INTERFACE_HOOK(16, write, store)

#undef ASAN_INTERFACE_HOOK

}  // namespace __asan
