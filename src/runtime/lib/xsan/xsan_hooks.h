//===-- xsan_hooks.h ------------------------------------------*- C++
//-*-===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is to offer integrated hooks.
//===----------------------------------------------------------------------===//
#pragma once

#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_internal_defs.h>
#include <sanitizer_common/sanitizer_platform_limits_posix.h>

#include "xsan_attribute.h"
#include "xsan_hooks_dispatch.h"
#include "xsan_interface_internal.h"
#include "xsan_thread.h"

namespace __sanitizer {
struct CommonFlags;
}

namespace __xsan {

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

struct XsanInterceptorContext {
  const char *interceptor_name;
  /// TODO: should use pointer or reference?
  XsanContext xsan_ctx;
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

// ---------------------- Special Function Hooks -----------------

class ScopedAtExitWrapper {
 public:
  ScopedAtExitWrapper(uptr pc, const void *ctx)
      : XSAN_HOOKS_INIT_VAR(pc, ctx) {}
  ~ScopedAtExitWrapper() {}

 private:
  XSAN_HOOKS_DEFINE_VAR(ScopedAtExitWrapper)
};
class ScopedAtExitHandler {
 public:
  ScopedAtExitHandler(uptr pc, const void *ctx)
      : XSAN_HOOKS_INIT_VAR(pc, ctx) {}
  ~ScopedAtExitHandler() {}

 private:
  XSAN_HOOKS_DEFINE_VAR(ScopedAtExitHandler)
};

ALWAYS_INLINE void OnPthreadCreate() { XSAN_HOOKS_EXEC(OnPthreadCreate); }

ALWAYS_INLINE void InitializeSanitizerFlags() {
  XSAN_HOOKS_EXEC(InitializeFlags);
}
ALWAYS_INLINE void SetSanitizerCommonFlags(CommonFlags &cf) {
  XSAN_HOOKS_EXEC(SetCommonFlags, cf);
}
ALWAYS_INLINE void ValidateSanitizerFlags() { XSAN_HOOKS_EXEC(ValidateFlags); }

ALWAYS_INLINE void SetSanitizerThreadName(const char *name) {
  XSAN_HOOKS_EXEC(SetThreadName, name);
}
ALWAYS_INLINE void SetSanitizerThreadNameByUserId(uptr uid, const char *name) {
  /// Should be asanThreadRegistry().SetThreadNameByUserId(thread, name)
  /// But asan does not remember UserId's for threads (pthread_t);
  /// and remembers all ever existed threads, so the linear search by UserId
  /// can be slow.
  // __asan::SetAsanThreadNameByUserId(uid, name);
  XSAN_HOOKS_EXEC(SetThreadNameByUserId, uid, name);
}

bool GetMellocStackTrace(u32 &stack_trace_id, uptr addr,
                         bool set_stack_trace_id);

/// Used in the Atexit registration.
ALWAYS_INLINE void AfterMmap(const XsanInterceptorContext &ctx, void *res,
                             uptr size, int fd) {
  XSAN_HOOKS_EXEC(AfterMmap, ctx.xsan_ctx, res, size, fd);
}
ALWAYS_INLINE void BeforeMunmap(const XsanInterceptorContext &ctx, void *addr,
                                uptr size) {
  /// Size too big can cause problems with the shadow memory.
  /// unmap on NULL is not allowed.
  if ((sptr)size < 0 || addr == nullptr)
    return;
  XSAN_HOOKS_EXEC(BeforeMunmap, ctx.xsan_ctx, addr, size);
}
/// To implement macro COMMON_INTERCEPTOR_SPILL_AREA in *vfork.S
/// Notably, this function is called TWICE at the attitude per process.
extern "C" void *__xsan_vfork_before_and_after();
/// To implement macro COMMON_INTERCEPTOR_HANDLE_VFORK in *vfork.S
extern "C" void __xsan_vfork_parent_after(void *sp);
/// Used to lock before fork
ALWAYS_INLINE void OnForkBefore() { XSAN_HOOKS_EXEC(OnForkBefore); }
/// Used to unlock after fork
ALWAYS_INLINE void OnForkAfter(bool is_child) {
  XSAN_HOOKS_EXEC(OnForkAfter, is_child);
}
void OnLibraryLoaded(const char *filename, void *handle);
void OnLibraryUnloaded();
ALWAYS_INLINE void OnLongjmp(void *env, const char *fn_name, uptr pc) {
  XSAN_HOOKS_EXEC(OnLongjmp, env, fn_name, pc);
}

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

PSEUDO_MACRO void ReadRange(void *_ctx, const void *offset, uptr size) {
  const XsanInterceptorContext *ctx = (XsanInterceptorContext *)_ctx;
  XSAN_HOOKS_EXEC(ReadRange, ctx->xsan_ctx, offset, size,
                  ctx->interceptor_name);
}
PSEUDO_MACRO void WriteRange(void *_ctx, const void *offset, uptr size) {
  const XsanInterceptorContext *ctx = (XsanInterceptorContext *)_ctx;
  XSAN_HOOKS_EXEC(WriteRange, ctx->xsan_ctx, offset, size,
                  ctx->interceptor_name);
}
PSEUDO_MACRO void CommonReadRange(void *_ctx, const void *offset, uptr size) {
  const XsanInterceptorContext *ctx = (XsanInterceptorContext *)_ctx;
  XSAN_HOOKS_EXEC(CommonReadRange, ctx->xsan_ctx, offset, size,
                  ctx->interceptor_name);
}
PSEUDO_MACRO void CommonWriteRange(void *_ctx, const void *offset, uptr size) {
  const XsanInterceptorContext *ctx = (XsanInterceptorContext *)_ctx;
  XSAN_HOOKS_EXEC(CommonWriteRange, ctx->xsan_ctx, offset, size,
                  ctx->interceptor_name);
}

}  // namespace __xsan
