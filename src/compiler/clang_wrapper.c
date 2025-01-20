/**
 * Modified from AFL afl-clang-fast.c and Angora angora-clang.c,
 * as a wrapper for clang. 
 *
 * TODO: use AFLplusplus's afl-cc.c as base, which is more powerful, supporting LTO.
 */
#define AFL_MAIN

#include "include/config.h"
#include "include/types.h"
#include "include/debug.h"
#include "include/alloc-inl.h"
#include "xsan_common.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


static u8*  obj_path;               /* Path to runtime libraries         */
static const u8 **cc_params;        /* Parameters passed to the real CC  */
static u32  cc_par_cnt = 1;         /* Param count, including argv0      */

#ifndef XSAN_PATH
  #define XSAN_PATH ""
#endif

/* MODIFIED FROM AFL++。 TODO: Don't hard code embed linux */
/* Try to find a specific runtime we need, returns NULL on fail. */

/*
  in find_object() we look here:

  1. if obj_path is already set we look there first
  2. then we check the $XSAN_PATH environment variable location if set
  3. next we check argv[0] if it has path information and use it
    a) we also check ../lib/linux
  4. if 3. failed we check /proc (only Linux, Android, NetBSD, DragonFly, and
     FreeBSD with procfs)
    a) and check here in ../lib/linux too
  5. we look into the XSAN_PATH define (usually /usr/local/lib/afl)
  6. we finally try the current directory

  if all these attempts fail - we return NULL and the caller has to decide
  what to do.
*/

static u8 *find_object(u8 *obj, u8 *argv0) {

  u8 *xsan_path = getenv("XSAN_PATH");
  u8 *slash = NULL, *tmp;

  if (xsan_path) {
    tmp = alloc_printf("%s/%s", xsan_path, obj);
    if (!access(tmp, R_OK)) {
      obj_path = xsan_path;
      return tmp;
    }
    ck_free(tmp);
  }

  if (argv0) {

    slash = strrchr(argv0, '/');
    if (slash) {

      u8 *dir = ck_strdup(argv0);

      slash = strrchr(dir, '/');
      *slash = 0;

      tmp = alloc_printf("%s/%s", dir, obj);
      if (!access(tmp, R_OK)) {
        obj_path = dir;
        return tmp;
      }

      ck_free(tmp);
      tmp = alloc_printf("%s/../lib/linux/%s", dir, obj);
      if (!access(tmp, R_OK)) {
        u8 *dir2 = alloc_printf("%s/../lib/linux", dir);
        obj_path = dir2;
        ck_free(dir);
        return tmp;
      }

      ck_free(tmp);
      ck_free(dir);

    }

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__linux__) || \
    defined(__ANDROID__) || defined(__NetBSD__)
  #define HAS_PROC_FS 1
#endif
#ifdef HAS_PROC_FS
    else {
      char *procname = NULL;
  #if defined(__FreeBSD__) || defined(__DragonFly__)
      procname = "/proc/curproc/file";
  #elif defined(__linux__) || defined(__ANDROID__)
      procname = "/proc/self/exe";
  #elif defined(__NetBSD__)
      procname = "/proc/curproc/exe";
  #endif
      if (procname) {
        char    exepath[PATH_MAX];
        ssize_t exepath_len = readlink(procname, exepath, sizeof(exepath));
        if (exepath_len > 0 && exepath_len < PATH_MAX) {

          exepath[exepath_len] = 0;
          slash = strrchr(exepath, '/');

          if (slash) {

            *slash = 0;
            tmp = alloc_printf("%s/%s", exepath, obj);
            if (!access(tmp, R_OK)) {
              u8 *dir = alloc_printf("%s", exepath);
              obj_path = dir;
              return tmp;
            }
            ck_free(tmp);
            tmp = alloc_printf("%s/../lib/linux/%s", exepath, obj);
            if (!access(tmp, R_OK)) {
              u8 *dir = alloc_printf("%s/../lib/linux/", exepath);
              obj_path = dir;
              return tmp;
            }
          }
        }
      }
    }

#endif
#undef HAS_PROC_FS
  }

  tmp = alloc_printf("%s/%s", XSAN_PATH, obj);
  if (!access(tmp, R_OK)) {
    obj_path = XSAN_PATH;
    return tmp;
  }
  ck_free(tmp);

  tmp = alloc_printf("./%s", obj);
  if (!access(tmp, R_OK)) {
    obj_path = ".";
    return tmp;
  }
  ck_free(tmp);

  return NULL;
}

/* Try to find the runtime libraries. If that fails, abort. */

static void find_obj(u8* argv0) {

  obj_path = find_object("", argv0);

  if (!obj_path) {
    FATAL("Unable to find object path. Please set XSAN_PATH");
  }

}

struct FrontEndOpt {
  // Middle-end options could not transfer to the asm compilation.
  u8 AsmAsSource;
  /* -no-integrated-as , 0 dy default */
  u8 DisableIntegratedAS;
  u8 SanitizeAddressGlobalsDeadStripping;
  enum ObjectFormatType obj_format;
} frontend_opt = {.AsmAsSource = 0,
                  .DisableIntegratedAS = 0,
                  .SanitizeAddressGlobalsDeadStripping = 1,
// { MachO, COFF, ELF, GOFF, XCOFF, UnknownObjectFormat };
#if defined(__APPLE__)
                  .obj_format = MachO
#elif defined(__linux__)
                   .obj_format = ELF
#elif defined(_WIN32)
                   .obj_format = COFF
#else
                   .obj_format = UnknownObjectFormat
#endif

};

typedef struct kXsanOption {
  u64 mask;
} XsanOption;

XsanOption xsan_options;
XsanOption xsan_recover_options;

void init(XsanOption *opt) {
#if XSAN_UBSAN
  opt->mask |= (u64)1 << UBSan;
#endif
#if XSAN_TSAN
  opt->mask |= (u64)1 << TSan;
#endif
#if XSAN_ASAN
  opt->mask |= (u64)1 << ASan;
#endif
  opt->mask |= (u64)1 << XSan;
}

/// TODO: handle unsupported sanTy.
void set(XsanOption *opt, enum SanitizerType sanTy) {
  if (sanTy == XSan) {
    init(opt);
  } else {
    opt->mask |= (u64)1 << sanTy;
  }
}

void clear(XsanOption *opt, enum SanitizerType sanTy) {
  opt->mask &= (sanTy == XSan) ? 0 : ~((u64)1 << sanTy);
}

u8 has(XsanOption *opt, enum SanitizerType sanTy) {
  return (opt->mask & (((u64)1 << sanTy))) != 0;
}

u8 has_any(XsanOption *opt) {
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

#define ADD_MIDDLE_END_OPTION(opt)                                             \
  if (!frontend_opt.AsmAsSource) {                                             \
    cc_params[cc_par_cnt++] = "-mllvm";                                        \
    cc_params[cc_par_cnt++] = (opt);                                           \
  }

static enum SanitizerType detect_san_type(const u32 argc, const char *argv[]) {
  enum SanitizerType xsanTy = SanNone;
  for (u32 i = 1; i < argc; i++) {
    const char* cur = argv[i];
    OPT_EQ_AND_THEN(cur, "-tsan", {
      if (has(&xsan_options, ASan))
        FATAL("'-tsan' could not be used with '-asan'");
      xsanTy = TSan;
      set(&xsan_options, TSan);
      continue;
    })

    OPT_EQ_AND_THEN(cur, "-asan", {
      if (has(&xsan_options, TSan))
        FATAL("'-asan' could not be used with '-tsan'");
      xsanTy = ASan;
      set(&xsan_options, ASan);
      continue;
    })

    OPT_EQ_AND_THEN(cur, "-ubsan", {
      /// Only if no other sanitizer is specified, we treat it as UBSan
      /// standalone.
      if (!has_any(&xsan_options))
        xsanTy = UBSan;
      set(&xsan_options, UBSan);
      continue;
    })

    OPT_EQ_AND_THEN(cur, "-xsan", {
      xsanTy = XSan;
      init(&xsan_options);
      continue;
    })

    u8 is_neg = 0;
    // Check prefix "-f"
    if (cur[0] != '-' || cur[1] != 'f') {
      continue;
    }
    cur += 2;
    // Check prefix "no-"
    if (cur[0] == 'n' && cur[1] == 'o' && cur[2] == '-') {
      is_neg = 1;
      cur += 3;
    }

    // -fsanitize=<value> / -fno-sanitize=<value>
    OPT_GET_VAL_AND_THEN(cur, "sanitize", {
      enum SanitizerType sanTy = SanNone;
      if (OPT_EQ(val, "all")) {
        sanTy = XSan;
      } else if (OPT_EQ(val, "address")) {
        sanTy = ASan;
      } else if (OPT_EQ(val, "thread")) {
        sanTy = TSan;
      } else if (OPT_EQ(val, "undefined")) {
        sanTy = UBSan;
      }

      if (is_neg) {
        clear(&xsan_options, sanTy);
      } else {
        set(&xsan_options, sanTy);
      }

      continue;
    })
  }

  /// Use our out-of-tree runtime
  if (xsanTy != SanNone && !has_any(&xsan_options)) {
    xsanTy = SanNone;
  }

  return xsanTy;
}

/*
 Handle the following options about ASan:
  -fsanitize-address-field-padding=<value>
  -fsanitize-address-globals-dead-stripping
  -fsanitize-address-poison-custom-array-cookie
  -fsanitize-address-use-after-return=<mode>
  -fsanitize-address-use-after-scope
  -fsanitize-address-use-odr-indicator
  -fsanitize-address-outline-instrumentation
  -fsanitize-recover=address|all
 Replace with 
   -mllvm -xxx
  TODO: refer to SanitizerArgs.cpp:1193, to support 
    1. -fsanitize=address,pointer-compare
    2. -fsanitize=address,pointer-subtract
 */
static u8 handle_asan_options(const char* arg, u8 is_mllvm_arg, u8 is_neg) {
  if (is_mllvm_arg) {
    if (!OPT_MATCH(arg, "-asan-")) {
      return 0;
    }
    arg += sizeof("-asan-") - 1;
    cc_params[cc_par_cnt++] = alloc_printf("-as-%s", arg);
    return 1;
  }

  if (!OPT_MATCH(arg, "address-")) {
    return 0;
  }
  arg += sizeof("address-") - 1;

  // -fsanitize-address-globals-dead-stripping
  OPT_EQ_AND_THEN(arg, "globals-dead-stripping", {
    frontend_opt.SanitizeAddressGlobalsDeadStripping = is_neg ? 0 : 1;
    return 0;
  })

  // -fsanitize-address-poison-custom-array-cookie
  OPT_EQ_AND_THEN(arg, "poison-custom-array-cookie", {
    // TODO
    return 0;
  })

  // -fsanitize-address-use-after-return=<mode>
  // frontend option, forward to middle-end option defined in PassRegistry.cpp
  OPT_GET_VAL_AND_THEN(arg, "use-after-return", {
    ADD_MIDDLE_END_OPTION(
        alloc_printf("-sanitize-address-use-after-return=%s", val));
    return 0;
  })

  // -fsanitize-address-use-after-scope
  // frontend option, forward to middle-end option defined in PassRegistry.cpp
  OPT_EQ_AND_THEN(arg, "use-after-scope", {
    ADD_MIDDLE_END_OPTION(is_neg ? "-sanitize-address-use-after-scope=0"
                                 : "-sanitize-address-use-after-scope");
    return 0;
  })

  // -fsanitize-address-use-odr-indicator
  // frontend option, forward to middle-end option defined in PassRegistry.cpp
  OPT_EQ_AND_THEN(arg, "use-odr-indicator", {
    ADD_MIDDLE_END_OPTION(is_neg ? "-sanitize-address-use-odr-indicator=0"
                                 : "-sanitize-address-use-odr-indicator");
    return 0;
  })

  // -fsanitize-address-destructor=<value>
  // frontend option, forward to middle-end option defined in PassRegistry.cpp
  OPT_GET_VAL_AND_THEN(arg, "destructor", {
    ADD_MIDDLE_END_OPTION(alloc_printf("-sanitize-address-destructor=%s", val));
    return 0;
  })

  // -fsanitize-address-outline-instrumentation
  OPT_EQ_AND_THEN(arg, "outline-instrumentation", {
    // clang driver translates this frontend option to middle-end options
    //          -mllvm -asan-instrumentation-with-call-threshold=0
    ADD_MIDDLE_END_OPTION(is_neg ? "-as-instrumentation-with-call-threshold"
                                 : "-as-instrumentation-with-call-threshold=0");
    return 1;
  })

  return 0;
}

/*
 Handle the following options about TSan:
  -fsanitize-thread-atomics
  -fsanitize-thread-func-entry-exit
  -fsanitize-thread-memory-access
  -fsanitize-recover=address|all
 Replace with 
   -mllvm -xxx
 Refer to SanitizerArgs.cpp:1142~1155
 */
static u8 handle_tsan_options(const char *arg, u8 is_mllvm_arg, u8 is_neg) {
  if (is_mllvm_arg) {
    if (!OPT_MATCH(arg, "-tsan-")) {
      return 0;
    }
    arg += sizeof("-tsan-") - 1;
    cc_params[cc_par_cnt++] = alloc_printf("-ts-%s", arg);
    return 1;
  }

  if (!OPT_MATCH(arg, "thread-")) {
    return 0;
  }
  arg += sizeof("thread-") - 1;

  // -fsanitize-thread-atomics
  OPT_EQ_AND_THEN(arg, "atomics", {
    // The clang driver translates this frontend option to middle-end options
    //          -mllvm -tsan-instrument-atomics=0
    if (!is_neg) {
      return 0;
    }
    ADD_MIDDLE_END_OPTION("-ts-instrument-atomics=0");
    return 1;
  })

  // -fsanitize-thread-func-entry-exit
  OPT_EQ_AND_THEN(arg, "func-entry-exit", {
    // The clang driver translates this frontend option to middle-end options
    //          -mllvm -tsan-instrument-func-entry-exit=0
    if (!is_neg) {
      return 0;
    }
    ADD_MIDDLE_END_OPTION("-ts-instrument-func-entry-exit=0");
    return 1;
  })

  // -fsanitize-thread-memory-access
  OPT_EQ_AND_THEN(arg, "memory-access", {
    // The clang driver translates this frontend option to middle-end options
    //          -mllvm -tsan-instrument-memory-accesses=0
    //          -mllvm -tsan-instrument-memintrinsics=0
    if (!is_neg) {
      return 0;
    }
    ADD_MIDDLE_END_OPTION("-ts-instrument-memory-accesses=0");
    ADD_MIDDLE_END_OPTION("-ts-instrument-memintrinsics=0");
    return 1;
  })

  return 0;
}

static u8 handle_ubsan_options(const char* opt) {
  return 0;
}



/* 
  Ports the arguments for sanitizers to our plugin sanitizers
  Dicards the original argument if return 1.

*/
static u8 handle_sanitizer_options(const char *arg, u8 is_mllvm_arg,
                                   enum SanitizerType sanTy) {
  OPT_EQ_AND_THEN(arg, "-lib-only", { return 1; })

  if (OPT_EQ(arg, "-asan") || OPT_EQ(arg, "-tsan") 
      || OPT_EQ(arg, "-ubsan") || OPT_EQ(arg, "-xsan")) {
    return 1;
  }

  u8 is_neg = 0;
  /// If this arg is not a mllvm option, do not check for -fsanitize-xxx
  if (!is_mllvm_arg) {
    // Check prefix "-f"
    if (arg[0] != '-' || arg[1] != 'f') {
      return 0;
    }
    arg += 2;
    // Check prefix "no-"
    if (arg[0] == 'n' && arg[1] == 'o' && arg[2] == '-') {
      is_neg = 1;
      arg += 3;
    }
    // Check prefix "sanitize-"
    if (!OPT_MATCH(arg, "sanitize-")) {
      return 0;
    }

    arg += sizeof("sanitize-") - 1;
  }

  // -fsanitize-recover
  // frontend option, forward to middle-end option defined in PassRegistry.cpp
  OPT_GET_VAL_AND_THEN(arg, "recover", {
    OPT_EQ_AND_THEN(val, "all", {
      if (is_neg) {
        clear(&xsan_recover_options, XSan);
      } else {
        set(&xsan_recover_options, XSan);
      }
    })
    OPT_EQ_AND_THEN(val, "address", {
      if (is_neg) {
        clear(&xsan_recover_options, ASan);
      } else {
        set(&xsan_recover_options, ASan);
      }
    })
    return 0;
  })

  switch (sanTy) {
  case ASan:
    return handle_asan_options(arg, is_mllvm_arg, is_neg);
  case TSan:
    return handle_tsan_options(arg, is_mllvm_arg, is_neg);
  case UBSan:
    return handle_ubsan_options(arg);
  case XSan:
    return handle_asan_options(arg, is_mllvm_arg, is_neg) |
           handle_tsan_options(arg, is_mllvm_arg, is_neg) | handle_ubsan_options(arg);
  case SanNone:
    break;
  }

  return 0;
}

static void init_sanitizer_setting(enum SanitizerType sanTy) {
  switch (sanTy) {
  case ASan:
  case TSan:
  case UBSan:
  case XSan:
    // Use env var to control clang only perform frontend 
    // transformation for sanitizers.
    setenv("XSAN_ONLY_FRONTEND", "1", 1);
    // Reuse the frontend code relevant to sanitizer
    if (has(&xsan_options, ASan)) {
      cc_params[cc_par_cnt++] = "-fsanitize=address";
    }
    if (has(&xsan_options, TSan)) {
      cc_params[cc_par_cnt++] = "-fsanitize=thread";
    }
    if (has(&xsan_options, UBSan)) {
      cc_params[cc_par_cnt++] = "-fsanitize=undefined";
      if (!!getenv("XSAN_IN_ASAN_TEST") || !!getenv("XSAN_IN_TSAN_TEST")) {
        /// There are so many C testcases of TSan/ASan end with suffix ".cpp",
        /// leading to the compiler frontend set `getLangOpts().CPlusPlus =
        /// true`. Subsequently, the C++ only check `-fsanitize=function` is
        /// applied, and its dependency on RTTI makes the C testcases fail to
        /// compile. Therefore, to make test pipepline happy, we need to disable
        /// the `-fsanitize=function` option.

        /// Notably, LLVM 17 uses type hash instead of RTTI to check function
        /// type, and thus supports both C/C++ code without RTTI. See the
        /// following commit for details:
        ///   - No RTTI:
        ///   https://github.com/llvm/llvm-project/commit/46f366494f3ca8cc98daa6fb4f29c7c446c176b6#diff-da4776ddc2b1fa6aaa0d2e00ff8a835dbec6d0606d2960c94875dc0502d222b8
        ///   - Support C:
        ///   https://github.com/llvm/llvm-project/commit/279a4d0d67c874e80c171666822f2fabdd6fa926#diff-9f23818ed51d0b117b5692129d0801721283d0f128a01cbc562353da0266d7adL948
      
        cc_params[cc_par_cnt++] = "-fno-sanitize=function";
      }
    }
    break;
  case SanNone:
    return;
  }
}

static u8 asan_use_globals_gc() {
  /*
    static bool asanUseGlobalsGC(const Triple &T, const CodeGenOptions &CGOpts) {
      if (!CGOpts.SanitizeAddressGlobalsDeadStripping)
        return false;
      switch (T.getObjectFormat()) {
      case Triple::MachO:
      case Triple::COFF:
        return true;
      case Triple::ELF:
        return !CGOpts.DisableIntegratedAS;
      case Triple::GOFF:
        llvm::report_fatal_error("ASan not implemented for GOFF");
      case Triple::XCOFF:
        llvm::report_fatal_error("ASan not implemented for XCOFF.");
      case Triple::Wasm:
      case Triple::DXContainer:
      case Triple::SPIRV:
      case Triple::UnknownObjectFormat:
        break;
      }
      return false;
    }
  */
  if (!frontend_opt.SanitizeAddressGlobalsDeadStripping) {
    return 0;
  }
  switch (frontend_opt.obj_format) {
    case MachO:
    case COFF:
      return 1;
    case ELF:
      return !frontend_opt.DisableIntegratedAS;
    default:
      return 0;
  }
}

/* This function forwards the frontend options that could not handled by XSan to
 * the middle-end options that XSan can handle.The relevant middle-end options
 * are defined in instrumenation/PassRegistry.cpp */
static void add_pass_options(enum SanitizerType sanTy) {
  /// We only add pass options if there is any sanitizers in XSan-project
  /// activated.
  if (sanTy == SanNone) {
    return;
  }

  if (has(&xsan_options, ASan)) {
    if (!asan_use_globals_gc()) {
      ADD_MIDDLE_END_OPTION("-asan-globals-gc=0");
    }

    if (has(&xsan_recover_options, ASan)) {
      ADD_MIDDLE_END_OPTION("-sanitize-recover-address");
    }
  }
}

static void regist_pass_plugin(enum SanitizerType sanTy) {
  /**
   * Need to enable corresponding llvm optimization level, 
   * where your pass is registed.
   */
  u8* san_pass= "";
  switch (sanTy) {
  case ASan:
    san_pass = "ASanInstPass.so";
    cc_params[cc_par_cnt++] = "-U_FORTIFY_SOURCE";
    cc_params[cc_par_cnt++] = "-D__SANITIZE_ADDRESS__";
    break;
  case TSan:
    san_pass = "TSanInstPass.so";
    break;
  case XSan:
    san_pass = "XSanInstPass.so";
    cc_params[cc_par_cnt++] = "-U_FORTIFY_SOURCE";
    cc_params[cc_par_cnt++] = "-D__SANITIZE_ADDRESS__";
    break;
  case UBSan:
  case SanNone:
    return;
  }

  /* The relevant code in Frontend/CompilerInvocation.cpp:CreateFromArgsImpl
    // FIXME: Override value name discarding when asan or msan is used because the
    // backend passes depend on the name of the alloca in order to print out
    // names.
    Res.getCodeGenOpts().DiscardValueNames &=
        !LangOpts.Sanitize.has(SanitizerKind::Address) &&
        !LangOpts.Sanitize.has(SanitizerKind::KernelAddress) &&
        !LangOpts.Sanitize.has(SanitizerKind::Memory) &&
        !LangOpts.Sanitize.has(SanitizerKind::KernelMemory)
   */
  cc_params[cc_par_cnt++] = "-fno-discard-value-names";


  san_pass = alloc_printf("%s/pass/%s", obj_path, san_pass);

  /* 
    From this issue https://github.com/llvm/llvm-project/issues/56137 
    To pass the option to plugin pass, we need add extra options
  */
  // cc_params[cc_par_cnt++] = "-Xclang";
  // cc_params[cc_par_cnt++] = "-load";
  // cc_params[cc_par_cnt++] = "-Xclang";
  // cc_params[cc_par_cnt++] = san_pass;
  // cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = alloc_printf("-fplugin=%s", san_pass);
  cc_params[cc_par_cnt++] = alloc_printf("-fpass-plugin=%s", san_pass);



  /**
   * Transfer the option to pass by `-mllvm -<opt>`
   */
  if (sanTy != XSan) {
    return;
  }
  if (!has(&xsan_options, ASan)) {
    ADD_MIDDLE_END_OPTION("-xsan-disable-asan");
  }
  if (!has(&xsan_options, TSan)) {
    ADD_MIDDLE_END_OPTION("-xsan-disable-tsan");
  }
}

static void add_wrap_link_option(enum SanitizerType sanTy, u8 is_cxx) {
  if (sanTy != XSan)
    return;

  /// TODO: only add this link arguments while TSan is enabled togother with LSan.
  // Use Linker Response File to include lots of -wrap=<symbol> options in one file.
  cc_params[cc_par_cnt++] = alloc_printf("-Wl,@%s/share/xsan_wrapped_symbols.txt", obj_path);
}

static void add_sanitizer_runtime(enum SanitizerType sanTy, u8 is_cxx, u8 is_dso) {

  /**
   * Need to enable corresponding llvm optimization level, 
   * where your pass is registed.
   */
  u8* san = "";
  switch (sanTy) {
  case ASan:
    san = "asan";
    break;
  case TSan:
    san = "tsan";
    break;
  case UBSan:
    san = "ubsan_standalone";
    break;
  case XSan:
    san = "xsan";
    break;
  case SanNone:
    return;
  }

  add_wrap_link_option(sanTy, is_cxx);

  /**
    // Always link the static runtime regardless of DSO or executable.
    if (SanArgs.needsAsanRt())
      HelperStaticRuntimes.push_back("asan_static");

    // Collect static runtimes.
    if (Args.hasArg(options::OPT_shared)) {
      // Don't link static runtimes into DSOs.
      return;
    }
  */
  
  if (sanTy == ASan || sanTy == XSan) {
    // Link all contents in *.a, rather than only link symbols in demands.
    // e.g., link preinit_array symbol, which is not used in user program.
    cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
    // TODO: eliminate "linux" in path, and do not hard-coded embed x86_64
    cc_params[cc_par_cnt++] = alloc_printf("%s/lib/linux/libclang_rt.%s_static-x86_64.a", obj_path, san);
    // Deativate the effect of `--whole-archive`, i.e., only link symbols in demands.
    cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
  }

  if (is_dso) {
    return;
  }

  // Link all contents in *.a, rather than only link symbols in demands.
  cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
  // TODO: eliminate "linux" in path, and do not hard-coded embed x86_64
  cc_params[cc_par_cnt++] = alloc_printf("%s/lib/linux/libclang_rt.%s-x86_64.a", obj_path, san);
  if (is_cxx) {
    cc_params[cc_par_cnt++] = alloc_printf("%s/lib/linux/libclang_rt.%s_cxx-x86_64.a", obj_path, san);
  }
  // Deativate the effect of `--whole-archive`, i.e., only link symbols in demands.
  cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
  // Customize the exported symbols
  cc_params[cc_par_cnt++] =
        alloc_printf("-Wl,--dynamic-list=%s/lib/linux/libclang_rt.%s-x86_64.a.syms", obj_path, san);
  if (is_cxx) {
    cc_params[cc_par_cnt++] =
          alloc_printf("-Wl,--dynamic-list=%s/lib/linux/libclang_rt.%s_cxx-x86_64.a.syms", obj_path, san);
  }
  cc_params[cc_par_cnt++] = "-lm";
  cc_params[cc_par_cnt++] = "-ldl";
  cc_params[cc_par_cnt++] = "-lpthread"; 
  if (is_cxx) {
    cc_params[cc_par_cnt++] = "-lstdc++";
  }


  /**
   * Transfer the option to pass by `-mllvm -<opt>`
   */
  // cc_params[cc_par_cnt++] = "-mllvm";
  // cc_params[cc_par_cnt++] = "-memlog-hook-inst=1";

}



static void afl_runtime() {
  cc_params[cc_par_cnt++] = "-lrt";
  cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
  cc_params[cc_par_cnt++] = alloc_printf("%s/lib/libafl_rt.a", obj_path);
  cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
}

static void link_constructor() {
  /**
   * Link __attribute__((constructor)) function, seems that in static library,
   * linker will not link the function if the function is not used in program.
   * Maybe can use __attribute__((used)) ?
   */ 
  //cc_params[cc_par_cnt++] = "-Wl,-u,__dfsw_debug_func";
  
}


static void sync_hook_id(char *dst, char *src) {
  // In order to make sure MemlogPass and DFSanPass hook same instructions 
  // with same HookID.
  char buf[8];
  memset(buf, 0, 8);
  FILE* src_f = fopen(src, "r");
  fread(buf, 1, sizeof(unsigned int), src_f);
  fclose(src_f);

  FILE* dst_f = fopen(dst, "w+");
  fwrite(buf, 1, sizeof(unsigned int), dst_f);
  fclose(dst_f);
}

static u8 handle_x_option(const u8* const* arg) {
  u8 *cur = arg[0];
  // Check prefix "-x"
  if (cur[0] != '-' || cur[1] != 'x') {
    return 0;
  }

  // If cur == '-xsan', just skip it.
  OPT_EQ_AND_THEN(cur + 2, "san", { return 0; })

  const u8 *language = (cur[2] == '\0') ? arg[1] : cur + 2;

  // assembler & assembler-with-cpp (with preprocessor)
  if (!strcmp(language, "assembler") ||
     !strcmp(language, "assembler-with-cpp")) {
    frontend_opt.AsmAsSource = 1;
  }
  return 1;
}

/* Copy argv to cc_params, making the necessary edits. */
static void edit_params(u32 argc, const char** argv) {

  u8 fortify_set = 0, asan_set = 0, x_set = 0, bit_mode = 0, shared_linking = 0,
     preprocessor_only = 0, have_unroll = 0, have_o = 0, have_pic = 0,
     have_c = 0, partial_linking = 0;
  u8 only_lib = 0;
  const u8 *name;
  enum SanitizerType xsanTy = SanNone;
  u8 is_cxx = 0;

  cc_params = ck_alloc((argc + 256) * sizeof(u8*));

  name = strrchr(argv[0], '/');
  if (!name) name = argv[0]; else name++;

  if (!strncmp(name, "xclang++", strlen("xclang++"))) {
    is_cxx = 1;
    u8* alt_cxx = getenv("X_CXX");
    cc_params[0] = alt_cxx ? alt_cxx : (u8*)"clang++";
  } else {
    u8* alt_cc = getenv("X_CC");
    cc_params[0] = alt_cc ? alt_cc : (u8*)"clang";
  }

  /* There are two ways to compile afl-clang-fast. In the traditional mode, we
     use afl-llvm-pass.so to inject instrumentation. In the experimental
     'trace-pc-guard' mode, we use native LLVM instrumentation callbacks
     instead. The latter is a very recent addition - see:

     http://clang.llvm.org/docs/SanitizerCoverage.html#tracing-pcs-with-guards */

/*#ifdef USE_TRACE_PC
  cc_params[cc_par_cnt++] = "-fsanitize-coverage=trace-pc-guard";
#ifndef __ANDROID__
  cc_params[cc_par_cnt++] = "-mllvm";
  cc_params[cc_par_cnt++] = -sanitize"r-coverage-block-threshold=0";
#endif
#else
  cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = "-load";
  cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = alloc_printf("%s/afl-llvm-pass.so", obj_path);
#endif*/ /* ^USE_TRACE_PC */

  cc_params[cc_par_cnt++] = "-Qunused-arguments";

  /* Detect the compilation mode in advance. */
  xsanTy = detect_san_type(argc, argv);
  for (u32 i = 1; i < argc; i++) {
    const u8* cur = argv[i];

    if (!strcmp(cur, "--driver-mode=g++")) is_cxx = 1;
    else if (!strcmp(cur, "-m32")) bit_mode = 32;
    else if (!strcmp(cur, "armv7a-linux-androideabi")) bit_mode = 32;
    else if (!strcmp(cur, "-m64")) bit_mode = 64;
    else if (handle_x_option(&cur)) x_set = 1;
    else if (!strcmp(cur, "-fsanitize=address") ||
             !strcmp(cur, "-fsanitize=memory")) asan_set = 1;
    else if (strstr(cur, "FORTIFY_SOURCE")) fortify_set = 1;
    else if (!strcmp(cur, "-E")) preprocessor_only = 1;
    else if (!strcmp(cur, "-shared")) shared_linking = 1;
    else if (!strcmp(cur, "-dynamiclib")) shared_linking = 1;
    else if (!strcmp(cur, "-Wl,-r")) partial_linking = 1;
    else if (!strcmp(cur, "-Wl,-i")) partial_linking = 1;
    else if (!strcmp(cur, "-Wl,--relocatable")) partial_linking = 1;
    else if (!strcmp(cur, "-r")) partial_linking = 1;
    else if (!strcmp(cur, "--relocatable")) partial_linking = 1;
    else if (!strcmp(cur, "-c")) have_c = 1;
    else if (!strncmp(cur, "-O", 2)) have_o = 1;
    else if (!strncmp(cur, "-funroll-loop", 13)) have_unroll = 1;
    else {
      OPT_EQ_AND_THEN(cur, "-lib-only", {
        only_lib = 1;
        continue;
      })

      // For ASan's global gc option.
      // Search "-asan-globals-gc=0" in this file for details.
      OPT_EQ_AND_THEN(cur, "-no-integrated-as", {
        frontend_opt.DisableIntegratedAS = 1;
        continue;
      })
      OPT_GET_VAL_AND_THEN(cur, "--target", {
        if (strstr(val, "apple")) {
          frontend_opt.obj_format = MachO;
        } else if (strstr(val, "linux")) {
          frontend_opt.obj_format = ELF;
        } else if (strstr(val, "mingw32")) {
          frontend_opt.obj_format = COFF;
        } else {
          frontend_opt.obj_format = UnknownObjectFormat;
        }
        continue;
      })

      // If source file is assembly, set AsmAsSource.
      if (frontend_opt.AsmAsSource) {
        continue;
      }
      // Clang has a list: https://github.com/llvm/llvm-project/blob/b9f3b7f89a4cb4cf541b7116d9389c73690f78fa/clang/lib/Driver/Types.cpp#L293
      // assembly_source_extensions = ('.s', '.asm')
      // assembly_needing_c_preprocessor_source_extensions = ('.S')
      // Check the suffix
      const char* suffix = strrchr(cur, '.');
      if (!suffix)
        continue;
      if (!strcmp(suffix, ".s") || !strcmp(suffix, ".S") ||
          !strcmp(suffix, ".asm")) {
        frontend_opt.AsmAsSource = 1;
        continue;
      }
    }
  }

  /// Set -x none for sanitizer rutnime libraries.
  if (x_set) {
    cc_params[cc_par_cnt++] = "-x";
    cc_params[cc_par_cnt++] = "none";
  }

  if (!only_lib)
    init_sanitizer_setting(xsanTy);

  /*
    We should put the sanitizer static runtime library just ahead of the
    input xxx.o/xxx.c  files, so that the CTor of sanitizers can be called
    before the CTors of user code (.preinit, module_ctor).

    What's more, the compiler also places the sanitizer runtime library before
    the input files after the Driver generates the command line, which should be
    adhered by this compiler wrapper.

    However, it is hard to judge whether an argument is an input file or not
    precisely, so we simply add the sanitizer runtime library as front as
    possible.

    See ASan's testcase "init_fini_sections.cpp" for details.
  */
  if (!only_lib)
    regist_pass_plugin(xsanTy);
  // *.c/cpp -o *.o, don't link sanitizer runtime library.
  if (!have_c) {
    add_sanitizer_runtime(xsanTy, is_cxx, shared_linking);
  }

  while (--argc) {
    const u8* cur = *(++argv);

    if (!strcmp(cur, "-Wl,-z,defs") ||
        !strcmp(cur, "-Wl,--no-undefined")) continue;

    if (handle_sanitizer_options(cur, 
        !strcmp(argv[-1], "-mllvm"), xsanTy)) {
      continue;
    }

    if (frontend_opt.AsmAsSource && !strcmp(cur, "-mllvm")) {
      ++argv;
      --argc;
      continue;
    }

    cc_params[cc_par_cnt++] = cur;

  }

  if (!only_lib)
    add_pass_options(xsanTy);

  if (getenv("X_HARDEN")) {

    cc_params[cc_par_cnt++] = "-fstack-protector-all";

    if (!fortify_set)
      cc_params[cc_par_cnt++] = "-D_FORTIFY_SOURCE=2";

  }

  if (!asan_set) {

    if (getenv("X_USE_ASAN")) {

      if (getenv("X_USE_MSAN"))
        FATAL("ASAN and MSAN are mutually exclusive");

      if (getenv("X_HARDEN"))
        FATAL("ASAN and X_HARDEN are mutually exclusive");

      cc_params[cc_par_cnt++] = "-U_FORTIFY_SOURCE";
      cc_params[cc_par_cnt++] = "-fsanitize=address";

    } else if (getenv("X_USE_MSAN")) {

      if (getenv("X_USE_ASAN"))
        FATAL("ASAN and MSAN are mutually exclusive");

      if (getenv("X_HARDEN"))
        FATAL("MSAN and X_HARDEN are mutually exclusive");

      cc_params[cc_par_cnt++] = "-U_FORTIFY_SOURCE";
      cc_params[cc_par_cnt++] = "-fsanitize=memory";

    }

  }

#ifdef USE_TRACE_PC

  if (getenv("X_INST_RATIO"))
    FATAL("X_INST_RATIO not available at compile time with 'trace-pc'.");

#endif /* USE_TRACE_PC */

   /**
   * Turn off builtin functions.
   * TODO: support LTO
   */
  if (getenv("X_NO_BUILTIN") /* || lto_mode */) {

    cc_params[cc_par_cnt++] = "-fno-builtin-strcmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-strncmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-strcasecmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-strncasecmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-memcmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-bcmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-strstr";
    cc_params[cc_par_cnt++] = "-fno-builtin-strcasestr";

  }

  /// Don't optimize initiatively
  // if (!getenv("X_DONT_OPTIMIZE")) {
  //   cc_params[cc_par_cnt++] = "-g";
  //   if (!have_o) cc_params[cc_par_cnt++] = "-O3";
  //   if (!have_unroll && !have_o) cc_params[cc_par_cnt++] = "-funroll-loops";
  // }
  

  // afl_runtime();
  
  /*if (getenv("SYNC_HOOK_ID")) {
    fprintf(stderr, "sync_hook_id\n");
    if (getenv("MEMLOG_MODE")) {
      sync_hook_id("/tmp/.DtaintHookID.txt", "/tmp/.MemlogHookID.txt");
    }
    else {
      sync_hook_id("/tmp/.MemlogHookID.txt", "/tmp/.DtaintHookID.txt");
    }
    unsetenv("SYNC_HOOK_ID");
  }*/
  
  // /**
  //  * Enable pie since dfsan maps shadow memory at 0x10000-0x200200000000, 
  //  * pie is needed to prevent overlapped.
  //  */
  // cc_params[cc_par_cnt++] = "-fPIE";
  // cc_params[cc_par_cnt++] = "-fPIC";
  // cc_params[cc_par_cnt++] = "-pie";
  
  /*if (getenv("X_NO_BUILTIN")) {

    cc_params[cc_par_cnt++] = "-fno-builtin-strcmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-strncmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-strcasecmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-strncasecmp";
    cc_params[cc_par_cnt++] = "-fno-builtin-memcmp";

  }*/

  /*cc_params[cc_par_cnt++] = "-D__AFL_HAVE_MANUAL_CONTROL=1";
  cc_params[cc_par_cnt++] = "-D__AFL_COMPILER=1";
  cc_params[cc_par_cnt++] = "-DFUZZING_BUILD_MODE_UNSAFE_FOR_PRODUCTION=1";*/

  /* When the user tries to use persistent or deferred forkserver modes by
     appending a single line to the program, we want to reliably inject a
     signature into the binary (to be picked up by afl-fuzz) and we want
     to call a function from the runtime .o file. This is unnecessarily
     painful for three reasons:

     1) We need to convince the compiler not to optimize out the signature.
        This is done with __attribute__((used)).

     2) We need to convince the linker, when called with -Wl,--gc-sections,
        not to do the same. This is done by forcing an assignment to a
        'volatile' pointer.

     3) We need to declare __afl_persistent_loop() in the global namespace,
        but doing this within a method in a class is hard - :: and extern "C"
        are forbidden and __attribute__((alias(...))) doesn't work. Hence the
        __asm__ aliasing trick.

   */

 /* cc_params[cc_par_cnt++] = "-D__AFL_LOOP(_A)="
    "({ static volatile char *_B __attribute__((used)); "
    " _B = (char*)\"" PERSIST_SIG "\"; "
#ifdef __APPLE__
    "__attribute__((visibility(\"default\"))) "
    "int _L(unsigned int) __asm__(\"___afl_persistent_loop\"); "
#else
    "__attribute__((visibility(\"default\"))) "
    "int _L(unsigned int) __asm__(\"__afl_persistent_loop\"); "
#endif*/ /* ^__APPLE__ */
    /*"_L(_A); })";

  cc_params[cc_par_cnt++] = "-D__AFL_INIT()="
    "do { static volatile char *_A __attribute__((used)); "
    " _A = (char*)\"" DEFER_SIG "\"; "
#ifdef __APPLE__
    "__attribute__((visibility(\"default\"))) "
    "void _I(void) __asm__(\"___afl_manual_init\"); "
#else
    "__attribute__((visibility(\"default\"))) "
    "void _I(void) __asm__(\"__afl_manual_init\"); "
#endif*/ /* ^__APPLE__ */
   // "_I(); } while (0)";


  cc_params[cc_par_cnt++] = "-fuse-ld=lld";
  // /usr/local/lib is not the default search path for lld, so we add it here.
  cc_params[cc_par_cnt++] = "-L/usr/local/lib";

/*#ifndef __ANDROID__
  switch (bit_mode) {

    case 0:
      cc_params[cc_par_cnt++] = alloc_printf("%s/afl-llvm-rt.o", obj_path);
      break;

    case 32:
      cc_params[cc_par_cnt++] = alloc_printf("%s/afl-llvm-rt-32.o", obj_path);

      if (access(cc_params[cc_par_cnt - 1], R_OK))
        FATAL("-m32 is not supported by your compiler");

      break;

    case 64:
      cc_params[cc_par_cnt++] = alloc_printf("%s/afl-llvm-rt-64.o", obj_path);

      if (access(cc_params[cc_par_cnt - 1], R_OK))
        FATAL("-m64 is not supported by your compiler");

      break;

  }
#endif*/

  cc_params[cc_par_cnt] = NULL;

}

static void print_cmdline(int argc) {

  for(int i = 0; i < argc + cc_par_cnt; i++) {
    if(cc_params[i] == NULL)
      break;
    printf("%s ", cc_params[i]);
  }
  printf("\n");
}
/* Main entry point */

int main(int argc, const char** argv) {

  if (isatty(2) && !getenv("AFL_QUIET")) {

/*#ifdef USE_TRACE_PC
    SAYF(cCYA "afl-clang-fast [tpcg] " cBRI VERSION  cRST " by <lszekeres@google.com>\n");
#else
    SAYF(cCYA "afl-clang-fast " cBRI VERSION  cRST " by <lszekeres@google.com>\n");
#endif*/ /* ^USE_TRACE_PC */

  }

  if (argc < 2) {

    SAYF("\n"
         "This is a helper application for afl-fuzz. It serves as a drop-in replacement\n"
         "for clang, letting you recompile third-party code with the required runtime\n"
         "instrumentation. A common use pattern would be one of the following:\n\n"

         "  CC=path/clang-wrapper ./configure\n"
         "  CXX=path/clang-wrapper++ ./configure\n\n"

         "In contrast to the traditional afl-clang tool, this version is implemented as\n"
         "an LLVM pass and tends to offer improved performance with slow programs.\n\n"

         "You can specify custom next-stage toolchain via AFL_CC and AFL_CXX. Setting\n"
         "AFL_HARDEN enables hardening optimizations in the compiled code.\n\n");

    exit(1);

  }


#ifndef __ANDROID__
  find_obj((u8 *)argv[0]);
#endif

  edit_params(argc, argv);

  if (!!getenv("XCLANG_DEBUG")) {
    print_cmdline(argc);
  }

  
  execvp(cc_params[0], (char**)cc_params);
 
  FATAL("Oops, failed to execute '%s' - check your PATH", cc_params[0]);

  return 0;

}
