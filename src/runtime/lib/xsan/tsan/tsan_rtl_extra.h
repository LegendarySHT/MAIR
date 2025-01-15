//===-- tsan_rtl_extra.h ----------------------------------------*- C++ -*-===//
// Used for provide some defintions of symbols
//===----------------------------------------------------------------------===//

#pragma once

#include "sanitizer_common/sanitizer_asm.h"
#include "tsan_rtl.h"

namespace __tsan {

/// Comes from  interceptor_ctx()->finalize_key;
unsigned& finalize_key();

class ScopedIgnoreTsan {
 public:
  ScopedIgnoreTsan(bool enable);

  ~ScopedIgnoreTsan();

 private:
  bool nomalloc_;
  bool enable_;
};

#if SANITIZER_DEBUG
#  define TSAN_ADDR_GUARD_CONDITION (!IsAppMem(addr))
#else
#  define TSAN_ADDR_GUARD_CONDITION (0)
#endif

// CheckRaces consists of TWO parts:
//   - Check : should be disabled when all sub-threads are joined
//   - Store : should be disabled when in single thread

#define TSAN_CHECK_GUARD_CONDIITON (is_now_all_joined())
/// FIXME: Shall we really need to guard the metatdat store?
/// StoreShadow is just one atomic operation, while TraceAccess is
/// relatively expensive.
#define TSAN_STORE_GUARD_CONDIITON (is_now_single_threaded())

#define TSAN_CHECK_GUARD                                         \
  if (TSAN_ADDR_GUARD_CONDITION || TSAN_CHECK_GUARD_CONDIITON) { \
    return;                                                      \
  }
}  // namespace __tsan