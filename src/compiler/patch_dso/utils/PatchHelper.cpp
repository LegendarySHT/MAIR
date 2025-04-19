#include <dlfcn.h>
#include <llvm/Support/Process.h>

#include "PatchHelper.h"
#include "xsan_common.h"

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

std::string getXsanAbsPath(llvm::StringRef rel_path) {
  if (!xsan_base_dir.has_value()) {
    std::fputs("Cannot parse the xsan base directory."
          "Please set it in environment varibale XSAN_BASE_DIR.",
          stderr);
    return "";
  }
  std::string absPath = xsan_base_dir.getValue() + "/";
  absPath += rel_path;
  return absPath;
}

void *getRealFuncAddr(void *InterceptorFunc) {
  Dl_info Info;
  dladdr(InterceptorFunc, &Info);
  void *RealFuncAddr = getRealFuncAddrImpl(Info.dli_sname, InterceptorFunc);
  assert(RealFuncAddr && "Failed to find the real function address");
  return RealFuncAddr;
}
