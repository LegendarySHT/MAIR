#pragma once

enum SanitizerType { SanNone, ASan, TSan, UBSan, XSan };

enum ObjectFormatType { MachO, COFF, ELF, UnknownObjectFormat };

#ifndef XSAN_UBSAN
#define XSAN_UBSAN 1
#endif

#ifndef XSAN_TSAN
#define XSAN_TSAN 1
#endif

#ifndef XSAN_ASAN
#define XSAN_ASAN 1
#endif