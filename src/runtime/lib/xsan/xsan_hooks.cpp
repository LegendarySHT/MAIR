//===-- xsan_hooks.cpp ---------------------------------------------------===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is used to manuplate the hooks for coordination between multiple
// sanitizers.
//===----------------------------------------------------------------------===//

#include "xsan_hooks.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

#include "asan/asan_thread.h"
#include "asan/orig/asan_internal.h"
#include "lsan/lsan_common.h"
#include "xsan_hooks_dispatch.h"
#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_thread.h"

#if XSAN_CONTAINS_TSAN
#include "tsan/tsan_rtl.h"
#endif

using namespace __xsan;
// ---------------------- State/Ignoration Management Hooks --------------------
namespace __xsan {

THREADLOCAL int xsan_in_intenal = 0;
THREADLOCAL int xsan_in_calloc = 0;
THREADLOCAL int is_in_symbolizer;

int get_exit_code(const void *ctx) {
  int exit_code = 0;
  if (CAN_SANITIZE_LEAKS && common_flags()->detect_leaks &&
      __lsan::HasReportedLeaks()) {
    return common_flags()->exitcode;
  }
#if XSAN_CONTAINS_TSAN
  auto *tsan_thr =
      ctx == nullptr
          ? __tsan::cur_thread()
          : ((const XsanInterceptorContext *)ctx)->xsan_ctx.tsan.thr_;
  exit_code = __tsan::Finalize(tsan_thr);
#endif
  return exit_code;
}

}  // namespace __xsan

// ---------- End of State Management Hooks -----------------

// ---------------------- Memory Management Hooks -------------------
/// As XSan uses ASan's heap allocator and fake stack directly, hence we don't
/// need to invoke ASan's hooks here.

namespace __asan {

SANITIZER_WEAK_ATTRIBUTE
bool GetASanMellocStackTrace(u32 &stack_trace_id, uptr addr,
                             bool set_stack_trace_id) {
  return false;
}

}  // namespace __asan

namespace __xsan {

bool GetMellocStackTrace(u32 &stack_trace_id, uptr addr,
                         bool set_stack_trace_id) {
  return __asan::GetASanMellocStackTrace(stack_trace_id, addr,
                                         set_stack_trace_id);
}

}  // namespace __xsan

// ---------- End of Memory Management Hooks -------------------

// ---------------------- Special Function Hooks -----------------
namespace __xsan {
/*
Provides hooks for special functions, such as
  - atexit / on_exit / _cxa_atexit
  - longjmp / siglongjmp / _longjmp / _siglongjmp
  - dlopen / dlclose
  - vfork / fork
*/

/// To implement macro COMMON_INTERCEPTOR_SPILL_AREA in *vfork.S
/// Notably, this function is called TWICE at the attitude per process.
extern "C" void *__xsan_vfork_before_and_after() {
  void *final_result = nullptr;
  /// Invoked TRIPLE totally, once before vfork to store the sp, twice after
  /// vfork child/parent to restore the sp.
  XSAN_HOOKS_EXEC_NEQ(final_result, nullptr, vfork_before_and_after);
  return final_result;
}

extern "C" void __xsan_vfork_parent_after(void *sp) {
  /// Unpoison vfork child's new stack space : [stack_bottom, sp]
  XSAN_HOOKS_EXEC(vfork_parent_after, sp);
}
// Ignore interceptors in OnLibraryLoaded()/Unloaded().  These hooks use code
// (ListOfModules::init, MemoryMappingLayout::DumpListOfModules) that make
// intercepted calls, which can cause deadlockes with ReportRace() which also
// uses this code.
void OnLibraryLoaded(const char *filename, void *handle) {
  __xsan::ScopedIgnoreInterceptors ignore;
  XSAN_HOOKS_EXEC(OnLibraryLoaded, filename, handle);
}

void OnLibraryUnloaded() {
  __xsan::ScopedIgnoreInterceptors ignore;
  XSAN_HOOKS_EXEC(OnLibraryUnloaded);
}
}  // namespace __xsan

// --------------- End of Special Function Hooks -----------------
