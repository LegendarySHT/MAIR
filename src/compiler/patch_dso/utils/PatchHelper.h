// Match functions
#include "xsan_common.h"
#include "llvm/ADT/StringRef.h"
#include <bitset>
#include <cassert>
#include <cstring>
#include <string>

extern const bool XsanEnabled;
extern const std::bitset<XSan + 1> xsan_mask;
extern const SanitizerType sanTy;

std::string getXsanAbsPath(llvm::StringRef rel_path);
void *getRealFuncAddr(void *InterceptorFunc);

template <typename FunPtr>
static auto getRealFuncAddr(FunPtr InterceptorFunc) -> std::enable_if_t<
    std::is_function<std::remove_pointer_t<FunPtr>>::value ||
        std::is_member_function_pointer<FunPtr>::value,
    FunPtr> {
  void *InterceptorAddr;
  std::memcpy(&InterceptorAddr, &InterceptorFunc, sizeof(InterceptorAddr));
  void *RealFuncAddr = getRealFuncAddr(InterceptorAddr);
  FunPtr RealFunc;
  std::memcpy(&RealFunc, &RealFuncAddr, sizeof(RealFunc));
  return RealFunc;
}
