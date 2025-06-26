#pragma once

/* Comment out to disable terminal colors (note that this makes afl-analyze
   a lot less nice): */

#define USE_COLOR

/* Comment out to disable fancy ANSI boxes and use poor man's 7-bit UI: */

#define FANCY_BOXES

enum SanitizerType { SanNone, ASan, TSan, MSan, UBSan, XSan, NumSanitizerTypes };

enum ObjectFormatType { MachO, COFF, ELF, UnknownObjectFormat };

// We have already defined and assigned values to the XSAN_CONTAINS_XXX macros in CMakeLists.txt
// we use these macros in clang_wrapper.c
