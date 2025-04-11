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
#include "xsan_hooks_todo.h"
#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_thread.h"

using namespace __xsan;
// ---------------------- State/Ignoration Management Hooks --------------------
namespace __xsan {

THREADLOCAL int xsan_in_intenal = 0;
THREADLOCAL int xsan_in_calloc = 0;
THREADLOCAL int is_in_symbolizer;

int get_exit_code(const void *ctx) {
  int exit_code = 0;
  if (CAN_SANITIZE_LEAKS && common_flags()->detect_leaks &&
      __lsan::HasReportedLeaks()) {
    return common_flags()->exitcode;
  }

  auto *tsan_thr =
      ctx == nullptr
          ? __tsan::cur_thread()
          : ((const XsanInterceptorContext *)ctx)->xsan_ctx.tsan.thr_;
  exit_code = __tsan::Finalize(tsan_thr);
  return exit_code;
}

}  // namespace __xsan

// ---------- End of State Management Hooks -----------------

// ---------------------- Memory Management Hooks -------------------
/// As XSan uses ASan's heap allocator and fake stack directly, hence we don't
/// need to invoke ASan's hooks here.

namespace __asan {

SANITIZER_WEAK_ATTRIBUTE
bool GetASanMellocStackTrace(u32 &stack_trace_id, uptr addr,
                             bool set_stack_trace_id) {
  return false;
}

}  // namespace __asan

namespace __xsan {

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

// ---------------------- Special Function Hooks -----------------
namespace __xsan {
/*
Provides hooks for special functions, such as
  - atexit / on_exit / _cxa_atexit
  - longjmp / siglongjmp / _longjmp / _siglongjmp
  - dlopen / dlclose
  - vfork / fork
*/

/// To implement macro COMMON_INTERCEPTOR_SPILL_AREA in *vfork.S
/// Notably, this function is called TWICE at the attitude per process.
extern "C" void *__xsan_vfork_before_and_after() {
  void *final_result = nullptr;
  /// Invoked TRIPLE totally, once before vfork to store the sp, twice after
  /// vfork child/parent to restore the sp.
  XSAN_HOOKS_EXEC_NEQ(final_result, nullptr, vfork_before_and_after);
  return final_result;
}

extern "C" void __xsan_vfork_parent_after(void *sp) {
  /// Unpoison vfork child's new stack space : [stack_bottom, sp]
  XSAN_HOOKS_EXEC(vfork_parent_after, sp);
}
// Ignore interceptors in OnLibraryLoaded()/Unloaded().  These hooks use code
// (ListOfModules::init, MemoryMappingLayout::DumpListOfModules) that make
// intercepted calls, which can cause deadlockes with ReportRace() which also
// uses this code.
void OnLibraryLoaded(const char *filename, void *handle) {
  __xsan::ScopedIgnoreInterceptors ignore;
  XSAN_HOOKS_EXEC(OnLibraryLoaded, filename, handle);
}

void OnLibraryUnloaded() {
  __xsan::ScopedIgnoreInterceptors ignore;
  XSAN_HOOKS_EXEC(OnLibraryUnloaded);
}
}  // namespace __xsan

// --------------- End of Special Function Hooks -----------------

// ---------- Thread-Related Hooks --------------------------

namespace __xsan {
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
