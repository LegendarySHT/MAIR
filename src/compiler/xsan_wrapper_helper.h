#pragma once

#include "types.h"
#include "xsan_common.h"

// ---- Implemented in clang_wrapper.c/gcc_wrapper.c ----
void add_param(const char *param);

typedef struct kXsanOption {
  u64 mask;
} XsanOption;

extern XsanOption xsan_options;
extern XsanOption xsan_recover_options;

static inline void init(XsanOption *opt) {
#if XSAN_CONTAINS_UBSAN
  opt->mask |= (u64)1 << UBSan;
#endif
#if XSAN_CONTAINS_TSAN
  opt->mask |= (u64)1 << TSan;
#endif
#if XSAN_CONTAINS_ASAN
  opt->mask |= (u64)1 << ASan;
#endif
  opt->mask |= (u64)1 << XSan;
}

/// TODO: handle unsupported sanTy.
static inline void set(XsanOption *opt, enum SanitizerType sanTy) {
  if (sanTy == XSan) {
    init(opt);
  } else {
    opt->mask |= (u64)1 << sanTy;
  }
}

static inline void clear(XsanOption *opt, enum SanitizerType sanTy) {
  opt->mask &= (sanTy == XSan) ? 0 : ~((u64)1 << sanTy);
}

static inline u8 has(XsanOption *opt, enum SanitizerType sanTy) {
  return (opt->mask & (((u64)1 << sanTy))) != 0;
}

static inline u8 has_any(XsanOption *opt) {
  return (opt->mask & ~(((u64)1 << XSan) | ((u64)1 << SanNone))) != 0;
}

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

enum SanitizerType detect_san_type(const u32 argc, const char *argv[]);
void init_sanitizer_setting(enum SanitizerType sanTy);
void add_wrap_link_option(enum SanitizerType sanTy, u8 is_cxx);
void add_sanitizer_runtime(enum SanitizerType sanTy, u8 is_cxx, u8 is_dso,
                           u8 needs_shared_rt);
