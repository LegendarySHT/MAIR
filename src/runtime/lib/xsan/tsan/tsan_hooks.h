#pragma once

#include "../xsan_hooks_default.h"
#include "../xsan_hooks_types.h"

namespace __tsan {

struct ThreadState;
ThreadState *cur_thread();
void EnterSymbolizer();
void ExitSymbolizer();

}  // namespace __tsan

namespace __tsan {

struct TsanContext {
  __tsan::ThreadState *thr_;
  uptr pc_;

  TsanContext() : thr_(nullptr), pc_(0) {}
  TsanContext(uptr pc) : thr_(__tsan::cur_thread()), pc_(pc) {}
};

struct TsanHooks : ::__xsan::DefaultHooks<TsanContext> {
  using Context = TsanContext;

  static void EnterSymbolizer() { __tsan::EnterSymbolizer(); }
  static void ExitSymbolizer() { __tsan::ExitSymbolizer(); }
  static bool ShouldIgnoreInterceptors(const Context &ctx);
  static bool ShouldIgnoreAllocFreeHook();

  static void OnAllocatorUnmap(uptr p, uptr size);
  static void OnXsanAllocHook(uptr ptr, uptr size, BufferedStackTrace *stack);
  static void OnXsanFreeHook(uptr ptr, uptr size, BufferedStackTrace *stack);
  static void OnXsanAllocFreeTailHook(uptr pc);
  static void OnFakeStackDestory(uptr addr, uptr size);

  /// TSan may spawn a background thread to recycle resource in pthread_create.
  /// What's more, TSan does not support starting new threads after
  /// multi-threaded fork.
  class ScopedPthreadJoin {
   public:
    ScopedPthreadJoin(const int &res, const Context &ctx, const void *th);
    ~ScopedPthreadJoin();

   private:
    const int &res_;
    const Context &ctx_;
    Tid tid_;
  };

  class ScopedPthreadDetach {
   public:
    ScopedPthreadDetach(const int &res, const Context &xsan_ctx,
                        const void *th);
    ~ScopedPthreadDetach();

   private:
    const int &res_;
    const Context &ctx_;
    Tid tid_;
  };

  class ScopedPthreadTryJoin {
   public:
    ScopedPthreadTryJoin(const int &res, const Context &xsan_ctx,
                         const void *th);
    ~ScopedPthreadTryJoin();

   private:
    uptr th_;
    const int &res_;
    const Context &ctx_;
    Tid tid_;
  };

  static void OnPthreadCreate();
};

}  // namespace __tsan

// Register the hooks for Tsan.
namespace __xsan {

template <>
struct XsanHooksSanitizerImpl<XsanHooksSanitizer::Tsan> {
  using Hooks = __tsan::TsanHooks;
};

}  // namespace __xsan
