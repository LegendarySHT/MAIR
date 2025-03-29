#pragma once

#include "../xsan_hooks_default.h"
#include "../xsan_hooks_types.h"

namespace __asan {

// Not use 'using' to avoid redefinition of conversion operator
XSAN_HOOKS_DEFINE_DEFAULT_CONTEXT;

struct AsanHooks : ::__xsan::DefaultHooks<XSAN_HOOKS_DEFAULT_CONTEXT_T> {
  using Context = XSAN_HOOKS_DEFAULT_CONTEXT_T;

  /// ASan 1) checks the correctness of main thread ID, 2) checks the init
  /// orders.
  static void OnPthreadCreate();
};

}  // namespace __asan

// Register the hooks for Asan.
namespace __xsan {

template <>
struct XsanHooksSanitizerImpl<XsanHooksSanitizer::Asan> {
  using Hooks = __asan::AsanHooks;
};

}  // namespace __xsan
