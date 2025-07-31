#pragma once

#include "asan/asan_interface_xsan.h"
#include "interception/interception.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "xsan_hooks.h"
#include "xsan_stack.h"

/// TODO: Implement suppression & flags

DECLARE_REAL(void *, memcpy, void *to, const void *from, SIZE_T size)
DECLARE_REAL(void *, memset, void *block, int c, SIZE_T size)

namespace __xsan {

// Behavior of functions like "memcpy" or "strcpy" is undefined
// if memory intervals overlap. We report error in this case.
// Macro is used to avoid creation of new frames.
static inline bool XsanRangesOverlap(const char *offset1, uptr length1,
                                     const char *offset2, uptr length2) {
  return !((offset1 + length1 <= offset2) || (offset2 + length2 <= offset1));
}

#define CHECK_RANGES_OVERLAP(name, _offset1, length1, _offset2, length2)      \
  do {                                                                        \
    const char *offset1 = (const char *)_offset1;                             \
    const char *offset2 = (const char *)_offset2;                             \
    if (UNLIKELY(                                                             \
            __xsan::XsanRangesOverlap(offset1, length1, offset2, length2))) { \
      __xsan::OnTwoRangesOverlap(offset1, length1, offset2, length2, name);   \
    }                                                                         \
  } while (0)

#define XSAN_READ_RANGE(ctx, offset, size)    \
  do {                                        \
    if (LIKELY(::__xsan::IsAppMem(offset)))   \
      ::__xsan::ReadRange(ctx, offset, size); \
  } while (0)

#define XSAN_WRITE_RANGE(ctx, offset, size)    \
  do {                                         \
    if (LIKELY(::__xsan::IsAppMem(offset)))    \
      ::__xsan::WriteRange(ctx, offset, size); \
  } while (0)

#define XSAN_USE_RANGE(ctx, offset, size)    \
  do {                                       \
    if (LIKELY(::__xsan::IsAppMem(offset)))  \
      ::__xsan::UseRange(ctx, offset, size); \
  } while (0)

#define XSAN_COPY_RANGE(ctx, dst, src, size)                        \
  do {                                                              \
    if (LIKELY(::__xsan::IsAppMem(dst) && __xsan::IsAppMem(src))) { \
      UNINITIALIZED BufferedStackTrace stack;                       \
      GetStackTraceCopy(stack);                                     \
      ::__xsan::CopyRange(ctx, dst, src, size, stack);              \
    }                                                               \
  } while (0)

#define XSAN_MOVE_RANGE(ctx, dst, src, size)                        \
  do {                                                              \
    if (LIKELY(::__xsan::IsAppMem(dst) && __xsan::IsAppMem(src))) { \
      UNINITIALIZED BufferedStackTrace stack;                       \
      GetStackTraceCopy(stack);                                     \
      ::__xsan::MoveRange(ctx, dst, src, size, stack);              \
    }                                                               \
  } while (0)

#define XSAN_INIT_RANGE(ctx, offset, size)    \
  do {                                        \
    if (LIKELY(::__xsan::IsAppMem(offset)))   \
      ::__xsan::InitRange(ctx, offset, size); \
  } while (0)

// --------------------- For sanitizer_common ---------------------

#define XSAN_COMMON_READ_RANGE(ctx, offset, size)   \
  do {                                              \
    if (LIKELY(::__xsan::IsAppMem(offset)))         \
      ::__xsan::CommonReadRange(ctx, offset, size); \
  } while (0)

#define XSAN_COMMON_WRITE_RANGE(ctx, offset, size)   \
  do {                                               \
    if (LIKELY(::__xsan::IsAppMem(offset)))          \
      ::__xsan::CommonWriteRange(ctx, offset, size); \
  } while (0)

#define XSAN_COMMON_UNPOISON_PARAM(count) ::__xsan::CommonUnpoisonParam(count)

#define XSAN_COMMON_INIT_RANGE(ptr, size)            \
  do {                                               \
    if (LIKELY(::__xsan::IsAppMem(ptr)))             \
      ::__xsan::CommonInitRange(nullptr, ptr, size); \
  } while (0)

// --------------------- Utility functions ---------------------

#define XSAN_READ_STRING_OF_LEN(ctx, s, len, n) \
  XSAN_READ_RANGE((ctx), (s),                   \
                  (common_flags()->strict_string_checks ? (len) + 1 : (n)))

#define XSAN_READ_STRING(ctx, s, n) \
  XSAN_READ_STRING_OF_LEN((ctx), (s), internal_strlen(s), (n))

// Only the last byte (0x0) is used for conditional judgement.
#define XSAN_USE_STRING(ctx, s, len)          \
  do {                                        \
    if (common_flags()->strict_string_checks) \
      XSAN_USE_RANGE(ctx, (s) + (len), 1);    \
  } while (0)

}  // namespace __xsan
