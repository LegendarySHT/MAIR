// For XSan to use MSan's API, while not introducing so many unrelated things.
#pragma once

#include "../xsan_platform.h"
#include "msan_chained_origin_depot.h"
#include "msan_flags.h"
#include "msan_interface_internal.h"
#include "msan_report.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_stacktrace.h"

extern "C" {

void __msan_unaligned_load2(uptr p);
void __msan_unaligned_load4(uptr p);
void __msan_unaligned_load8(uptr p);
void __msan_unaligned_store2(uptr p);
void __msan_unaligned_store4(uptr p);
void __msan_unaligned_store8(uptr p);

}  // extern "C"

namespace __msan {

using ::__sanitizer::StackTrace;
using ::__sanitizer::uptr;

#define MSAN_MEM_TO_SHADOW(p) ((p) ^ MAP_FIELD(kMSanShadowXor))
#define MSAN_SHADOW_TO_ORIGIN(p) ((p) + MAP_FIELD(kMSanShadowAdd))
#define MSAN_MEM_TO_ORIGIN(p) MSAN_SHADOW_TO_ORIGIN(MSAN_MEM_TO_SHADOW(p))

#define MSAN_CVT_FUNC(func, cvt)                              \
  XSAN_MAP_FUNC(uptr, func, (uptr p), (p)) { return cvt(p); } \
  template <typename T>                                       \
  ALWAYS_INLINE uptr func(T p) {                              \
    return func((uptr)p);                                     \
  }

#define MSAN_MAP_ORIGIN_FUNC(func, field)                  \
  XSAN_MAP_FUNC_VOID(uptr, func) {                         \
    return MSAN_SHADOW_TO_ORIGIN(MAP_FIELD(kMSan##field)); \
  }

XSAN_MAP_FIELD_FUNC(LoShadowBeg, kMSanLoShadowBeg)
XSAN_MAP_FIELD_FUNC(LoShadowEnd, kMSanLoShadowEnd)
XSAN_MAP_FIELD_FUNC(MidShadowBeg, kMSanMidShadowBeg)
XSAN_MAP_FIELD_FUNC(MidShadowEnd, kMSanMidShadowEnd)
XSAN_MAP_FIELD_FUNC(HiShadowBeg, kMSanHiShadowBeg)
XSAN_MAP_FIELD_FUNC(HiShadowEnd, kMSanHiShadowEnd)
XSAN_MAP_FIELD_FUNC(HeapShadowBeg, kMSanHeapShadowBeg)
XSAN_MAP_FIELD_FUNC(HeapShadowEnd, kMSanHeapShadowEnd)

MSAN_CVT_FUNC(MemToShadow, MSAN_MEM_TO_SHADOW)
MSAN_CVT_FUNC(ShadowToOrigin, MSAN_SHADOW_TO_ORIGIN)
MSAN_CVT_FUNC(MemToOrigin, MSAN_MEM_TO_ORIGIN)

MSAN_MAP_ORIGIN_FUNC(LoOriginBeg, LoShadowBeg)
MSAN_MAP_ORIGIN_FUNC(LoOriginEnd, LoShadowEnd)
MSAN_MAP_ORIGIN_FUNC(MidOriginBeg, MidShadowBeg)
MSAN_MAP_ORIGIN_FUNC(MidOriginEnd, MidShadowEnd)
MSAN_MAP_ORIGIN_FUNC(HiOriginBeg, HiShadowBeg)
MSAN_MAP_ORIGIN_FUNC(HiOriginEnd, HiShadowEnd)
MSAN_MAP_ORIGIN_FUNC(HeapOriginBeg, HeapShadowBeg)
MSAN_MAP_ORIGIN_FUNC(HeapOriginEnd, HeapShadowEnd)

#undef MSAN_MAP_ORIGIN_FUNC
#undef MSAN_CVT_FUNC
#undef MSAN_MEM_TO_ORIGIN
#undef MSAN_SHADOW_TO_ORIGIN
#undef MSAN_MEM_TO_SHADOW

// ------------------ Check Functions --------------------

void CopyShadowAndOrigin(const void *dst, const void *src, uptr size,
                         StackTrace *stack);
void MoveShadowAndOrigin(const void *dst, const void *src, uptr size,
                         StackTrace *stack);
void PrintWarningWithOrigin(uptr pc, uptr bp, u32 origin);
void UnpoisonParam(uptr n);

// ------------------- Interceptor Scope -------------------

extern THREADLOCAL int in_interceptor_scope;

struct InterceptorScope {
  ALWAYS_INLINE InterceptorScope() { ++in_interceptor_scope; }
  ALWAYS_INLINE ~InterceptorScope() { --in_interceptor_scope; }
};

ALWAYS_INLINE bool IsInInterceptorScope() { return in_interceptor_scope; }

// ------------------ Symbolizer and Unwinder ---------------

extern THREADLOCAL int is_in_symbolizer_or_unwinder;
ALWAYS_INLINE void EnterSymbolizerOrUnwider() {
  ++is_in_symbolizer_or_unwinder;
}
ALWAYS_INLINE void ExitSymbolizerOrUnwider() { --is_in_symbolizer_or_unwinder; }
ALWAYS_INLINE bool IsInSymbolizerOrUnwider() {
  return is_in_symbolizer_or_unwinder;
}

// ------------------ Signal Handler Scopers ----------------

class ScopedThreadLocalStateBackup {
 public:
  ScopedThreadLocalStateBackup() { Backup(); }
  ~ScopedThreadLocalStateBackup() { Restore(); }
  void Backup();
  void Restore();

 private:
  u64 va_arg_overflow_size_tls;
};

// ------------------ Init Msan --------------------

extern bool msan_init_is_running;
extern int msan_inited;
void InitializeFlags();
void InitializeInterceptors();
void MsanInitFromXsan();

// ------------------ At Exit --------------------
void MsanAtExit();

}  // namespace __msan
