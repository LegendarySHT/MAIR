#pragma once

#include "sanitizer_common/sanitizer_internal_defs.h"
#include "sanitizer_common/sanitizer_flag_parser.h"

// XSan flag values can be defined in four ways:
// 1) initialized with default values at startup.
// 2) overriden during compilation of XSan runtime by providing
//    compile definition XSAN_DEFAULT_OPTIONS.
// 3) overriden from string returned by user-specified function
//    __xsan_default_options().
// 4) overriden from env variable XSAN_OPTIONS.
// 5) overriden during XSan activation (for now used on Android only).

namespace __xsan {

struct Flags {
#define XSAN_FLAG(Type, Name, DefaultValue, Description) Type Name;
#include "xsan_flags.inc"
#undef XSAN_FLAG

  void SetDefaults();
};

extern Flags xsan_flags_dont_use_directly;
inline Flags *flags() {
  return &xsan_flags_dont_use_directly;
}

void InitializeFlags();

}  // namespace __xsan


