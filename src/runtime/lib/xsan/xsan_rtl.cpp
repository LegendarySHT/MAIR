#include <lsan/lsan_common.h>
#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_interface_internal.h>
#include <ubsan/ubsan_init.h>

#include "asan/asan_init.h"
#include "sanitizer_common/sanitizer_atomic.h"
#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_mutex.h"
#include "tsan/tsan_init.h"
#include "xsan_activation.h"
#include "xsan_hooks.h"
#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_platform.h"
#include "xsan_stack.h"

#define INIT_ONCE           \
  static bool done = false; \
  if (done)                 \
    return;                 \
  done = true;

namespace __xsan {

// With the zero shadow base we can not actually map pages starting from 0.
// This constant is somewhat arbitrary.
constexpr uptr ZeroBaseShadowStart = 0;
constexpr uptr ZeroBaseMaxShadowStart = 1 << 18;

// -------------------------- Globals --------------------- {{{1
static StaticSpinMutex xsan_inited_mutex;
static atomic_uint8_t xsan_inited = {0};

/// Whether we are currently in XSan initialization.
/// Only set to false after all sub-santizers's initialization.
bool xsan_in_init;
bool replace_intrin_cached;

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

uptr ALWAYS_INLINE HeapEnd() {
  return HeapMemEnd() + PrimaryAllocator::AdditionalSize();
}

static void ProtectRange(uptr beg, uptr end) { 
  if (beg == end) return;
  ProtectGap(beg, end - beg, ZeroBaseShadowStart, ZeroBaseMaxShadowStart);
}

void CheckAndProtect() {
  // Ensure that the binary is indeed compiled with -pie.
  MemoryMappingLayout proc_maps(true);
  MemoryMappedSegment segment;
  while (proc_maps.Next(&segment)) {
    if (IsAppMem(segment.start))
      continue;
    if (segment.start >= HeapMemEnd() && segment.start < HeapEnd())
      continue;
    if (__xsan::IsSanitizerShadowMem(segment.start))
      continue;
    if (segment.protection == 0)  // Zero page or mprotected.
      continue;
    if (segment.start >= VdsoBeg())  // vdso
      break;
    Printf("FATAL: XSan: unexpected memory mapping 0x%zx-0x%zx\n",
           segment.start, segment.end);
    Die();
  }

#    if SANITIZER_IOS && !SANITIZER_IOSSIM
  ProtectRange(HeapMemEnd(), TsanShadowBeg());
  ProtectRange(TsanShadowEnd(), TsanMetaShadowBeg());
  ProtectRange(TsanMetaShadowEnd(), HiAppMemBeg());
#    else
  /// TODO: migrate GAP caculation in xsn_platform.h
  ProtectRange(LoAppMemEnd(), AsanLowShadowBeg());
  /// Protected in asan::InitializeShadowMemory  
  // ProtectRange(AsanLowShadowEnd(), AsanHighShadowBeg());
  ProtectRange(AsanHighShadowEnd(), TsanShadowBeg());
  if (MidAppMemBeg()) {
    //Printf("Protecting range: start = 0x%lx, end = 0x%lx\n", TsanMetaShadowEnd(), MidAppMemBeg());
    ProtectRange(TsanMetaShadowEnd(), MidAppMemBeg());
    ProtectRange(MidAppMemEnd(), HeapMemBeg());
  } else {
    ProtectRange(TsanMetaShadowEnd(), HeapMemBeg());
  }
  ProtectRange(HeapEnd(), HiAppMemBeg());
#    endif

#    if defined(__s390x__)
  // Protect the rest of the address space.
  const uptr user_addr_max_l4 = 0x0020000000000000ull;
  const uptr user_addr_max_l5 = 0xfffffffffffff000ull;
  // All the maintained s390x kernels support at least 4-level page tables.
  ProtectRange(HiAppMemEnd(), user_addr_max_l4);
  // Older s390x kernels may not support 5-level page tables.
  TryProtectRange(user_addr_max_l4, user_addr_max_l5);
#    endif
}

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

/// Before any other initialization.
/// Used to initialize state of sub-santizers, e.g., Context of TSan.
static void XsanInitVeryEarly() {
  INIT_ONCE
  __tsan::TsanInitFromXsanVeryEarly();
}

/// After flags initialization, before any other initialization.
static void XsanInitEarly() {
  INIT_ONCE
  __tsan::InitializePlatformEarly();
}

/// Almost after all is done, e.g., flags, memory, allocator, threads, etc.
static void XsanInitLate() {
  INIT_ONCE
  {
    ScopedSanitizerToolName tool_name("AddressSanitizer");
    __asan::AsanInitFromXsanLate();
  }
  {
    ScopedSanitizerToolName tool_name("ThreadSanitizer");
    __tsan::TsanInitFromXsanLate();
  }
}

static bool XsanInitInternal() {
  if (LIKELY(XsanInited()))
    return true;
  SanitizerToolName = "XSan";
  ScopedSanitizerToolName tool_name("XSan");
  xsan_in_init = true;

  XsanCheckDynamicRTPrereqs();
  XsanInitVeryEarly();
  /// note that place this after cur_thread_init()
  __xsan::ScopedIgnoreInterceptors ignore;

  // Install tool-specific callbacks in sanitizer_common.
  // Combine ASan's and TSan's logic
  SetCheckUnwindCallback(CheckUnwind);

  CacheBinaryName();
  CheckASLR();

  InitializeFlags();

  __sanitizer::InitializePlatformEarly();
  XsanInitEarly();

  // Stop performing init at this point if we are being loaded via
  // dlopen() and the platform supports it.
  if (SANITIZER_SUPPORTS_INIT_FOR_DLOPEN && UNLIKELY(HandleDlopenInit())) {
    VReport(1, "AddressSanitizer init is being performed for dlopen().\n");
    return false;
  }

  // Make sure we are not statically linked.
  __interception::DoesNotSupportStaticLinking();

  AvoidCVE_2016_2143();

  // Setup correct file descriptor for error reports.
  __sanitizer_set_report_path(common_flags()->log_path);

#if !SANITIZER_GO
  // InitializeAllocator();
  /// TODO: For Android, move it to Xsan initialization
  ReplaceSystemMalloc();
#endif

  DisableCoreDumperIfNecessary();

  InitializeXsanInterceptors();

  XsanTSDInit(XsanTSDDtor);

  // We need to initialize ASan before xsan::InitializeMainThread() because
  // the latter call asan::GetCurrentThread to get the main thread of ASan.
  CheckAndProtect();
  {
    ScopedSanitizerToolName tool_name("AddressSanitizer");
    __asan::AsanInitFromXsan();
  }
  {
    ScopedSanitizerToolName tool_name("ThreadSanitizer");
    __tsan::TsanInitFromXsan();
  }

 

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

  XsanInitLate();

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

void NOINLINE __xsan_handle_no_return() {
  if (UNLIKELY(!XsanInited()))
    return;

  __asan_handle_no_return();
}

// Initialize as requested from instrumented application code.
// We use this call as a trigger to wake up ASan from deactivated state.
void __xsan_init() {
  XsanActivate();
  XsanInitFromRtl();
}