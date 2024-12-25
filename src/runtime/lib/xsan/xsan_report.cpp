//===-- xsan_report.cpp ---------------------------------------------------===//
//
// This file is a part of XSan, an composition of several sanitizers.
//
// This file is used to provide unified interface for reporting errors and
// warnings from XSan.
//
//===----------------------------------------------------------------------===//

#include "xsan_report.h"

#include "sanitizer_common/sanitizer_symbolizer.h"
#include "xsan_internal.h"

namespace __asan {
/// ASan modifies the global memory structure, so we need to correct the
/// global variable description obtained from the symbolizer.
void CorrectGlobalVariableDesc(const uptr addr, DataInfo &desc);

}  // namespace __asan

namespace __xsan {
void CorrectGlobalVariableDesc([[maybe_unused]] const uptr addr,
                               [[maybe_unused]] DataInfo &desc) {
#if XSAN_CONTAINS_ASAN
  __asan::CorrectGlobalVariableDesc(addr, desc);
#endif
}
}  // namespace __xsan