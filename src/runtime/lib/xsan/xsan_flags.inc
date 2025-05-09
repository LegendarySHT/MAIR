//===-- xsan_flags.inc ------------------------------------------*- C++ -*-===//
//
// XSan runtime flags.
//
//===----------------------------------------------------------------------===//
#ifndef XSAN_FLAG
#  error "Define XSAN_FLAG prior to including this file!"
#endif

// XSAN_FLAG(Type, Name, DefaultValue, Description)
// See COMMON_FLAG in sanitizer_flags.inc for more details.

XSAN_FLAG(bool, replace_intrin, true,
          "If set, uses custom wrappers for memset/memcpy/memmove intrinsics.")

XSAN_FLAG(bool, tsan_ignore_single_thread, true,
          "If set, disable TSan on single-threaded scenarios as possible.")

XSAN_FLAG(bool, tsan_ignore_runtime, false,
          "If set, disable TSan during runtime.")

XSAN_FLAG(bool, verify_xsan_link_order, true,
          "Check position of XSan runtime in library list (needs to be disabled"
          " when other library has to be preloaded system-wide)")

