#include "xsan_thread.h"

#include <pthread.h>
#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_internal_defs.h>
#include <sanitizer_common/sanitizer_placement_new.h>
#include <sanitizer_common/sanitizer_stackdepot.h>
#include <sanitizer_common/sanitizer_tls_get_addr.h>

#include "asan/asan_thread.h"
#include "tsan/tsan_rtl.h"
#include "xsan_common_defs.h"
#include "xsan_hooks.h"
#include "xsan_interceptors.h"
#include "xsan_internal.h"

namespace __xsan {

XsanThread *XsanThread::Create(thread_callback_t start_routine, void *arg,
                               u32 parent_tid, StackTrace *stack,
                               bool detached) {
  uptr PageSize = GetPageSizeCached();
  uptr size = RoundUpTo(sizeof(XsanThread), PageSize);
  XsanThread *thread = (XsanThread *)MmapOrDie(size, __func__);
  thread->is_inited_ = false;
  thread->in_ignored_lib_ = false;

  thread->start_routine_ = start_routine;
  thread->arg_ = arg;
  thread->destructor_iterations_ = GetPthreadDestructorIterations();
  thread->detached_ = detached;
  thread->is_main_thread_ =
      (start_routine == nullptr && parent_tid == kMainTid);

  auto *asan_thread = __asan::AsanThread::Create(
      /* start_routine */ start_routine, /* arg */ arg,
      /* parent_tid */ parent_tid, /* stack */ stack, /* detached */ detached);

  if (thread->is_main_thread_) {
    // Main thread.
    auto *tsan_thread = __tsan::cur_thread_init();
    __tsan::Processor *proc = __tsan::ProcCreate();
    __tsan::ProcWire(proc, tsan_thread);
    thread->tsan_tid_ = __tsan::ThreadCreate(nullptr, 0, 0, true);
    thread->tsan_thread_ = tsan_thread;
    tsan_thread->xsan_thread = thread;
  } else {
    // Other thread create for TSan is called in CreateTsanThread.
  }

  thread->asan_thread_ = asan_thread;
# if XSAN_CONTAINS_ASAN
  thread->tid_ = asan_thread->tid();
#else
  static u32 tid = 0;
  thread->tid_ = tid++;
# endif
  return thread;
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

uptr XsanThread::stack_top() { return GetStackBounds().top; }

uptr XsanThread::stack_bottom() { return GetStackBounds().bottom; }

uptr XsanThread::stack_size() {
  const auto bounds = GetStackBounds();
  return bounds.top - bounds.bottom;
}

void XsanThread::Init(const InitOptions *options) {
  next_stack_top_ = next_stack_bottom_ = 0;
  atomic_store(&stack_switching_, false, memory_order_release);
  CHECK_EQ(this->stack_size(), 0U);
  SetThreadStackAndTls(options);
  is_inited_ = true;
  int local = 0;
  VReport(1, "T%d: stack [%p,%p) size 0x%zx; local=%p\n", tid(),
          (void *)stack_bottom_, (void *)stack_top_, stack_top_ - stack_bottom_,
          (void *)&local);
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
  this->tsan_thread_->DestroyThreadState();
  // Common resource, must be managed by the XSan
  this->asan_thread_->Destroy();
  UnmapOrDie(this, size);
  DTLS_Destroy();
}

// Fuchsia doesn't use ThreadStart.
// xsan_fuchsia.c definies CreateMainThread and SetThreadStackAndTls.
#if !SANITIZER_FUCHSIA

Tid XsanThread::PostCreateTsanThread(uptr pc, uptr uid) {
  /// TODO: merge ASan's ThreadContext and TSan's ThreadContext.
  Tid tsan_tid = __tsan::ThreadCreate(__tsan::cur_thread_init(), pc, uid,
                                      IsStateDetached(detached_));
  CHECK_NE(tsan_tid, kMainTid);
  tsan_tid_ = tsan_tid;

  return tsan_tid;
}

void XsanThread::AsanBeforeThreadStart(tid_t os_id) {
  asan_thread_->BeforeThreadStart(os_id);
}

void XsanThread::TsanBeforeThreadStart(tid_t os_id) {
  __tsan::ThreadState *thr = tsan_thread_;
  if (isMainThread()) {
    __tsan::ThreadStart(thr, tsan_tid_, os_id, ThreadType::Regular);
  } else {
    __tsan::Processor *proc = __tsan::ProcCreate();
    __tsan::ProcWire(proc, thr);
    __tsan::ThreadStart(thr, tsan_tid_, os_id, ThreadType::Regular);
  }
}

thread_return_t XsanThread::ThreadStart(tid_t os_id, Semaphore *created, Semaphore *started) {
  // XSanThread doesn't have a registry.
  // xsanThreadRegistry().StartThread(tid(), os_id, ThreadType::Regular,
  // nullptr);

  /// TODO: should TSan care these heap blocks allocated by ASan?
  /// TODO: unify ASan's and TSan's thread context as XSan's thread context.
  {
    // Thread-local state is not initialized yet.
    __xsan::ScopedIgnoreInterceptors ignore;
    if (created) created->Wait();

    TsanBeforeThreadStart(os_id);
    AsanBeforeThreadStart(os_id);

    Init();
    if (started) started->Post();

    /// Now only ASan uses this, so let's consider it as ASan's exclusive
    /// resource.
    // if (common_flags()->use_sigaltstack) SetAlternateSignalStack();

    if (!start_routine_) {
      // start_routine_ == 0 if we're on the main thread or on one of the
      // OS X libdispatch worker threads. But nobody is supposed to call
      // ThreadStart() for the worker threads.
      CHECK_EQ(tid(), 0);
      return 0;
    }
  }

  thread_return_t res = start_routine_(arg_);

  {
    __xsan::ScopedIgnoreInterceptors ignore;
    asan_thread_->AfterThreadStart();
    // On POSIX systems we defer this to the TSD destructor. LSan will consider
    // the thread's memory as non-live from the moment we call Destroy(), even
    // though that memory might contain pointers to heap objects which will be
    // cleaned up by a user-defined TSD destructor. Thus, calling Destroy() before
    // the TSD destructors have run might cause false positives in LSan.
    if (!SANITIZER_POSIX)
      this->Destroy();
  }

  return res;
}

XsanThread *CreateMainThread() {
  XsanThread *main_thread = XsanThread::Create(
      /* start_routine */ nullptr, /* arg */ nullptr, /* parent_tid */ kMainTid,
      /* stack */ nullptr, /* detached */ true);

  SetCurrentThread(main_thread);

  return main_thread;
}

void InitializeMainThread() {
  XsanThread *main_thread = CreateMainThread();
  main_thread->ThreadStart(internal_getpid());
  CHECK_EQ(kMainTid, main_thread->tid());
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

uptr XsanThread::GetStackVariableShadowStart(uptr addr) {
  return asan_thread_->GetStackVariableShadowStart(addr);
}

bool XsanThread::AddrIsInRealStack(uptr addr) {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}

bool XsanThread::AddrIsInFakeStack(uptr addr) {
  __asan::FakeStack *fake_stack = this->asan_thread_->get_fake_stack();
  if (fake_stack) {
    return fake_stack->AddrIsInFakeStack((uptr)addr);
  }
  return false;
}

bool XsanThread::AddrIsInStack(uptr addr) {
  return AddrIsInRealStack(addr) || AddrIsInFakeStack(addr);
}

bool XsanThread::AddrIsInTls(uptr addr) {
  return tls_begin() <= addr && addr < tls_end();
}

static pthread_key_t tsd_key;
static bool tsd_key_inited = false;

void XsanTSDInit(void (*destructor)(void *tsd)) {
  CHECK(!tsd_key_inited);
  tsd_key_inited = true;
  CHECK_EQ(0, pthread_key_create(&tsd_key, destructor));
}

static THREADLOCAL XsanThread *xsan_current_thread;

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
    xsan_current_thread->tsan_thread_ = __tsan::cur_thread();
  }
  return xsan_current_thread; 
}

void SetCurrentThread(XsanThread *t) {
  __asan::SetCurrentThread(t->asan_thread_);
  auto *tsan_thread = __tsan::SetCurrentThread();
  t->tsan_thread_ = tsan_thread;
  tsan_thread->xsan_thread = t;

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
