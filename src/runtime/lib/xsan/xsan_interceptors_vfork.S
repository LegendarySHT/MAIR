#include "sanitizer_common/sanitizer_asm.h"

#if defined(__linux__)
#define COMMON_INTERCEPTOR_SPILL_AREA __xsan_vfork_before_and_after
#define COMMON_INTERCEPTOR_HANDLE_VFORK __xsan_vfork_parent_after
#include "sanitizer_common/sanitizer_common_interceptors_vfork_aarch64.inc.S"
#include "sanitizer_common/sanitizer_common_interceptors_vfork_arm.inc.S"
#include "sanitizer_common/sanitizer_common_interceptors_vfork_i386.inc.S"
#include "sanitizer_common/sanitizer_common_interceptors_vfork_loongarch64.inc.S"
#include "sanitizer_common/sanitizer_common_interceptors_vfork_riscv64.inc.S"
#include "sanitizer_common/sanitizer_common_interceptors_vfork_x86_64.inc.S"
#endif

NO_EXEC_STACK_DIRECTIVE
