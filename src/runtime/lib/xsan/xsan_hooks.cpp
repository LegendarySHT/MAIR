//===-- xsan_hooks.cpp ------------------------------------------*- C++
//-*-===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is used to manuplate the hooks for coordination between multiple
// sanitizers.
//===----------------------------------------------------------------------===//

#include "xsan_hooks.h"

#include <sanitizer_common/sanitizer_common.h>

#include "xsan_interface_internal.h"
#include "xsan_internal.h"

// ---------------------- Memory Management Hooks -------------------
/// As XSan uses ASan's heap allocator and fake stack directly, hence we don't
/// need to invoke ASan's hooks here.
namespace __tsan {

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnXsanAllocHook(uptr ptr, uptr size, bool write, uptr pc) {}

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnXsanFreeHook(uptr ptr, bool write, uptr pc) {}

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnXsanAllocFreeTailHook(uptr pc) {}

SANITIZER_WEAK_CXX_DEFAULT_IMPL
void OnFakeStackDestory(uptr addr, uptr size) {}
}  // namespace __tsan

namespace __xsan {
void XsanAllocHook(uptr ptr, uptr size, bool write, uptr pc) {
  __tsan::OnXsanAllocHook(ptr, size, write, pc);
}

void XsanFreeHook(uptr ptr, bool write, uptr pc) {
  __tsan::OnXsanFreeHook(ptr, write, pc);
}

void XsanAllocFreeTailHook(uptr pc) { __tsan::OnXsanAllocFreeTailHook(pc); }

void OnFakeStackDestory(uptr addr, uptr size) {
  __tsan::OnFakeStackDestory(addr, size);
}

}  // namespace __xsan

// ---------- End of Memory Management Hooks -------------------

// ---------------------- Flags Registration Hooks ---------------
namespace __asan {
void InitializeFlags();
void SetCommonFlags(CommonFlags &cf);
void ValidateFlags();
}  // namespace __asan

namespace __tsan {
void InitializeFlags();
void SetCommonFlags(CommonFlags &cf);
void ValidateFlags();
}  // namespace __tsan

namespace __xsan {
void InitializeSanitizerFlags() {
  {
    ScopedSanitizerToolName tool_name("AddressSanitizer");
    // Initialize flags. This must be done early, because most of the
    // initialization steps look at flags().
    __asan::InitializeFlags();
  }
  {
    ScopedSanitizerToolName tool_name("ThreadSanitizer");
    __tsan::InitializeFlags();
  }
}

void SetSanitizerCommonFlags(CommonFlags &cf) {
  __asan::SetCommonFlags(cf);
  __tsan::SetCommonFlags(cf);
}

void ValidateSanitizerFlags() {
  __asan::ValidateFlags();
  __tsan::ValidateFlags();
}
}  // namespace __xsan

// ---------- End of Flags Registration Hooks ---------------

// ---------- Thread-Related Hooks --------------------------
namespace __asan {
SANITIZER_WEAK_CXX_DEFAULT_IMPL
void SetAsanThreadName(const char *name) {}
void SetAsanThreadNameByUserId(uptr uid, const char *name) {}
}  // namespace __asan

namespace __tsan {
SANITIZER_WEAK_CXX_DEFAULT_IMPL
void SetTsanThreadName(const char *name) {}
SANITIZER_WEAK_CXX_DEFAULT_IMPL
void SetTsanThreadNameByUserId(uptr uid, const char *name) {}
}  // namespace __tsan
namespace __xsan {
void SetSanitizerThreadName(const char *name) {
  __asan::SetAsanThreadName(name);
  __tsan::SetTsanThreadName(name);
}

void SetSanitizerThreadNameByUserId(uptr uid, const char *name) {
  /// Should be asanThreadRegistry().SetThreadNameByUserId(thread, name)
  /// But asan does not remember UserId's for threads (pthread_t);
  /// and remembers all ever existed threads, so the linear search by UserId
  /// can be slow.
  // __asan::SetAsanThreadNameByUserId(uid, name);
  __tsan::SetTsanThreadNameByUserId(uid, name);
}
}  // namespace __xsan