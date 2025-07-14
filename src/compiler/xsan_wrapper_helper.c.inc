//===------------- xsan_wrapper_helper.c.inc --------------------*- C++ -*-===//
//
// This file defines some common functions used in different compiler wrappers
//
// This file should be included into the tool's wrapper file,
// which has to define its own macros before :
//   WRAP_GCC or WRAP_CLANG
//===----------------------------------------------------------------------===//

#include "config_compile.h"
#include "include/alloc-inl.h"
#include "include/debug.h"
#include "include/types.h"
#include "xsan_common.h"
#include "xsan_wrapper_helper.h"

#include <limits.h>
#include <string.h>
#include <unistd.h>

#if defined(WRAP_GCC)
#undef WRAP_GCC
#define WRAP_GCC 1
#define WRAP_CLANG 0
// Selection of hotpatches according to compiler
#define XSAN_DSO_PATCH XSAN_GCC_DSO_PATCH
#elif defined(WRAP_CLANG)
#undef WRAP_CLANG
#define WRAP_GCC 0
#define WRAP_CLANG 1
#define XSAN_DSO_PATCH XSAN_CLANG_DSO_PATCH
#else

#error "Please specify either clang or gcc to wrap via macro defines. "
"E.g., #define WRAP_GCC or #define WRAP_CLANG"
#endif

// Now, gcc only doesn't support MSan.
// So we set XSAN_CONTAINS_MSAN to 0.
// If some gcc version does not support any sanitizer,
// we can set XSAN_CONTAINS_<san> to 0.
// Clang support all sanitizers.
#ifdef COMPILER_UNSUPPORT_MSAN
#undef XSAN_CONTAINS_MSAN
#define XSAN_CONTAINS_MSAN 0
#endif

static const u8 **cc_params;      /* Parameters passed to the real CC  */
static u32 cc_par_cnt = 1;        /* Param count, including argv0      */
static u8 *obj_path;              /* Path to runtime libraries         */
static u8 support_dso_inject = 0; /* Support for DSO injection         */

static XsanOption xsan_options;
static XsanOption xsan_recover_options;

static void init(XsanOption *opt) {
#if XSAN_CONTAINS_UBSAN
  opt->mask |= (u64)1 << UBSan;
#endif
#if XSAN_CONTAINS_TSAN
  opt->mask |= (u64)1 << TSan;
#endif
#if XSAN_CONTAINS_MSAN
  opt->mask |= (u64)1 << MSan;
#endif
#if XSAN_CONTAINS_ASAN
  opt->mask |= (u64)1 << ASan;
#endif
  opt->mask |= (u64)1 << XSan;
}

/// TODO: handle unsupported sanTy.
static void set(XsanOption *opt, enum SanitizerType sanTy) {
  if (sanTy == XSan) {
    init(opt);
  } else {
    opt->mask |= (u64)1 << sanTy;
  }
}

static void clear(XsanOption *opt, enum SanitizerType sanTy) {
  opt->mask &= (sanTy == XSan) ? 0 : ~((u64)1 << sanTy);
}

static u8 has(XsanOption *opt, enum SanitizerType sanTy) {
  return (opt->mask & (((u64)1 << sanTy))) != 0;
}

static u8 has_any(XsanOption *opt) {
  return (opt->mask & ~(((u64)1 << XSan) | ((u64)1 << SanNone))) != 0;
}

static u8 whether_to_support_dso_injection(const char *arg) {
  // Construct command string, and transmit the argument to the scripyt
  const char *command =
      alloc_printf("%s/" XSAN_SUPPORT_DSO_CHECKER " %s", obj_path, arg);

  // Execute the command
  int status = system(command);
  if (status == -1) {
    PFATAL("Cannot execute script to check whether to support DSO "
           "injection:\n\t%s",
           command);
  }

  // Check the return status
  if (WIFEXITED(status)) {
    ck_free((void *)command);
    int exit_code = WEXITSTATUS(status);
    return exit_code == 0;
  } else {
    PFATAL("Command %s terminated abnormally", command);
  }
}

/*
  in find_object() we look here:

  1. if obj_path is already set we look there first
  2. then we check the $XSAN_PATH environment variable location if set
  3. then we check /proc (on Linux, etc.) to find the real executable path.
    a) We also check ../lib/linux here.
  4. next we check argv[0] if it has path information and use it
    a) we also check ../lib/linux
  5. if 4. failed we check /proc (only Linux, Android, NetBSD, DragonFly, and
     FreeBSD with procfs)
    a) and check here in ../lib/linux too
  6. we look into the XSAN_PATH define (usually /usr/local/lib/afl)
  7. we finally try the current directory

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

#if defined(__linux__) || defined(__ANDROID__)
  char real_path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", real_path, sizeof(real_path) - 1);
  if (len != -1) {
    real_path[len] = '\0';
    slash = strrchr(real_path, '/');
    if (slash) {
      *slash = 0; // "real_path" is now the directory of the executable

      // Search in the same directory as the executable
      tmp = alloc_printf("%s/%s", real_path, obj);
      if (!access(tmp, R_OK)) {
        obj_path = ck_strdup((u8 *)real_path);
        return tmp;
      }
      ck_free(tmp);

      // Search in ../lib/linux relative to the executable
      tmp = alloc_printf("%s/../lib/linux/%s", real_path, obj);
      if (!access(tmp, R_OK)) {
        obj_path = alloc_printf("%s/../lib/linux", real_path);
        return tmp;
      }
      ck_free(tmp);
    }
  }
#endif

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

#if defined(__FreeBSD__) || defined(__DragonFly__) || defined(__linux__) ||    \
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
        char exepath[PATH_MAX];
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

static void find_obj(u8 *argv0) {

  obj_path = find_object("", argv0);

  if (!obj_path) {
    FATAL("Unable to find object path. Please set XSAN_PATH");
  }
}

static u8 handle_x_option(const u8 *const *arg, u8 *asm_as_source) {
  const u8 *cur = arg[0];
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
    *asm_as_source = 1;
  }
  return 1;
}

static enum SanitizerType detect_san_type(const u32 argc, const char *argv[]) {
  static const enum SanitizerType incompatible_san_types[][NumSanitizerTypes] =
      {
          [ASan] = {XSan, TSan, MSan},
          [TSan] = {XSan, ASan, MSan},
          [MSan] = {XSan, ASan, TSan},
          [XSan] = {ASan, TSan, MSan},
      };

  static const u8 support_subsanitizers[] = {
      [ASan] = XSAN_CONTAINS_ASAN,
      [TSan] = XSAN_CONTAINS_TSAN,
      [MSan] = XSAN_CONTAINS_MSAN,
      [UBSan] = XSAN_CONTAINS_UBSAN,
  };

  static const char *const option_names[] = {[ASan] = "-asan",
                                             [TSan] = "-tsan",
                                             [MSan] = "-msan",
                                             [UBSan] = "-ubsan",
                                             [XSan] = "-xsan"};

  enum SanitizerType xsanTy = SanNone;
  for (u32 i = 1; i < argc; i++) {
    const char *cur = argv[i];
    for (enum SanitizerType san = ASan; san < NumSanitizerTypes; san++) {
      OPT_EQ_AND_THEN(cur, option_names[san], {
        for (u32 incomp_idx = 0; incomp_idx < NumSanitizerTypes; incomp_idx++) {
          enum SanitizerType incomp = incompatible_san_types[san][incomp_idx];
          if (!incomp)
            break;
          if (has(&xsan_options, incomp))
            FATAL("'%s' could not be used with '%s'", option_names[incomp],
                  option_names[san]);
        }
        if (!support_subsanitizers[san]) {
          FATAL("'%s' is not supported by this compiler", option_names[san]);
        }
        if (san == UBSan) {
          /// Only if no other sanitizer is specified, we treat it as UBSan
          /// standalone.
          if (!has_any(&xsan_options))
            xsanTy = UBSan;
        } else {
          xsanTy = san;
        }
        set(&xsan_options, xsanTy);
        continue;
      })
    }

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
      // split value by ',' : -fsanitize=address,undefined
      char *val_str = ck_strdup((void *)val);
      char *val_ptr = val_str;
      while (1) {
        char *comma = strchr(val_ptr, ',');
        if (comma) {
          *comma = 0;
        }
        if (OPT_EQ(val_ptr, "address")) {
          sanTy = ASan;
        } else if (OPT_EQ(val_ptr, "thread")) {
          sanTy = TSan;
        } else if (OPT_EQ(val_ptr, "memory")) {
          sanTy = MSan;
        } else if (OPT_EQ(val_ptr, "undefined")) {
          sanTy = UBSan;
        } else if (OPT_EQ(val_ptr, "all")) {
          sanTy = XSan;
        } else {
          /// TODO: support other sanitizers
        }
        if (is_neg) {
          clear(&xsan_options, sanTy);
        } else {
          set(&xsan_options, sanTy);
        }
        if (!comma) {
          break;
        }
        val_ptr = comma + 1;
      }
      ck_free(val_str);
      continue;
    })
  }

  if (xsanTy == SanNone) {
    u8 has_conflict = 0;
    for (enum SanitizerType san = ASan;
         san < NumSanitizerTypes && !has_conflict; san++) {
      if (san == XSan || san == UBSan)
        continue;
      if (!has(&xsan_options, san))
        continue;
      for (u32 incomp_idx = 0; incomp_idx < NumSanitizerTypes && !has_conflict;
           incomp_idx++) {
        enum SanitizerType incomp = incompatible_san_types[san][incomp_idx];
        if (!incomp)
          break;
        has_conflict = has_conflict || has(&xsan_options, incomp);
      }
    }

    if (has_conflict) {
      xsanTy = XSan;
      xsan_options.mask |= (u64)1 << XSan;
    }
  }

  /// TODO: figure out whether we need to do that.
  // /// Use our out-of-tree runtime
  if (xsanTy != XSan && !has(&xsan_options, xsanTy))
    xsanTy = SanNone;

  return xsanTy;
}

static void init_sanitizer_setting(enum SanitizerType sanTy) {
  u8 *str_options;
  switch (sanTy) {
  case ASan:
  case TSan:
  case MSan:
  case UBSan:
  case XSan:
    /// TODO: support unmodified clang
    // Use env var to control clang only perform frontend
    // transformation for sanitizers.
    str_options = alloc_printf("%llu", xsan_options.mask);
    setenv("XSAN_ONLY_FRONTEND", str_options, 1);

    // Reuse the frontend code relevant to sanitizer
    if (has(&xsan_options, ASan)) {
      if (sanTy == XSan && !XSAN_CONTAINS_ASAN) {
        FATAL("xsan did not contain asan, '-xsan' could not be used with "
              "'-fsanitize=address'");
      }
      cc_params[cc_par_cnt++] = "-fsanitize=address";
    }
    if (has(&xsan_options, TSan)) {
      if (sanTy == XSan && !XSAN_CONTAINS_TSAN) {
        FATAL("xsan did not contain tsan, '-xsan' could not be used with "
              "'-fsanitize=thread'");
      }
      cc_params[cc_par_cnt++] = "-fsanitize=thread";
    }
    if (has(&xsan_options, MSan)) {
      if (sanTy == XSan && !XSAN_CONTAINS_MSAN) {
        FATAL("xsan did not contain msan, '-xsan' could not be used with "
              "'-fsanitize=memory'");
      }
      cc_params[cc_par_cnt++] = "-fsanitize=memory";
    }
    if (has(&xsan_options, UBSan)) {
      cc_params[cc_par_cnt++] = "-fsanitize=undefined";
      /// FIXME:
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

      /// Some sub-functionality of UBSan is duplicated with ASan
      if (has(&xsan_options, ASan)) {
        /// -object-size option is duplicated with -fsanitize=address, which
        /// detects overflow issues for object accesses.
        /// What's more, in LLVM 15, -fsanitize=object-size affects the
        /// the function inlining, which may cause some performance issues.
        /// For those case using libc++: std::string str; str.size();
        cc_params[cc_par_cnt++] = "-fno-sanitize=object-size";
        /// -bounds option is duplicated with -fsanitize=address, which
        /// detects overflow issues for array accesses.
        cc_params[cc_par_cnt++] = "-fno-sanitize=bounds";
        /// Similarly, -null option is duplicated with -fsanitize=address, which
        /// detects null pointer dereferences.
        cc_params[cc_par_cnt++] = "-fno-sanitize=null";
      }
    }

    support_dso_inject = whether_to_support_dso_injection(cc_params[0]);
    if (support_dso_inject) {
      const char *ld_preload = getenv("LD_PRELOAD");
      const char *dso_path = alloc_printf("%s/" XSAN_DSO_PATCH, obj_path);
      const char *new_ld_preload =
          (ld_preload != NULL)
              ? (char *)alloc_printf("%s:%s", ld_preload, dso_path)
              : dso_path;
      setenv("LD_PRELOAD", new_ld_preload, 1);
      setenv("XSAN_BASE_DIR", obj_path, 1);
    }
    break;
  case SanNone:
  default:
    return;
  }
}

static void add_wrap_link_option(enum SanitizerType sanTy, u8 is_cxx) {
  // clang's live patch `add_wrap_link_option` in CommonArgs.cpp take over
  // Symbol wrapped, However, gcc's live patch implements but does not use
  // `add_wrap_link_option` in gcc.cpp, which is just for future usage. Please
  // refer to RoStrPatcher()@gcc.cpp for details.
  if (sanTy != XSan || (support_dso_inject && !WRAP_GCC))
    return;

  // Use Linker Response File to include lots of -wrap=<symbol> options in one
  // file.
  cc_params[cc_par_cnt++] =
      alloc_printf("-Wl,@%s/share/xsan_wrapped_symbols.txt", obj_path);
}

static void add_sanitizer_runtime(enum SanitizerType sanTy, u8 is_cxx,
                                  u8 is_dso, u8 needs_shared_rt) {
  // If support_dso_inject is true, we don't need to add sanitizer runtime here.
  // Runtime should be added in the DSO injector.
  // clang's live patch `replaceSanitizerRt` in CommonArgs.cpp take over linked
  // runtime libraries. However, gcc's live patch implements but does not use
  // `add_runtime` in gcc.cpp, which is just for future usage. Please refer to
  // RoStrPatcher()@gcc.cpp for details.
  if (support_dso_inject && !WRAP_GCC) {
    return;
  }

  /**
   * Need to enable corresponding llvm optimization level,
   * where your pass is registed.
   */
  u8 *san = "";
  switch (sanTy) {
  case ASan:
    san = "asan";
    break;
  case TSan:
    san = "tsan";
    break;
  case MSan:
    san = "msan";
    break;
  case UBSan:
    san = "ubsan_standalone";
    break;
  case XSan:
    san = "xsan";
    break;
  case SanNone:
  default:
    return;
  }

  add_wrap_link_option(sanTy, is_cxx);

  if (needs_shared_rt &&
      (sanTy == ASan || sanTy == TSan || sanTy == UBSan || sanTy == XSan)) {
    cc_params[cc_par_cnt++] = alloc_printf("-Wl,-rpath,%s/lib/linux", obj_path);
    cc_params[cc_par_cnt++] =
        alloc_printf("%s/lib/linux/libclang_rt.%s-x86_64.so", obj_path, san);
  }

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
    if (needs_shared_rt && !is_dso) {
      /// TODO: skip in Android
      /*
        To support DSO of XSan, see the following commit for details:
        https://github.com/llvm/llvm-project/commit/56b6ee9833137e0e79667f8e4378895fed5dc2c2

        // These code comes from CommonArgs.cpp in LLVM 15
        if (!Args.hasArg(options::OPT_shared) && !TC.getTriple().isAndroid())
          HelperStaticRuntimes.push_back("asan-preinit");
      */
      cc_params[cc_par_cnt++] = alloc_printf(
          "%s/lib/linux/libclang_rt.%s-preinit-x86_64.a", obj_path, san);
    }
    // TODO: eliminate "linux" in path, and do not hard-coded embed x86_64
    cc_params[cc_par_cnt++] = alloc_printf(
        "%s/lib/linux/libclang_rt.%s_static-x86_64.a", obj_path, san);
    // Deativate the effect of `--whole-archive`, i.e., only link symbols in
    // demands.
    cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
  }

  if (is_dso) {
    return;
  }

  if (needs_shared_rt) {
    return;
  }

  // Link all contents in *.a, rather than only link symbols in demands.
  cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
  // TODO: eliminate "linux" in path, and do not hard-coded embed x86_64
  cc_params[cc_par_cnt++] =
      alloc_printf("%s/lib/linux/libclang_rt.%s-x86_64.a", obj_path, san);
  if (is_cxx) {
    cc_params[cc_par_cnt++] =
        alloc_printf("%s/lib/linux/libclang_rt.%s_cxx-x86_64.a", obj_path, san);
  }
  // Deativate the effect of `--whole-archive`, i.e., only link symbols in
  // demands.
  cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
  // Customize the exported symbols
  cc_params[cc_par_cnt++] = alloc_printf(
      "-Wl,--dynamic-list=%s/lib/linux/libclang_rt.%s-x86_64.a.syms", obj_path,
      san);
  if (is_cxx) {
    cc_params[cc_par_cnt++] = alloc_printf(
        "-Wl,--dynamic-list=%s/lib/linux/libclang_rt.%s_cxx-x86_64.a.syms",
        obj_path, san);
  }
  // If not using livepatch, we might need to link some libraries manually.
  // At least for clang, we should.
  /// TODO: ref to llvm-source/clang/lib/Driver/ToolChains/CommonArgs.cpp:824
  /// linkSanitizerRuntimeDeps
  cc_params[cc_par_cnt++] = "-lpthread";
  cc_params[cc_par_cnt++] = "-lrt";
  cc_params[cc_par_cnt++] = "-lm";
  cc_params[cc_par_cnt++] = "-ldl";
  cc_params[cc_par_cnt++] = "-lresolv";
  // if (is_cxx) {
  //   cc_params[cc_par_cnt++] = "-lstdc++";
  // }
}

static void print_help() {
#if WRAP_GCC
#define CC "gcc"
#define DESC CC "'s frontend & pass + XSan-local runtime.\n"
#elif WRAP_CLANG
#define CC "clang"
#define DESC CC "'s frontend + XSan-local pass & runtime.\n"
#else
#error "Please specify either clang or gcc to wrap via macro defines. "
  "E.g., #define WRAP_GCC 1 or #define WRAP_CLANG 1"
#endif
  puts(
      "Sanitizer Options:\n"
      "  -asan          Use our AddressSanitizer (ASan). \n"
      "                 Uses " DESC
      "                 Cannot be used with -tsan or -msan.\n"
      "  -tsan          Use our ThreadSanitizer (TSan).\n"
      "                 Uses " DESC
      "                 Cannot be used with -asan or -msan.\n"
#if WRAP_CLANG
      "  -msan          Use our MemorySanitizer (MSan).\n"
      "                 Uses " DESC
      "                 Cannot be used with -asan or -tsan.\n"
#endif
      "  -ubsan         Use our UndefinedBehaviorSanitizer (UBSan). \n"
      "                 Uses the " CC " frontend (no midend required).\n"
      "  -xsan          Use our eXtended Sanitizer (XSAN).\n"
#if WRAP_CLANG
      "                 Integrates ASan, TSan, MSan and UBSan. \n"
#elif WRAP_GCC
      "                 Integrates ASan, TSan and UBSan. \n"
#endif

      "\nOriginal Sanitizers, for example:\n"
      "  -fsanitize=address   Use LLVM's original AddressSanitizer.\n"
      "  -fsanitize=thread    Use LLVM's original ThreadSanitizer.\n"
#if WRAP_CLANG
      "  -fsanitize=memory    Use LLVM's original MemorySanitizer.\n"
#endif
      "  -fsanitize=undefined Use LLVM's original UndefinedBehaviorSanitizer.\n"

      "\nAutomatically activate XSan:\n"
      "  -fsanitize=address,thread          Use XSan to enable ASan and TSan.\n"
#if WRAP_CLANG
      "  -fsanitize=address,memory          Use XSan to enable ASan and MSan.\n"
      "  -fsanitize=thread,memory           Use XSan to enable TSan and MSan.\n"
      "  -fsanitize=address,thread,memory   Use XSan to enable ASan, TSan and "
      "MSan.\n"
#endif
      "If some 'incompatible' sanitizers are enabled via `-fsanitize=...`,\n"
      "XSan will be automatically enabled to support such composition.\n"

      "\nDisable Sanitizers(works for both XSan and original ones), "
      "for example:\n"
      "  -fno-sanitize=address              Disable AddressSanitizer.\n"
      "  -fno-sanitize=thread               Disable ThreadSanitizer.\n"
#if WRAP_CLANG
      "  -fno-sanitize=memory               Disable MemorySanitizer.\n"
#endif
      "  -fno-sanitize=undefined            Disable "
      "UndefinedBehaviorSanitizer.\n"

      "\nNotes:\n"
      "  - Use -fsanitize=<sanitizer> for LLVM's original sanitizers.\n"
      "  - Use -fno-sanitize=<sanitizer> to disable specific sanitizers.\n"
      "  - These options are conflicting: -tsan, -asan, -xsan\n");
#undef CC
#undef DESC
}
