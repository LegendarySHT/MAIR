//===-- tsan_interceptors_memintrinsics.cpp -------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of ThreadSanitizer (TSan), a race detector.
//
//===----------------------------------------------------------------------===//

#define SANITIZER_COMMON_NO_REDEFINE_BUILTINS

#include "interception/interception.h"
#include "tsan_interceptors.h"
#include "tsan_interface.h"

using namespace __tsan;

// #include "sanitizer_common/sanitizer_common_interceptors_memintrinsics.inc"

// The following MACROs are moved from tsan_interceptors.h

#define COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED \
  (!cur_thread_init()->is_inited)

#define COMMON_INTERCEPTOR_WRITE_RANGE(ctx, ptr, size)                    \
  MemoryAccessRange(((TsanInterceptorContext *)ctx)->thr,                 \
                    ((TsanInterceptorContext *)ctx)->pc, (uptr)ptr, size, \
                    true)

#define COMMON_INTERCEPTOR_READ_RANGE(ctx, ptr, size)                     \
  MemoryAccessRange(((TsanInterceptorContext *)ctx)->thr,                 \
                    ((TsanInterceptorContext *)ctx)->pc, (uptr)ptr, size, \
                    false)

#define COMMON_INTERCEPTOR_ENTER(ctx, func, ...) \
  SCOPED_TSAN_INTERCEPTOR(func, __VA_ARGS__);    \
  TsanInterceptorContext _ctx = {thr, pc};       \
  ctx = (void *)&_ctx;                           \
  (void)ctx;

#define COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, v, size) \
  {                                                       \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)        \
      return internal_memset(dst, v, size);               \
    COMMON_INTERCEPTOR_ENTER(ctx, memset, dst, v, size);  \
    if (common_flags()->intercept_intrin)                 \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);     \
    return REAL(memset)(dst, v, size);                    \
  }

#define COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size) \
  {                                                          \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED)           \
      return internal_memmove(dst, src, size);               \
    COMMON_INTERCEPTOR_ENTER(ctx, memmove, dst, src, size);  \
    if (common_flags()->intercept_intrin) {                  \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);        \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, src, size);         \
    }                                                        \
    return REAL(memmove)(dst, src, size);                    \
  }

#define COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size) \
  {                                                         \
    if (COMMON_INTERCEPTOR_NOTHING_IS_INITIALIZED) {        \
      return internal_memmove(dst, src, size);              \
    }                                                       \
    COMMON_INTERCEPTOR_ENTER(ctx, memcpy, dst, src, size);  \
    if (common_flags()->intercept_intrin) {                 \
      COMMON_INTERCEPTOR_WRITE_RANGE(ctx, dst, size);       \
      COMMON_INTERCEPTOR_READ_RANGE(ctx, src, size);        \
    }                                                       \
    return REAL(memcpy)(dst, src, size);                    \
  }

DECLARE_REAL(void *, memset, void *dst, int c, uptr size);
DECLARE_REAL(void *, memmove, void *dst, const void *src, uptr size);
DECLARE_REAL(void *, memcpy, void *dst, const void *src, uptr size);

extern "C" {

void *__tsan_memcpy(void *dst, const void *src, uptr size) {
  void *ctx;
#if PLATFORM_HAS_DIFFERENT_MEMCPY_AND_MEMMOVE
  COMMON_INTERCEPTOR_MEMCPY_IMPL(ctx, dst, src, size);
#else
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
#endif
}

void *__tsan_memset(void *dst, int c, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMSET_IMPL(ctx, dst, c, size);
}

void *__tsan_memmove(void *dst, const void *src, uptr size) {
  void *ctx;
  COMMON_INTERCEPTOR_MEMMOVE_IMPL(ctx, dst, src, size);
}

}  // extern "C"
