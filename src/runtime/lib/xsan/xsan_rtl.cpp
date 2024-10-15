#include <lsan/lsan_common.h>
#include <sanitizer_common/sanitizer_interface_internal.h>
#include <ubsan/ubsan_init.h>

#include "asan/asan_init.h"
#include "tsan/tsan_init.h"
#include "xsan_activation.h"
#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_stack.h"

namespace __xsan {

// -------------------------- Globals --------------------- {{{1
int xsan_inited;
bool xsan_init_is_running;

#if !ASAN_FIXED_MAPPING
uptr kHighMemEnd, kMidMemBeg, kMidMemEnd;
#endif

static void CheckUnwind() {
  // --------------- TSan's logic -------------------------
  __tsan::ScopedIgnoreInterceptors ignore;
#if !SANITIZER_GO
  __tsan::ThreadState* thr = __tsan::cur_thread();
  thr->nomalloc = false;
  thr->ignore_sync++;
  thr->ignore_reads_and_writes++;
  atomic_store_relaxed(&thr->in_signal_handler, 0);
#endif

  GET_STACK_TRACE(kStackTraceMax, common_flags()->fast_unwind_on_check);
  stack.Print();
}

// -------------------------- Run-time entry ------------------- {{{1

static void XsanInitInternal() {
  if (LIKELY(xsan_inited))
    return;
  SanitizerToolName = "XSan";
  CHECK(!xsan_init_is_running && "XSan init calls itself!");
  xsan_init_is_running = true;

  // Install tool-specific callbacks in sanitizer_common.
  // Combine ASan's and TSan's logic
  SetCheckUnwindCallback(CheckUnwind);

  CacheBinaryName();
  CheckASLR();

  InitializeFlags();

  AvoidCVE_2016_2143();
  __sanitizer::InitializePlatformEarly();

  // Setup correct file descriptor for error reports.
  __sanitizer_set_report_path(common_flags()->log_path);

#if !SANITIZER_GO
  // InitializeAllocator();
  /// TODO: For Android, move it to Xsan initialization
  ReplaceSystemMalloc();
#endif

  InitializeXsanInterceptors();

  XsanTSDInit(XsanTSDDtor);

  // We need to initialize ASan before xsan::InitializeMainThread() because
  // the latter call asan::GetCurrentThread to get the main thread of ASan.
  __asan::AsanInitFromXsan();

  __tsan::TsanInitFromXsan();

  /// TODO: figure out whether we need to replace the callback with XSan's
  InstallDeadlySignalHandlers(__asan::AsanOnDeadlySignal);

  // On Linux AsanThread::ThreadStart() calls malloc() that's why xsan_inited
  // should be set to 1 prior to initializing the threads.
  xsan_inited = 1;
  xsan_init_is_running = false;

  // This function call interceptors, so it should be called after,
  // waiting for __asan::AsanTSDInit() finished.
  // Fix: InitTlsSize should be initialized before MainThread initialization.
  InitTlsSize();

  // Initialize main thread after __asan::AsanInitFromXsan() because it
  // Because we need to wait __asan::AsanTSDInit() to be called.
  InitializeMainThread();

  /// TODO: this is registed in ASan's initialization
  // InstallAtExitCheckLeaks();

  InitializeCoverage(common_flags()->coverage, common_flags()->coverage_dir);

#if TSAN_CONTAINS_UBSAN
  __ubsan::InitAsPlugin();
#endif

  /// Initialize Symbolizer in the last
  if (CAN_SANITIZE_LEAKS) {
    // LateInitialize() calls dlsym, which can allocate an error string buffer
    // in the TLS.  Let's ignore the allocation to avoid reporting a leak.
    __lsan::ScopedInterceptorDisabler disabler;
    Symbolizer::LateInitialize();
  } else {
    Symbolizer::LateInitialize();
  }
}

// Initialize as requested from some part of ASan runtime library (interceptors,
// allocator, etc).
void XsanInitFromRtl() { XsanInitInternal(); }

}  // namespace __xsan

// ---------------------- Interface ---------------- {{{1
using namespace __xsan;

void NOINLINE __xsan_handle_no_return() {
  if (xsan_init_is_running)
    return;

  /// TODO: complete handle_no_return
  __asan_handle_no_return();
}

// Initialize as requested from instrumented application code.
// We use this call as a trigger to wake up ASan from deactivated state.
void __xsan_init() {
  XsanActivate();
  XsanInitInternal();
}