#include <cstdio>
#include <dlfcn.h>
#include <filesystem>
#include <llvm/Support/Process.h>
#include <string_view>

#include "PatchHelper.h"
#include "xsan_common.h"

namespace fs = std::filesystem;

/// TODO: do not use env var to transmit base dir, but parse the DSO link path.
static const llvm::Optional<std::string> xsan_base_dir =
    llvm::sys::Process::GetEnv("XSAN_BASE_DIR");
const bool XsanEnabled =
    llvm::sys::Process::GetEnv("XSAN_ONLY_FRONTEND").hasValue();
static const std::string &str_mask =
    XsanEnabled ? llvm::sys::Process::GetEnv("XSAN_ONLY_FRONTEND").getValue()
                : "";
const std::bitset<XSan + 1>
    xsan_mask(XsanEnabled ? std::strtoul(str_mask.c_str(), nullptr, 0) : 0);

namespace {

void *getRealFuncAddrImpl(const char *ManagledName, void *InterceptorAddr) {
  void *RealAddr = dlsym(RTLD_NEXT, ManagledName);
  if (!RealAddr) {
    RealAddr = dlsym(RTLD_DEFAULT, ManagledName);
    if (RealAddr == InterceptorAddr) {
      RealAddr = nullptr;
    }
  }
  return RealAddr;
}

constexpr SanitizerType gen_sanitizer_type() {
  SanitizerType sanTy = SanNone;
  for (SanitizerType i : {XSan, ASan, TSan, /* MSan, */ UBSan}) {
    if (xsan_mask.test(i)) {
      sanTy = i;
      break;
    }
  }
  return sanTy;
}

} // namespace

const SanitizerType sanTy = gen_sanitizer_type();

fs::path getThisPatchDsoPath() {
  fs::path path;
  Dl_info info;
  // Pass the address of the current function to dladdr
  if (dladdr((void *)&getThisPatchDsoPath, &info) != 0 && info.dli_fname) {
    /// Canonicalize the path to remove any symbolic links.
    return fs::canonical(info.dli_fname);
  }
  return "";
}

fs::path getXsanAbsPath(std::string_view rel_path) {
  // <base_dir>/patch/libclang-patch.so -> <base_dir>/
  static const fs::path PatchBaseDir =
      getThisPatchDsoPath().parent_path().parent_path();
  static const bool ExistEnvVarBaseDir =
      xsan_base_dir.has_value() && fs::exists(xsan_base_dir.getValue());
  static const fs::path EnvVarBaseDir =
      ExistEnvVarBaseDir ? fs::canonical(xsan_base_dir.getValue()) : fs::path();

  fs::path abs_path;
  if (!xsan_base_dir.has_value()) {
    abs_path = PatchBaseDir / rel_path;
  } else if (EnvVarBaseDir.empty()) {
    // xsan_base_dir has value, but EnvVarBaseDir is empty.
    std::fprintf(stderr,
                 "Warning: The path provided by the environment variable "
                 "XSAN_BASE_DIR (\"%s\") is invalid. Using auto-detected base "
                 "path: %s\n",
                 xsan_base_dir.getValue().c_str(), PatchBaseDir.c_str());
    abs_path = PatchBaseDir / rel_path;
  } else {
    abs_path = EnvVarBaseDir / rel_path;
  }

  // xsan_base_dir has value, and EnvVarBaseDir is not empty.
  return abs_path;
}

void *getRealFuncAddr(void *InterceptorFunc) {
  Dl_info Info;
  dladdr(InterceptorFunc, &Info);
  void *RealFuncAddr = getRealFuncAddrImpl(Info.dli_sname, InterceptorFunc);
  assert(RealFuncAddr && "Failed to find the real function address");
  return RealFuncAddr;
}
