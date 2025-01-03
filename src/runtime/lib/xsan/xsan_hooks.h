//===-- xsan_hooks.h ------------------------------------------*- C++
//-*-===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is used to manuplate the hooks for coordination between multiple
// sanitizers.
//===----------------------------------------------------------------------===//
#pragma once

#include "xsan_interface_internal.h"

#include <sanitizer_common/sanitizer_platform_limits_posix.h>

namespace __sanitizer {
struct CommonFlags;
}

namespace __tsan {
struct ThreadState;
ThreadState *cur_thread();
}  // namespace __tsan

namespace __xsan {
class XsanThread;
struct XsanInterceptorContext;
XsanThread *GetCurrentThread();
// These structs is to hold the context of sub-sanitizers.
// - TSan requires a thread state and a pc everywhere.
// - ASan needs a BufferedStackTrace everywhere.
struct TsanContext {
  __tsan::ThreadState *thr_;
  uptr pc_;
};
struct XsanContext {
  TsanContext tsan_ctx_;

  XsanContext() : tsan_ctx_({nullptr, 0}) {}
  XsanContext(uptr pc)
      : tsan_ctx_({__tsan::cur_thread(), pc}) {}
};
}

namespace __tsan {
/// -------------- Hooks for pthread functions -----------------
using __xsan::TsanContext;
class ScopedPthreadJoin {
 public:
  ScopedPthreadJoin(const int &res, const __xsan::XsanContext &xsan_ctx,
                    const void *th);
  ~ScopedPthreadJoin();

 private:
  const int &res_;
  const TsanContext &tsan_ctx_;
  Tid tid_;
};
class ScopedPthreadDetach {
 public:
  ScopedPthreadDetach(const int &res, const __xsan::XsanContext &xsan_ctx,
                      const void *th);
  ~ScopedPthreadDetach();

 private:
  const int &res_;
  const TsanContext &tsan_ctx_;
  Tid tid_;
};
class ScopedPthreadTryJoin {
 public:
  ScopedPthreadTryJoin(const int &res, const __xsan::XsanContext &xsan_ctx,
                       const void *th);
  ~ScopedPthreadTryJoin();

 private:
  uptr th_;
  const int &res_;
  const TsanContext &tsan_ctx_;
  Tid tid_;
};

}  // namespace __tsan

namespace __xsan {

// ---------------------- Hook for other Sanitizers -------------------
/// Notifies Xsan that the current thread is entering an completely internal
/// Xsan function, e.g., __asan_handle_no_return.
/// In such cases, Xsan should not do sanity checks.
/// Compared to ShouldIgnoreInterceptos(), this function ignores in_ignore_lib.
bool IsInXsanInternal();
class ScopedXsanInternal {
 public:
  ScopedXsanInternal();
  ~ScopedXsanInternal();
};
extern THREADLOCAL int is_in_symbolizer;
inline bool in_symbolizer() { return UNLIKELY(is_in_symbolizer > 0); }
void EnterSymbolizer();
void ExitSymbolizer();
bool ShouldSanitzerIgnoreInterceptors(const XsanContext &xsan_thr);
bool ShouldSanitzerIgnoreAllocFreeHook();
// Integrates different sanitizers' exit code logic.
int get_exit_code(const void *ctx = nullptr);

void OnAllocatorMap(uptr p, uptr size);
void OnAllocatorMapSecondary(uptr p, uptr size, uptr user_begin,
                             uptr user_size);
void OnAllocatorUnmap(uptr p, uptr size);
void XsanAllocHook(uptr ptr, uptr size, bool write, uptr pc);
void XsanFreeHook(uptr ptr, bool write, uptr pc);
void XsanAllocFreeTailHook(uptr pc);
void OnFakeStackDestory(uptr addr, uptr size);

void OnPthreadCreate();
class ScopedPthreadJoin {
 public:
  /// args:
  ///   - res : the return value of pthread_join, 0 for success, non-zero for
  ///   failure.
  ///   - xsan_ctx : the context of Xsan.
  ///   - th : the pthread_t of the thread to be joined.
  ScopedPthreadJoin(const int &res, const __xsan::XsanContext &xsan_ctx,
                    const void *th)
      : tsan_join_(res, xsan_ctx, th) {}
  ~ScopedPthreadJoin();

 private:
  __tsan::ScopedPthreadJoin tsan_join_;
};
class ScopedPthreadDetach {
 public:
  ScopedPthreadDetach(const int &res, const __xsan::XsanContext &xsan_ctx,
                      const void *th)
      : tsan_detach_(res, xsan_ctx, th) {}
  ~ScopedPthreadDetach() {}

 private:
  __tsan::ScopedPthreadDetach tsan_detach_;
};
class ScopedPthreadTryJoin {
 public:
  ScopedPthreadTryJoin(const int &res, const __xsan::XsanContext &xsan_ctx,
                       const void *th)
      : tsan_try_join_(res, xsan_ctx, th) {}
  ~ScopedPthreadTryJoin() {}

 private:
  __tsan::ScopedPthreadTryJoin tsan_try_join_;
};

void InitializeSanitizerFlags();
void SetSanitizerCommonFlags(CommonFlags &cf);
void ValidateSanitizerFlags();

void SetSanitizerThreadName(const char *name);
void SetSanitizerThreadNameByUserId(uptr uid, const char *name);

bool GetMellocStackTrace(u32 &stack_trace_id, uptr addr, bool set_stack_trace_id);

void OnAcquire(const void *ctx, uptr addr);
void OnDirAcquire(const void *ctx, const char *path);
void OnDirRelease(const void *ctx, const char *path);
void OnRelease(const void *ctx, uptr addr);
void OnFdAcquire(const void *ctx, int fd);
void OnFdRelease(const void *ctx, int fd);
void OnFdAccess(const void *ctx, int fd);
void OnFdSocketAccept(const void *ctx, int fd, int newfd);
void OnFileOpen(const void *ctx, void *file, const char *path);
void OnFileClose(const void *ctx, void *file);
void OnHandleRecvmsg(const void *ctx,  __sanitizer_msghdr *msg);
/// Used in the Atexit registration.
class ScopedAtExitWrapper {
 public:
  ScopedAtExitWrapper(uptr pc, const void *ctx);
  ~ScopedAtExitWrapper();
};
class ScopedAtExitHandler {
 public:
  ScopedAtExitHandler(uptr pc, const void *ctx);
  ~ScopedAtExitHandler();
};
void AfterMmap(const XsanInterceptorContext &ctx, void *res, uptr size, int fd);
void BeforeMunmap(const XsanInterceptorContext &ctx,void *addr, uptr size);
/// To implement macro COMMON_INTERCEPTOR_SPILL_AREA in *vfork.S
/// Notably, this function is called TWICE at the attitude per process.
extern "C" void * __xsan_vfork_before_and_after();
/// To implement macro COMMON_INTERCEPTOR_HANDLE_VFORK in *vfork.S
extern "C" void __xsan_vfork_parent_after(void *sp);
void OnLibraryLoaded(const char *filename, void *handle);
void OnLibraryUnloaded();
void OnLongjmp(void *env, const char *fn_name, uptr pc);
}  // namespace __xsan