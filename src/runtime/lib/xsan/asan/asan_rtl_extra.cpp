#include "../xsan_common_defs.h"
#include "../xsan_hooks.h"
#include "asan_report.h"
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

/// ASan modifies the global memory structure, so we need to correct the
/// global variable description obtained from the symbolizer.
void CorrectGlobalVariableDesc(const uptr addr, DataInfo &desc) {
  __asan_global globals[4];
  int numCandidates =
      GetGlobalsForAddress(addr, globals, nullptr, ARRAY_SIZE(globals));

  /// ASan gets globals near to the address. We need to find out the one
  /// containing the address.
  for (int i = 0; i < numCandidates; i++) {
    __asan_global &g = globals[i];
    if (g.beg <= addr && addr < g.beg + g.size) {
      desc.size = g.size;
      break;
    }
  }
}

}  // namespace __asan

// FakeStack::Destroy()
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
