#include "xsan_thread.h"

#include <pthread.h>

#include "asan/asan_thread.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_thread_arg_retval.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "xsan_common_defs.h"
#include "xsan_hooks_dispatch.h"
#include "xsan_interceptors.h"
#include "xsan_internal.h"

namespace __xsan {

static pthread_key_t tsd_key;
static bool tsd_key_inited = false;

THREADLOCAL XsanThread *xsan_current_thread;

static ThreadArgRetval *thread_data;

static void InitThreads() {
  static bool initialized;
  // Don't worry about thread_safety - this should be called when there is
  // a single thread.
  if (LIKELY(initialized))
    return;
  // Never reuse ASan threads: we store pointer to AsanThreadContext
  // in TSD and can't reliably tell when no more TSD destructors will
  // be called. It would be wrong to reuse AsanThreadContext for another
  // thread before all TSD destructors will be called for it.

  // MIPS requires aligned address
  alignas(alignof(ThreadArgRetval)) static char
      thread_data_placeholder[sizeof(ThreadArgRetval)];

  thread_data = new (thread_data_placeholder) ThreadArgRetval();
  initialized = true;
}

ThreadArgRetval &xsanThreadArgRetval() {
  InitThreads();
  return *thread_data;
}

XsanThread *XsanThread::Create(const void *start_data, uptr data_size,
                               u32 parent_tid, uptr child_uid,
                               StackTrace *stack, bool detached) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(XsanThread), PageSize);
  XsanThread *thread = (XsanThread *)MmapOrDie(size, __func__);

  thread->is_inited_ = false;
  thread->in_ignored_lib_ = false;

  if (data_size) {
    uptr availible_size = (uptr)thread + size - (uptr)(thread->start_data_);
    CHECK_LE(data_size, availible_size);
    internal_memcpy(thread->start_data_, start_data, data_size);
  }

  thread->destructor_iterations_ = GetPthreadDestructorIterations();
  thread->is_main_thread_ = (data_size == 0 && parent_tid == kMainTid);

  /// Create sub-sanitizers' thread data.
  if (UNLIKELY(thread->is_main_thread_))
    thread->CreateMainThread();
  else
    thread->CreateThread(start_data, data_size, parent_tid, child_uid, stack,
                         detached);

  static u32 tid = kMainTid;
  thread->tid_ = tid++;
  return thread;
}

void XsanThread::GetStartData(void *out, uptr out_size) const {
  internal_memcpy(out, start_data_, out_size);
}

inline XsanThread::StackBounds XsanThread::GetStackBounds() const {
  if (!atomic_load(&stack_switching_, memory_order_acquire)) {
    // Make sure the stack bounds are fully initialized.
    if (stack_bottom_ >= stack_top_)
      return {0, 0};
    return {stack_bottom_, stack_top_};
  }
  char local;
  const uptr cur_stack = (uptr)&local;
  // Note: need to check next stack first, because FinishSwitchFiber
  // may be in process of overwriting stack_top_/bottom_. But in such case
  // we are already on the next stack.
  if (cur_stack >= next_stack_bottom_ && cur_stack < next_stack_top_)
    return {next_stack_bottom_, next_stack_top_};
  return {stack_bottom_, stack_top_};
}

uptr XsanThread::stack_top() const { return GetStackBounds().top; }

uptr XsanThread::stack_bottom() const { return GetStackBounds().bottom; }

uptr XsanThread::stack_size() const {
  const auto bounds = GetStackBounds();
  return bounds.top - bounds.bottom;
}

ALWAYS_INLINE void XsanThread::DestroyThread() {
  XSAN_HOOKS_EXEC(DestroyThread, *this);
  // Since XSan use ASan's resource, ASan should be manually destroyed by XSan
  // in the last. So `DestroyThread` is left empty and provide
  // `DestroyThreadReal` for XSan to call
  // static void DestroyThread(Thread &thread);
  XsanHooksSanitizerImpl<XsanHooksSanitizer::Asan>::Hooks::DestroyThreadReal(
      *this);
}

void XsanThread::TSDDtor(void *tsd) {
  XsanThread *t = (XsanThread *)tsd;
  t->Destroy();
}

void XsanThread::Destroy() {
  /// TODO: now use ASanThread to manage malloc
  // malloc_storage().CommitBack();

  if (XsanThread *thread = GetCurrentThread())
    CHECK_EQ(this, thread);

  /// Now only ASan uses this, so let's consider it as ASan's exclusive
  /// resource.
  // if (common_flags()->use_sigaltstack)
  //   UnsetAlternateSignalStack();

  uptr size = RoundUpTo(sizeof(XsanThread), GetPageSizeCached());

  // Destroy sub-sanitizers' thread data.
  this->DestroyThread();

  UnmapOrDie(this, size);
  /// TODO: ASan destroy DTLS only if was_running == true
  // if (was_running)
  DTLS_Destroy();
}

ALWAYS_INLINE void XsanThread::ChildThreadInit() {
  XSAN_HOOKS_EXEC(ChildThreadInit, *this, os_id_);
}

ALWAYS_INLINE void XsanThread::ChildThreadStart() {
  // Tsan should be ready to process any possible thread-related events even
  // happened when other sub-sanitizers are starting, such as ASan calls
  // `pthread_getattr_np` -> `realloc` -> `__sanitizer_malloc_hook`, where
  // user may trigger thread-related events. So we need to make TSan start
  // first.
  XsanHooksSanitizerImpl<XsanHooksSanitizer::Tsan>::Hooks::ChildThreadStartReal(
      *this, os_id_);
  XSAN_HOOKS_EXEC(ChildThreadStart, *this, os_id_);
}

void XsanThread::ThreadInit(tid_t os_id) {
  os_id_ = os_id;

  // Make sure we do not reset the current XsanThread.
  CHECK_EQ(0, xsan_current_thread);
  xsan_current_thread = this;
  // Make sure that XsanTSDDtor gets called at the end.
  CHECK(tsd_key_inited);
  pthread_setspecific(tsd_key, (void *)this);

  // Set current thread for each sub-sanitizer.
  this->ChildThreadInit();
}

// Fuchsia doesn't use ThreadStart.
// xsan_fuchsia.c definies CreateMainThread and SetThreadStackAndTls.
#if !SANITIZER_FUCHSIA

ALWAYS_INLINE void XsanThread::CreateMainThread() {
  XSAN_HOOKS_ASSIGN_VAR(CreateMainThread);
};

ALWAYS_INLINE void XsanThread::CreateThread(const void *start_data,
                                            uptr data_size, u32 parent_tid,
                                            uptr child_uid, StackTrace *stack,
                                            bool detached) {
  XSAN_HOOKS_ASSIGN_VAR(CreateThread, parent_tid, child_uid, stack, start_data,
                        data_size, detached);
}

void XsanThread::ThreadStart() {
  // XSanThread doesn't have a registry.
  // xsanThreadRegistry().StartThread(tid(), os_id, ThreadType::Regular,
  // nullptr);

  /// TODO: should TSan care these heap blocks allocated by ASan?
  /// TODO: unify ASan's and TSan's thread context as XSan's thread context.
  {
    // Thread-local state is not initialized yet.
    __xsan::ScopedIgnoreInterceptors ignore;

    /// Initialize sub-sanitizers' thread data in new thread and before the real
    /// callback execution.
    this->ChildThreadStart();

    next_stack_top_ = next_stack_bottom_ = 0;
    atomic_store(&stack_switching_, false, memory_order_release);
    CHECK_EQ(this->stack_size(), 0U);
    SetThreadStackAndTls(nullptr);
    is_inited_ = true;
    int local = 0;
    VReport(1, "T%d: stack [%p,%p) size 0x%zx; local=%p\n", tid(),
            (void *)stack_bottom_, (void *)stack_top_,
            stack_top_ - stack_bottom_, (void *)&local);

    /// Now only ASan uses this, so let's consider it as ASan's exclusive
    /// resource.
    // if (common_flags()->use_sigaltstack) SetAlternateSignalStack();
  }
}

void InitializeMainThread() {
  XsanThread *main_thread = XsanThread::Create(
      /* parent_tid */ kMainTid,
      /* stack */ nullptr, /* detached */ true);
  main_thread->ThreadInit(internal_getpid());
  main_thread->ThreadStart();
  CHECK_EQ(kMainTid, main_thread->tid());
}

// This implementation doesn't use the argument, which is just passed down
// from the caller of Init (which see, above).  It's only there to support
// OS-specific implementations that need more information passed through.
void XsanThread::SetThreadStackAndTls(const InitOptions *options) {
  DCHECK_EQ(options, nullptr);
  GetThreadStackAndTls(tid() == kMainTid, &stack_bottom_, &stack_top_,
                       &tls_begin_, &tls_end_);
  /// TODO: use a more generic way to get the range of stack
  stack_top_ = RoundDownTo(stack_top_, ASAN_SHADOW_GRANULARITY);
  stack_bottom_ = RoundDownTo(stack_bottom_, ASAN_SHADOW_GRANULARITY);
  dtls_ = DTLS_Get();

  if (stack_top_ != stack_bottom_) {
    int local;
    CHECK(AddrIsInStack((uptr)&local));
  }
}

#endif  // !SANITIZER_FUCHSIA

bool XsanThread::AddrIsInRealStack(uptr addr) const {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}

bool XsanThread::AddrIsInFakeStack(uptr addr) const {
  __asan::FakeStack *fake_stack = asan.asan_thread->get_fake_stack();
  if (fake_stack) {
    return fake_stack->AddrIsInFakeStack((uptr)addr);
  }
  return false;
}

bool XsanThread::AddrIsInStack(uptr addr) const {
  return AddrIsInRealStack(addr) || AddrIsInFakeStack(addr);
}

bool XsanThread::AddrIsInTls(uptr addr) const {
  return tls_begin() <= addr && addr < tls_end();
}

void XsanTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, destructor));
}

void XsanTSDDtor(void *tsd) {
  XsanThread *t = (XsanThread *)tsd;
  if (t->destructor_iterations_ > 1) {
    t->destructor_iterations_--;
    CHECK_EQ(0, pthread_setspecific(tsd_key, tsd));
    return;
  }
  xsan_current_thread = nullptr;
  // Make sure that signal handler can not see a stale current thread pointer.
  atomic_signal_fence(memory_order_seq_cst);
  XsanThread::TSDDtor(tsd);
}

XsanThread *GetCurrentThread() {
  /// As the current tsan_thread_ might change as the fiber switch, we need to
  /// get the current thread from the current fiber.
  if (xsan_current_thread) {
    xsan_current_thread->tsan.tsan_thread = __tsan::cur_thread();
  }
  return xsan_current_thread;
}

u32 GetCurrentTidOrInvalid() {
  XsanThread *t = GetCurrentThread();
  return t ? t->tid() : kInvalidTid;
}

XsanThread *FindThreadByStackAddress(uptr addr) { UNIMPLEMENTED(); }

/// Seems this function is not useful for XSan.
/// ASan use threadContextRegistry to map os_id to AsanThreadContext, and futher
/// map AsanThreadContext to AsanThread.
/// XSan does not use such a registry. And the sole purpose of this function is
/// used in LSan, which has been already integrated into ASan.
__xsan::XsanThread *GetXsanThreadByOsIDLocked(tid_t os_id) {
  /// TODO: If migrate LSan to XSan, we need to implement this function.
  UNIMPLEMENTED();
}

}  // namespace __xsan

// --- Implementation of LSan-specific functions --- {{{1
namespace __lsan {
/// TODO: If migrate LSan to XSan, we need to implement this part.

// bool GetThreadRangesLocked(tid_t os_id, uptr *stack_begin, uptr *stack_end,
//                            uptr *tls_begin, uptr *tls_end, uptr *cache_begin,
//                            uptr *cache_end, DTLS **dtls) {
//   __xsan::XsanThread *t = __xsan::GetXsanThreadByOsIDLocked(os_id);
//   if (!t) return false;
//   *stack_begin = t->stack_bottom();
//   *stack_end = t->stack_top();
//   *tls_begin = t->tls_begin();
//   *tls_end = t->tls_end();
//   // ASan doesn't keep allocator caches in TLS, so these are unused.
//   *cache_begin = 0;
//   *cache_end = 0;
//   *dtls = t->dtls();
//   return true;
// }

// void GetAllThreadAllocatorCachesLocked(InternalMmapVector<uptr> *caches) {}

// void ForEachExtraStackRange(tid_t os_id, RangeIteratorCallback callback,
//                             void *arg) {
//   __xsan::XsanThread *t = __xsan::GetXsanThreadByOsIDLocked(os_id);
//   if (!t)
//     return;
//   __xsan::FakeStack *fake_stack = t->get_fake_stack();
//   if (!fake_stack)
//     return;
//   fake_stack->ForEachFakeFrame(callback, arg);
// }

// void LockThreadRegistry() {
//   __xsan::xsanThreadRegistry().Lock();
// }

// void UnlockThreadRegistry() {
//   __xsan::xsanThreadRegistry().Unlock();
// }

// ThreadRegistry *GetThreadRegistryLocked() {
//   __xsan::xsanThreadRegistry().CheckLocked();
//   return &__xsan::xsanThreadRegistry();
// }

// void EnsureMainThreadIDIsCorrect() {
//   __xsan::EnsureMainThreadIDIsCorrect();
// }
}  // namespace __lsan

// ---------------------- Interface ---------------- {{{1
using namespace __xsan;

extern "C" {}
