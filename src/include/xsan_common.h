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
