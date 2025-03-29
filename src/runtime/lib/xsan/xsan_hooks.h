//===-- xsan_hooks.h ------------------------------------------*- C++
//-*-===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is to offer integrated hooks.
//===----------------------------------------------------------------------===//
#pragma once

#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_platform_limits_posix.h>

#include "xsan_hooks_default.h"
#include "xsan_hooks_todo.h"
#include "xsan_interface_internal.h"
#include "xsan_thread.h"

namespace __sanitizer {
struct CommonFlags;
}

namespace __xsan {

struct XsanInterceptorContext;
XsanThread *GetCurrentThread();

// These structs is to hold the context of sub-sanitizers.
// - TSan requires a thread state and a pc everywhere.
// - ASan needs a BufferedStackTrace everywhere.
struct XsanContext {
  XSAN_HOOKS_DEFINE_VAR(Context)
  XSAN_HOOKS_DEFINE_VAR_CVT(Context)

  XsanContext() : XSAN_HOOKS_INIT_VAR() {}
  XsanContext(uptr pc) : XSAN_HOOKS_INIT_VAR(pc) {}
};

// ---------------------- Hook for other Sanitizers -------------------
/// Notifies Xsan that the current thread is entering an completely internal
/// Xsan function, e.g., __asan_handle_no_return.
/// In such cases, Xsan should not do sanity checks.
/// Compared to ShouldIgnoreInterceptos(), this function ignores in_ignore_lib.
extern THREADLOCAL int xsan_in_intenal;
ALWAYS_INLINE bool IsInXsanInternal() { return xsan_in_intenal != 0; }
class ScopedXsanInternal {
 public:
  ALWAYS_INLINE ScopedXsanInternal() { xsan_in_intenal++; }
  ALWAYS_INLINE ~ScopedXsanInternal() { xsan_in_intenal--; }
};
extern THREADLOCAL int xsan_in_calloc;
ALWAYS_INLINE bool IsInXsanCalloc() { return xsan_in_calloc != 0; }
class ScopedXsanCalloc {
 public:
  ALWAYS_INLINE ScopedXsanCalloc() { xsan_in_calloc++; }
  ALWAYS_INLINE ~ScopedXsanCalloc() { xsan_in_calloc--; }
};
extern THREADLOCAL int is_in_symbolizer;
ALWAYS_INLINE bool in_symbolizer() { return UNLIKELY(is_in_symbolizer > 0); }

ALWAYS_INLINE void EnterSymbolizer() {
  ++is_in_symbolizer;
  XSAN_HOOKS_EXEC(EnterSymbolizer);
}

ALWAYS_INLINE void ExitSymbolizer() {
  --is_in_symbolizer;
  XSAN_HOOKS_EXEC(ExitSymbolizer);
}

ALWAYS_INLINE bool ShouldSanitzerIgnoreInterceptors(
    const XsanContext &xsan_thr) {
  /// Avoid sanity checks in XSan internal.
  if (IsInXsanInternal())
    return true;
  bool should_ignore = false;
  /// TODO: to support libignore, we plan to migrate it to Xsan.
  /// xsan_suppressions.cpp is required accordingly.
  XSAN_HOOKS_EXEC_OR(should_ignore, ShouldIgnoreInterceptors, xsan_thr);
  return should_ignore;
}

ALWAYS_INLINE bool ShouldSanitzerIgnoreAllocFreeHook() {
  bool should_ignore = false;
  XSAN_HOOKS_EXEC_OR(should_ignore, ShouldIgnoreAllocFreeHook);
  return should_ignore;
}
// Integrates different sanitizers' exit code logic.
/// TODO: unify the exit code logic.
int get_exit_code(const void *ctx = nullptr);

ALWAYS_INLINE void OnAllocatorMap(uptr p, uptr size) {
  XSAN_HOOKS_EXEC(OnAllocatorMap, p, size);
}

ALWAYS_INLINE void OnAllocatorMapSecondary(uptr p, uptr size, uptr user_begin,
                                           uptr user_size) {
  XSAN_HOOKS_EXEC(OnAllocatorMapSecondary, p, size, user_begin, user_size);
}

ALWAYS_INLINE void OnAllocatorUnmap(uptr p, uptr size) {
  XSAN_HOOKS_EXEC(OnAllocatorUnmap, p, size);
}

ALWAYS_INLINE void XsanAllocHook(uptr ptr, uptr size,
                                 BufferedStackTrace *stack) {
  XSAN_HOOKS_EXEC(OnXsanAllocHook, ptr, size, stack);
}

ALWAYS_INLINE void XsanFreeHook(uptr ptr, uptr size,
                                BufferedStackTrace *stack) {
  XSAN_HOOKS_EXEC(OnXsanFreeHook, ptr, size, stack);
}

ALWAYS_INLINE void XsanAllocFreeTailHook(uptr pc) {
  XSAN_HOOKS_EXEC(OnXsanAllocFreeTailHook, pc);
}

ALWAYS_INLINE void OnFakeStackDestory(uptr addr, uptr size) {
  XSAN_HOOKS_EXEC(OnFakeStackDestory, addr, size);
}

class ScopedPthreadJoin {
 public:
  /// args:
  ///   - res : the return value of pthread_join, 0 for success, non-zero for
  ///   failure.
  ///   - xsan_ctx : the context of Xsan.
  ///   - th : the pthread_t of the thread to be joined.
  ScopedPthreadJoin(const int &res, const __xsan::XsanContext &xsan_ctx,
                    const void *th)
      : XSAN_HOOKS_INIT_VAR(res, xsan_ctx, th) {}
  ~ScopedPthreadJoin() {}

 private:
  XSAN_HOOKS_DEFINE_VAR(ScopedPthreadJoin)
};

class ScopedPthreadDetach {
 public:
  ScopedPthreadDetach(const int &res, const __xsan::XsanContext &xsan_ctx,
                      const void *th)
      : XSAN_HOOKS_INIT_VAR(res, xsan_ctx, th) {}
  ~ScopedPthreadDetach() {}

 private:
  XSAN_HOOKS_DEFINE_VAR(ScopedPthreadDetach)
};

class ScopedPthreadTryJoin {
 public:
  ScopedPthreadTryJoin(const int &res, const __xsan::XsanContext &xsan_ctx,
                       const void *th)
      : XSAN_HOOKS_INIT_VAR(res, xsan_ctx, th) {}
  ~ScopedPthreadTryJoin() {}

 private:
  XSAN_HOOKS_DEFINE_VAR(ScopedPthreadTryJoin)
};

ALWAYS_INLINE void OnPthreadCreate() { XSAN_HOOKS_EXEC(OnPthreadCreate); }

void InitializeSanitizerFlags();
void SetSanitizerCommonFlags(CommonFlags &cf);
void ValidateSanitizerFlags();

void SetSanitizerThreadName(const char *name);
void SetSanitizerThreadNameByUserId(uptr uid, const char *name);

bool GetMellocStackTrace(u32 &stack_trace_id, uptr addr,
                         bool set_stack_trace_id);

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
void OnHandleRecvmsg(const void *ctx, __sanitizer_msghdr *msg);
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
void BeforeMunmap(const XsanInterceptorContext &ctx, void *addr, uptr size);
/// To implement macro COMMON_INTERCEPTOR_SPILL_AREA in *vfork.S
/// Notably, this function is called TWICE at the attitude per process.
extern "C" void *__xsan_vfork_before_and_after();
/// To implement macro COMMON_INTERCEPTOR_HANDLE_VFORK in *vfork.S
extern "C" void __xsan_vfork_parent_after(void *sp);
void OnForkBefore();
void OnForkAfter(bool is_child);
void OnLibraryLoaded(const char *filename, void *handle);
void OnLibraryUnloaded();
void OnLongjmp(void *env, const char *fn_name, uptr pc);

class ScopedUnwinding {
 public:
  ALWAYS_INLINE explicit ScopedUnwinding(XsanThread *t) {
    if (thread) {
      can_unwind = !thread->isUnwinding();
      thread->setUnwinding(true);
    }
    XSAN_HOOKS_EXEC(OnEnterUnwind);
  }
  ALWAYS_INLINE ~ScopedUnwinding() {
    XSAN_HOOKS_EXEC(OnExitUnwind);
    if (thread) {
      thread->setUnwinding(false);
    }
  }

  ALWAYS_INLINE bool CanUnwind() const { return can_unwind; }

 private:
  XsanThread *thread = nullptr;
  bool can_unwind = true;
};

enum class XsanStackTraceType { store };

template <XsanStackTraceType type>
ALWAYS_INLINE bool RequireStackTraces() {
  bool require = false;
  XSAN_HOOKS_EXEC_OR(require, RequireStackTraces);
  return require;
}

template <XsanStackTraceType type>
ALWAYS_INLINE int RequireStackTracesSize() {
  int size = -1;
  XSAN_HOOKS_EXEC_MAX(size, RequireStackTracesSize);
  return size;
}

}  // namespace __xsan
