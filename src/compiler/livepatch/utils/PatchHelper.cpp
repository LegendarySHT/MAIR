#include <cstdio>
#include <dlfcn.h>
#include <filesystem>
#include <link.h>
#include <llvm/Support/Memory.h>
#include <llvm/Support/Process.h>
#include <string_view>

#include "PatchHelper.h"
#include "config_compile.h"
#include "debug.h"
#include "types.h"
#include "xsan_common.h"

namespace fs = std::filesystem;

// Lazy initialization for XsanEnabled
bool isXsanEnabled() {
  static const bool enabled =
      llvm::sys::Process::GetEnv("XSAN_ONLY_FRONTEND").hasValue();
  return enabled;
}

// Lazy initialization for str_mask
const std::string &getStrMask() {
  static const std::string val =
      isXsanEnabled()
          ? llvm::sys::Process::GetEnv("XSAN_ONLY_FRONTEND").getValue()
          : "";
  return val;
}

// Lazy initialization for xsan_mask
const std::bitset<XSan + 1> &getXsanMask() {
  static const std::bitset<XSan + 1> mask(
      isXsanEnabled() ? std::strtoul(getStrMask().c_str(), nullptr, 0) : 0);
  return mask;
}

SanitizerType getSanType() {
  static const SanitizerType sanTy = []() {
    for (SanitizerType i : {XSan, ASan, TSan, UBSan}) {
      if (getXsanMask().test(i)) {
        return i;
      }
    }
    return SanNone;
  }();
  return sanTy;
}

fs::path getSelfPath() {
  static std::optional<fs::path> self_path = std::nullopt;
  if (self_path.has_value()) {
    return self_path.value();
  }

  // Get the executable path of the current process.
  char proc_path[PATH_MAX];
  ssize_t len = readlink("/proc/self/exe", proc_path, sizeof(proc_path) - 1);
  if (len == -1) {
    FATAL("Failed to read /proc/self/exe");
  }
  proc_path[len] = '\0';

  // Obtain the executable name.
  self_path = fs::path(proc_path);
  return self_path.value();
}

bool isPatchingProc(const char *proc_name) {
  // Obtain the executable name.
  std::string pname = getSelfPath().filename().string();
  llvm::StringRef proc_name_str = pname;

  // Retrieve the position of proc_name in the string.
  size_t pos = pname.find(proc_name);
  if (pos == std::string::npos) {
    return false;
  }
  
  // Check if there is a letter character before proc_name.
  if (pos > 0 && std::isalpha(pname[pos - 1])) {
    return false;
  }
  
  // Check if there is a letter character after proc_name.
  size_t after_pos = pos + strlen(proc_name);
  if (after_pos < pname.length() && std::isalpha(pname[after_pos])) {
    return false;
  }
  
  return true;
}

fs::path getThisPatchDsoPath() {
  static std::optional<fs::path> this_dso_path = std::nullopt;
  if (this_dso_path.has_value()) {
    return this_dso_path.value();
  }
  fs::path path;
  Dl_info info;
  // Pass the address of the current function to dladdr
  if (dladdr((void *)&getThisPatchDsoPath, &info) != 0 && info.dli_fname) {
    /// Canonicalize the path to remove any symbolic links.
    this_dso_path = fs::canonical(info.dli_fname);
  } else {
    this_dso_path = fs::path();
  }
  return this_dso_path.value();
}

fs::path getXsanAbsPath(std::string_view rel_path) {
  /// TODO: do not use env var to transmit base dir, but parse the DSO link
  /// path.
  static const llvm::Optional<std::string> &xsan_base_dir =
      llvm::sys::Process::GetEnv("XSAN_BASE_DIR");
  // <base_dir>/patch/libclang-patch.so -> <base_dir>/
  static const fs::path PatchBaseDir =
      getThisPatchDsoPath().parent_path().parent_path();
  static const bool ExistEnvVarBaseDir =
      xsan_base_dir.hasValue() && fs::exists(xsan_base_dir.getValue());
  static const fs::path EnvVarBaseDir =
      ExistEnvVarBaseDir ? fs::canonical(xsan_base_dir.getValue()) : fs::path();

  fs::path abs_path;
  if (!xsan_base_dir.hasValue()) {
    abs_path = PatchBaseDir / rel_path;
  } else if (EnvVarBaseDir.empty()) {
    // xsan_base_dir has value, but EnvVarBaseDir is empty.
    WARNF("Warning: The path provided by the environment variable "
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

static const char *getMangledName(void *SymAddr) {
  Dl_info Info;
  if (!dladdr(SymAddr, &Info)) {
    FATAL("dladdr failed for address %p. Error: %s", SymAddr,
          dlerror() ? dlerror() : "Not available");
  }
  return Info.dli_sname;
}

static const char *getDSOName(void *SymAddr) {
  Dl_info Info;
  if (!dladdr(SymAddr, &Info)) {
    FATAL("dladdr failed for address %p. Error: %s", SymAddr,
          dlerror() ? dlerror() : "Not available");
  }
  return Info.dli_fname;
}

static void *checkedDlsym(void *__restrict handle,
                          const char *__restrict name) {
  void *addr = dlsym(handle, name);
  const char *err = dlerror();
  if (err) {
    FATAL("dlsym(%s, \"%s\") failed: %s",
          handle == (void *)RTLD_NEXT ? "RTLD_NEXT" : "RTLD_DEFAULT", name,
          err);
  }
  if (!addr) {
    FATAL("Failed to find the real function address for %s, but dlerror() is "
          "null.",
          name);
  }
  return addr;
}

void *getRealFuncAddr(const char *ManagledName) {
  return checkedDlsym(RTLD_NEXT, ManagledName);
}

void *getRealFuncAddr(void *InterceptorFunc) {
  const char *ManagledName = getMangledName(InterceptorFunc);
  return getRealFuncAddr(ManagledName);
}

void *getMyFuncAddr(const char *ManagledName) {
  void *MyFuncAddr = checkedDlsym(RTLD_DEFAULT, ManagledName);
  const char *DSOName = getDSOName(MyFuncAddr);
  if (!llvm::StringRef(DSOName).endswith(XSAN_DSO_PATCH_FILE)) {
    FATAL("Found wrong my function address for %s, please check LD_PRELOAD. "
          "DSOName: %s",
          ManagledName, DSOName);
  }
  return MyFuncAddr;
}

class ScopedApplyPatch {
  using Memory = llvm::sys::Memory;

  void changeMemoryProtection(unsigned flags) {
    if (auto Ret = Memory::protectMappedMemory(Mem, flags)) {
      FATAL("Failed to protect memory for patching: %s", Ret.message().c_str());
    }
  }

public:
  ScopedApplyPatch(void *FuncAddr) : Mem(FuncAddr, sizeof(XsanPatch)) {
    changeMemoryProtection(Memory::MF_RWE_MASK);
  }
  ~ScopedApplyPatch() {
    changeMemoryProtection(Memory::MF_READ | Memory::MF_EXEC);
  }

private:
  llvm::sys::MemoryBlock Mem;
};

void XsanPatch::validateInit() const {
  if (unlikely(!isInitialized)) {
    FATAL("Cannot use patch without initialization");
  }
}

void XsanPatch::initialize(void *MyFunc, void *RealFunc) {
  std::memcpy(patch, PatchTempl, PatchSize);
  std::memcpy(&patch[FuncAddrOffset], (void *)&MyFunc, sizeof(void *));
  std::memcpy(backup, RealFunc, PatchSize);
  isInitialized = true;
}

bool XsanPatch::isPatched(void *FuncAddr) {
  uint8_t *addr = (uint8_t *)FuncAddr;
  return std::equal(PatchTempl, PatchTempl + FuncAddrOffset, addr) &&
         std::equal(PatchTempl + FuncAddrOffset + sizeof(void *),
                    PatchTempl + PatchSize,
                    addr + FuncAddrOffset + sizeof(void *));
}

void XsanPatch::applyPatch(void *FuncAddr) const {
  validateInit();
  ScopedApplyPatch Scoper(FuncAddr);
  std::memcpy(FuncAddr, patch, sizeof(Data));
}

void XsanPatch::applyBackup(void *FuncAddr) const {
  validateInit();
  ScopedApplyPatch Scoper(FuncAddr);
  std::memcpy(FuncAddr, backup, sizeof(Data));
}

// Return the base address of the executable file
void *get_base_address() {
  static std::optional<void *> base_addr = std::nullopt;
  if (base_addr.has_value()) {
    return base_addr.value();
  }
  std::filesystem::path exe_path = getSelfPath();
  std::string exe_path_str = exe_path.string();
  struct ctx_t {
    std::string exe_path_str;
    void *base_addr;
  } ctx = {exe_path_str, (void *)1};
  dl_iterate_phdr(
      [](struct dl_phdr_info *info, size_t size, void *data) -> int {
        auto *ctx = static_cast<ctx_t *>(data);
        if (!info->dlpi_name || info->dlpi_name[0] == '\0' ||
            ctx->exe_path_str == info->dlpi_name) {
          ctx->base_addr = (void *)info->dlpi_addr;
          return 1; // Stop iteration
        }
        return 0; // Continue iteration
      },
      &ctx);

  if (ctx.base_addr == (void *)1) {
    FATAL("Failed to find base address via dl_iterate_phdr");
  }
  base_addr = ctx.base_addr;
  return base_addr.value();
}

bool is_self_proc_pie() { return get_base_address() != (void *)0; }
