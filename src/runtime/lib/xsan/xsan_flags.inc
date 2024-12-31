//===-- xsan_flags.inc ------------------------------------------*- C++ -*-===//
//
// XSan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef XSAN_FLAG
# error "Define XSAN_FLAG prior to including this file!"
#endif

// XSAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

XSAN_FLAG(bool, replace_intrin, true,
          "If set, uses custom wrappers for memset/memcpy/memmove intrinsics.")