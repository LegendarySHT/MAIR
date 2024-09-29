#include "asan_init.h"
#include "asan_activation.h"
#include "asan_allocator.h"
#include "asan_fake_stack.h"
#include "asan_interceptors.h"
#include "asan_interface_internal.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "asan_poisoning.h"
#include "asan_report.h"
#include "asan_stack.h"
#include "asan_stats.h"
#include "asan_suppressions.h"
#include "asan_thread.h"
#include "lsan/lsan_common.h"
#include <sanitizer_common/sanitizer_atomic.h>
#include <sanitizer_common/sanitizer_flags.h>
#include <sanitizer_common/sanitizer_interface_internal.h>
#include <sanitizer_common/sanitizer_libc.h>
#include <sanitizer_common/sanitizer_symbolizer.h>
#include "ubsan/ubsan_init.h"
#include "ubsan/ubsan_platform.h"


namespace __asan {

// --------------- LowLevelAllocateCallbac ---------- {{{1
static void OnLowLevelAllocate(uptr ptr, uptr size) {
  PoisonShadow(ptr, size, kAsanInternalHeapMagic);
}

static void InitializeHighMemEnd() {
#if !ASAN_FIXED_MAPPING
  kHighMemEnd = GetMaxUserVirtualAddress();
  // Increase kHighMemEnd to make sure it's properly
  // aligned together with kHighMemBeg:
  kHighMemEnd |= (GetMmapGranularity() << ASAN_SHADOW_SCALE) - 1;
#endif  // !ASAN_FIXED_MAPPING
  CHECK_EQ((kHighMemBeg % GetMmapGranularity()), 0);
}

static void AsanDie() {
  static atomic_uint32_t num_calls;
  if (atomic_fetch_add(&num_calls, 1, memory_order_relaxed) != 0) {
    // Don't die twice - run a busy loop.
    while (1) {
      internal_sched_yield();
    }
  }
  if (common_flags()->print_module_map >= 1)
    DumpProcessMap();

  WaitForDebugger(flags()->sleep_before_dying, "before dying");

  if (flags()->unmap_shadow_on_exit) {
    if (kMidMemBeg) {
      UnmapOrDie((void*)kLowShadowBeg, kMidMemBeg - kLowShadowBeg);
      UnmapOrDie((void*)kMidMemEnd, kHighShadowEnd - kMidMemEnd);
    } else {
      if (kHighShadowEnd)
        UnmapOrDie((void*)kLowShadowBeg, kHighShadowEnd - kLowShadowBeg);
    }
  }
}


static void CheckUnwind() {
  GET_STACK_TRACE(kStackTraceMax, common_flags()->fast_unwind_on_check);
  stack.Print();
}

static void asan_atexit() {
  Printf("AddressSanitizer exit stats:\n");
  __asan_print_accumulated_stats();
  // Print AsanMappingProfile.
  for (uptr i = 0; i < kAsanMappingProfileSize; i++) {
    if (AsanMappingProfile[i] == 0) continue;
    Printf("asan_mapping.h:%zd -- %zd\n", i, AsanMappingProfile[i]);
  }
}

// exported functions
#define ASAN_REPORT_ERROR(type, is_write, size)                     \
extern "C" NOINLINE INTERFACE_ATTRIBUTE                             \
void __asan_report_ ## type ## size(uptr addr);                     \
extern "C" NOINLINE INTERFACE_ATTRIBUTE                             \
void __asan_report_exp_ ## type ## size(uptr addr, u32 exp);        \
extern "C" NOINLINE INTERFACE_ATTRIBUTE                             \
void __asan_report_ ## type ## size ## _noabort(uptr addr);                                                 \

ASAN_REPORT_ERROR(load, false, 1)
ASAN_REPORT_ERROR(load, false, 2)
ASAN_REPORT_ERROR(load, false, 4)
ASAN_REPORT_ERROR(load, false, 8)
ASAN_REPORT_ERROR(load, false, 16)
ASAN_REPORT_ERROR(store, true, 1)
ASAN_REPORT_ERROR(store, true, 2)
ASAN_REPORT_ERROR(store, true, 4)
ASAN_REPORT_ERROR(store, true, 8)
ASAN_REPORT_ERROR(store, true, 16)

#define ASAN_REPORT_ERROR_N(type, is_write)                                 \
extern "C" NOINLINE INTERFACE_ATTRIBUTE                                     \
void __asan_report_ ## type ## _n(uptr addr, uptr size);                    \
extern "C" NOINLINE INTERFACE_ATTRIBUTE                                     \
void __asan_report_exp_ ## type ## _n(uptr addr, uptr size, u32 exp);       \
extern "C" NOINLINE INTERFACE_ATTRIBUTE                                     \
void __asan_report_ ## type ## _n_noabort(uptr addr, uptr size);                                                      \

ASAN_REPORT_ERROR_N(load, false)
ASAN_REPORT_ERROR_N(store, true)


#define ASAN_MEMORY_ACCESS_CALLBACK(type, is_write, size)                      \
  extern "C" NOINLINE INTERFACE_ATTRIBUTE                                      \
  void __asan_##type##size(uptr addr);                                         \
  extern "C" NOINLINE INTERFACE_ATTRIBUTE                                      \
  void __asan_exp_##type##size(uptr addr, u32 exp);                            \
  extern "C" NOINLINE INTERFACE_ATTRIBUTE                                      \
  void __asan_##type##size ## _noabort(uptr addr);

ASAN_MEMORY_ACCESS_CALLBACK(load, false, 1)
ASAN_MEMORY_ACCESS_CALLBACK(load, false, 2)
ASAN_MEMORY_ACCESS_CALLBACK(load, false, 4)
ASAN_MEMORY_ACCESS_CALLBACK(load, false, 8)
ASAN_MEMORY_ACCESS_CALLBACK(load, false, 16)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 1)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 2)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 4)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 8)
ASAN_MEMORY_ACCESS_CALLBACK(store, true, 16)

extern "C"
NOINLINE INTERFACE_ATTRIBUTE
void __asan_loadN(uptr addr, uptr size);
extern "C"
NOINLINE INTERFACE_ATTRIBUTE
void __asan_exp_loadN(uptr addr, uptr size, u32 exp);

extern "C"
NOINLINE INTERFACE_ATTRIBUTE
void __asan_loadN_noabort(uptr addr, uptr size);

extern "C"
NOINLINE INTERFACE_ATTRIBUTE
void __asan_storeN(uptr addr, uptr size);

extern "C"
NOINLINE INTERFACE_ATTRIBUTE
void __asan_exp_storeN(uptr addr, uptr size, u32 exp);

extern "C"
NOINLINE INTERFACE_ATTRIBUTE
void __asan_storeN_noabort(uptr addr, uptr size);

// Force the linker to keep the symbols for various ASan interface functions.
// We want to keep those in the executable in order to let the instrumented
// dynamic libraries access the symbol even if it is not used by the executable
// itself. This should help if the build system is removing dead code at link
// time.
static NOINLINE void force_interface_symbols() {
  volatile int fake_condition = 0;  // prevent dead condition elimination.
  // __asan_report_* functions are noreturn, so we need a switch to prevent
  // the compiler from removing any of them.
  // clang-format off
  switch (fake_condition) {
    case 1:  __asan_report_load1(0); break;
    case 2:  __asan_report_load2(0); break;
    case 3:  __asan_report_load4(0); break;
    case 4:  __asan_report_load8(0); break;
    case 5:  __asan_report_load16(0); break;
    case 6:  __asan_report_load_n(0, 0); break;
    case 7:  __asan_report_store1(0); break;
    case 8:  __asan_report_store2(0); break;
    case 9:  __asan_report_store4(0); break;
    case 10: __asan_report_store8(0); break;
    case 11: __asan_report_store16(0); break;
    case 12: __asan_report_store_n(0, 0); break;
    case 13: __asan_report_exp_load1(0, 0); break;
    case 14: __asan_report_exp_load2(0, 0); break;
    case 15: __asan_report_exp_load4(0, 0); break;
    case 16: __asan_report_exp_load8(0, 0); break;
    case 17: __asan_report_exp_load16(0, 0); break;
    case 18: __asan_report_exp_load_n(0, 0, 0); break;
    case 19: __asan_report_exp_store1(0, 0); break;
    case 20: __asan_report_exp_store2(0, 0); break;
    case 21: __asan_report_exp_store4(0, 0); break;
    case 22: __asan_report_exp_store8(0, 0); break;
    case 23: __asan_report_exp_store16(0, 0); break;
    case 24: __asan_report_exp_store_n(0, 0, 0); break;
    case 25: __asan_register_globals(nullptr, 0); break;
    case 26: __asan_unregister_globals(nullptr, 0); break;
    case 27: __asan_set_death_callback(nullptr); break;
    case 28: __asan_set_error_report_callback(nullptr); break;
    case 29: __asan_handle_no_return(); break;
    case 30: __asan_address_is_poisoned(nullptr); break;
    case 31: __asan_poison_memory_region(nullptr, 0); break;
    case 32: __asan_unpoison_memory_region(nullptr, 0); break;
    case 34: __asan_before_dynamic_init(nullptr); break;
    case 35: __asan_after_dynamic_init(); break;
    case 36: __asan_poison_stack_memory(0, 0); break;
    case 37: __asan_unpoison_stack_memory(0, 0); break;
    case 38: __asan_region_is_poisoned(0, 0); break;
    case 39: __asan_describe_address(0); break;
    case 40: __asan_set_shadow_00(0, 0); break;
    case 41: __asan_set_shadow_f1(0, 0); break;
    case 42: __asan_set_shadow_f2(0, 0); break;
    case 43: __asan_set_shadow_f3(0, 0); break;
    case 44: __asan_set_shadow_f5(0, 0); break;
    case 45: __asan_set_shadow_f8(0, 0); break;
  }
  // clang-format on
}

void AsanInitFromXsan() {
  if (LIKELY(asan_inited)) return;
  SanitizerToolName = "AddressSanitizer";
  CHECK(!asan_init_is_running && "ASan init calls itself!");
  asan_init_is_running = true;

//   CacheBinaryName();

  // Initialize flags. This must be done early, because most of the
  // initialization steps look at flags().
  InitializeFlags();

  WaitForDebugger(flags()->sleep_before_init, "before init");

  // Stop performing init at this point if we are being loaded via
  // dlopen() and the platform supports it.
  if (SANITIZER_SUPPORTS_INIT_FOR_DLOPEN && UNLIKELY(HandleDlopenInit())) {
    asan_init_is_running = false;
    VReport(1, "AddressSanitizer init is being performed for dlopen().\n");
    return;
  }

  AsanCheckIncompatibleRT();
  AsanCheckDynamicRTPrereqs();
  AvoidCVE_2016_2143();

  SetCanPoisonMemory(flags()->poison_heap);
  SetMallocContextSize(common_flags()->malloc_context_size);

  InitializePlatformExceptionHandlers();

  InitializeHighMemEnd();

  // Make sure we are not statically linked.
  AsanDoesNotSupportStaticLinkage();

  // Install tool-specific callbacks in sanitizer_common.
  AddDieCallback(AsanDie);
  SetCheckUnwindCallback(CheckUnwind);
  SetPrintfAndReportCallback(AppendToErrorMessageBuffer);

  __sanitizer_set_report_path(common_flags()->log_path);

  __asan_option_detect_stack_use_after_return =
      flags()->detect_stack_use_after_return;

  __sanitizer::InitializePlatformEarly();

  // Setup internal allocator callback.
  SetLowLevelAllocateMinAlignment(ASAN_SHADOW_GRANULARITY);
  SetLowLevelAllocateCallback(OnLowLevelAllocate);

  InitializeAsanInterceptors();
  CheckASLR();

  // Enable system log ("adb logcat") on Android.
  // Doing this before interceptors are initialized crashes in:
  // AsanInitInternal -> android_log_write -> __interceptor_strcmp
  AndroidLogInit();

  /// TODO: For Android, move it to Xsan initialization 
  ReplaceSystemMalloc();

  DisableCoreDumperIfNecessary();

  InitializeShadowMemory();

//   AsanTSDInit(PlatformTSDDtor);
  InstallDeadlySignalHandlers(AsanOnDeadlySignal);

  /// Use Asan wrapper allocator and XSan inner allocator
  AllocatorOptions allocator_options;
  allocator_options.SetFrom(flags(), common_flags());
  InitializeAllocator(allocator_options);

  if (SANITIZER_START_BACKGROUND_THREAD_IN_ASAN_INTERNAL)
    MaybeStartBackgroudThread();

  // On Linux AsanThread::ThreadStart() calls malloc() that's why asan_inited
  // should be set to 1 prior to initializing the threads.
  asan_inited = 1;
  asan_init_is_running = false;

  if (flags()->atexit)
    Atexit(asan_atexit);

//   InitializeCoverage(common_flags()->coverage, common_flags()->coverage_dir);

  // Now that ASan runtime is (mostly) initialized, deactivate it if
  // necessary, so that it can be re-activated when requested.
  if (flags()->start_deactivated)
    AsanDeactivate();

  // interceptors
//   InitTlsSize();

  /// TODO: use unified XSanThread! Now we use ASanThread temporarily
  // Create main thread.
  AsanThread *main_thread = CreateMainThread();
  CHECK_EQ(0, main_thread->tid());
  force_interface_symbols();  // no-op.
  SanitizerInitializeUnwinder();

  if (CAN_SANITIZE_LEAKS) {
    __lsan::InitCommonLsan();
    InstallAtExitCheckLeaks();
  }

#if CAN_SANITIZE_UB
  __ubsan::InitAsPlugin();
#endif

  InitializeSuppressions();

  if (CAN_SANITIZE_LEAKS) {
    // LateInitialize() calls dlsym, which can allocate an error string buffer
    // in the TLS.  Let's ignore the allocation to avoid reporting a leak.
    __lsan::ScopedInterceptorDisabler disabler;
    Symbolizer::LateInitialize();
  } else {
    Symbolizer::LateInitialize();
  }

  VReport(1, "AddressSanitizer Init done\n");

  WaitForDebugger(flags()->sleep_after_init, "after init");
}
}