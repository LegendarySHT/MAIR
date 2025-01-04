#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_file.h>
#include <sanitizer_common/sanitizer_interface_internal.h>
#include <sanitizer_common/sanitizer_placement_new.h>

#include "orig/tsan_suppressions.h"
#include "orig/tsan_symbolize.h"
#include "tsan_rtl.h"

extern volatile int __tsan_resumed;
namespace __tsan {

/// TODO: Reuse this in tsan_rtl.cpp
alignas(SANITIZER_CACHE_LINE_SIZE) static char ctx_placeholder[sizeof(Context)];

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnInitialize();

#if !SANITIZER_GO
void MemoryProfiler(u64 uptime);
static bool InitializeMemoryProfiler() {
  ctx->memprof_fd = kInvalidFd;
  const char *fname = flags()->profile_memory;
  if (!fname || !fname[0])
    return false;
  if (internal_strcmp(fname, "stdout") == 0) {
    ctx->memprof_fd = 1;
  } else if (internal_strcmp(fname, "stderr") == 0) {
    ctx->memprof_fd = 2;
  } else {
    InternalScopedString filename;
    filename.AppendF("%s.%d", fname, (int)internal_getpid());
    ctx->memprof_fd = OpenFile(filename.data(), WrOnly);
    if (ctx->memprof_fd == kInvalidFd) {
      Printf("ThreadSanitizer: failed to open memory profile file '%s'\n",
             filename.data());
      return false;
    }
  }
  MemoryProfiler(0);
  return true;
}

#endif

void TsanInitFromXsanVeryEarly() {
  ctx = new (ctx_placeholder) Context;
  cur_thread_init();
}

void TsanInitFromXsan() {
  // Thread safe because done before all threads exist.
  if (is_initialized)
    return;

  cur_thread_init();
  is_initialized = true;
  // We are not ready to handle interceptors yet.
  ScopedIgnoreInterceptors ignore;
  /// Moved this to XSan
  // Install tool-specific callbacks in sanitizer_common.
  // SetCheckUnwindCallback(CheckUnwind);

  /// Moved to XSan's very early initialization stage.
  // ctx = new (ctx_placeholder) Context;
  // const char *env_name = SANITIZER_GO ? "GORACE" : "TSAN_OPTIONS";
  // const char *options = GetEnv(env_name);
  /// Moved this to XSan
  //   CacheBinaryName();
  //   CheckASLR();
  /// Moved this to XSan
  //   InitializeFlags(&ctx->flags, options, env_name);
  // AvoidCVE_2016_2143();
  // __sanitizer::InitializePlatformEarly();
  /// Moved this to XSan's early initialization stage.
  // __tsan::InitializePlatformEarly();

#if !SANITIZER_GO
  /// Moved this to XSan
  // InitializeAllocator();
  // ReplaceSystemMalloc();
#endif
  if (common_flags()->detect_deadlocks)
    ctx->dd = DDetector::Create(flags());
  ///   Moved to XSan's InitializeMainThread
  //   Processor *proc = ProcCreate();
  //   ProcWire(proc, thr);

  /// Move to XSan
  //   InitializeInterceptors();
  InitializePlatform();
  InitializeDynamicAnnotations();
#if !SANITIZER_GO
  InitializeShadowMemory();
  InitializeAllocatorLate();
//   InstallDeadlySignalHandlers(TsanOnDeadlySignal);
#endif
  // Setup correct file descriptor for error reports.
  /// Moved this to XSan
  //   __sanitizer_set_report_path(common_flags()->log_path);
  InitializeSuppressions();
#if !SANITIZER_GO
  InitializeLibIgnore();
  // Symbolizer::GetOrInit()->AddHooks(EnterSymbolizer, ExitSymbolizer);
#endif

  VPrintf(1, "***** Running under ThreadSanitizer v3 (pid %d) *****\n",
          (int)internal_getpid());

  //   // Initialize thread 0.
  //   Tid tid = ThreadCreate(nullptr, 0, 0, true);
  //   CHECK_EQ(tid, kMainTid);
  //   ThreadStart(thr, tid, GetTid(), ThreadType::Regular);
  /// Moved this to XSan
  // #if TSAN_CONTAINS_UBSAN
  //   __ubsan::InitAsPlugin();
  // #endif

#if !SANITIZER_GO
  /// Moved this to XSan
  //   Symbolizer::LateInitialize();
  if (InitializeMemoryProfiler() || flags()->force_background_thread)
    MaybeSpawnBackgroundThread();
#endif
  ctx->initialized = true;

  if (flags()->stop_on_start) {
    Printf(
        "ThreadSanitizer is suspended at startup (pid %d)."
        " Call __tsan_resume().\n",
        (int)internal_getpid());
    while (__tsan_resumed == 0) {
    }
  }

  /// Moved to TsanInitFromXsanLate, i.e., after when TSan has been fully
  /// initialized.
  // OnInitialize();
}

void TsanInitFromXsanLate() {
  OnInitialize();
}

}  // namespace __tsan