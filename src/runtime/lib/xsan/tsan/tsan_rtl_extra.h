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
#define ADDR_GUARD (!IsAppMem(addr))
#else
#define ADDR_GUARD (0)
#endif

#define TSAN_CHECK_GUARD \
  if (ADDR_GUARD) { \
    return;         \
  }
}  // namespace __tsan