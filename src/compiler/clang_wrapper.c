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
#include "common-enum.h"

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>


static u8*  obj_path;               /* Path to runtime libraries         */
static u8** cc_params;              /* Parameters passed to the real CC  */
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

// TODO: support sanitizer composition
static enum SanitizerType detect_san_type(u32 argc, u8* argv[]) {
  enum SanitizerType xsanTy = SanNone;
  for (u32 i = 1; i < argc; i++) {
    u8* cur = argv[i];
    if (!strcmp(cur, "-tsan")) {
      if (xsanTy != SanNone && xsanTy != UBSan)
        FATAL("'-tsan' could not be used with '-asan'");
      // Reuse the frontend code relevant to sanitizer
      cc_params[cc_par_cnt++] = "-fsanitize=thread";
      xsanTy = TSan;
      continue;
    } else if (!strcmp(cur, "-asan")) {
      if (xsanTy != SanNone && xsanTy != UBSan)
        FATAL("'-asan' could not be used with '-tsan'");
      // Reuse the frontend code relevant to sanitizer
      cc_params[cc_par_cnt++] = "-fsanitize=address";
      xsanTy = ASan;
      continue;
    } else if (!strcmp(cur, "-ubsan")) {
      // Reuse the frontend code relevant to sanitizer
      cc_params[cc_par_cnt++] = "-fsanitize=undefined";
      xsanTy = UBSan;
      continue;
    } else if (!strncmp(cur, "-fno-sanitize=", 14)){
      u8 *val = cur + 14;
      if (!strcmp(val, "all") 
        || (xsanTy == ASan && !strcmp(val, "address"))
        || (xsanTy == TSan && !strcmp(val, "thread"))
      ) {
        xsanTy = SanNone;
      }
    }
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
static u8 handle_asan_options(u8* opt, u8 is_mllvm_arg) {
  if (!strcmp(opt, "-fsanitize-address-globals-dead-stripping")) {
    // TODO
    return 1;
  } else if (!strcmp(opt, "-fsanitize-address-poison-custom-array-cookie")) {
    // TODO
    return 1;
  } else if (!strncmp(opt, "-fsanitize-address-use-after-return=", 36)) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = alloc_printf("-as-use-after-return=%s", opt + 36);
    return 1;
  } else if (!strcmp(opt, "-fsanitize-address-use-after-scope")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-as-use-after-scope";
    return 1;
  } else if (!strcmp(opt, "-fno-sanitize-address-use-after-scope")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-as-use-after-scope=0";
    return 1;
  } else if (!strcmp(opt, "-fsanitize-address-use-odr-indicator")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-as-use-odr-indicator";
    return 1;
  } else if (!strcmp(opt, "-fno-sanitize-address-use-odr-indicator")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-as-use-odr-indicator=0";
    return 1;
  } else if (!strcmp(opt, "-fsanitize-address-outline-instrumentation")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-as-instrumentation-with-call-threshold=0";
    return 1;
  } else if (!strncmp(opt, "-fsanitize-recover=", 19)) {
    u8* val = opt + 19;
    if (!strcmp(val, "address") || !strcmp(val, "all")) {
      cc_params[cc_par_cnt++] = "-mllvm";
      cc_params[cc_par_cnt++] = "-as-recover";
      return 1;
    }
  }

  if (is_mllvm_arg && !strncmp(opt, "-asan-", 6)) {
    cc_params[cc_par_cnt++] = alloc_printf("-as-%s", opt + 6);
    return 1;
  }
  return 0;
}

/*
 Handle the following options about ASan:
  -fsanitize-thread-atomics
  -fsanitize-thread-func-entry-exit
  -fsanitize-thread-memory-access
  -fsanitize-recover=address|all
 Replace with 
   -mllvm -xxx
 Refer to SanitizerArgs.cpp:1142~1155
 */
static u8 handle_tsan_options(u8* opt, u8 is_mllvm_arg) {
  if (!strcmp(opt, "-fsanitize-thread-atomics")) {
    return 1;
  } else if (!strcmp(opt, "-fno-sanitize-thread-atomics")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-ts-instrument-atomics=0";
    return 1;
  } else if (!strcmp(opt, "-fsanitize-thread-func-entry-exit")) {
    return 1;
  } else if (!strcmp(opt, "-fno-sanitize-thread-func-entry-exit")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-ts-instrument-func-entry-exit=0";
    return 1;
  } else if (!strcmp(opt, "-fsanitize-thread-memory-access")) {
    return 1;
  } else if (!strcmp(opt, "-fno-sanitize-thread-memory-access")) {
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-ts-instrument-memory-accesses=0";
    cc_params[cc_par_cnt++] = "-mllvm";
    cc_params[cc_par_cnt++] = "-ts-instrument-memintrinsics=0";
    return 1;
  } else if (!strncmp(opt, "-fsanitize-recover=", 19)) {
    u8* val = opt + 19;
    if (!strcmp(val, "thread") || !strcmp(val, "all")) {
      cc_params[cc_par_cnt++] = "-mllvm";
      cc_params[cc_par_cnt++] = "-as-recover";
      return 1;
    }
  }

  if (is_mllvm_arg && !strncmp(opt, "-tsan-", 6)) {
    cc_params[cc_par_cnt++] = alloc_printf("-ts-%s", opt + 6);
    return 1;
  }
  return 0;
}

static u8 handle_ubsan_options(u8* opt) {
  return 0;
}



/* 
  Ports the arguments for sanitizers to our plugin sanitizers
  Dicards the original argument if return 1.

  TODO: handle those -fno-sanitize-xxx properly, which could cancel the 
        corresponding argument -fsanitize-xxx theoretically.
*/
static u8 handle_sanitizer_options(u8* opt, u8 is_mllvm_arg, enum SanitizerType sanTy)  {
  if (!strcmp(opt, "-asan") || !strcmp(opt, "-tsan") || !strcmp(opt, "-ubsan")) {
    return 1;
  }

  switch (sanTy) {
  case ASan:
    return handle_asan_options(opt, is_mllvm_arg);
  case TSan:
    return handle_tsan_options(opt, is_mllvm_arg);
  case UBSan:
    return handle_ubsan_options(opt);
  case XSan:
    break;
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
    // Use env var to control clang only perform frontend 
    // transformation for sanitizers.
    setenv("XSAN_ONLY_FRONTEND", "1", 1);
    break;
  case XSan:
  case SanNone:
    return;
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
  case UBSan:
  case XSan:
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
  // cc_params[cc_par_cnt++] = "-mllvm";
  // cc_params[cc_par_cnt++] = "-memlog-hook-inst=1";

}

static void add_sanitizer_runtime(enum SanitizerType sanTy, u8 is_cxx, u8 is_dso) {
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
  
  if (sanTy == ASan) {
    // Link all contents in *.a, rather than only link symbols in demands.
    // e.g., link preinit_array symbol, which is not used in user program.
    cc_params[cc_par_cnt++] = "-Wl,--whole-archive";
    // TODO: eliminate "linux" in path, and do not hard-coded embed x86_64
    cc_params[cc_par_cnt++] = alloc_printf("%s/lib/linux/libclang_rt.asan_static-x86_64.a", obj_path);
    // Deativate the effect of `--whole-archive`, i.e., only link symbols in demands.
    cc_params[cc_par_cnt++] = "-Wl,--no-whole-archive";
  }

  if (is_dso) {
    return;
  }
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
  case SanNone:
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
/* Copy argv to cc_params, making the necessary edits. */

static void edit_params(u32 argc, char** argv) {

  u8 fortify_set = 0, asan_set = 0, x_set = 0, bit_mode = 0, shared_linking = 0,
     preprocessor_only = 0, have_unroll = 0, have_o = 0, have_pic = 0,
     have_c = 0, partial_linking = 0;
  u8 *name;
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
  cc_params[cc_par_cnt++] = "-sanitizer-coverage-block-threshold=0";
#endif
#else
  cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = "-load";
  cc_params[cc_par_cnt++] = "-Xclang";
  cc_params[cc_par_cnt++] = alloc_printf("%s/afl-llvm-pass.so", obj_path);
#endif*/ /* ^USE_TRACE_PC */

  cc_params[cc_par_cnt++] = "-Qunused-arguments";

  xsanTy = detect_san_type(argc, argv);
  while (--argc) {
    u8* cur = *(++argv);

    if (!strcmp(cur, "--driver-mode=g++")) is_cxx = 1;

    if (!strcmp(cur, "-m32")) bit_mode = 32;
    if (!strcmp(cur, "armv7a-linux-androideabi")) bit_mode = 32;
    if (!strcmp(cur, "-m64")) bit_mode = 64;

    if (!strcmp(cur, "-xc++")) x_set = 1;
    if (!strcmp(cur, "-xc")) x_set = 1;
    if (!strcmp(cur, "-x")) x_set = 1;

    if (!strcmp(cur, "-fsanitize=address") ||
        !strcmp(cur, "-fsanitize=memory")) asan_set = 1;

    if (strstr(cur, "FORTIFY_SOURCE")) fortify_set = 1;

    if (!strcmp(cur, "-Wl,-z,defs") ||
        !strcmp(cur, "-Wl,--no-undefined")) continue;

    if (!strcmp(cur, "-E")) preprocessor_only = 1;
    if (!strcmp(cur, "-shared")) shared_linking = 1;
    if (!strcmp(cur, "-dynamiclib")) shared_linking = 1;
    if (!strcmp(cur, "-Wl,-r")) partial_linking = 1;
    if (!strcmp(cur, "-Wl,-i")) partial_linking = 1;
    if (!strcmp(cur, "-Wl,--relocatable")) partial_linking = 1;
    if (!strcmp(cur, "-r")) partial_linking = 1;
    if (!strcmp(cur, "--relocatable")) partial_linking = 1;
    if (!strcmp(cur, "-c")) have_c = 1;

    if (!strncmp(cur, "-O", 2)) have_o = 1;
    if (!strncmp(cur, "-funroll-loop", 13)) have_unroll = 1;
    if (handle_sanitizer_options(cur, 
        !strcmp(argv[-1], "-mllvm"), xsanTy)) {
      continue;
    }

    cc_params[cc_par_cnt++] = cur;

  }

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

  if (!getenv("X_DONT_OPTIMIZE")) {
    cc_params[cc_par_cnt++] = "-g";
    if (!have_o) cc_params[cc_par_cnt++] = "-O3";
    if (!have_unroll && !have_o) cc_params[cc_par_cnt++] = "-funroll-loops";
  }
  

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

  if (x_set) {
    cc_params[cc_par_cnt++] = "-x";
    cc_params[cc_par_cnt++] = "none";
  }

  init_sanitizer_setting(xsanTy);
  regist_pass_plugin(xsanTy);
  add_sanitizer_runtime(xsanTy, is_cxx, shared_linking);

  cc_params[cc_par_cnt++] = "-fuse-ld=lld";

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

int main(int argc, char** argv) {

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
  find_obj(argv[0]);
#endif

  edit_params(argc, argv);

  if (!!getenv("XCLANG_DEBUG")) {
    print_cmdline(argc);
  }

  
  execvp(cc_params[0], (char**)cc_params);
 
  FATAL("Oops, failed to execute '%s' - check your PATH", cc_params[0]);

  return 0;

}
