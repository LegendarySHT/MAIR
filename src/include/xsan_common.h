#pragma once

enum SanitizerType { SanNone, ASan, TSan, MSan, UBSan, XSan };

enum ObjectFormatType { MachO, COFF, ELF, UnknownObjectFormat };

// We have already defined and assigned values to the XSAN_CONTAINS_XXX macros in CMakeLists.txt
// we use these macros in clang_wrapper.c
