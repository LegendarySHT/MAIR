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

template <typename T>
ALWAYS_INLINE uptr MemToShadow(T p) {
  return ((uptr)p ^ ::__xsan::Mapping48AddressSpace::kMSanShadowXor);
}
template <typename T>
ALWAYS_INLINE uptr ShadowToOrigin(T p) {
  return ((uptr)p + ::__xsan::Mapping48AddressSpace::kMSanShadowAdd);
}
template <typename T>
ALWAYS_INLINE uptr MemToOrigin(T p) {
  return ShadowToOrigin(MemToShadow(p));
}

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