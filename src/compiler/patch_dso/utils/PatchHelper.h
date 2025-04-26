// Match functions
#include "xsan_common.h"
#include <bitset>
#include <cassert>
#include <cstring>
#include <string>
#include <type_traits>
#include <filesystem>

extern const bool XsanEnabled;
extern const std::bitset<XSan + 1> xsan_mask;
extern const SanitizerType sanTy;

std::filesystem::path getThisPatchDsoPath();
std::filesystem::path getXsanAbsPath(std::string_view rel_path);
void *getRealFuncAddr(void *InterceptorFunc);

template <typename FunPtr>
static auto getRealFuncAddr(FunPtr InterceptorFunc)
    -> std::enable_if_t<std::is_function_v<std::remove_pointer_t<FunPtr>> ||
                            std::is_member_function_pointer_v<FunPtr>,
                        FunPtr> {
  void *InterceptorAddr;
  std::memcpy(&InterceptorAddr, &InterceptorFunc, sizeof(InterceptorAddr));
  void *RealFuncAddr = getRealFuncAddr(InterceptorAddr);
  FunPtr RealFunc;
  std::memcpy(&RealFunc, &RealFuncAddr, sizeof(RealFunc));
  return RealFunc;
}
