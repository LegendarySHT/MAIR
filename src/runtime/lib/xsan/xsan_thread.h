#pragma once

#include "asan/asan_thread.h"
#include "asan_internal.h"
#include "xsan_allocator.h"
#include "xsan_internal.h"
#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_libc.h"
#include "sanitizer_common/sanitizer_thread_registry.h"

namespace __sanitizer {
struct DTLS;
}  // namespace __sanitizer

namespace __xsan {

// XsanThread are stored in TSD and destroyed when the thread dies.
class XsanThread {
 public:
  using StackFrameAccess = __asan::AsanThread::StackFrameAccess;
  
 public:
  static XsanThread *Create(thread_callback_t start_routine, void *arg,
                            u32 parent_tid, StackTrace *stack, bool detached);
  static void TSDDtor(void *tsd);
  void Destroy();

  struct InitOptions;
  void Init(const InitOptions *options = nullptr);

  thread_return_t ThreadStart(tid_t os_id);

  uptr stack_top();
  uptr stack_bottom();
  uptr stack_size();
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  DTLS *dtls() { return dtls_; }
  u32 tid() { return 0; }


  bool GetStackFrameAccessByAddr(uptr addr, StackFrameAccess *access);

  // Returns a pointer to the start of the stack variable's shadow memory.
  uptr GetStackVariableShadowStart(uptr addr);

  bool AddrIsInStack(uptr addr);

  // True is this thread is currently unwinding stack (i.e. collecting a stack
  // trace). Used to prevent deadlocks on platforms where libc unwinder calls
  // malloc internally. See PR17116 for more details.
  bool isUnwinding() const { return unwinding_; }
  void setUnwinding(bool b) { unwinding_ = b; }

  // XsanThreadLocalMallocStorage &malloc_storage() { return malloc_storage_; }

  void *extra_spill_area() { return &extra_spill_area_; }

  void *get_arg() { return arg_; }

  int destructor_iterations_;
  __asan::AsanThread *asan_thread_;

 private:
  // NOTE: There is no XsanThread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.

  void SetThreadStackAndTls(const InitOptions *options);

  void ClearShadowForThreadStackAndTLS();

  struct StackBounds {
    uptr bottom;
    uptr top;
  };
  StackBounds GetStackBounds() const;

  
  thread_callback_t start_routine_;
  void *arg_;

  uptr stack_top_;
  uptr stack_bottom_;
  // these variables are used when the thread is about to switch stack
  uptr next_stack_top_;
  uptr next_stack_bottom_;
  // true if switching is in progress
  atomic_uint8_t stack_switching_;

  uptr tls_begin_;
  uptr tls_end_;
  DTLS *dtls_;

  // XsanThreadLocalMallocStorage malloc_storage_;
  bool unwinding_;
  uptr extra_spill_area_;

};

// Returns a single instance of registry.
ThreadRegistry &xsanThreadRegistry();

// Get the current thread. May return 0.
XsanThread *GetCurrentThread();
void SetCurrentThread(XsanThread *t);
u32 GetCurrentTidOrInvalid();
XsanThread *FindThreadByStackAddress(uptr addr);

// Used to handle fork().
void EnsureMainThreadIDIsCorrect();
} // namespace __xsan