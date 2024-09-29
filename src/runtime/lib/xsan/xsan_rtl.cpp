#include "xsan_activation.h"
#include "xsan_interface_internal.h"
#include "xsan_interceptors.h"
#include "xsan_internal.h"
#include "asan/asan_init.h"
namespace __xsan {

// -------------------------- Globals --------------------- {{{1
int xsan_inited;
bool xsan_init_is_running;

#if !ASAN_FIXED_MAPPING
uptr kHighMemEnd, kMidMemBeg, kMidMemEnd;
#endif

// -------------------------- Run-time entry ------------------- {{{1

static void XsanInitInternal() {
  if (LIKELY(xsan_inited)) return;
  SanitizerToolName = "XSan";
  CHECK(!xsan_init_is_running && "XSan init calls itself!");
  xsan_init_is_running = true;

  CacheBinaryName();

  InitializeFlags();
  InitializeXsanInterceptors();

  XsanTSDInit(XsanTSDDtor);

  /// TODO: figure out whether we need to replace the callback with XSan's
  InstallDeadlySignalHandlers(__asan::AsanOnDeadlySignal);

  // We need to initialize ASan before xsan::InitializeMainThread() because
  // the latter call asan::GetCurrentThread to get the main thread of ASan.
  __asan::AsanInitFromXsan();

  // On Linux AsanThread::ThreadStart() calls malloc() that's why xsan_inited
  // should be set to 1 prior to initializing the threads.
  xsan_inited = 1;
  xsan_init_is_running = false;

  InitTlsSize();
  InitializeMainThread();

  /// TODO: this is registed in ASan's initialization
  // InstallAtExitCheckLeaks();

  InitializeCoverage(common_flags()->coverage, common_flags()->coverage_dir);
}

// Initialize as requested from some part of ASan runtime library (interceptors,
// allocator, etc).
void XsanInitFromRtl() {
  XsanInitInternal();
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
  XsanInitInternal();
}