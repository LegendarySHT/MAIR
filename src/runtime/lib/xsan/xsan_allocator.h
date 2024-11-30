#pragma once

#include <sanitizer_common/sanitizer_allocator.h>
#include <sanitizer_common/sanitizer_list.h>
#include <sanitizer_common/sanitizer_platform.h>

#include "asan/asan_allocator.h"
#include "xsan_flags.h"
#include "xsan_internal.h"

namespace __xsan {

using __asan::AllocType;
using PrimaryAllocator = __asan::PrimaryAllocator;

/// Provides some interfaces for other sanitizer.
class XsanAllocator {
public:
  using AllocatorCache = __asan::AllocatorCache;

public:
  bool PointerIsMine(const void*p);
  void *GetBlockBegin(const void*p);
  void *AllocateInternel(uptr size, BufferedStackTrace *stack);
  void DeallocateInternal(void *ptr, BufferedStackTrace *stack);
  void PrintStats();
};

XsanAllocator *alloctor();

void *xsan_memalign(uptr alignment, uptr size, BufferedStackTrace *stack,
                    AllocType alloc_type);
void xsan_free(void *ptr, BufferedStackTrace *stack, AllocType alloc_type);
void xsan_delete(void *ptr, uptr size, uptr alignment,
                 BufferedStackTrace *stack, AllocType alloc_type);

void *xsan_malloc(uptr size, BufferedStackTrace *stack);
void *xsan_calloc(uptr nmemb, uptr size, BufferedStackTrace *stack);
void *xsan_realloc(void *p, uptr size, BufferedStackTrace *stack);
void *xsan_reallocarray(void *p, uptr nmemb, uptr size,
                        BufferedStackTrace *stack);
void *xsan_valloc(uptr size, BufferedStackTrace *stack);
void *xsan_pvalloc(uptr size, BufferedStackTrace *stack);

void *xsan_aligned_alloc(uptr alignment, uptr size, BufferedStackTrace *stack);
int xsan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        BufferedStackTrace *stack);
uptr xsan_malloc_usable_size(const void *ptr, uptr pc, uptr bp);

uptr xsan_mz_size(const void *ptr);
void xsan_mz_force_lock();
void xsan_mz_force_unlock();


/// ------ Comes from tsan/tsan_mman.h. -----------
// For internal data structures.
void *Alloc(uptr sz);
void FreeImpl(void *p);

template <typename T, typename... Args>
T *New(Args &&...args) {
  return new (Alloc(sizeof(T))) T(static_cast<Args &&>(args)...);
}

template <typename T>
void Free(T *&p) {
  if (p == nullptr)
    return;
  FreeImpl(p);
  p = nullptr;
}

template <typename T>
void DestroyAndFree(T *&p) {
  if (p == nullptr)
    return;
  p->~T();
  Free(p);
}

/// ------------------------------------------------

}  // namespace __xsan
