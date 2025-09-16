#include "sanitizer_common/sanitizer_allocator_internal.h"
#include "sanitizer_common/sanitizer_platform.h"
#if SANITIZER_FREEBSD || SANITIZER_FUCHSIA || SANITIZER_LINUX || \
    SANITIZER_NETBSD || SANITIZER_SOLARIS

/// To provide SIZE_T definition, these headers should be included first
/// Otherwise sanitizer_common/sanitizer_allocator_dlsym.h raises an error
#  include "xsan_allocator.h"
#  include "xsan_hooks.h"
#  include "xsan_interceptors.h"
#  include "xsan_internal.h"
#  include "xsan_stack.h"

#  include "lsan/lsan_common.h"
#  include "sanitizer_common/sanitizer_allocator_checks.h"
#  include "sanitizer_common/sanitizer_allocator_dlsym.h"
#  include "sanitizer_common/sanitizer_errno.h"

// ---------------------- Replacement functions ---------------- {{{1
using namespace __xsan;

struct DlsymAlloc : public DlSymAllocator<DlsymAlloc> {
  static bool UseImpl() { return !XsanInited(); }
  static void OnAllocate(const void *ptr, uptr size) {
#  if CAN_SANITIZE_LEAKS
    // Suppress leaks from dlerror(). Previously dlsym hack on global array was
    // used by leak sanitizer as a root region.
    __lsan_register_root_region(ptr, size);
#  endif
  }
  static void OnFree(const void *ptr, uptr size) {
#  if CAN_SANITIZE_LEAKS
    __lsan_unregister_root_region(ptr, size);
#  endif
  }
};

INTERCEPTOR(void, free, void *ptr) {
  if (ptr == 0) 
    return; 
  if (__xsan::in_symbolizer())
    return InternalFree(ptr);
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  SCOPED_XSAN_INTERCEPTOR_MALLOC(free, ptr);
  xsan_free(ptr, &stack, __asan::FROM_MALLOC);
}

#  if SANITIZER_INTERCEPT_CFREE
INTERCEPTOR(void, cfree, void *ptr) {
  if (ptr == 0) 
    return; 
  if (__xsan::in_symbolizer())
    return InternalFree(ptr);
  if (DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Free(ptr);
  SCOPED_XSAN_INTERCEPTOR_MALLOC(cfree, ptr);
  xsan_free(ptr, &stack, __asan::FROM_MALLOC);
}
#  endif  // SANITIZER_INTERCEPT_CFREE

INTERCEPTOR(void *, malloc, uptr size) {
  if (__xsan::in_symbolizer())
    return InternalAlloc(size);
  if (DlsymAlloc::Use())
    return DlsymAlloc::Allocate(size);
  SCOPED_XSAN_INTERCEPTOR_MALLOC(malloc, size);
  return xsan_malloc(size, &stack);
}

INTERCEPTOR(void *, calloc, uptr nmemb, uptr size) {
  if (__xsan::in_symbolizer())
    return InternalCalloc(size, size);
  if (DlsymAlloc::Use())
    return DlsymAlloc::Callocate(nmemb, size);
  FUNC_SCOPE(calloc);
  SCOPED_XSAN_INTERCEPTOR_MALLOC(calloc, nmemb, size);
  return xsan_calloc(nmemb, size, &stack);
}

INTERCEPTOR(void *, realloc, void *ptr, uptr size) {
  if (__xsan::in_symbolizer())
    return InternalRealloc(ptr, size);
  if (DlsymAlloc::Use() || DlsymAlloc::PointerIsMine(ptr))
    return DlsymAlloc::Realloc(ptr, size);
  SCOPED_XSAN_INTERCEPTOR_MALLOC(realloc, ptr, size);
  return xsan_realloc(ptr, size, &stack);
}

#  if SANITIZER_INTERCEPT_REALLOCARRAY
INTERCEPTOR(void *, reallocarray, void *ptr, uptr nmemb, uptr size) {
  if (__xsan::in_symbolizer())
    return InternalReallocArray(ptr, size, size);
  XsanInitFromRtl();
  SCOPED_XSAN_INTERCEPTOR_MALLOC(reallocarray, ptr, nmemb, size);
  return xsan_reallocarray(ptr, nmemb, size, &stack);
}
#  endif  // SANITIZER_INTERCEPT_REALLOCARRAY

#  if SANITIZER_INTERCEPT_MEMALIGN
INTERCEPTOR(void *, memalign, uptr boundary, uptr size) {
  SCOPED_XSAN_INTERCEPTOR_MALLOC(memalign, boundary, size);
  return xsan_memalign(boundary, size, &stack, __asan::FROM_MALLOC);
}

INTERCEPTOR(void *, __libc_memalign, uptr boundary, uptr size) {
  SCOPED_XSAN_INTERCEPTOR_MALLOC(__libc_memalign, boundary, size);
  return xsan_memalign(boundary, size, &stack, __asan::FROM_MALLOC);
}
#  endif  // SANITIZER_INTERCEPT_MEMALIGN

#  if SANITIZER_INTERCEPT_ALIGNED_ALLOC
INTERCEPTOR(void *, aligned_alloc, uptr boundary, uptr size) {
  if (__xsan::in_symbolizer())
    return InternalAlloc(size, nullptr, boundary);
  SCOPED_XSAN_INTERCEPTOR_MALLOC(aligned_alloc, boundary, size);
  return xsan_aligned_alloc(boundary, size, &stack);
}
#  endif  // SANITIZER_INTERCEPT_ALIGNED_ALLOC

INTERCEPTOR(uptr, malloc_usable_size, void *ptr) {
  GET_CURRENT_PC_BP_SP;
  (void)sp;
  return xsan_malloc_usable_size(ptr, pc, bp);
}

#  if SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO
// Interceptors use NRVO and assume that sret will be pre-allocated in
// caller frame.
INTERCEPTOR(__sanitizer_struct_mallinfo, mallinfo, void) {
  __sanitizer_struct_mallinfo sret;
  XsanInitFromRtl();
  internal_memset(&sret, 0, sizeof(sret));
  XSAN_INIT_RANGE(nullptr, &sret, sizeof(sret));
  return sret;
}

INTERCEPTOR(__sanitizer_struct_mallinfo2, mallinfo2, void) {
  __sanitizer_struct_mallinfo2 sret;
  XsanInitFromRtl();
  internal_memset(&sret, 0, sizeof(sret));
  XSAN_INIT_RANGE(nullptr, &sret, sizeof(sret));
  return sret;
}

INTERCEPTOR(int, mallopt, int cmd, int value) { return 0; }
#  endif  // SANITIZER_INTERCEPT_MALLOPT_AND_MALLINFO

INTERCEPTOR(int, posix_memalign, void **memptr, uptr alignment, uptr size) {
  if (__xsan::in_symbolizer()) {
    void *p = InternalAlloc(size, nullptr, alignment);
    if (!p)
      return errno_ENOMEM;
    *memptr = p;
    return 0;
  }
  SCOPED_XSAN_INTERCEPTOR_MALLOC(posix_memalign, memptr, alignment, size);
  int res = xsan_posix_memalign(memptr, alignment, size, &stack);
  if (!res)
    XSAN_INIT_RANGE(nullptr, memptr, sizeof(memptr));
  return res;
}

INTERCEPTOR(void *, valloc, uptr size) {
  if (__xsan::in_symbolizer())
    return InternalAlloc(size, nullptr, GetPageSizeCached());
  SCOPED_XSAN_INTERCEPTOR_MALLOC(valloc, size);
  return xsan_valloc(size, &stack);
}

#  if SANITIZER_INTERCEPT_PVALLOC
INTERCEPTOR(void *, pvalloc, uptr size) {
  if (__xsan::in_symbolizer()) {
    uptr PageSize = GetPageSizeCached();
    size = size ? RoundUpTo(size, PageSize) : PageSize;
    return InternalAlloc(size, nullptr, PageSize);
  }
  SCOPED_XSAN_INTERCEPTOR_MALLOC(pvalloc, size);
  return xsan_pvalloc(size, &stack);
}
#  endif  // SANITIZER_INTERCEPT_PVALLOC

INTERCEPTOR(void, malloc_stats, void) { __asan_print_accumulated_stats(); }

#  if SANITIZER_ANDROID
// Format of __libc_malloc_dispatch has changed in Android L.
// While we are moving towards a solution that does not depend on bionic
// internals, here is something to support both K* and L releases.
struct MallocDebugK {
  void *(*malloc)(uptr bytes);
  void (*free)(void *mem);
  void *(*calloc)(uptr n_elements, uptr elem_size);
  void *(*realloc)(void *oldMem, uptr bytes);
  void *(*memalign)(uptr alignment, uptr bytes);
  uptr (*malloc_usable_size)(void *mem);
};

struct MallocDebugL {
  void *(*calloc)(uptr n_elements, uptr elem_size);
  void (*free)(void *mem);
  fake_mallinfo (*mallinfo)(void);
  void *(*malloc)(uptr bytes);
  uptr (*malloc_usable_size)(void *mem);
  void *(*memalign)(uptr alignment, uptr bytes);
  int (*posix_memalign)(void **memptr, uptr alignment, uptr size);
  void *(*pvalloc)(uptr size);
  void *(*realloc)(void *oldMem, uptr bytes);
  void *(*valloc)(uptr size);
};

alignas(32)
const MallocDebugK xsan_malloc_dispatch_k = {
    WRAP(malloc),  WRAP(free),     WRAP(calloc),
    WRAP(realloc), WRAP(memalign), WRAP(malloc_usable_size)};

alignas(32)
const MallocDebugL xsan_malloc_dispatch_l = {WRAP(calloc),
                                             WRAP(free),
                                             WRAP(mallinfo),
                                             WRAP(malloc),
                                             WRAP(malloc_usable_size),
                                             WRAP(memalign),
                                             WRAP(posix_memalign),
                                             WRAP(pvalloc),
                                             WRAP(realloc),
                                             WRAP(valloc)};

namespace __xsan {
void ReplaceSystemMalloc() {
  void **__libc_malloc_dispatch_p =
      (void **)AsanDlSymNext("__libc_malloc_dispatch");
  if (__libc_malloc_dispatch_p) {
    // Decide on K vs L dispatch format by the presence of
    // __libc_malloc_default_dispatch export in libc.
    void *default_dispatch_p = AsanDlSymNext("__libc_malloc_default_dispatch");
    if (default_dispatch_p)
      *__libc_malloc_dispatch_p = (void *)&xsan_malloc_dispatch_k;
    else
      *__libc_malloc_dispatch_p = (void *)&xsan_malloc_dispatch_l;
  }
}
}  // namespace __xsan

#  else   // SANITIZER_ANDROID

namespace __xsan {
void ReplaceSystemMalloc() {}
}  // namespace __xsan
#  endif  // SANITIZER_ANDROID

#endif  // SANITIZER_FREEBSD || SANITIZER_FUCHSIA || SANITIZER_LINUX ||
        // SANITIZER_NETBSD || SANITIZER_SOLARIS
