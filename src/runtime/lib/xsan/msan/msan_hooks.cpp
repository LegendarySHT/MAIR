#include "msan_hooks.h"

#include "../xsan_allocator.h"
#include "../xsan_hooks.h"
#include "../xsan_thread.h"

#undef MEM_TO_SHADOW  // bad macro from xsan_allocator.h

#include "msan.h"
#include "msan_interface_xsan.h"
#include "msan_origin.h"
#include "sanitizer_common/sanitizer_platform_interceptors.h"
#include "sanitizer_common/sanitizer_tls_get_addr.h"

namespace __msan {

THREADLOCAL MsanThread *MsanThread::msan_current_thread;

void MsanHooks::OnAllocatorUnmap(uptr p, uptr size) {
  __msan_unpoison((void *)p, size);

  // We are about to unmap a chunk of user memory.
  // Mark the corresponding shadow memory as not needed.
  uptr shadow_p = MEM_TO_SHADOW(p);
  ReleaseMemoryPagesToOS(shadow_p, shadow_p + size);
  if (__msan_get_track_origins()) {
    uptr origin_p = MEM_TO_ORIGIN(p);
    ReleaseMemoryPagesToOS(origin_p, origin_p + size);
  }
}

void MsanHooks::OnXsanAllocHook(uptr ptr, uptr size,
                                BufferedStackTrace *stack) {
  void *allocated = (void *)ptr;
  if (FuncScope<::__xsan::ScopedFunc::calloc>::in_calloc_scope) {
    __msan_unpoison(allocated, size);
  } else if (flags()->poison_in_malloc) {
    __msan_poison(allocated, size);
    if (__msan_get_track_origins()) {
      stack->tag = StackTrace::TAG_ALLOC;
      Origin o = Origin::CreateHeapOrigin(stack);
      __msan_set_origin(allocated, size, o.raw_id());
    }
  }
  UnpoisonParam(2);
}

void MsanHooks::OnXsanFreeHook(uptr ptr, uptr size, BufferedStackTrace *stack) {
  void *p = (void *)ptr;
  UnpoisonParam(1);
  // This memory will not be reused by anyone else, so we are free to keep it
  // poisoned. The secondary allocator will unmap and unpoison by
  // OnAllocatorUnmap, no need to poison it here.
  if (flags()->poison_in_free && ::__xsan::allocator()->FromPrimary(p)) {
    __msan_poison(p, size);
    if (__msan_get_track_origins()) {
      stack->tag = StackTrace::TAG_DEALLOC;
      Origin o = Origin::CreateHeapOrigin(stack);
      __msan_set_origin(p, size, o.raw_id());
    }
  }
}

void MsanHooks::OnLibraryLoaded(const char *filename, void *handle) {
#if SANITIZER_INTERCEPT_DLOPEN_DLCLOSE
  link_map *map = GET_LINK_MAP_BY_DLOPEN_HANDLE((handle));
  if (filename && map)
    ForEachMappedRegion(map, __msan_unpoison);
#endif
}

int MsanHooks::RequireStackTracesSize() {
  int size = flags()->store_context_size;
  return SANITIZER_CAN_FAST_UNWIND ? size : Min(1, size);
}

static void ClearShadowForThreadStackAndTLS(MsanHooks::Thread &thread) {
  auto stack = __xsan::XsanThread::GetStackBounds(thread.xsan_key);
  auto tls = __xsan::XsanThread::GetTlsBounds(thread.xsan_key);
  __msan_unpoison((void *)stack.bottom, stack.top - stack.bottom);
  if (tls.begin != tls.end)
    __msan_unpoison((void *)tls.begin, tls.end - tls.begin);
  DTLS *dtls = __xsan::XsanThread::GetDtls(thread.xsan_key);
  ForEachDVT(dtls, [](const DTLS::DTV &dtv, int id) {
    __msan_unpoison((void *)(dtv.beg), dtv.size);
  });
}

void MsanHooks::ChildThreadInit(Thread &thread, tid_t os_id) {
  __xsan::XsanThread::SetQueryKey(thread.xsan_key);
  // Make sure we do not reset the current MsanThread.
  CHECK_EQ(0, Thread::msan_current_thread);
  Thread::msan_current_thread = &thread;
}

void MsanHooks::ChildThreadStart(Thread &thread, tid_t os_id) {
  ClearShadowForThreadStackAndTLS(thread);
}

void MsanHooks::DestroyThread(Thread &thread) {
  ClearShadowForThreadStackAndTLS(thread);
  Thread::msan_current_thread = nullptr;
}

#define MSAN_INTERFACE_HOOK(size, xop, mop)              \
  template <>                                            \
  void MsanHooks::__xsan_unaligned_##xop<size>(uptr p) { \
    __msan_unaligned_##mop##size(p);                     \
  }

MSAN_INTERFACE_HOOK(2, read, load)
MSAN_INTERFACE_HOOK(4, read, load)
MSAN_INTERFACE_HOOK(8, read, load)

MSAN_INTERFACE_HOOK(2, write, store)
MSAN_INTERFACE_HOOK(4, write, store)
MSAN_INTERFACE_HOOK(8, write, store)

#undef MSAN_INTERFACE_HOOK

THREADLOCAL int
    MsanHooks::FuncScope<__xsan::ScopedFunc::calloc>::in_calloc_scope = 0;

THREADLOCAL int MsanHooks::FuncScope<__xsan::ScopedFunc::common>::saved_scope;

THREADLOCAL int MsanHooks::FuncScope<
    __xsan::ScopedFunc::xsan_memintrinsic>::in_xsan_memintrinsic_scope = 0;

MsanHooks::FuncScope<__xsan::ScopedFunc::signal>::FuncScope() {
  if (MsanThread *t = GetCurrentThread())
    t->EnterSignalHandler();
}

MsanHooks::FuncScope<__xsan::ScopedFunc::signal>::~FuncScope() {
  if (MsanThread *t = GetCurrentThread())
    t->LeaveSignalHandler();
}

}  // namespace __msan
