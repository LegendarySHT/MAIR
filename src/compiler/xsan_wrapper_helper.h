#pragma once

#include "types.h"
#include "xsan_common.h"

// ---- Implemented in clang_wrapper.c/gcc_wrapper.c ----
typedef struct kXsanOption {
  u64 mask;
} XsanOption;

#define OPT_EQ(arg, opt) (!strcmp((arg), opt))
#define OPT_EQ_AND_THEN(arg, opt, ...)                                         \
  if (OPT_EQ(arg, opt)) {                                                      \
    __VA_ARGS__                                                                \
  }

#define OPT_MATCH(arg, opt) (!strncmp((arg), opt, sizeof(opt) - 1))
#define OPT_EMATCH_AND_THEN(arg, opt, ...)                                     \
  if (OPT_MATCH(arg, opt)) {                                                   \
    __VA_ARGS__                                                                \
  }
#define OPT_GET_VAL_AND_THEN(arg, opt, ...)                                    \
  if (OPT_MATCH(arg, (opt "="))) {                                             \
    const char *val = arg + sizeof(opt "=") - 1;                               \
    __VA_ARGS__                                                                \
  }

#define ADD_LLVM_MIDDLE_END_OPTION(opt)                                        \
  if (!frontend_opt.AsmAsSource) {                                             \
    cc_params[cc_par_cnt++] = "-mllvm";                                        \
    cc_params[cc_par_cnt++] = (opt);                                           \
  }

/* The following XSan functions is generally common, so they are implemented by
 * xsan_wrapper_helper.
 * However, for some platform-specific options, they need
 * to be handled separately, such as handle_asan_options/regist_pass_plugin in
 * clang_wrapper.
 */

static enum SanitizerType detect_san_type(const u32 argc, const char *argv[]);
static void init_sanitizer_setting(enum SanitizerType sanTy);
static void add_sanitizer_runtime(enum SanitizerType sanTy, u8 is_cxx,
                                  u8 is_dso, u8 needs_shared_rt);
static void find_obj(u8 *argv0);
static u8 handle_x_option(const u8 *const *arg, u8 *asm_as_source);
