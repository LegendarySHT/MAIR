#pragma once

#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_stacktrace.h"
// To reuse the macro GET_STACK_TRACE and etc.
#include "asan/asan_stack.h"

namespace __xsan {

static const u32 kDefaultMallocContextSize = 30;

void SetMallocContextSize(u32 size);
u32 GetMallocContextSize();

} // namespace __xsan
