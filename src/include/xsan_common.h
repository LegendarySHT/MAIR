#pragma once

/* Comment out to disable terminal colors (note that this makes afl-analyze
   a lot less nice): */

#define USE_COLOR

/* Comment out to disable fancy ANSI boxes and use poor man's 7-bit UI: */

#define FANCY_BOXES

enum SanitizerType {
  /* As both options and sanitizer type*/
  SanNone = 0,
  ASan = 1,
  TSan = 2,
  MSan = 3,
  UBSan = 4,
  /// XSan mode with auto supported sanitizers specification
  XSan = 5,
  /* Only as option*/
  /// XSan mode with manual sanitizer specification
  XSanOnly = 6,
  /* Utility enum */
  NumSanitizerTypes = 6,
  /* Sanitizer type XSan has two options: -xsan and -xsan-only */
  NumSanitizerOptions = 7,
};

enum ObjectFormatType { MachO, COFF, ELF, UnknownObjectFormat };

#define ERR_MSG_4_ORIG_PASS                                                    \
  "The original sanitizer pass is incompatible with other sanitizers, "        \
  "and its memory mappings differ under XSan;\n"                               \
  "I.e., it will lead to a SIGSEGV during runtime.\n\n"                        \
  "Possible causes:\n"                                                         \
  "  - Livepatch failed or did not apply to this clang build (especially "     \
  "for customized Clang).\n"                                                   \
  "  - You are using a Clang build that does not export the expected "         \
  "dynamic symbols.\n"                                                         \
  "  - The input code has already been instrumented by some "                  \
  "sanitizer.\n\n"                                                             \
  "Recommended actions:\n"                                                     \
  "  1) Verify Clang is a standard build (built from the official "            \
  "sources, installed via apt, or from the official clang+llvm "               \
  "archive).\n"                                                                \
  "  2) If you have a customized Clang, the standard hotpatch may not "        \
  "work — either rebuild a livepatch adapted to your Clang, or manually "    \
  "patch Clang sources to disable the original sanitizer pass.\n"              \
  "    2-1) For manual patching, refer to the changes in llvm-15.0.7.patch "   \
  "as a reference and apply equivalent edits to your Clang source.\n"          \
  "    2-2) For livepatch-based fixes, examine "                               \
  "src/compiler/livepatch/clang-15/BackendUtil.cpp and the LLVM "              \
  "addSanitizers logic to craft a livepatch matching your Clang.\n\n"          \
  "Aborting to avoid undefined behavior / crash.\n"
