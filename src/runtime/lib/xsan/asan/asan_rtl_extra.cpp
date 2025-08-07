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

// -------------------------- FakeStack --------------------------
// XSan needs expose some hooks for FakeStack::Destroy() / malloc  / free
// As these modifications are too minor, we do not want directly modify the
// original code. Instead, we wrap the original functions and call the original
// functions in the wrapped functions.
#include "asan_fake_stack.h"
using __asan::FakeStack;

/// One Thread has only ONE FakeStack
// Thread-local variable tracking the size of the current thread's FakeStack.
// This is set on FakeStack creation and used in the Destroy hook to
// inform the hook of the stack size being destroyed.
static THREADLOCAL uptr fake_stack_size = 0;

// static __asan::FakeStack::Create(uptr stack_size_log)
XSAN_WRAPPER(FakeStack *, _ZN6__asan9FakeStack6CreateEm, uptr stack_size_log) {
  static uptr kMinStackSizeLog = 16;
  static uptr kMaxStackSizeLog = FIRST_32_SECOND_64(24, 28);
  if (stack_size_log < kMinStackSizeLog)
    stack_size_log = kMinStackSizeLog;
  if (stack_size_log > kMaxStackSizeLog)
    stack_size_log = kMaxStackSizeLog;
  ::fake_stack_size = FakeStack::RequiredSize(stack_size_log);
  return XSAN_REAL(_ZN6__asan9FakeStack6CreateEm)(stack_size_log);
}

// __asan::FakeStack::Destroy(int tid)
XSAN_WRAPPER(void, _ZN6__asan9FakeStack7DestroyEi, FakeStack *self, int tid) {
  ::__xsan::OnFakeStackDestroy(reinterpret_cast<uptr>(self), ::fake_stack_size);
  XSAN_REAL(_ZN6__asan9FakeStack7DestroyEi)(self, tid);
}

#define WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(class_id)                    \
  SANITIZER_INTERFACE_ATTRIBUTE XSAN_DECLARE_WRAPPER(                     \
      uptr, __asan_stack_malloc_##class_id, uptr size);                   \
  XSAN_WRAPPER(uptr, __asan_stack_malloc_##class_id, uptr size) {         \
    uptr ptr = XSAN_REAL(__asan_stack_malloc_##class_id)(size);           \
    ::__xsan::OnFakeStackAlloc(ptr, size);                                \
    return ptr;                                                           \
  }                                                                       \
  SANITIZER_INTERFACE_ATTRIBUTE XSAN_DECLARE_WRAPPER(                     \
      uptr, __asan_stack_malloc_always_##class_id, uptr size);            \
  XSAN_WRAPPER(uptr, __asan_stack_malloc_always_##class_id, uptr size) {  \
    uptr ptr = XSAN_REAL(__asan_stack_malloc_always_##class_id)(size);    \
    ::__xsan::OnFakeStackAlloc(ptr, size);                                \
    return ptr;                                                           \
  }                                                                       \
  SANITIZER_INTERFACE_ATTRIBUTE XSAN_DECLARE_WRAPPER(                     \
      void, __asan_stack_free_##class_id, uptr ptr, uptr size);           \
  XSAN_WRAPPER(void, __asan_stack_free_##class_id, uptr ptr, uptr size) { \
    ::__xsan::OnFakeStackFree(ptr, size);                                 \
    XSAN_REAL(__asan_stack_free_##class_id)(ptr, size);                   \
  }

WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(0)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(1)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(2)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(3)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(4)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(5)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(6)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(7)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(8)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(9)
WRAP_STACK_MALLOC_FREE_WITH_CLASS_ID(10)
