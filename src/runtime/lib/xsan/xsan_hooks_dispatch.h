#pragma once

#include "xsan_hooks_gen.h"

static inline void ReduceOr(bool& res, bool a) { res = res || a; }

template <typename T>
static inline void ReduceMax(T& res, T a) {
  if (res < a)
    res = a;
}

template <auto val, typename T>
static inline void ReduceNeq(T& res, T a) {
  if (res == val)
    res = a;
}

template <typename Arr1, typename Arr2>
static inline void ReduceExtend(Arr1& res, const Arr2& a) {
  for (auto& i : a) res.push_back(i);
}

#define XSAN_HOOKS_EXEC_OR(RES, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, ReduceOr, FUNC, __VA_ARGS__)

// `Max` is implemented in sanitizer_common
#define XSAN_HOOKS_EXEC_MAX(RES, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, ReduceMax, FUNC, __VA_ARGS__)

#define XSAN_HOOKS_EXEC_NEQ(RES, VAL, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, ReduceNeq<VAL>, FUNC, __VA_ARGS__)

#define XSAN_HOOKS_EXEC_EXTEND(RES, FUNC, ...) \
  XSAN_HOOKS_EXEC_REDUCE(RES, ReduceExtend, FUNC, __VA_ARGS__)
