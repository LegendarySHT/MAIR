#pragma once

#include <sanitizer_common/sanitizer_flags.h>
#include <sanitizer_common/sanitizer_stacktrace.h>
// To reuse the macro GET_STACK_TRACE and etc.
#include "asan/orig/asan_stack.h"

namespace __xsan {

static const u32 kDefaultMallocContextSize = 30;

using __asan::GetMallocContextSize;
using __asan::SetMallocContextSize;

}  // namespace __xsan
