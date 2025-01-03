#pragma once

#include <sanitizer_common/sanitizer_flags.h>
#include <sanitizer_common/sanitizer_stacktrace.h>

#ifdef GET_STACK_TRACE_FATAL
/// TODO: Find a better way to avoidheader file MACRO redefinitions.
// Before: tsan_rtl.h   -> GET_STACK_TRACE_FATAL
// After : xsan_stack.h -> GET_STACK_TRACE_FATAL
#undef GET_STACK_TRACE_FATAL
#endif

// To reuse the macro GET_STACK_TRACE and etc.
#include "asan/orig/asan_stack.h"

namespace __xsan {

static const u32 kDefaultMallocContextSize = 30;

using __asan::GetMallocContextSize;
using __asan::SetMallocContextSize;

}  // namespace __xsan
