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

namespace __xsan {
class XsanThread;

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
bool ShouldSanitzerIgnoreInterceptors(XsanThread *xsan_thr);
bool ShouldSanitzerIgnoreAllocFreeHook();
// Integrates different sanitizers' exit code logic.
int get_exit_code(void *ctx = nullptr);

void XsanAllocHook(uptr ptr, uptr size, bool write, uptr pc);
void XsanFreeHook(uptr ptr, bool write, uptr pc);
void XsanAllocFreeTailHook(uptr pc);
void OnFakeStackDestory(uptr addr, uptr size);

void OnPthreadCreate();

void InitializeSanitizerFlags();
void SetSanitizerCommonFlags(CommonFlags &cf);
void ValidateSanitizerFlags();

void SetSanitizerThreadName(const char *name);
void SetSanitizerThreadNameByUserId(uptr uid, const char *name);

bool GetMellocStackTrace(u32 &stack_trace_id, uptr addr, bool set_stack_trace_id);

void OnAcquire(void *ctx, uptr addr);
void OnDirAcquire(void *ctx, const char *path);
void OnDirRelease(void *ctx, const char *path);
void OnRelease(void *ctx, uptr addr);
void OnFdAcquire(void *ctx, int fd);
void OnFdRelease(void *ctx, int fd);
void OnFdAccess(void *ctx, int fd);
void OnFdSocketAccept(void *ctx, int fd, int newfd);
void OnFileOpen(void *ctx, void *file, const char *path);
void OnFileClose(void *ctx, void *file);
void OnHandleRecvmsg(void *ctx,  __sanitizer_msghdr *msg);
/// Used in the Atexit registration.
class ScopedAtExitWrapper {
 public:
  ScopedAtExitWrapper(uptr pc, void *ctx);
  ~ScopedAtExitWrapper();
};
class ScopedAtExitHandler {
 public:
  ScopedAtExitHandler(uptr pc, void *ctx);
  ~ScopedAtExitHandler();
};
void AfterMmap(void *ctx, void *res, uptr size, int fd);
/// To implement macro COMMON_INTERCEPTOR_SPILL_AREA in *vfork.S
/// Notably, this function is called TWICE at the attitude per process.
extern "C" void * __xsan_vfork_before_and_after();
/// To implement macro COMMON_INTERCEPTOR_HANDLE_VFORK in *vfork.S
extern "C" void __xsan_vfork_parent_after(void *sp);
void OnLibraryLoaded(const char *filename, void *handle);
void OnLibraryUnloaded();
void OnLongjmp(void *env, const char *fn_name, uptr pc);
}  // namespace __xsan