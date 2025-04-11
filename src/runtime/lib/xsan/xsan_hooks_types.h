#pragma once

namespace __xsan {

/// TODO: This enum should be unified
enum class XsanHooksSanitizer {
  Asan,
  Tsan,
};

template <XsanHooksSanitizer san>
struct XsanHooksSanitizerUnImpl;

template <XsanHooksSanitizer san>
struct XsanHooksSanitizerImpl {
  // Hooks should implement like DefaultHooks in `xsan_hooks_default.h:25`.
  // You can simply inherit from it to use most of the default hooks.
  // If you see this error, you need to specialize this struct to register the
  // hooks for your sanitizer.
  using Hooks = XsanHooksSanitizerUnImpl<san>;
};

static inline bool Or(bool a, bool b) { return a || b; }

template <auto val, typename T>
static inline T Neq(T a, T b) {
  return a != val ? a : b;
}

#define XSAN_HOOKS_EXEC_OR(RES, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, Or, FUNC, __VA_ARGS__)

// `Max` is implemented in sanitizer_common
#define XSAN_HOOKS_EXEC_MAX(RES, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, Max, FUNC, __VA_ARGS__)

#define XSAN_HOOKS_EXEC_NEQ(RES, VAL, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, Neq<VAL>, FUNC, __VA_ARGS__)

}  // namespace __xsan
