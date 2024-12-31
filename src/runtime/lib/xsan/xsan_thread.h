#pragma once

#include "sanitizer_common/sanitizer_thread_arg_retval.h"
#include "xsan_internal.h"

namespace __sanitizer {
struct DTLS;
}  // namespace __sanitizer

namespace __tsan {
struct ThreadState;
}  // namespace __tsan

namespace __asan {
class AsanThread;
}  // namespace __asan

namespace __xsan {

/// FIXME: Should we actually need such a complex class?
// XsanThread are stored in TSD and destroyed when the thread dies.
class XsanThread {
 public:
  // For sub-threads. data is used to store rountine & args.
  template <typename T>
  static XsanThread *Create(const T &data, u32 parent_tid, StackTrace *stack,
                            bool detached) {
    return Create(&data, sizeof(data), parent_tid, stack, detached);
  }
  // For MainThread.
  static XsanThread *Create(u32 parent_tid, StackTrace *stack, bool detached) {
    return Create(nullptr, 0, parent_tid, stack, detached);
  }

  static void TSDDtor(void *tsd);
  void Destroy();

  struct InitOptions;
  void Init(const InitOptions *options = nullptr);

  Tid PostNonMainThreadCreate(uptr pc, uptr uid);

  void ThreadStart(tid_t os_id);
  /// ASan declares this method, but not implements.
  thread_return_t RunThread();

  uptr stack_top();
  uptr stack_bottom();
  uptr stack_size();
  uptr tls_begin() { return tls_begin_; }
  uptr tls_end() { return tls_end_; }
  DTLS *dtls() { return dtls_; }
  u32 tid() { return tid_; }

  bool AddrIsInRealStack(uptr addr);
  /// ASan uses FakeStack to detect use-after-return bugs just like what it does
  /// to detect use-after-free bugs.
  /// Based on ASan, XSan also provides API to query if an address is in the
  /// fake stack.
  bool AddrIsInFakeStack(uptr addr);
  bool AddrIsInStack(uptr addr);
  bool AddrIsInTls(uptr addr);

  // True is this thread is currently unwinding stack (i.e. collecting a stack
  // trace). Used to prevent deadlocks on platforms where libc unwinder calls
  // malloc internally. See PR17116 for more details.
  bool isUnwinding() const { return unwinding_; }
  void setUnwinding(bool b) { unwinding_ = b; }

  // XsanThreadLocalMallocStorage &malloc_storage() { return malloc_storage_; }

  void *extra_spill_area() { return &extra_spill_area_; }

  template <typename T>
  void GetStartData(T &data) const {
    GetStartData(&data, sizeof(data));
  }

  bool isMainThread() { return is_main_thread_; }

  int destructor_iterations_;
  __asan::AsanThread *asan_thread_;
  __tsan::ThreadState *tsan_thread_;
  bool is_inited_;
  bool in_ignored_lib_;

 private:
  // NOTE: There is no XsanThread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.

  static XsanThread *Create(const void *start_data, uptr data_size,
                            u32 parent_tid, StackTrace *stack, bool detached);

  /// Create sub-sanitizers' thread data.
  void OnThreadCreate(const void *start_data, uptr data_size,
                            u32 parent_tid, StackTrace *stack, bool detached);
  /// Distroy sub-sanitizers' thread data.
  void OnThreadDestroy();
  /// Initialize sub-sanitizers' thread data in new thread and before the real
  /// callback execution.
  void BeforeThreadStart(tid_t os_id);
  void AfterThreadStart();

  void SetThreadStackAndTls(const InitOptions *options);

  struct StackBounds {
    uptr bottom;
    uptr top;
  };
  StackBounds GetStackBounds() const;

  void GetStartData(void *out, uptr out_size) const;

  u32 tid_;

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

  bool detached_;
  bool is_main_thread_;
  Tid tsan_tid_;

  char start_data_[];
};

ThreadArgRetval &xsanThreadArgRetval();


// Get the current thread. May return 0.
XsanThread *GetCurrentThread();
/// Set the current thread and enable the TSD and TSD destructor.
void SetCurrentThread(XsanThread *t);
u32 GetCurrentTidOrInvalid();
XsanThread *FindThreadByStackAddress(uptr addr);

}  // namespace __xsan