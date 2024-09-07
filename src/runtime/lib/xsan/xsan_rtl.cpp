#include "xsan_activation.h"
#include "xsan_interface_internal.h"

namespace __xsan {

// -------------------------- Globals --------------------- {{{1
int xsan_inited;
bool xsan_init_is_running;

#if !ASAN_FIXED_MAPPING
uptr kHighMemEnd, kMidMemBeg, kMidMemEnd;
#endif

// -------------------------- Run-time entry ------------------- {{{1

// Initialize as requested from some part of ASan runtime library (interceptors,
// allocator, etc).
void AsanInitFromRtl() {
}



}  // namespace __xsan

// ---------------------- Interface ---------------- {{{1
using namespace __xsan;

void NOINLINE __xsan_handle_no_return() {
  if (xsan_init_is_running)
    return;
  
  /// TODO: complete handle_no_return

}


// Initialize as requested from instrumented application code.
// We use this call as a trigger to wake up ASan from deactivated state.
void __xsan_init() {
  XsanActivate();
}