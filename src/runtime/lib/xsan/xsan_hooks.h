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
// ---------------------- Hook for other Sanitizers -------------------
void XsanAllocHook(uptr ptr, uptr size, bool write, uptr pc);
void XsanFreeHook(uptr ptr, bool write, uptr pc);
void XsanAllocFreeTailHook(uptr pc);
void OnFakeStackDestory(uptr addr, uptr size);

void InitializeSanitizerFlags();
void SetSanitizerCommonFlags(CommonFlags &cf);
void ValidateSanitizerFlags();

}  // namespace __xsan