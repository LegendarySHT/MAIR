#include "../xsan_common_defs.h"
#include "../xsan_hooks.h"
#include "asan_thread.h"
#include "orig/asan_fake_stack.h"
#include "sanitizer_common/sanitizer_symbolizer.h"
namespace __asan {

void OnPthreadCreate() {
  EnsureMainThreadIDIsCorrect();

  // Strict init-order checking is thread-hostile.
  if (__asan::flags()->strict_init_order)
    __asan::StopInitOrderChecking();
}

/// Util function to get the real size of a global variable by address
/// Returns 0 if the global is not found or if mu_for_globals is locked.
uptr GetRealGlobalSizeByAddr(uptr addr);

}  // namespace __asan

// __asan::FakeStack::Destroy()
// XSan needs to intercept FakeStack::Destroy() to register the hook
// `OnFakeStackDestory()` for notifying other sanitizers that the fake stack is
// destroyed.
XSAN_WRAPPER(void, _ZN6__asan9FakeStack7DestroyEi,
             __asan::FakeStack *fake_stack, int tid) {
  __xsan::OnFakeStackDestory(
      reinterpret_cast<uptr>(fake_stack),
      fake_stack->RequiredSize(fake_stack->stack_size_log()));
  XSAN_REAL(_ZN6__asan9FakeStack7DestroyEi)(fake_stack, tid);
}

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
