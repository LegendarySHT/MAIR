#include "tsan_rtl_extra.h"

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "tsan_rtl.h"

#include "../xsan_common_defs.h"
namespace __tsan {

static atomic_uint8_t TsanDisabled {0};

static struct {
  int ignore_reads_and_writes;
  int ignore_sync;
} main_thread_state;

static void StoreCurrentTsanState(ThreadState *thr) {
  main_thread_state.ignore_reads_and_writes = thr->ignore_reads_and_writes;
  main_thread_state.ignore_sync = thr->ignore_sync;
}

/// Used for longjmp in signal handlers
/// 1. CallUserSignalHandler set and recover state before and after the signal
///    handler
/// 2. signal handler calls longjmp, leading to the state not being recovered in
///    CallUserSignalHandler unexpectly.
/// 3. Therefore, we provide this function to recover the state in Longjmp.
void RestoreTsanState(ThreadState *thr) {
  thr->ignore_reads_and_writes = main_thread_state.ignore_reads_and_writes;
  thr->ignore_sync = main_thread_state.ignore_sync;
}

static void DisableTsan(ThreadState *thr) {
  ThreadIgnoreSyncBegin(thr, 0);
  ThreadIgnoreBegin(thr, 0);
}

static void EnableTsan(ThreadState *thr) {
  ThreadIgnoreEnd(thr);
  ThreadIgnoreSyncEnd(thr);
}

void DisableMainThreadTsan(ThreadState *thr) {
  if (atomic_load_relaxed(&thr->in_signal_handler)) {
    return;
  }
  if (atomic_exchange(&TsanDisabled, 1, memory_order_relaxed) == 1)
    return;

  DisableTsan(thr);

  StoreCurrentTsanState(thr);
}

void EnableMainThreadTsan(ThreadState *thr) {
  if (atomic_load_relaxed(&thr->in_signal_handler)) {
    return;
  }
  if (atomic_exchange(&TsanDisabled, 0, memory_order_relaxed) == 0)
    return;

  EnableTsan(thr);

  StoreCurrentTsanState(thr);
}

ScopedIgnoreTsan::ScopedIgnoreTsan(bool enable) : enable_(enable) {
#if !SANITIZER_GO
  if (enable_) {
    ThreadState *thr = cur_thread();
    nomalloc_ = thr->nomalloc;
    in_signal_handler_ = atomic_load_relaxed(&thr->in_signal_handler);
    DisableTsan(thr);
    thr->nomalloc = false;
    atomic_store_relaxed(&thr->in_signal_handler, 0);
  }
#endif
}

ScopedIgnoreTsan::~ScopedIgnoreTsan() {
#if !SANITIZER_GO
  if (enable_) {
    ThreadState *thr = cur_thread();
    thr->nomalloc = nomalloc_;
    atomic_store_relaxed(&thr->in_signal_handler, in_signal_handler_);
    EnableTsan(thr);
  }
#endif
}

bool ShouldIgnoreInterceptors(ThreadState *thr) {
  return !thr->is_inited || thr->ignore_interceptors || thr->in_ignored_lib;
}

bool ShouldIgnoreInterceptors() {
  return ShouldIgnoreInterceptors(cur_thread());
}

bool ShouldIgnoreAllocFreeHook() {
  ThreadState *thr = cur_thread();
  return (ctx == 0 || !ctx->initialized || thr->ignore_interceptors);
}

void OnPthreadCreate() {
  MaybeSpawnBackgroundThread();

  if (ctx->after_multithreaded_fork) {
    if (flags()->die_after_fork) {
      Report(
          "ThreadSanitizer: starting new threads after multi-threaded "
          "fork is not supported. Dying (set die_after_fork=0 to override)\n");
      Die();
    } else {
      VPrintf(1,
              "ThreadSanitizer: starting new threads after multi-threaded "
              "fork is not supported (pid %lu). Continuing because of "
              "die_after_fork=0, but you are on your own\n",
              internal_getpid());
    }
  }
}

THREADLOCAL bool disabled_tsan = false;

/// See the following comment in tsan_interceptors_posix.cpp:vfork for details.
// Some programs (e.g. openjdk) call close for all file descriptors
// in the child process. Under tsan it leads to false positives, because
// address space is shared, so the parent process also thinks that
// the descriptors are closed (while they are actually not).
// This leads to false positives due to missed synchronization.
// Strictly saying this is undefined behavior, because vfork child is not
// allowed to call any functions other than exec/exit. But this is what
// openjdk does, so we want to handle it.
// We could disable interceptors in the child process. But it's not possible
// to simply intercept and wrap vfork, because vfork child is not allowed
// to return from the function that calls vfork, and that's exactly what
// we would do. So this would require some assembly trickery as well.
// Instead we simply turn vfork into fork.
void DisableTsanForVfork() {
  /* VFORK is epected to call exit/exec soon, so we should not do TSan's sanity
   * checks for its child. */
  if (disabled_tsan)
    return;
  disabled_tsan = true;
  ThreadState *thr = cur_thread();
  thr->ignore_interceptors = true;
  // We've just forked a multi-threaded process. We cannot reasonably function
  // after that (some mutexes may be locked before fork). So just enable
  // ignores for everything in the hope that we will exec soon.
  thr->ignore_interceptors++;
  thr->suppress_reports++;
  thr->in_vfork_child = true;
  ThreadIgnoreBegin(thr, 0);
  ThreadIgnoreSyncBegin(thr, 0);
}

void RecoverTsanAfterVforkParent() {
  ThreadState *thr = cur_thread();
  // We've just forked a multi-threaded process. We cannot reasonably function
  // after that (some mutexes may be locked before fork). So just enable
  // ignores for everything in the hope that we will exec soon.
  thr->ignore_interceptors--;
  thr->suppress_reports--;
  thr->in_vfork_child = false;
  ThreadIgnoreEnd(thr);
  ThreadIgnoreSyncEnd(thr);
  disabled_tsan = false;
}

}  // namespace __tsan

extern "C" {

/// TODO: define these functions only if LSan is used.
/*
  TSan intercepts dl_iterate_phdr, and leads to unexpected crashes in LSan,
which uses dl_iterate_phdr. The ideal solution is that ignore the
interceptors after LSan's checks are activated. However, LSan indeed
provides no interface for us to instrument a ScopedIgnoreInterceptors.
Therefore, we need to use other approaches to sense whether LSan's checks
are active. Here are some ideas:
  1. (Intrusive) Modify LSan's source code to provide interface to query its
    status.
  2. (Intrusive & Incompatible) Modify LSan's source code to replace
    dl_iterate_phdr with internal_dl_iterate_phdr.
  3. (Intrusive & Incompatible) Modify LSan's source code and register a
    ScopedIgnoreInterceptors before checks.
  4. (Non-intrusive) Use -Wl,-wrap,<symbol> to replace the symbols.
  5. (Non-intrusive & tricky) Use entry 'hook' __lsan_is_turned_off to
    obtain the caller ra of its caller, which could be considered as exit.,
  6. (Non-intrusive & heary) Use -finstrument-functions to register a
    callback for each function call.
Excluding all intrusive approaches, the only non-tricky simple solution is
to use -Wl,-wrap,<symbolc> (approach 4).

  The only trouble is that we need to intercept all the exported symbols about
LSan's check, as this approach does not work for those calls that locate in
the same file defining the called symbols.
*/

#define TSAN_INTERCEPT_AND_IGNORE_VOID(ret, f)            \
  ret XSAN_REAL(f)(void);                                 \
  ret XSAN_WRAP(f)(void) {                                \
    __tsan::ScopedIgnoreInterceptors ignore_interceptors; \
    return XSAN_REAL(f)();                                \
  }

#define TSAN_INTERCEPT_AND_IGNORE(ret, f, params, args)   \
  ret XSAN_REAL(f) params;                                \
  ret XSAN_WRAP(f) params {                               \
    __tsan::ScopedIgnoreInterceptors ignore_interceptors; \
    return XSAN_REAL(f) args;                             \
  }

// __lsan::DoLeakCheck
TSAN_INTERCEPT_AND_IGNORE_VOID(void, _ZN6__lsan11DoLeakCheckEv)
// __lsan::DoRecoverableLeakCheck
TSAN_INTERCEPT_AND_IGNORE_VOID(void, _ZN6__lsan26DoRecoverableLeakCheckVoidEv)
// __lsan_do_leak_check
TSAN_INTERCEPT_AND_IGNORE_VOID(void, __lsan_do_leak_check)
// __lsan_do_recoverable_leak_check
TSAN_INTERCEPT_AND_IGNORE_VOID(void, __lsan_do_recoverable_leak_check)



/// TODO: define these functions only if UBSan is used.
/// UBSan uses this function to check if a memory range is accessible, where
/// `pipe` is called internally. Meanwhile, TSan's interceptor of `pipe` will
/// lead to unexpected behaviors. To avoid this, we need to intercept this
/// function and ignore the TSan's interceptor.
// __santizer::IsAccessibleMemoryRange
TSAN_INTERCEPT_AND_IGNORE(bool, _ZN11__sanitizer23IsAccessibleMemoryRangeEmm,
                     (uptr beg, uptr size), (beg, size))

#undef TSAN_INTERCEPT_AND_IGNORE
#undef TSAN_INTERCEPT_AND_IGNORE_VOID

using namespace __tsan;
// ----------- Intercept Data Race Checking Functions -----------
/// void MemoryRangeImitateWrite(ThreadState* thr, uptr pc, 
///                              uptr addr, uptr size)
XSAN_WRAPPER(void, _ZN6__tsan23MemoryRangeImitateWriteEPNS_11ThreadStateEmmm,
              ThreadState* thr, uptr pc, uptr addr, uptr size) {
  /// FIXME: is it a bug? should report to TSan officiallly?
  /// We should directly return if fast_state.GetIgnoreBit() is true,
  /// Because
  ///   1. fast_state.ignore_accesses_ shares the same bit with
  ///      shadow.is_atomic_
  ///      -->
  ///      If not return while fast_state.ignore_accesses_ be true,
  ///      Shadow with access type ATOMIC will be stored.
  ///   2. TraceMemoryAccessRange is not allowed to be atomic
  ///      -->
  ///      TSan cannot restore a stack from access range event if the relevant
  ///      access event is not atomic (Shadow with access type ATOMIC).
  ///   3. If race happens, TSan will restore the stack trace of the past access
  ///   from
  ///      the relevant conflicted shadow. If the stack trace cannot be
  ///      restored, TSan will give up the race report.
  /// i.e., even if we store the shadows while fast_state.ignore_accesses_ is
  /// true, we cannot restore the stack from the access range event, leading to
  /// the relevant potential races would never be reported.
  ///
  /// In other words, recording the access range is in vain if
  /// fast_state.GetIgnoreBit() is set.
  if (UNLIKELY(thr->fast_state.GetIgnoreBit()))
    return;
  XSAN_REAL(_ZN6__tsan23MemoryRangeImitateWriteEPNS_11ThreadStateEmmm)(thr, pc, addr, size);
}
}