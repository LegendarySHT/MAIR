//===-- xsan_flags.cpp ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan flag parsing logic.
//===----------------------------------------------------------------------===//

#include "xsan_activation.h"
#include "xsan_flags.h"
#include "xsan_interface_internal.h"
#include "xsan_stack.h"
#include "lsan/lsan_common.h"
#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_flags.h>
#include <sanitizer_common/sanitizer_flag_parser.h>
#include "ubsan/ubsan_flags.h"
#include "ubsan/ubsan_platform.h"

namespace __xsan {

Flags xsan_flags_dont_use_directly;  // use via flags().

static const char *MaybeUseXsanDefaultOptionsCompileDefinition() {
#ifdef XSAN_DEFAULT_OPTIONS
  return SANITIZER_STRINGIFY(XSAN_DEFAULT_OPTIONS);
#else
  return "";
#endif
}

void Flags::SetDefaults() {
#define XSAN_FLAG(Type, Name, DefaultValue, Description) Name = DefaultValue;
#include "xsan_flags.inc"
#undef XSAN_FLAG
}

static void RegisterXsanFlags(FlagParser *parser, Flags *f) {
#define XSAN_FLAG(Type, Name, DefaultValue, Description) \
  RegisterFlag(parser, #Name, Description, &f->Name);
#include "xsan_flags.inc"
#undef XSAN_FLAG
}

void InitializeFlags() {
  // Set the default values and prepare for parsing ASan and common flags.
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.detect_leaks = cf.detect_leaks && CAN_SANITIZE_LEAKS;
    cf.external_symbolizer_path = GetEnv("XSAN_SYMBOLIZER_PATH");
    cf.malloc_context_size = kDefaultMallocContextSize;
    cf.intercept_tls_get_addr = true;
    cf.exitcode = 1;
    OverrideCommonFlags(cf);
  }
  Flags *f = flags();
  f->SetDefaults();

  FlagParser xsan_parser;
  RegisterXsanFlags(&xsan_parser, f);
  RegisterCommonFlags(&xsan_parser);


  // Override from ASan compile definition.
  const char *xsan_compile_def = MaybeUseXsanDefaultOptionsCompileDefinition();
  xsan_parser.ParseString(xsan_compile_def);

  // Override from user-specified string.
  const char *xsan_default_options = __xsan_default_options();
  xsan_parser.ParseString(xsan_default_options);

  // Override from command line.
  xsan_parser.ParseStringFromEnv("XSAN_OPTIONS");


  InitializeCommonFlags();

  // Flag validation:
  if (!CAN_SANITIZE_LEAKS && common_flags()->detect_leaks) {
    Report("%s: detect_leaks is not supported on this platform.\n",
           SanitizerToolName);
    Die();
  }

}

}  // namespace __xsan

SANITIZER_INTERFACE_WEAK_DEF(const char*, __xsan_default_options, void) {
  return "";
}
