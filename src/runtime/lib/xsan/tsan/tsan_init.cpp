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
static char ctx_placeholder[sizeof(Context)] ALIGNED(SANITIZER_CACHE_LINE_SIZE);

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
    filename.append("%s.%d", fname, (int)internal_getpid());
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

static void *BackgroundThread(void *arg) {
  // This is a non-initialized non-user thread, nothing to see here.
  // We don't use ScopedIgnoreInterceptors, because we want ignores to be
  // enabled even when the thread function exits (e.g. during pthread thread
  // shutdown code).
  cur_thread_init()->ignore_interceptors++;
  const u64 kMs2Ns = 1000 * 1000;
  const u64 start = NanoTime();

  u64 last_flush = start;
  uptr last_rss = 0;
  while (!atomic_load_relaxed(&ctx->stop_background_thread)) {
    SleepForMillis(100);
    u64 now = NanoTime();

    // Flush memory if requested.
    if (flags()->flush_memory_ms > 0) {
      if (last_flush + flags()->flush_memory_ms * kMs2Ns < now) {
        VReport(1, "ThreadSanitizer: periodic memory flush\n");
        FlushShadowMemory();
        now = last_flush = NanoTime();
      }
    }
    if (flags()->memory_limit_mb > 0) {
      uptr rss = GetRSS();
      uptr limit = uptr(flags()->memory_limit_mb) << 20;
      VReport(1,
              "ThreadSanitizer: memory flush check"
              " RSS=%llu LAST=%llu LIMIT=%llu\n",
              (u64)rss >> 20, (u64)last_rss >> 20, (u64)limit >> 20);
      if (2 * rss > limit + last_rss) {
        VReport(1, "ThreadSanitizer: flushing memory due to RSS\n");
        FlushShadowMemory();
        rss = GetRSS();
        now = NanoTime();
        VReport(1, "ThreadSanitizer: memory flushed RSS=%llu\n",
                (u64)rss >> 20);
      }
      last_rss = rss;
    }

    MemoryProfiler(now - start);

    // Flush symbolizer cache if requested.
    if (flags()->flush_symbolizer_ms > 0) {
      u64 last =
          atomic_load(&ctx->last_symbolize_time_ns, memory_order_relaxed);
      if (last != 0 && last + flags()->flush_symbolizer_ms * kMs2Ns < now) {
        Lock l(&ctx->report_mtx);
        ScopedErrorReportLock l2;
        SymbolizeFlush();
        atomic_store(&ctx->last_symbolize_time_ns, 0, memory_order_relaxed);
      }
    }
  }
  return nullptr;
}

static void StartBackgroundThread() {
  ctx->background_thread = internal_start_thread(&BackgroundThread, 0);
}

#  ifndef __mips__
static void StopBackgroundThread() {
  atomic_store(&ctx->stop_background_thread, 1, memory_order_relaxed);
  internal_join_thread(ctx->background_thread);
  ctx->background_thread = 0;
}
#  endif
#endif

void TsanInitFromXsanEarly() {
  ctx = new (ctx_placeholder) Context;
  ThreadState *thr = cur_thread_init();
}

void TsanInitFromXsan() {
  __xsan::ScopedSanitizerToolName tool_name("ThreadSanitizer");

  ThreadState *thr = cur_thread_init();
  // Thread safe because done before all threads exist.
  if (is_initialized)
    return;
  is_initialized = true;
  // We are not ready to handle interceptors yet.
  ScopedIgnoreInterceptors ignore;
  /// TODO: Move this from ASan and TSan to XSan
  // Install tool-specific callbacks in sanitizer_common.
  // SetCheckUnwindCallback(CheckUnwind);

  /// Moved to TsanInitFromXsanEarly
  // ctx = new (ctx_placeholder) Context;
  // const char *env_name = SANITIZER_GO ? "GORACE" : "TSAN_OPTIONS";
  // const char *options = GetEnv(env_name);
  /// TODO: Move this from ASan and TSan to XSan
  //   CacheBinaryName();
  //   CheckASLR();
  /// TODO: Move this to XSan
  //   InitializeFlags(&ctx->flags, options, env_name);
  // AvoidCVE_2016_2143();
  // __sanitizer::InitializePlatformEarly();
  __tsan::InitializePlatformEarly();

#if !SANITIZER_GO
/// TODO: Move this from ASan and TSan to XSan
//   InitializeAllocator();
//   ReplaceSystemMalloc();
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
  /// TODO: Move this from ASan and TSan to XSan
  //   __sanitizer_set_report_path(common_flags()->log_path);
  InitializeSuppressions();
#if !SANITIZER_GO
  InitializeLibIgnore();
  Symbolizer::GetOrInit()->AddHooks(EnterSymbolizer, ExitSymbolizer);
#endif

  VPrintf(1, "***** Running under ThreadSanitizer v3 (pid %d) *****\n",
          (int)internal_getpid());

  //   // Initialize thread 0.
  //   Tid tid = ThreadCreate(nullptr, 0, 0, true);
  //   CHECK_EQ(tid, kMainTid);
  //   ThreadStart(thr, tid, GetTid(), ThreadType::Regular);
  /// TODO: Move this from ASan and TSan to XSan
  // #if TSAN_CONTAINS_UBSAN
  //   __ubsan::InitAsPlugin();
  // #endif

#if !SANITIZER_GO
  /// TODO: Move this from ASan and TSan to XSan
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

  OnInitialize();
}

}  // namespace __tsan