//===-- xsan_hooks_default.h ------------------------------------------*- C++
//-*-===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file contains the defualt implementation of the hooks
//===----------------------------------------------------------------------===//
#pragma once

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "xsan_attribute.h"
#include "xsan_hooks_types.h"

namespace __xsan {

// The use of templates here ensures that the `Context` class for sanitizers
// using the default context is not the same class. This is necessary because
// implicit type conversion rules need to be defined in bulk. Without templates,
// the `Context` for sanitizers using the default context would be the same
// class, which would lead to duplicate definitions of conversion rules.
template <XsanHooksSanitizer san>
struct DefaultContext {
  ALWAYS_INLINE DefaultContext() {};
  ALWAYS_INLINE DefaultContext(uptr pc) {};
};

struct DefaultThread {};

template <typename Context, typename Thread>
struct DefaultHooks {
  using BufferedStackTrace = ::__sanitizer::BufferedStackTrace;
  using u32 = ::__sanitizer::u32;
  using uptr = ::__sanitizer::uptr;

  // ----------- State/Ignoration Management Hooks -----------
  /*
   Manage and notify the following states:
    - if XSan is in internal
    - if XSan is in symbolizer
    - if XSan should ignore interceptors
    - the exit code of XSan
  */

  ALWAYS_INLINE static void EnterSymbolizer() {}
  ALWAYS_INLINE static void ExitSymbolizer() {}

  /*
   The sub-sanitizers implement the following ignore predicates to ignore
    - Interceptors
    - Allocation/Free Hooks
   which are shared by all sub-sanitizers.
 */
  ALWAYS_INLINE static bool ShouldIgnoreInterceptors(const Context &ctx) {
    return false;
  }
  ALWAYS_INLINE static bool ShouldIgnoreAllocFreeHook() { return false; }

  // ---------- End of State Management Hooks -----------------

  // ---------------------- Memory Management Hooks -------------------
  /// As XSan uses ASan's heap allocator and fake stack directly, hence we don't
  /// need to invoke ASan's hooks here.

  ALWAYS_INLINE static void OnAllocatorMap(uptr p, uptr size) {}
  ALWAYS_INLINE static void OnAllocatorMapSecondary(uptr p, uptr size,
                                                    uptr user_begin,
                                                    uptr user_size) {}
  ALWAYS_INLINE static void OnAllocatorUnmap(uptr p, uptr size) {}
  ALWAYS_INLINE static void OnXsanAllocHook(uptr ptr, uptr size,
                                            BufferedStackTrace *stack) {}
  ALWAYS_INLINE static void OnXsanFreeHook(uptr ptr, uptr size,
                                           BufferedStackTrace *stack) {}
  ALWAYS_INLINE static void OnXsanAllocFreeTailHook(uptr pc) {}
  ALWAYS_INLINE static void OnFakeStackDestory(uptr addr, uptr size) {}

  // ---------- End of Memory Management Hooks -------------------

  // ---------------------- pthread-related hooks -----------------
  /* pthread_create, pthread_join, pthread_detach, pthread_tryjoin_np, ... */

  class ScopedPthreadJoin {
   public:
    /// args:
    ///   - res : the return value of pthread_join, 0 for success, non-zero for
    ///   failure.
    ///   - ctx : the context of sub-sanitizer.
    ///   - th : the pthread_t of the thread to be joined.
    ALWAYS_INLINE ScopedPthreadJoin(const int &res, const Context &ctx,
                                    const void *th) {}
    ALWAYS_INLINE ~ScopedPthreadJoin() {}
  };

  class ScopedPthreadDetach {
   public:
    ALWAYS_INLINE ScopedPthreadDetach(const int &res, const Context &ctx,
                                      const void *th) {}
    ALWAYS_INLINE ~ScopedPthreadDetach() {}
  };

  class ScopedPthreadTryJoin {
   public:
    ALWAYS_INLINE ScopedPthreadTryJoin(const int &res, const Context &ctx,
                                       const void *th) {}
    ALWAYS_INLINE ~ScopedPthreadTryJoin() {}
  };

  ALWAYS_INLINE static void OnPthreadCreate() {}

  // ---------------- End of pthread-related hooks -----------------

  // ---------------------- Special Function Hooks -----------------
  class ScopedAtExitWrapper {
   public:
    ALWAYS_INLINE ScopedAtExitWrapper(uptr pc, const void *ctx) {}
    ALWAYS_INLINE ~ScopedAtExitWrapper() {}
  };
  class ScopedAtExitHandler {
   public:
    ALWAYS_INLINE ScopedAtExitHandler(uptr pc, const void *ctx) {}
    ALWAYS_INLINE ~ScopedAtExitHandler() {}
  };
  ALWAYS_INLINE static void *vfork_before_and_after() { return nullptr; }
  ALWAYS_INLINE static void vfork_parent_after(void *sp) {}
  ALWAYS_INLINE static void OnForkBefore() {}
  ALWAYS_INLINE static void OnForkAfter(bool is_child) {}
  ALWAYS_INLINE static void OnLibraryLoaded(const char *filename,
                                            void *handle) {}
  ALWAYS_INLINE static void OnLibraryUnloaded() {}
  ALWAYS_INLINE static void OnLongjmp(void *env, const char *fn_name, uptr pc) {
  }
  // --------- End of Special Function Hooks ---------

  // ---------- Unwind-related Hooks ----------------

  ALWAYS_INLINE static void OnEnterUnwind() {}
  ALWAYS_INLINE static void OnExitUnwind() {}

  // ---------- End of Unwind-related Hooks ----------------

  // ---------- Require Stack Trace Hooks ----------------
  ALWAYS_INLINE static bool RequireStackTraces() { return false; }
  ALWAYS_INLINE static int RequireStackTracesSize() { return -1; }
  // ---------- End of Require Stack Trace Hooks ----------------

  // ---------------------- Flags Registration Hooks ---------------
  ALWAYS_INLINE static void InitializeFlags() {}
  ALWAYS_INLINE static void InitializeSanitizerFlags() {}
  ALWAYS_INLINE static void SetCommonFlags(CommonFlags &cf) {}
  ALWAYS_INLINE static void ValidateFlags() {}
  // ----------End of Flags Registration Hooks ---------------

  // ---------- Thread-Related Hooks --------------------------
  ALWAYS_INLINE static void SetThreadName(const char *name) {}
  ALWAYS_INLINE static void SetThreadNameByUserId(uptr uid, const char *name) {}
  /*
  The following events happen in order while `pthread_create` is executed:

  - [parent-thread] : pthread_create
    - [parent-thread] : XsanThread::Create
      - [parent-thread] : (**HOOK**) XsanThread::CreateThread
    - [child-thread] : xsan_thread_start
      - [child-thread] : XsanThread::ThreadInit
        - [child-thread] : (**HOOK**) XsanThread::ChildThreadInit
      - [child-thread] : XsanThread::ThreadStart (with scoper)
        - [child-thread] : (**HOOK**) XsanThread::ChildThreadStart
        - [child-thread] : start_routine_

  - [child-thread] : TSD destroy / Active destroy
    - [child-thread] : XsanThread::Destroy
      - [child-thread] : (**HOOK**) XsanThread::DestroyThread
  */
  ALWAYS_INLINE static Thread CreateMainThread() { return Thread{}; }
  /// TODO: figure out data
  ALWAYS_INLINE static Thread CreateThread(u32 parent_tid, uptr child_uid,
                                           StackTrace *stack, const void *data,
                                           uptr data_size, bool detached) {
    return Thread{};
  }
  ALWAYS_INLINE static void ChildThreadInit(Thread &thread, tid_t os_id) {}
  ALWAYS_INLINE static void ChildThreadStart(Thread &thread, tid_t os_id) {}
  ALWAYS_INLINE static void DestroyThread(Thread &thread) {}
  // ---------- End of Thread-Related Hooks --------------------------

  // ---------- Synchronization and File-Related Hooks ------------------------
  ALWAYS_INLINE static void AfterMmap(const Context &ctx, void *res, uptr size,
                                      int fd) {}
  ALWAYS_INLINE static void BeforeMunmap(const Context &ctx, void *addr,
                                         uptr size) {}
  // ---------- End of Synchronization and File-Related Hooks ----------------

  // ---------- Generic Hooks in Interceptors ----------------
  PSEUDO_MACRO static void ReadRange(const Context *ctx, const void *offset,
                                     uptr size, const char *func_name) {}
  PSEUDO_MACRO static void WriteRange(const Context *ctx, const void *offset,
                                      uptr size, const char *func_name) {}
  PSEUDO_MACRO static void CommonReadRange(const Context *ctx,
                                           const void *offset, uptr size,
                                           const char *func_name) {}
  PSEUDO_MACRO static void CommonWriteRange(const Context *ctx,
                                            const void *offset, uptr size,
                                            const char *func_name) {}
  // ---------- End of Generic Hooks in Interceptors ----------------
};

}  // namespace __xsan
