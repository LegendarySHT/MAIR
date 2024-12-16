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
}  // namespace __tsan