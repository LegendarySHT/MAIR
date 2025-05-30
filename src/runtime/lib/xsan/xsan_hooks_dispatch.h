#pragma once

#include "xsan_hooks_gen.h"

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
