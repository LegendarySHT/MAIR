#include <lsan/lsan_common.h>
#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_interface_internal.h>
#include <ubsan/ubsan_init.h>

#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "xsan_activation.h"
#include "xsan_hooks.h"
#include "xsan_hooks.h"
#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_platform.h"
#include "xsan_stack.h"
#include "xsan_allocator.h"

#define INIT_ONCE           \
  static bool done = false; \
  if (done)                 \
    return;                 \
  done = true;

namespace __xsan {

// -------------------------- Globals --------------------- {{{1
static StaticSpinMutex xsan_inited_mutex;
static atomic_uint8_t xsan_inited = {0};

// Originally, __asan_init initialized but didn't activate on the first
// call, while activated but didn't initialize on the other calls.
// But __xsan_init may have been called many times, such as called from
// interceptors. So we can't use xsan_inited to determine whether performing
// initialization or activation.
static atomic_uint8_t xsan_asan_inited = {0};

/// Whether we are currently in XSan initialization.
/// Only set to false after all sub-santizers's initialization.
bool xsan_in_init;
bool replace_intrin_cached;
bool is_heap_init = false;
uptr vmaSize;

#if !ASAN_FIXED_MAPPING
uptr kHighMemEnd, kMidMemBeg, kMidMemEnd;
#endif

static void SetXsanInited() {
  atomic_store(&xsan_inited, 1, memory_order_release);
}

bool XsanInited() {
  return atomic_load(&xsan_inited, memory_order_acquire) == 1;
}

// -------------------------- Functions --------------------------

static void CheckUnwind() {
  __xsan::ScopedIgnoreInterceptors sii(true);

  UNINITIALIZED BufferedStackTrace stack;
  GetStackTrace(stack, kStackTraceMax, common_flags()->fast_unwind_on_check);
  stack.Print();
}

/// Implemented in ASan RTL. Currently not used.
/// TODO: Use XSan to initialize the allocator.
/// @return Whether the heap is initialized.
bool InitializeAllocator();

// -------------------------- Run-time entry ------------------- {{{1
/* The sanitizer initialization sequence is as follows (those marked with * are
   sub-santizers can interact with XSan):
   1. *Very early initialization : initialize some necessary globals.
      - e.g., TSan's `ctx` and `thr`
   2. Early common initialization
   3. *Flags initialization: parse sanitizers' flags, set up common flags.
   4. *Early initialization: initialize sub-santizers' before major
   initialization.
      - this happens before memory/allocator/threads initialization.
      - e.g., TSan's InitializePlatformEarly() can perform in this step, as it
   checks memory mapping.
   5. Some common initializations
   6. *Major initializations: sub-sanitizers initialize their shaodw/state here.
      - e.g., AsanInitFromXsan / TsanInitFromXsan
   7. Some common initializations
   8. Thread initialization: XsanThread delegates the main thread initialization
      to sub-santizers.
   9. *Late initialization: sub-santizers initialize their state after major
      initialization, i.e., after almost everything is set up.
  10. The final common initializations/
*/

static bool XsanInitInternal() {
  if (LIKELY(XsanInited()))
    return true;
  SanitizerToolName = "XSan";
  ScopedSanitizerToolName tool_name("XSan");
  ScopedXsanInternal scoped_xsan_internal;
  xsan_in_init = true;

  XsanCheckDynamicRTPrereqs();
  InitFromXsanVeryEarly();
  /// note that place this after cur_thread_init()
  __xsan::ScopedIgnoreInterceptors ignore;

  // Install tool-specific callbacks in sanitizer_common.
  // Combine ASan's and TSan's logic
  SetCheckUnwindCallback(CheckUnwind);

  CacheBinaryName();
  CheckASLR();
  InitializeFlags();
  AvoidCVE_2016_2143();
  __sanitizer::InitializePlatformEarly();
  /// Core: if memory mappings does not fit xsan_platform.h, ReExec() is called.
  InitializePlatformEarly();
  __xsan::InitFromXsanEarly();

  // Stop performing init at this point if we are being loaded via
  // dlopen() and the platform supports it.
  if (SANITIZER_SUPPORTS_INIT_FOR_DLOPEN && UNLIKELY(HandleDlopenInit())) {
    VReport(1, "AddressSanitizer init is being performed for dlopen().\n");
    return false;
  }

  // Make sure we are not statically linked.
  __interception::DoesNotSupportStaticLinking();

#if !SANITIZER_GO
  /// TODO: For Android, move it to Xsan initialization
  ReplaceSystemMalloc();
#endif

  InitializeXsanInterceptors();

  // Setup correct file descriptor for error reports.
  __sanitizer_set_report_path(common_flags()->log_path);

  XsanTSDInit(XsanTSDDtor);

  /// TODO: might be too early to call this
  // See
  // https://github.com/llvm/llvm-project/commit/bbb90feb8742b4a83c4bbfbbbdf0f9735939d184
  /// Core: if memory mappings does not fit xsan_platform.h, ReExec() is called.
  InitializePlatform();

  // We need to initialize ASan before xsan::InitializeMainThread() because
  // the latter call asan::GetCurrentThread to get the main thread of ASan.
  __xsan::InitFromXsan();

  // is_heap_init = InitializeAllocator();

  /// TODO: please fix me
  // The InitializePlatform() should be called after allocator/sub-sanitizers'
  // initialization.
  // See
  // https://github.com/llvm/llvm-project/commit/bbb90feb8742b4a83c4bbfbbbdf0f9735939d184
  // However, it crashes as the non-app regions allocated by sanitizers.
  // Core: if memory mappings does not fit xsan_platform.h, ReExec() is called.
  // InitializePlatform();

  /// TODO: figure out whether we need to replace the callback with XSan's
  InstallDeadlySignalHandlers(__asan::AsanOnDeadlySignal);

  // On Linux AsanThread::ThreadStart() calls malloc() that's why XsanInited()
  // should be set to 1 prior to initializing the threads.
  SetXsanInited();
  replace_intrin_cached = flags()->replace_intrin;

  // This function call interceptors, so it should be called after,
  // waiting for __asan::AsanTSDInit() finished.
  // Fix: InitTlsSize should be initialized before MainThread initialization.
  /// FIXME: this has been moved to InitializePlatformEarly, is it Okay?
  // InitTlsSize();

  // Initialize main thread after __asan::AsanInitFromXsan() because it
  // Because we need to wait __asan::AsanTSDInit() to be called.
  InitializeMainThread();

  __xsan::InitFromXsanLate();

  InitializeCoverage(common_flags()->coverage, common_flags()->coverage_dir);

  InstallAtForkHandler();

#if CAN_SANITIZE_UB && !SANITIZER_GO
  __ubsan::InitAsPlugin();
#endif

  Symbolizer::GetOrInit()->AddHooks(EnterSymbolizer, ExitSymbolizer);

  /// Initialize Symbolizer in the last
  if (CAN_SANITIZE_LEAKS) {
    // LateInitialize() calls dlsym, which can allocate an error string buffer
    // in the TLS.  Let's ignore the allocation to avoid reporting a leak.
    __lsan::ScopedInterceptorDisabler disabler;
    Symbolizer::LateInitialize();
  } else {
    Symbolizer::LateInitialize();
  }
  xsan_in_init = false;

  return true;
}

// Initialize as requested from some part of ASan runtime library (interceptors,
// allocator, etc).
void XsanInitFromRtl() { 
  if (LIKELY(XsanInited()))
    return;
  SpinMutexLock lock(&xsan_inited_mutex);
  XsanInitInternal(); 
}


bool TryXsanInitFromRtl() {
  if (UNLIKELY(XsanInited())) {
    return true;
  }
  if (!xsan_inited_mutex.TryLock()) {
    return false;
  }

  bool result = XsanInitInternal();
  xsan_inited_mutex.Unlock();
  return result;
}

}  // namespace __xsan

// ---------------------- Interface ---------------- {{{1
using namespace __xsan;

// void NOINLINE __xsan_handle_no_return() {
//   if (UNLIKELY(!XsanInited()))
//     return;

//   __asan_handle_no_return();
// }

// Initialize as requested from instrumented application code.
void __xsan_init() { XsanInitFromRtl(); }

// __asan_init has different semantics.
// We use this call as a trigger to wake up ASan from deactivated state.
void __xsan_asan_init() {
  // See details about this 'if' in the comment of xsan_asan_inited.
  if (!atomic_exchange(&xsan_asan_inited, 1, memory_order_acq_rel))
    XsanActivate();  // This isn't the first call, so we can activate XSan.
  XsanInitFromRtl();
}
