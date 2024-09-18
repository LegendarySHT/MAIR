#pragma once

#include "xsan_flags.h"
#include "xsan_interceptors.h"
#include "xsan_internal.h"
#include "sanitizer_common/sanitizer_allocator.h"
#include "sanitizer_common/sanitizer_list.h"
#include "sanitizer_common/sanitizer_platform.h"

namespace __xsan {

enum AllocType {
  FROM_MALLOC = 1,  // Memory block came from malloc, calloc, realloc, etc.
  FROM_NEW = 2,     // Memory block came from operator new.
  FROM_NEW_BR = 3   // Memory block came from operator new [ ]
};

class XsanChunk;

struct AllocatorOptions {
  u32 quarantine_size_mb;
  u32 thread_local_quarantine_size_kb;
  u16 min_redzone;
  u16 max_redzone;
  u8 may_return_null;
  u8 alloc_dealloc_mismatch;
  s32 release_to_os_interval_ms;

  void SetFrom(const Flags *f, const CommonFlags *cf);
  void CopyTo(Flags *f, CommonFlags *cf);
};

void InitializeAllocator(const AllocatorOptions &options);
void ReInitializeAllocator(const AllocatorOptions &options);
void GetAllocatorOptions(AllocatorOptions *options);

class XsanChunkView {
 public:
  explicit XsanChunkView(XsanChunk *chunk) : chunk_(chunk) {}
  bool IsValid() const;        // Checks if XsanChunkView points to a valid
                               // allocated or quarantined chunk.
  bool IsAllocated() const;    // Checks if the memory is currently allocated.
  bool IsQuarantined() const;  // Checks if the memory is currently quarantined.
  uptr Beg() const;            // First byte of user memory.
  uptr End() const;            // Last byte of user memory.
  uptr UsedSize() const;       // Size requested by the user.
  u32 UserRequestedAlignment() const;  // Originally requested alignment.
  uptr AllocTid() const;
  uptr FreeTid() const;
  bool Eq(const XsanChunkView &c) const { return chunk_ == c.chunk_; }
  u32 GetAllocStackId() const;
  u32 GetFreeStackId() const;
  AllocType GetAllocType() const;
  bool AddrIsInside(uptr addr, uptr access_size, sptr *offset) const {
    if (addr >= Beg() && (addr + access_size) <= End()) {
      *offset = addr - Beg();
      return true;
    }
    return false;
  }
  bool AddrIsAtLeft(uptr addr, uptr access_size, sptr *offset) const {
    (void)access_size;
    if (addr < Beg()) {
      *offset = Beg() - addr;
      return true;
    }
    return false;
  }
  bool AddrIsAtRight(uptr addr, uptr access_size, sptr *offset) const {
    if (addr + access_size > End()) {
      *offset = addr - End();
      return true;
    }
    return false;
  }

 private:
  XsanChunk *const chunk_;
};

XsanChunkView FindHeapChunkByAddress(uptr address);
XsanChunkView FindHeapChunkByAllocBeg(uptr address);

// List of XsanChunks with total size.
class XsanChunkFifoList: public IntrusiveList<XsanChunk> {
 public:
  explicit XsanChunkFifoList(LinkerInitialized) { }
  XsanChunkFifoList() { clear(); }
  void Push(XsanChunk *n);
  void PushList(XsanChunkFifoList *q);
  XsanChunk *Pop();
  uptr size() { return size_; }
  void clear() {
    IntrusiveList<XsanChunk>::clear();
    size_ = 0;
  }
 private:
  uptr size_;
};

struct XsanMapUnmapCallback {
  void OnMap(uptr p, uptr size) const;
  void OnUnmap(uptr p, uptr size) const;
};

#if SANITIZER_CAN_USE_ALLOCATOR64
# if SANITIZER_FUCHSIA
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x40000000000ULL;  // 4T.
typedef DefaultSizeClassMap SizeClassMap;
# elif defined(__powerpc64__)
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x20000000000ULL;  // 2T.
typedef DefaultSizeClassMap SizeClassMap;
# elif defined(__aarch64__) && SANITIZER_ANDROID
// Android needs to support 39, 42 and 48 bit VMA.
const uptr kAllocatorSpace =  ~(uptr)0;
const uptr kAllocatorSize  =  0x2000000000ULL;  // 128G.
typedef VeryCompactSizeClassMap SizeClassMap;
#elif SANITIZER_RISCV64
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize = 0x2000000000ULL;  // 128G.
typedef VeryDenseSizeClassMap SizeClassMap;
# elif defined(__aarch64__)
// AArch64/SANITIZER_CAN_USE_ALLOCATOR64 is only for 42-bit VMA
// so no need to different values for different VMA.
const uptr kAllocatorSpace =  0x10000000000ULL;
const uptr kAllocatorSize  =  0x10000000000ULL;  // 3T.
typedef DefaultSizeClassMap SizeClassMap;
#elif defined(__sparc__)
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize = 0x20000000000ULL;  // 2T.
typedef DefaultSizeClassMap SizeClassMap;
# elif SANITIZER_WINDOWS
const uptr kAllocatorSpace = ~(uptr)0;
const uptr kAllocatorSize  =  0x8000000000ULL;  // 500G
typedef DefaultSizeClassMap SizeClassMap;
# else
const uptr kAllocatorSpace = 0x610000000000ULL;
const uptr kAllocatorSize  =  0x20000000000ULL;  // 2T.
typedef DefaultSizeClassMap SizeClassMap;
# endif
struct AP64 {  // Allocator64 parameters. Deliberately using a short name.
  static const uptr kSpaceBeg = kAllocatorSpace;
  static const uptr kSpaceSize = kAllocatorSize;
  static const uptr kMetadataSize = 0;
  typedef __xsan::SizeClassMap SizeClassMap;
  typedef XsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
  using AddressSpaceView = LocalAddressSpaceView;
};


using PrimaryAllocator = SizeClassAllocator64<AP64>;
#else  // Fallback to SizeClassAllocator32.
typedef CompactSizeClassMap SizeClassMap;
template <typename AddressSpaceViewTy>
struct AP32 {
  static const uptr kSpaceBeg = 0;
  static const u64 kSpaceSize = SANITIZER_MMAP_RANGE_SIZE;
  static const uptr kMetadataSize = 0;
  typedef __xsan::SizeClassMap SizeClassMap;
  static const uptr kRegionSizeLog = 20;
  using AddressSpaceView = AddressSpaceViewTy;
  typedef XsanMapUnmapCallback MapUnmapCallback;
  static const uptr kFlags = 0;
};
template <typename AddressSpaceView>
using PrimaryAllocatorASVT = SizeClassAllocator32<AP32<AddressSpaceView> >;
using PrimaryAllocator = PrimaryAllocatorASVT<LocalAddressSpaceView>;
#endif  // SANITIZER_CAN_USE_ALLOCATOR64

static const uptr kNumberOfSizeClasses = SizeClassMap::kNumClasses;


using XsanAllocator = CombinedAllocator<PrimaryAllocator>;
using AllocatorCache = XsanAllocator::AllocatorCache;

struct XsanThreadLocalMallocStorage {
  uptr quarantine_cache[16];
  AllocatorCache allocator_cache;
  void CommitBack();
 private:
  // These objects are allocated via mmap() and are zero-initialized.
  XsanThreadLocalMallocStorage() {}
};

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

void PrintInternalAllocatorStats();
void XsanSoftRssLimitExceededCallback(bool exceeded);

}  // namespace __xsan
