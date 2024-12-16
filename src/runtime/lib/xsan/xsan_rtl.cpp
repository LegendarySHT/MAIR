#include <lsan/lsan_common.h>
#include <sanitizer_common/sanitizer_interface_internal.h>
#include <sanitizer_common/sanitizer_common.h>
#include <ubsan/ubsan_init.h>

#include "asan/asan_init.h"
#include "tsan/tsan_init.h"
#include "xsan_activation.h"
#include "xsan_interceptors.h"
#include "xsan_interface_internal.h"
#include "xsan_internal.h"
#include "xsan_platform.h"
#include "xsan_stack.h"

namespace __xsan {

// With the zero shadow base we can not actually map pages starting from 0.
// This constant is somewhat arbitrary.
constexpr uptr ZeroBaseShadowStart = 0;
constexpr uptr ZeroBaseMaxShadowStart = 1 << 18;

// -------------------------- Globals --------------------- {{{1
int xsan_inited;
/// Indicate whether XSan's initialization is in progress.
/// Set to false before sub-santizers's initialization.
bool xsan_init_is_running;
/// Whether we are currently in XSan initialization.
/// Only set to false after all sub-santizers's initialization.
bool xsan_in_init;

#if !ASAN_FIXED_MAPPING
uptr kHighMemEnd, kMidMemBeg, kMidMemEnd;
#endif

static void CheckUnwind() {
  __xsan::ScopedIgnoreInterceptors sii(true);

  GET_STACK_TRACE(kStackTraceMax, common_flags()->fast_unwind_on_check);
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

static void XsanInitInternal() {
  if (LIKELY(xsan_inited))
    return;
  SanitizerToolName = "XSan";
  ScopedSanitizerToolName tool_name("XSan");
  CHECK(!xsan_init_is_running && "XSan init calls itself!");
  xsan_init_is_running = true;
  xsan_in_init = true;

  __tsan::TsanInitFromXsanEarly();
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
  CheckAndProtect();
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

  __asan::AsanInitFromXsanLate();

  __tsan::TsanInitFromXsanLate();


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
  xsan_in_init = false;
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