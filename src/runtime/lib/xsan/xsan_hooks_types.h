#pragma once

namespace __xsan {

/// TODO: This enum should be unified
enum class XsanHooksSanitizer {
  Asan,
  Tsan,
};

template <XsanHooksSanitizer san>
struct XsanHooksSanitizerTraits;

__attribute__((always_inline)) inline bool Or(bool a, bool b) { return a || b; }

#define XSAN_HOOKS_EXEC_OR(RES, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, Or, FUNC, __VA_ARGS__)

#define XSAN_HOOKS_EXEC_MAX(RES, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, Max, FUNC, __VA_ARGS__)

}  // namespace __xsan
