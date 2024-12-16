//===-- xsan_hooks.h ------------------------------------------*- C++
//-*-===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is used to manuplate the hooks for coordination between multiple
// sanitizers.
//===----------------------------------------------------------------------===//

#include "xsan_interface_internal.h"

namespace __sanitizer {
struct CommonFlags;
}

namespace __xsan {
class XsanThread;

// ---------------------- Hook for other Sanitizers -------------------
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

/// Used in the Atexit registration.
class ScopedAtExitWrapper {
 public:
  ScopedAtExitWrapper(uptr pc, void *ctx);
  ~ScopedAtExitWrapper();
};
void AfterMmap(void *ctx, void *res, uptr size, int fd);

bool ShouldSanitzerIgnoreInterceptors(XsanThread *xsan_thr);
bool ShouldSanitzerIgnoreAllocFreeHook();
}  // namespace __xsan