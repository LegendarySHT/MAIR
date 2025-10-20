#pragma once

namespace __xsan {

/// Use a different name (i.e., uintptr instead of uptr) to avoid type
/// conflicts.
#if defined(__UINTPTR_TYPE__)
#  if defined(__arm__) && defined(__linux__)
// Linux Arm headers redefine __UINTPTR_TYPE__ and disagree with clang/gcc.
typedef unsigned int uintptr;
typedef int sintptr;
#  else
typedef __UINTPTR_TYPE__ uintptr;
typedef __INTPTR_TYPE__ sintptr;
#  endif
#elif defined(_WIN64)
// 64-bit Windows uses LLP64 data model.
typedef unsigned long long uintptr;
typedef signed long long sintptr;
#elif defined(_WIN32)
typedef unsigned int uintptr;
typedef signed int sintptr;
#else
#  error Unsupported compiler, missing __UINTPTR_TYPE__
#endif  // defined(__UINTPTR_TYPE__)
#if defined(__x86_64__)
// Since x32 uses ILP32 data model in 64-bit hardware mode, we must use
// 64-bit pointer to unwind stack frame.
typedef unsigned long long uinthwptr;
#else
typedef uintptr uinthwptr;
#endif

enum class RegionType { App, Shadow, Gap };

struct MemRegion {
  uintptr beg;
  uintptr end;
  RegionType type;
  const char *desc;
};

#include "platforms/xsan_platform_aarch64_48.h"
#include "platforms/xsan_platform_x64_48.h"

#if SANITIZER_GO
#  error "unsupported platform"
#  if defined(__powerpc64__)
#  elif defined(__mips64)
#  elif defined(__s390x__)
#  elif defined(__aarch64__)
#  elif SANITIZER_WINDOWS
#  else
#  endif
#else  // SANITIZER_GO
#  if SANITIZER_IOS && !SANITIZER_IOSSIM
#    error "unsupported platform"
#  elif defined(__x86_64__) || SANITIZER_APPLE
using XsanMapping = MappingX64_48;
#  elif defined(__aarch64__)

/// TODO: adapt to dynamic mapping selection adhering to TSan, as
/// the VMA size in aarch64 could be dynamic changed to 39, 42, 48.
/// However, MSan does not support aarch64_39 and aarch64_42, so we
/// simply use aarch64_48 for now.
using XsanMapping = MappingAarch64_48;

#  elif defined(__powerpc64__)
#    error "unsupported platform"
#  elif defined(__mips64)
#    error "unsupported platform"
#  elif defined(__s390x__)
#    error "unsupported platform"
#  else
#    error "unsupported platform"
#  endif
#endif

}  // namespace __xsan
