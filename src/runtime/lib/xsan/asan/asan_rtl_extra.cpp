#include "../xsan_common_defs.h"
#include "../xsan_hooks.h"
#include "orig/asan_fake_stack.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
namespace __asan {

/// Util function to get the real size of a global variable by address
/// Returns 0 if the global is not found or if mu_for_globals is locked.
uptr GetRealGlobalSizeByAddr(uptr addr);

}  // namespace __asan

// __asan::PlatformUnpoisonStacks()
// Some internal XSan code, e.g., __asan_handle_no_return, performed sanity
// checks before, leading to FP.
// Now we skip such internal sanity checks by maintaining a new state
// `xsan_in_internal`.
XSAN_WRAPPER(void, _ZN6__asan22PlatformUnpoisonStacksEv, ) {
  /// To avoid sanity checks and alloca.
  __xsan::ScopedXsanInternal sxi;
  XSAN_REAL(_ZN6__asan22PlatformUnpoisonStacksEv)();
}

// __sanitizer::Symbolizer::SymbolizeData
// ASan modifies the global memory structure, so we need to correct the
// global variable description obtained from the symbolizer.
XSAN_WRAPPER(bool, _ZN11__sanitizer10Symbolizer13SymbolizeDataEmPNS_8DataInfoE,
             __sanitizer::Symbolizer *self, uptr address,
             __sanitizer::DataInfo *info) {
  bool symbolized_success =
      XSAN_REAL(_ZN11__sanitizer10Symbolizer13SymbolizeDataEmPNS_8DataInfoE)(
          self, address, info);
  if (info && symbolized_success) {
    uptr global_size = __asan::GetRealGlobalSizeByAddr(address);
    if (global_size) {
      info->size = global_size;
    }
  }
  return symbolized_success;
}
