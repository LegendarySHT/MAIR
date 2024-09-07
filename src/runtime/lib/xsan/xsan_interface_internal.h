#pragma once

#include "sanitizer_common/sanitizer_internal_defs.h"


using __sanitizer::uptr;
using __sanitizer::u64;
using __sanitizer::u32;

extern "C" {
  // This function should be called at the very beginning of the process,
  // before any instrumented code is executed and before any call to malloc.
  SANITIZER_INTERFACE_ATTRIBUTE void __xsan_init();
}