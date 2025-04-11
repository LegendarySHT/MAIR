#pragma once

#include "../xsan_hooks_default.h"
#include "../xsan_hooks_types.h"
#include "../xsan_internal.h"

extern "C" {
void *__asan_extra_spill_area();
void __asan_handle_vfork(void *sp);
void __asan_handle_no_return();
void InitializeFlags();
}

namespace __asan {

using AsanContext = ::__xsan::DefaultContext<__xsan::XsanHooksSanitizer::Asan>;

struct AsanHooks : ::__xsan::DefaultHooks<AsanContext> {
  using Context = AsanContext;

  /// ASan 1) checks the correctness of main thread ID, 2) checks the init
  /// orders.
  static void OnPthreadCreate();
  // ---------------------- Special Function Hooks -----------------
  class ScopedAtExitHandler {
   public:
    ScopedAtExitHandler(uptr pc, const void *ctx);
    ~ScopedAtExitHandler();
  };
  static void *vfork_before_and_after() { return __asan_extra_spill_area(); }
  static void vfork_parent_after(void *sp) { __asan_handle_vfork(sp); }
  static void OnForkBefore();
  static void OnForkAfter(bool is_child);
  static void OnLongjmp(void *env, const char *fn_name, uptr pc) {
    __asan_handle_no_return();
  }
  // ---------------------- Flags Registration Hooks ---------------
  static void InitializeFlags();
  static void InitializeSanitizerFlags() {
    {
      __xsan::ScopedSanitizerToolName tool_name("AddressSanitizer");
      // Initialize flags. This must be done early, because most of the
      // initialization steps look at flags().
      InitializeFlags();
    }
  }
  static void SetCommonFlags(CommonFlags &cf);
  static void ValidateFlags();
  // ---------- Thread-Related Hooks --------------------------
  static void SetThreadName(const char *name);
  static void SetThreadNameByUserId(uptr uid, const char *name) {}
  // static void OnSetCurrentThread(
  //     __xsan::XsanThread
  //         *t);  /// TODO: The arguement `__xsan::XsanThread` right?
  // static void OnThreadCreate(__xsan::XsanThread *xsan_thread,
  //                            const void *start_data, uptr data_size,
  //                            u32 parent_tid, StackTrace *stack, bool
  //                            detached);
  // static void OnThreadDestroy(AsanThread *asan_thread) {
  //   asan_thread->Destroy();
  // }
  // static void BeforeThreadStart(__xsan::XsanThread *xsan_thread, tid_t os_id)
  // {
  //   xsan_thread->asan_thread_->BeforeThreadStart(os_id);
  // }
  // static void AfterThreadStart(__xsan::XsanThread *xsan_thread) {
  //   xsan_thread->asan_thread_->AfterThreadStart();
  // }
  // ---------- Synchronization and File-Related Hooks ------------------------
  static void AfterMmap(const Context &ctx, void *res, uptr size, int fd);
  static void BeforeMunmap(const Context &ctx, void *addr, uptr size);
};

}  // namespace __asan

// Register the hooks for Asan.
namespace __xsan {

template <>
struct XsanHooksSanitizerImpl<XsanHooksSanitizer::Asan> {
  using Hooks = __asan::AsanHooks;
};

}  // namespace __xsan
