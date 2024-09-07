//===-- xsan_preinit.cpp --------------------------------------------------===//
//
// This file is a part of ThreadSanitizer.
//
// Call __xsan_init at the very early stage of process startup.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_internal_defs.h"

#if SANITIZER_CAN_USE_PREINIT_ARRAY


extern "C" {
  extern void __xsan_init();
}

// The symbol is called __local_xsan_preinit, because it's not intended to be
// exported.
// This code linked into the main executable when -fsanitize=thread is in
// the link flags. It can only use exported interface functions.
__attribute__((section(".preinit_array"), used))
void (*__local_xsan_preinit)(void) = __xsan_init;

#endif
