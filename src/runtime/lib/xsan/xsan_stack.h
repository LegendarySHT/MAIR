// For XSan internal use only.
#pragma once

#include <sanitizer_common/sanitizer_flags.h>
#include <sanitizer_common/sanitizer_stacktrace.h>

#include "xsan_attribute.h"
#include "xsan_flags.h"
#include "xsan_hooks.h"
#include "xsan_stack_interface.h"

namespace __xsan {

PSEUDO_MACRO void GetStackTraceCopy(BufferedStackTrace& stack) {
  if (RequireStackTraces<XsanStackTraceType::copy>()) {
    GetStackTrace(stack, ::__xsan::flags()->store_context_size,
                  common_flags()->fast_unwind_on_malloc);
  }
}

}  // namespace __xsan
