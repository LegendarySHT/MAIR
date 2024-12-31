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

#include "xsan_flags.h"

#include "sanitizer_common/sanitizer_common.h"
#include "sanitizer_common/sanitizer_flag_parser.h"
#include "sanitizer_common/sanitizer_flags.h"
#include "sanitizer_common/sanitizer_win_interception.h"


#include "ubsan/ubsan_flags.h"
#include "ubsan/ubsan_platform.h"
#include "xsan_hooks.h"
#include "xsan_interface_internal.h"
#include "xsan_stack.h"

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

static void DisplayHelpMessages(FlagParser *parser) {
  // TODO(eugenis): dump all flags at verbosity>=2?
  if (Verbosity()) {
    ReportUnrecognizedFlags();
  }

  if (common_flags()->help) {
    parser->PrintFlagDescriptions();
  }
}

static void InitializeDefaultFlags() {
  Flags *f = flags();
  FlagParser xsan_parser;

  // Set the default values and prepare for parsing ASan and common flags.
  SetCommonFlagsDefaults();
  {
    CommonFlags cf;
    cf.CopyFrom(*common_flags());
    cf.external_symbolizer_path = GetEnv("XSAN_SYMBOLIZER_PATH");
    cf.malloc_context_size = kDefaultMallocContextSize;
    cf.intercept_tls_get_addr = true;
    /// Set exitcode to 66 for TSan for 
    cf.exitcode = 66; // TSan
    // cf.exitcode = 1; // ASan
    SetSanitizerCommonFlags(cf);
    OverrideCommonFlags(cf);
  }
  f->SetDefaults();

  RegisterXsanFlags(&xsan_parser, f);
  RegisterCommonFlags(&xsan_parser);

#if CAN_SANITIZE_UB
  __ubsan::Flags *uf = __ubsan::flags();
  uf->SetDefaults();

  FlagParser ubsan_parser;
  __ubsan::RegisterUbsanFlags(&ubsan_parser, uf);
  RegisterCommonFlags(&ubsan_parser);
#endif

  // Override from ASan compile definition.
  const char *xsan_compile_def = MaybeUseXsanDefaultOptionsCompileDefinition();
  xsan_parser.ParseString(xsan_compile_def);

  // Override from user-specified string.
  const char *xsan_default_options = __xsan_default_options();
  xsan_parser.ParseString(xsan_default_options);

#if CAN_SANITIZE_UB
  const char *ubsan_default_options = __ubsan_default_options();
  ubsan_parser.ParseString(ubsan_default_options);
#endif

  // Override from command line.
  xsan_parser.ParseStringFromEnv("XSAN_OPTIONS");
#if CAN_SANITIZE_UB
  ubsan_parser.ParseStringFromEnv("UBSAN_OPTIONS");
#endif

  InitializeSanitizerFlags();
  InitializeCommonFlags();

  // Flag validation:

  // TODO(samsonov): print all of the flags (ASan, LSan, common).
  DisplayHelpMessages(&xsan_parser);

  CHECK_LE((uptr)common_flags()->malloc_context_size, kStackTraceMax);
}


void InitializeFlags() {
  InitializeDefaultFlags();
  ValidateSanitizerFlags();

#if SANITIZER_WINDOWS
  // On Windows, weak symbols are emulated by having the user program
  // register which weak functions are defined.
  // The ASAN DLL will initialize flags prior to user module initialization,
  // so __asan_default_options will not point to the user definition yet.
  // We still want to ensure we capture when options are passed via
  // __asan_default_options, so we add a callback to be run
  // when it is registered with the runtime.

  // There is theoretically time between the initial ProcessFlags and
  // registering the weak callback where a weak function could be added and we
  // would miss it, but in practice, InitializeFlags will always happen under
  // the loader lock (if built as a DLL) and so will any calls to
  // __sanitizer_register_weak_function.
  AddRegisterWeakFunctionCallback(
      reinterpret_cast<uptr>(__xsan_default_options), []() {
        FlagParser asan_parser;

        RegisterAsanFlags(&asan_parser, flags());
        RegisterCommonFlags(&asan_parser);
        asan_parser.ParseString(__asan_default_options());

        DisplayHelpMessages(&asan_parser);
        ProcessFlags();

        // TODO: Update other globals and data structures that may need to change
        // after initialization due to new flags potentially being set changing after
        // `__asan_default_options` is registered.
        // See GH issue 'https://github.com/llvm/llvm-project/issues/117925' for
        // details.
        SetAllocatorMayReturnNull(common_flags()->allocator_may_return_null);
      });

#  if CAN_SANITIZE_UB
  AddRegisterWeakFunctionCallback(
      reinterpret_cast<uptr>(__ubsan_default_options), []() {
        FlagParser ubsan_parser;

        __ubsan::RegisterUbsanFlags(&ubsan_parser, __ubsan::flags());
        RegisterCommonFlags(&ubsan_parser);
        ubsan_parser.ParseString(__ubsan_default_options());

        // To match normal behavior, do not print UBSan help.
        ProcessFlags();
      });
#  endif
#endif
}


}  // namespace __xsan

SANITIZER_INTERFACE_WEAK_DEF(const char *, __xsan_default_options, void) {
  return "";
}
