#pragma once

#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "interception/interception.h"


/// TODO: Implement suppression & flags

DECLARE_REAL(void*, memcpy, void *to, const void *from, uptr size)
DECLARE_REAL(void*, memset, void *block, int c, uptr size)

namespace __xsan {


struct XsanInterceptorContext {
  const char *interceptor_name;
};

/// TODO: Implement this
#define ACCESS_MEMORY_RANGE(ctx, offset, size, isWrite) do {            \
    (void)ctx;                                                          \
  } while (0)

// memcpy is called during __xsan_init() from the internals of printf(...).
// We do not treat memcpy with to==from as a bug.
// See http://llvm.org/bugs/show_bug.cgi?id=11763.
#define XSAN_MEMCPY_IMPL(ctx, to, from, size)                           \
  do {                                                                  \
    if (UNLIKELY(!xsan_inited)) return internal_memcpy(to, from, size); \
    if (xsan_init_is_running) {                                         \
      return REAL(memcpy)(to, from, size);                              \
    }                                                                   \
    ENSURE_XSAN_INITED();                                               \
    if (to != from) {                                                   \
      CHECK_RANGES_OVERLAP("memcpy", to, size, from, size);             \
    }                                                                   \
    XSAN_READ_RANGE(ctx, from, size);                                   \
    XSAN_WRITE_RANGE(ctx, to, size);                                    \
    return REAL(memcpy)(to, from, size);                                \
  } while (0)

// memset is called inside Printf.
#define XSAN_MEMSET_IMPL(ctx, block, c, size)                           \
  do {                                                                  \
    if (UNLIKELY(!xsan_inited)) return internal_memset(block, c, size); \
    if (xsan_init_is_running) {                                         \
      return REAL(memset)(block, c, size);                              \
    }                                                                   \
    ENSURE_XSAN_INITED();                                               \
    XSAN_WRITE_RANGE(ctx, block, size);                                 \
    return REAL(memset)(block, c, size);                                \
  } while (0)

#define XSAN_MEMMOVE_IMPL(ctx, to, from, size)                           \
  do {                                                                   \
    if (UNLIKELY(!xsan_inited)) return internal_memmove(to, from, size); \
    ENSURE_XSAN_INITED();                                                \
    XSAN_READ_RANGE(ctx, from, size);                                    \
    XSAN_WRITE_RANGE(ctx, to, size);                                     \
    return internal_memmove(to, from, size);                             \
  } while (0)

#define XSAN_READ_RANGE(ctx, offset, size) \
  ACCESS_MEMORY_RANGE(ctx, offset, size, false)
#define XSAN_WRITE_RANGE(ctx, offset, size) \
  ACCESS_MEMORY_RANGE(ctx, offset, size, true)

// Behavior of functions like "memcpy" or "strcpy" is undefined
// if memory intervals overlap. We report error in this case.
// Macro is used to avoid creation of new frames.
static inline bool RangesOverlap(const char *offset1, uptr length1,
                                 const char *offset2, uptr length2) {
  return !((offset1 + length1 <= offset2) || (offset2 + length2 <= offset1));
}

/// TODO: Implement this
#define CHECK_RANGES_OVERLAP(name, _offset1, length1, _offset2, length2)   \
  do {                                                                     \
    (void)name;                                                            \
  } while (0)

}  // namespace __xsan

