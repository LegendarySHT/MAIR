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

namespace __xsan {

#define XSAN_HOOKS_DEFINE_DEFAULT_CONTEXT     \
  struct DefaultContext {                     \
    ALWAYS_INLINE DefaultContext() {};        \
    ALWAYS_INLINE DefaultContext(uptr pc) {}; \
  }

#define XSAN_HOOKS_DEFAULT_CONTEXT_T DefaultContext

template <typename Context>
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

  // ---------- Unwind-related Hooks ----------------

  ALWAYS_INLINE static void OnEnterUnwind() {}
  ALWAYS_INLINE static void OnExitUnwind() {}

  // ---------- End of Unwind-related Hooks ----------------

  // ---------- Require Stack Trace Hooks ----------------

  ALWAYS_INLINE static bool RequireStackTraces() { return false; }
  ALWAYS_INLINE static int RequireStackTracesSize() { return -1; }

  // ---------- End of Require Stack Trace Hooks ----------------
};

}  // namespace __xsan
