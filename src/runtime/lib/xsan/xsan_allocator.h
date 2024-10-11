#pragma once

#include <sanitizer_common/sanitizer_allocator.h>
#include <sanitizer_common/sanitizer_list.h>
#include <sanitizer_common/sanitizer_platform.h>

#include "asan/asan_allocator.h"
#include "xsan_flags.h"
#include "xsan_interceptors.h"
#include "xsan_internal.h"

namespace __xsan {

using __asan::AllocType;

void *xsan_memalign(uptr alignment, uptr size, XsanExtraAllocArgs &extra_arg,
                    AllocType alloc_type);
void xsan_free(void *ptr, XsanExtraAllocArgs &extra_arg, AllocType alloc_type);
void xsan_delete(void *ptr, uptr size, uptr alignment,
                 XsanExtraAllocArgs &extra_arg, AllocType alloc_type);

void *xsan_malloc(uptr size, XsanExtraAllocArgs &extra_arg);
void *xsan_calloc(uptr nmemb, uptr size, XsanExtraAllocArgs &extra_arg);
void *xsan_realloc(void *p, uptr size, XsanExtraAllocArgs &extra_arg);
void *xsan_reallocarray(void *p, uptr nmemb, uptr size,
                        XsanExtraAllocArgs &extra_arg);
void *xsan_valloc(uptr size, XsanExtraAllocArgs &extra_arg);
void *xsan_pvalloc(uptr size, XsanExtraAllocArgs &extra_arg);

void *xsan_aligned_alloc(uptr alignment, uptr size,
                         XsanExtraAllocArgs &extra_arg);
int xsan_posix_memalign(void **memptr, uptr alignment, uptr size,
                        XsanExtraAllocArgs &extra_arg);
uptr xsan_malloc_usable_size(const void *ptr, uptr pc, uptr bp);

uptr xsan_mz_size(const void *ptr);
void xsan_mz_force_lock();
void xsan_mz_force_unlock();

}  // namespace __xsan
