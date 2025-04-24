#pragma once

#include "sanitizer_common/sanitizer_thread_arg_retval.h"
#include "xsan_hooks_dispatch.h"
#include "xsan_internal.h"

namespace __sanitizer {
struct DTLS;
}  // namespace __sanitizer

struct ScopedSyscall;

namespace __xsan {

extern THREADLOCAL XsanThread *xsan_current_thread;

ThreadArgRetval &xsanThreadArgRetval();

// Get the current thread. May return 0.
XsanThread *GetCurrentThread();
u32 GetCurrentTidOrInvalid();
XsanThread *FindThreadByStackAddress(uptr addr);

/// FIXME: Should we actually need such a complex class?
// XsanThread are stored in TSD and destroyed when the thread dies.
class XsanThread {
  friend XsanThread *GetCurrentThread();
  /// TODO: offer uniform hooks to remove this
  friend struct ::ScopedSyscall;

  struct StackBounds {
    uptr bottom;
    uptr top;
  };

 public:
  // For sub-threads. data is used to store rountine & args.
  template <typename T>
  static XsanThread *Create(const T &data, u32 parent_tid, uptr child_uid,
                            StackTrace *stack, bool detached) {
    return Create(&data, sizeof(data), parent_tid, child_uid, stack, detached);
  }
  // For MainThread.
  static XsanThread *Create(u32 parent_tid, StackTrace *stack, bool detached) {
    return Create(nullptr, 0, parent_tid, 0, stack, detached);
  }

  static void TSDDtor(void *tsd);
  void Destroy();

  void ThreadInit(tid_t os_id);
  void ThreadStart();
  /// ASan declares this method, but not implements.
  thread_return_t RunThread();

  uptr stack_top() const;
  uptr stack_bottom() const;
  uptr stack_size() const;
  uptr tls_begin() const { return tls_begin_; }
  uptr tls_end() const { return tls_end_; }
  DTLS *dtls() const { return dtls_; }
  u32 tid() const { return tid_; }

  // ------------ Query Functions for Sub-Sanitizers -----------

  static void SetQueryKey(XsanThreadQueryKey &t) {
    t.xsan_thread_ = xsan_current_thread;
  }

  /// For __tsan::ThreadState obtained from fiber create,
  /// we don't have XsanThread, so we can't check it.
  /// What's more, these fiber-created 'thr' doesn't have
  /// any meaningful stack/TLS space, so we can just return false.
  static bool AddrIsInRealStack(const XsanThreadQueryKey &t, uptr addr) {
    return t.xsan_thread_ ? t.xsan_thread_->AddrIsInRealStack(addr) : false;
  }
  /// ASan uses FakeStack to detect use-after-return bugs just like what it does
  /// to detect use-after-free bugs.
  /// Based on ASan, XSan also provides API to query if an address is in the
  /// fake stack.
  static bool AddrIsInFakeStack(const XsanThreadQueryKey &t, uptr addr) {
    return t.xsan_thread_ ? t.xsan_thread_->AddrIsInFakeStack(addr) : false;
  }
  static bool AddrIsInStack(const XsanThreadQueryKey &t, uptr addr) {
    return t.xsan_thread_ ? t.xsan_thread_->AddrIsInStack(addr) : false;
  }
  static bool AddrIsInTls(const XsanThreadQueryKey &t, uptr addr) {
    return t.xsan_thread_ ? t.xsan_thread_->AddrIsInTls(addr) : false;
  }

  static bool isMainThread() { return xsan_current_thread->is_main_thread_; }
  static bool isMainThread(const XsanThreadQueryKey &t) {
    return t.xsan_thread_ ? t.xsan_thread_->is_main_thread_ : false;
  }

  // ------------ End of Query Functions ----------------

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

  int destructor_iterations_;

  bool is_inited_;
  bool in_ignored_lib_;

 private:
  // NOTE: There is no XsanThread constructor. It is allocated
  // via mmap() and *must* be valid in zero-initialized state.
  static XsanThread *Create(const void *start_data, uptr data_size,
                            u32 parent_tid, uptr child_uid, StackTrace *stack,
                            bool detached);

  /// Create sub-sanitizers' main thread data. (Call by main thread)
  void CreateMainThread();
  /// Create sub-sanitizers' thread data. (Call by parent thread)
  void CreateThread(const void *start_data, uptr data_size, u32 parent_tid,
                    uptr child_uid, StackTrace *stack, bool detached);
  /// Set the current thread and enable the TSD and TSD destructor. (Call by
  /// child thread)
  void ChildThreadInit();
  void ChildThreadStart();
  /// Distroy sub-sanitizers' thread data. (Call by child thread)
  void DestroyThread();

  struct InitOptions;
  void SetThreadStackAndTls(const InitOptions *options);

  StackBounds GetStackBounds() const;

  void GetStartData(void *out, uptr out_size) const;

  bool AddrIsInRealStack(uptr addr) const;
  bool AddrIsInFakeStack(uptr addr) const;
  bool AddrIsInStack(uptr addr) const;
  bool AddrIsInTls(uptr addr) const;

  XSAN_HOOKS_DEFINE_VAR(Thread);
  XSAN_HOOKS_DEFINE_VAR_CVT(Thread);
  u32 tid_;
  tid_t os_id_;

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

  bool is_main_thread_;

  char start_data_[];
};

}  // namespace __xsan
