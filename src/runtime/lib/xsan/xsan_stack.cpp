#include "xsan_stack.h"

#include "asan/asan_thread.h"
#include "xsan_hooks.h"
#include "xsan_thread.h"

namespace __xsan {

class ScopedUnwinding {
 public:
  ALWAYS_INLINE explicit ScopedUnwinding(XsanThread *t) : thread(t) {
    if (thread) {
      can_unwind = !thread->isUnwinding();
      thread->setUnwinding(true);
    }
    OnEnterUnwind();
  }
  ALWAYS_INLINE ~ScopedUnwinding() {
    OnExitUnwind();
    if (thread) {
      thread->setUnwinding(false);
    }
  }

  ALWAYS_INLINE bool CanUnwind() const { return can_unwind; }

 private:
  XsanThread *thread = nullptr;
  bool can_unwind = true;
};

}  // namespace __xsan

void __sanitizer::BufferedStackTrace::UnwindImpl(uptr pc, uptr bp,
                                                 void *context,
                                                 bool request_fast,
                                                 u32 max_depth) {
  using namespace __xsan;
  size = 0;
  if (UNLIKELY(!XsanInited()))
    return;
  request_fast = StackTrace::WillUseFastUnwind(request_fast);
  XsanThread *_t = GetCurrentThread();
  ScopedUnwinding unwind_scope(_t);
  /// TODO: Make XSan centrally manage the stack trace.
  __asan::AsanThread *t = _t ? _t->asan.asan_thread : nullptr;
  if (!unwind_scope.CanUnwind())
    return;
  if (request_fast) {
    if (t) {
      Unwind(max_depth, pc, bp, nullptr, t->stack_top(), t->stack_bottom(),
             true);
    }
    return;
  }
  if (SANITIZER_MIPS && t &&
      !IsValidFrame(bp, t->stack_top(), t->stack_bottom()))
    return;
  Unwind(max_depth, pc, bp, context, t ? t->stack_top() : 0,
         t ? t->stack_bottom() : 0, false);
}
