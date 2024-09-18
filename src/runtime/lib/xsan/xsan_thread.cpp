//===-- xsan_thread.cpp ---------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Thread-related code.
//===----------------------------------------------------------------------===//
#include "xsan_allocator.h"
#include "xsan_interceptors.h"
#include "xsan_stack.h"
#include "xsan_thread.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_placement_new.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"
#include "lsan/lsan_common.h"

namespace __xsan {


static Mutex mu_for_thread_context;
static LowLevelAllocator allocator_for_thread_context;


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

// We want to create the FakeStack lazily on the first use, but not earlier
// than the stack size is known and the procedure has to be async-signal safe.
FakeStack *XsanThread::AsyncSignalSafeLazyInitFakeStack() {
  uptr stack_size = this->stack_size();
  if (stack_size == 0)  // stack_size is not yet available, don't use FakeStack.
    return nullptr;
  uptr old_val = 0;
  // fake_stack_ has 3 states:
  // 0   -- not initialized
  // 1   -- being initialized
  // ptr -- initialized
  // This CAS checks if the state was 0 and if so changes it to state 1,
  // if that was successful, it initializes the pointer.
  if (atomic_compare_exchange_strong(
      reinterpret_cast<atomic_uintptr_t *>(&fake_stack_), &old_val, 1UL,
      memory_order_relaxed)) {
    uptr stack_size_log = Log2(RoundUpToPowerOfTwo(stack_size));
    CHECK_LE(flags()->min_uar_stack_size_log, flags()->max_uar_stack_size_log);
    stack_size_log =
        Min(stack_size_log, static_cast<uptr>(flags()->max_uar_stack_size_log));
    stack_size_log =
        Max(stack_size_log, static_cast<uptr>(flags()->min_uar_stack_size_log));
    fake_stack_ = FakeStack::Create(stack_size_log);
    DCHECK_EQ(GetCurrentThread(), this);
    SetTLSFakeStack(fake_stack_);
    return fake_stack_;
  }
  return nullptr;
}

void XsanThread::Init(const InitOptions *options) {
  DCHECK_NE(tid(), kInvalidTid);
  next_stack_top_ = next_stack_bottom_ = 0;
  atomic_store(&stack_switching_, false, memory_order_release);
  CHECK_EQ(this->stack_size(), 0U);
  SetThreadStackAndTls(options);
  if (stack_top_ != stack_bottom_) {
    CHECK_GT(this->stack_size(), 0U);
    CHECK(AddrIsInMem(stack_bottom_));
    CHECK(AddrIsInMem(stack_top_ - 1));
  }
  ClearShadowForThreadStackAndTLS();
  fake_stack_ = nullptr;
  if (__xsan_option_detect_stack_use_after_return &&
      tid() == GetCurrentTidOrInvalid()) {
    // AsyncSignalSafeLazyInitFakeStack makes use of threadlocals and must be
    // called from the context of the thread it is initializing, not its parent.
    // Most platforms call XsanThread::Init on the newly-spawned thread, but
    // Fuchsia calls this function from the parent thread.  To support that
    // approach, we avoid calling AsyncSignalSafeLazyInitFakeStack here; it will
    // be called by the new thread when it first attempts to access the fake
    // stack.
    AsyncSignalSafeLazyInitFakeStack();
  }
  int local = 0;
  VReport(1, "T%d: stack [%p,%p) size 0x%zx; local=%p\n", tid(),
          (void *)stack_bottom_, (void *)stack_top_, stack_top_ - stack_bottom_,
          (void *)&local);
}

// Fuchsia doesn't use ThreadStart.
// xsan_fuchsia.c definies CreateMainThread and SetThreadStackAndTls.
#if !SANITIZER_FUCHSIA

thread_return_t XsanThread::ThreadStart(tid_t os_id) {
  Init();
  xsanThreadRegistry().StartThread(tid(), os_id, ThreadType::Regular, nullptr);

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
  SetCurrentThread(main_thread);
  main_thread->ThreadStart(internal_getpid());
  return main_thread;
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
  if (stack_top_ != stack_bottom_)
    PoisonShadow(stack_bottom_, stack_top_ - stack_bottom_, 0);
  if (tls_begin_ != tls_end_) {
    uptr tls_begin_aligned = RoundDownTo(tls_begin_, ASAN_SHADOW_GRANULARITY);
    uptr tls_end_aligned = RoundUpTo(tls_end_, ASAN_SHADOW_GRANULARITY);
    FastPoisonShadow(tls_begin_aligned, tls_end_aligned - tls_begin_aligned, 0);
  }
}

bool XsanThread::GetStackFrameAccessByAddr(uptr addr,
                                           StackFrameAccess *access) {
  if (stack_top_ == stack_bottom_)
    return false;

  uptr bottom = 0;
  if (AddrIsInStack(addr)) {
    bottom = stack_bottom();
  } else if (FakeStack *fake_stack = get_fake_stack()) {
    bottom = fake_stack->AddrIsInFakeStack(addr);
    CHECK(bottom);
    access->offset = addr - bottom;
    access->frame_pc = ((uptr*)bottom)[2];
    access->frame_descr = (const char *)((uptr*)bottom)[1];
    return true;
  }
  uptr aligned_addr = RoundDownTo(addr, SANITIZER_WORDSIZE / 8);  // align addr.
  uptr mem_ptr = RoundDownTo(aligned_addr, ASAN_SHADOW_GRANULARITY);
  u8 *shadow_ptr = (u8*)MemToShadow(aligned_addr);
  u8 *shadow_bottom = (u8*)MemToShadow(bottom);

  while (shadow_ptr >= shadow_bottom &&
         *shadow_ptr != kXsanStackLeftRedzoneMagic) {
    shadow_ptr--;
    mem_ptr -= ASAN_SHADOW_GRANULARITY;
  }

  while (shadow_ptr >= shadow_bottom &&
         *shadow_ptr == kXsanStackLeftRedzoneMagic) {
    shadow_ptr--;
    mem_ptr -= ASAN_SHADOW_GRANULARITY;
  }

  if (shadow_ptr < shadow_bottom) {
    return false;
  }

  uptr *ptr = (uptr *)(mem_ptr + ASAN_SHADOW_GRANULARITY);
  CHECK(ptr[0] == kCurrentStackFrameMagic);
  access->offset = addr - (uptr)ptr;
  access->frame_pc = ptr[2];
  access->frame_descr = (const char*)ptr[1];
  return true;
}

uptr XsanThread::GetStackVariableShadowStart(uptr addr) {
  uptr bottom = 0;
  if (AddrIsInStack(addr)) {
    bottom = stack_bottom();
  } else if (FakeStack *fake_stack = get_fake_stack()) {
    bottom = fake_stack->AddrIsInFakeStack(addr);
    if (bottom == 0) {
      return 0;
    }
  } else {
    return 0;
  }

  uptr aligned_addr = RoundDownTo(addr, SANITIZER_WORDSIZE / 8);  // align addr.
  u8 *shadow_ptr = (u8*)MemToShadow(aligned_addr);
  u8 *shadow_bottom = (u8*)MemToShadow(bottom);

  while (shadow_ptr >= shadow_bottom &&
         (*shadow_ptr != kXsanStackLeftRedzoneMagic &&
          *shadow_ptr != kXsanStackMidRedzoneMagic &&
          *shadow_ptr != kXsanStackRightRedzoneMagic))
    shadow_ptr--;

  return (uptr)shadow_ptr + 1;
}

bool XsanThread::AddrIsInStack(uptr addr) {
  const auto bounds = GetStackBounds();
  return addr >= bounds.bottom && addr < bounds.top;
}

static bool ThreadStackContainsAddress(ThreadContextBase *tctx_base,
                                       void *addr) {
  XsanThreadContext *tctx = static_cast<XsanThreadContext *>(tctx_base);
  XsanThread *t = tctx->thread;
  if (!t)
    return false;
  if (t->AddrIsInStack((uptr)addr))
    return true;
  FakeStack *fake_stack = t->get_fake_stack();
  if (!fake_stack)
    return false;
  return fake_stack->AddrIsInFakeStack((uptr)addr);
}

XsanThread *GetCurrentThread() {
  XsanThreadContext *context =
      reinterpret_cast<XsanThreadContext *>(XsanTSDGet());
  if (!context) {
    if (SANITIZER_ANDROID) {
      // On Android, libc constructor is called _after_ xsan_init, and cleans up
      // TSD. Try to figure out if this is still the main thread by the stack
      // address. We are not entirely sure that we have correct main thread
      // limits, so only do this magic on Android, and only if the found thread
      // is the main thread.
      XsanThreadContext *tctx = GetThreadContextByTidLocked(kMainTid);
      if (tctx && ThreadStackContainsAddress(tctx, &context)) {
        SetCurrentThread(tctx->thread);
        return tctx->thread;
      }
    }
    return nullptr;
  }
  return context->thread;
}

void SetCurrentThread(XsanThread *t) {
  CHECK(t->context());
  VReport(2, "SetCurrentThread: %p for thread %p\n", (void *)t->context(),
          (void *)GetThreadSelf());
  // Make sure we do not reset the current XsanThread.
  CHECK_EQ(0, XsanTSDGet());
  XsanTSDSet(t->context());
  CHECK_EQ(t->context(), XsanTSDGet());
}

u32 GetCurrentTidOrInvalid() {
  XsanThread *t = GetCurrentThread();
  return t ? t->tid() : kInvalidTid;
}

XsanThread *FindThreadByStackAddress(uptr addr) {
  xsanThreadRegistry().CheckLocked();
  XsanThreadContext *tctx = static_cast<XsanThreadContext *>(
      xsanThreadRegistry().FindThreadContextLocked(ThreadStackContainsAddress,
                                                   (void *)addr));
  return tctx ? tctx->thread : nullptr;
}

void EnsureMainThreadIDIsCorrect() {
  XsanThreadContext *context =
      reinterpret_cast<XsanThreadContext *>(XsanTSDGet());
  if (context && (context->tid == kMainTid))
    context->os_id = GetTid();
}

__xsan::XsanThread *GetXsanThreadByOsIDLocked(tid_t os_id) {
  __xsan::XsanThreadContext *context = static_cast<__xsan::XsanThreadContext *>(
      __xsan::xsanThreadRegistry().FindThreadContextByOsIDLocked(os_id));
  if (!context) return nullptr;
  return context->thread;
}
} // namespace __xsan

// --- Implementation of LSan-specific functions --- {{{1
namespace __lsan {
bool GetThreadRangesLocked(tid_t os_id, uptr *stack_begin, uptr *stack_end,
                           uptr *tls_begin, uptr *tls_end, uptr *cache_begin,
                           uptr *cache_end, DTLS **dtls) {
  __xsan::XsanThread *t = __xsan::GetXsanThreadByOsIDLocked(os_id);
  if (!t) return false;
  *stack_begin = t->stack_bottom();
  *stack_end = t->stack_top();
  *tls_begin = t->tls_begin();
  *tls_end = t->tls_end();
  // ASan doesn't keep allocator caches in TLS, so these are unused.
  *cache_begin = 0;
  *cache_end = 0;
  *dtls = t->dtls();
  return true;
}

void GetAllThreadAllocatorCachesLocked(InternalMmapVector<uptr> *caches) {}

void ForEachExtraStackRange(tid_t os_id, RangeIteratorCallback callback,
                            void *arg) {
  __xsan::XsanThread *t = __xsan::GetXsanThreadByOsIDLocked(os_id);
  if (!t)
    return;
  __xsan::FakeStack *fake_stack = t->get_fake_stack();
  if (!fake_stack)
    return;
  fake_stack->ForEachFakeFrame(callback, arg);
}

void LockThreadRegistry() {
  __xsan::xsanThreadRegistry().Lock();
}

void UnlockThreadRegistry() {
  __xsan::xsanThreadRegistry().Unlock();
}

ThreadRegistry *GetThreadRegistryLocked() {
  __xsan::xsanThreadRegistry().CheckLocked();
  return &__xsan::xsanThreadRegistry();
}

void EnsureMainThreadIDIsCorrect() {
  __xsan::EnsureMainThreadIDIsCorrect();
}
} // namespace __lsan

// ---------------------- Interface ---------------- {{{1
using namespace __xsan;

extern "C" {
SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_start_switch_fiber(void **fakestacksave, const void *bottom,
                                    uptr size) {
  XsanThread *t = GetCurrentThread();
  if (!t) {
    VReport(1, "__xsan_start_switch_fiber called from unknown thread\n");
    return;
  }
  t->StartSwitchFiber((FakeStack**)fakestacksave, (uptr)bottom, size);
}

SANITIZER_INTERFACE_ATTRIBUTE
void __sanitizer_finish_switch_fiber(void* fakestack,
                                     const void **bottom_old,
                                     uptr *size_old) {
  XsanThread *t = GetCurrentThread();
  if (!t) {
    VReport(1, "__xsan_finish_switch_fiber called from unknown thread\n");
    return;
  }
  t->FinishSwitchFiber((FakeStack*)fakestack,
                       (uptr*)bottom_old,
                       (uptr*)size_old);
}
}
