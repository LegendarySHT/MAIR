//===-- xsan_preinit.cpp --------------------------------------------------===//
//
// This file is a part of ThreadSanitizer.
//
// Call __xsan_init at the very early stage of process startup.
//===----------------------------------------------------------------------===//

#include <sanitizer_common/sanitizer_internal_defs.h>

#if SANITIZER_CAN_USE_PREINIT_ARRAY

extern "C" {
extern void __xsan_init();
}

// This section is linked into the main executable when -fsanitize=address is
// specified to perform initialization at a very early stage.
__attribute__((section(".preinit_array"), used)) static auto preinit =
    __xsan_init;

#endif
