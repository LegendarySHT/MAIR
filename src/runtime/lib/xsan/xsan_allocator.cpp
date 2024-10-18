#include "xsan_allocator.h"

// #include "xsan_mapping.h"
// #include "xsan_poisoning.h"
// #include "xsan_report.h"
#include "asan_allocator.h"
#include "tsan_rtl.h"
#include "xsan_stack.h"
#include "xsan_thread.h"
// #include "xsan_thread.h"
#include <lsan/lsan_common.h>
#include <sanitizer_common/sanitizer_allocator_checks.h>
#include <sanitizer_common/sanitizer_allocator_interface.h>
#include <sanitizer_common/sanitizer_errno.h>
#include <sanitizer_common/sanitizer_flags.h>
#include <sanitizer_common/sanitizer_internal_defs.h>
#include <sanitizer_common/sanitizer_list.h>
#include <sanitizer_common/sanitizer_quarantine.h>
#include <sanitizer_common/sanitizer_stackdepot.h>

namespace __xsan {

void xsan_free(void *ptr, BufferedStackTrace *stack, AllocType alloc_type) {
  __asan::asan_free(ptr, stack, alloc_type);
}

void xsan_delete(void *ptr, uptr size, uptr alignment,
                 BufferedStackTrace *stack, AllocType alloc_type) {
  __asan::asan_delete(ptr, size, alignment, stack, alloc_type);
}

void *xsan_malloc(uptr size, BufferedStackTrace *stack) {
  void *res = __asan::asan_malloc(size, stack);
  return res;
}

void *xsan_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack) {
  void *res = __asan::asan_calloc(nmemb, size, stack);
  return res;
}

void *xsan_reallocarray(void *p, uptr nmemb, uptr size,
                        BufferedStackTrace *stack) {
  void *res = __asan::asan_reallocarray(p, nmemb, size, stack);
  return res;
}

void *xsan_realloc(void *p, uptr size, BufferedStackTrace *stack) {
  void *res = __asan::asan_realloc(p, size, stack);
  return res;
}

void *xsan_valloc(uptr size, BufferedStackTrace *stack) {
  void *res = __asan::asan_valloc(size, stack);
  return res;
}

void *xsan_pvalloc(uptr size, BufferedStackTrace *stack) {
  void *res = __asan::asan_pvalloc(size, stack);
  return res;
}

void *xsan_memalign(uptr alignment, uptr size, BufferedStackTrace *stack,
                    AllocType alloc_type) {
  void *res = __asan::asan_memalign(alignment, size, stack, alloc_type);
  return res;
}

void *xsan_aligned_alloc(uptr alignment, uptr size, BufferedStackTrace *stack) {
  void *res = __asan::asan_aligned_alloc(alignment, size, stack);
  return res;
}

int xsan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        BufferedStackTrace *stack) {
  int res = __asan::asan_posix_memalign(memptr, alignment, size, stack);
  return res;
}

uptr xsan_malloc_usable_size(const void *ptr, uptr pc, uptr bp) {
  uptr res = __asan::asan_malloc_usable_size(ptr, pc, bp);
  return res;
}

uptr xsan_mz_size(const void *ptr) {
  uptr res = __asan::asan_mz_size(ptr);
  return res;
}

void xsan_mz_force_lock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  __asan::asan_mz_force_lock();
}

void xsan_mz_force_unlock() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  __asan::asan_mz_force_unlock();
}
}  // namespace __xsan

namespace __tsan {
void SignalUnsafeCall(ThreadState *thr, uptr pc);
}

namespace __xsan {
// ---------------------- API exposed by xsan::Alloctor ---------------
XsanAllocator xsan_alloctor;

XsanAllocator *alloctor() { return &xsan_alloctor; }


/// ASan's embedded metadata should not be exposed
bool XsanAllocator::PointerIsMine(const void *p) {
  return __asan::UserPointerIsMine(p);
}

/// ASan's embedded metadata should not be exposed
void *XsanAllocator::GetBlockBegin(const void *p) {
  return __asan::GetUserBlockBegin(p);
}

void *XsanAllocator::AllocateInternel(uptr size, BufferedStackTrace *stack) {
  return __asan::asan_malloc_internal(size, stack);
}

void XsanAllocator::DeallocateInternal(void *ptr, BufferedStackTrace *stack) {
  __asan::asan_free_internal(ptr, stack);
}

void XsanAllocator::PrintStats() {
  __asan::PrintInternalAllocatorStats();
}

// ---------------------- Hook for other Sanitizers -------------------
void XsanAllocHook(uptr ptr, uptr size, bool write) {
  auto [thr, pc] = GetCurrentThread()->getTsanArgs();
  if (__tsan::is_tsan_initialized()) {
    /// TODO: remove code related to tsan's uaf checking
    __tsan::OnUserAlloc(thr, pc, ptr, size, write);
  }
}

void XsanFreeHook(uptr p, bool write) {
  auto [thr, pc] = GetCurrentThread()->getTsanArgs();
  if (__tsan::is_tsan_initialized()) {
    /// TODO: remove code related to tsan's uaf checking
    __tsan::OnUserFree(thr, pc, p, write);
  }
}

void XsanAllocFreeTailHook() {
  auto [thr, pc] = GetCurrentThread()->getTsanArgs();
  /// TODO: handle calls from tsan_fd.cpp
  __tsan::SignalUnsafeCall(thr, pc);
}

}  // namespace __xsan

// ---------------------- Interface ---------------- {{{1

/// This part now is implemented by asan/asan_allocator.cpp

// using namespace __xsan;

// // ASan allocator doesn't reserve extra bytes, so normally we would
// // just return "size". We don't want to expose our redzone sizes, etc here.
// uptr __sanitizer_get_estimated_allocated_size(uptr size) {
//   return size;
// }

// int __sanitizer_get_ownership(const void *p) {
//   return
// }

// uptr __sanitizer_get_allocated_size(const void *p) {
//   if (!p) return 0;
//   uptr ptr = reinterpret_cast<uptr>(p);
//   uptr allocated_size = instance.AllocationSize(ptr);
//   // Die if p is not malloced or if it is already freed.
//   if (allocated_size == 0) {
//     GET_STACK_TRACE_FATAL_HERE;
//     ReportSanitizerGetAllocatedSizeNotOwned(ptr, &stack);
//   }
//   return allocated_size;
// }

// void __sanitizer_purge_allocator() {
//   GET_STACK_TRACE_MALLOC;
//   instance.Purge(&stack);
// }

// int __xsan_update_allocation_context(void* addr) {
//   GET_STACK_TRACE_MALLOC;
//   return instance.UpdateAllocationStack((uptr)addr, &stack);
// }
