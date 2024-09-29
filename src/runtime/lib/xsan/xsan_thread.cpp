#include "asan/orig/asan_internal.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "xsan_allocator.h"
#include "xsan_interceptors.h"
#include "xsan_internal.h"
#include "xsan_thread.h"
#include "xsan_stack.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "lsan/lsan_common.h"
#include <pthread.h>

namespace __xsan {

XsanThread *XsanThread::Create(thread_callback_t start_routine, void *arg,
                               u32 parent_tid, StackTrace *stack,
                               bool detached) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(XsanThread), PageSize);
  XsanThread *thread = (XsanThread*)MmapOrDie(size, __func__);
  thread->start_routine_ = start_routine;
  thread->arg_ = arg;
  thread->destructor_iterations_ = GetPthreadDestructorIterations();
  
  return thread;
}


inline XsanThread::StackBounds XsanThread::GetStackBounds() const {
  if (!atomic_load(&stack_switching_, memory_order_acquire)) {
    // Make sure the stack bounds are fully initialized.
    if (stack_bottom_ >= stack_top_) return {0, 0};
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

uptr XsanThread::stack_top() {
  return GetStackBounds().top;
}

uptr XsanThread::stack_bottom() {
  return GetStackBounds().bottom;
}

uptr XsanThread::stack_size() {
  const auto bounds = GetStackBounds();
  return bounds.top - bounds.bottom;
}


void XsanThread::Init(const InitOptions *options) {
  next_stack_top_ = next_stack_bottom_ = 0;
  atomic_store(&stack_switching_, false, memory_order_release);
  CHECK_EQ(this->stack_size(), 0U);
  SetThreadStackAndTls(options);
  ClearShadowForThreadStackAndTLS();
  int local = 0;
  VReport(1, "T%d: stack [%p,%p) size 0x%zx; local=%p\n", tid(),
          (void *)stack_bottom_, (void *)stack_top_, stack_top_ - stack_bottom_,
          (void *)&local);
}

void XsanThread::TSDDtor(void *tsd) {
  XsanThread *t = (XsanThread*)tsd;
  t->Destroy();
}

void XsanThread::Destroy() {
  int tid = this->tid();
  VReport(1, "T%d exited\n", tid);

  /// TODO: now use ASanThread to manage malloc
  // malloc_storage().CommitBack();

 
  if (XsanThread *thread = GetCurrentThread())
    CHECK_EQ(this, thread);
  if (common_flags()->use_sigaltstack)
    UnsetAlternateSignalStack();

  // We also clear the shadow on thread destruction because
  // some code may still be executing in later TSD destructors
  // and we don't want it to have any poisoned stack.
  ClearShadowForThreadStackAndTLS();

  uptr size = RoundUpTo(sizeof(XsanThread), GetPageSizeCached());
  UnmapOrDie(this, size);

  DTLS_Destroy();
}

// Fuchsia doesn't use ThreadStart.
// xsan_fuchsia.c definies CreateMainThread and SetThreadStackAndTls.
#if !SANITIZER_FUCHSIA

thread_return_t XsanThread::ThreadStart(tid_t os_id) {
  Init();
  // XSanThread doesn't have a registry.
  // xsanThreadRegistry().StartThread(tid(), os_id, ThreadType::Regular, nullptr);

  if (common_flags()->use_sigaltstack) SetAlternateSignalStack();

  if (!start_routine_) {
    // start_routine_ == 0 if we're on the main thread or on one of the
    // OS X libdispatch worker threads. But nobody is supposed to call
    // ThreadStart() for the worker threads.
    CHECK_EQ(tid(), 0);
    return 0;
  }

  thread_return_t res = start_routine_(arg_);

  // On POSIX systems we defer this to the TSD destructor. LSan will consider
  // the thread's memory as non-live from the moment we call Destroy(), even
  // though that memory might contain pointers to heap objects which will be
  // cleaned up by a user-defined TSD destructor. Thus, calling Destroy() before
  // the TSD destructors have run might cause false positives in LSan.
  if (!SANITIZER_POSIX)
    this->Destroy();

  return res;
}

XsanThread *CreateMainThread() {
  XsanThread *main_thread = XsanThread::Create(
      /* start_routine */ nullptr, /* arg */ nullptr, /* parent_tid */ kMainTid,
      /* stack */ nullptr, /* detached */ true);
  
  /// TODO: add TSan thread support.
  // auto *asan_thread = __asan::CreateMainThread();
  // main_thread->asan_thread_ = asan_thread;

  SetCurrentThread(main_thread);
  main_thread->ThreadStart(internal_getpid());
  return main_thread;
}

void InitializeMainThread() {
  XsanThread *main_thread = CreateMainThread();
  CHECK_EQ(0, main_thread->tid());
}

// This implementation doesn't use the argument, which is just passed down
// from the caller of Init (which see, above).  It's only there to support
// OS-specific implementations that need more information passed through.
void XsanThread::SetThreadStackAndTls(const InitOptions *options) {
  DCHECK_EQ(options, nullptr);
  uptr tls_size = 0;
  uptr stack_size = 0;
  GetThreadStackAndTls(tid() == kMainTid, &stack_bottom_, &stack_size,
                       &tls_begin_, &tls_size);
  stack_top_ = RoundDownTo(stack_bottom_ + stack_size, ASAN_SHADOW_GRANULARITY);
  tls_end_ = tls_begin_ + tls_size;
  dtls_ = DTLS_Get();

  if (stack_top_ != stack_bottom_) {
    int local;
    CHECK(AddrIsInStack((uptr)&local));
  }
}

#endif  // !SANITIZER_FUCHSIA

void XsanThread::ClearShadowForThreadStackAndTLS() {
  asan_thread_->ClearShadowForThreadStackAndTLS();
}

bool XsanThread::GetStackFrameAccessByAddr(uptr addr,
                                           StackFrameAccess *access) {
  return asan_thread_->GetStackFrameAccessByAddr(addr, access);
}

uptr XsanThread::GetStackVariableShadowStart(uptr addr) {
  return asan_thread_->GetStackVariableShadowStart(addr);
}

bool XsanThread::AddrIsInStack(uptr addr) {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}


static pthread_key_t tsd_key;
static bool tsd_key_inited = false;

void XsanTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, destructor));
}


static THREADLOCAL XsanThread* xsan_current_thread;

void XsanTSDDtor(void *tsd) {
  XsanThread *t = (XsanThread*)tsd;
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
  return xsan_current_thread;
}

void SetCurrentThread(XsanThread *t) {
  // Make sure we do not reset the current XsanThread.
  CHECK_EQ(0, xsan_current_thread);
  xsan_current_thread = t;
  // Make sure that XsanTSDDtor gets called at the end.
  CHECK(tsd_key_inited);
  pthread_setspecific(tsd_key, (void *)t);
}

u32 GetCurrentTidOrInvalid() {
  XsanThread *t = GetCurrentThread();
  return t ? t->tid() : kInvalidTid;
}

XsanThread *FindThreadByStackAddress(uptr addr) {
  UNIMPLEMENTED();
}

void EnsureMainThreadIDIsCorrect() {

}

__xsan::XsanThread *GetXsanThreadByOsIDLocked(tid_t os_id) {
  /// TODO: If migrate LSan to XSan, we need to implement this function.
  UNIMPLEMENTED();
}
} // namespace __xsan

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
} // namespace __lsan

// ---------------------- Interface ---------------- {{{1
using namespace __xsan;

extern "C" {

}
