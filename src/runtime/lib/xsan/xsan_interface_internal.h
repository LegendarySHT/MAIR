#pragma once

#include "sanitizer_common/sanitizer_internal_defs.h"


using __sanitizer::uptr;
using __sanitizer::u64;
using __sanitizer::u32;

extern "C" {
  // This function should be called at the very beginning of the process,
  // before any instrumented code is executed and before any call to malloc.
  SANITIZER_INTERFACE_ATTRIBUTE void __xsan_init();

  // Performs cleanup before a NoReturn function. Must be called before things
  // like _exit and execl to avoid false positives on stack.
  SANITIZER_INTERFACE_ATTRIBUTE void __xsan_handle_no_return();


  SANITIZER_INTERFACE_ATTRIBUTE
  const char *__xsan_default_options();
}