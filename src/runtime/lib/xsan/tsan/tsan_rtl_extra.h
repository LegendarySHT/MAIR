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
  ScopedIgnoreTsan(bool enable) : enable_(enable) {
#if !SANITIZER_GO
    if (enable_) {
      ThreadState* thr = cur_thread();
      nomalloc_ = thr->nomalloc;
      thr->nomalloc = false;
      thr->ignore_sync++;
      thr->ignore_reads_and_writes++;
      atomic_store_relaxed(&thr->in_signal_handler, 0);
    }
#endif
  }

  ~ScopedIgnoreTsan() {
#if !SANITIZER_GO
    if (enable_) {
      ThreadState* thr = cur_thread();
      thr->nomalloc = nomalloc_;
      thr->ignore_sync--;
      thr->ignore_reads_and_writes--;
      atomic_store_relaxed(&thr->in_signal_handler, 0);
    }
#endif
  }

 private:
  bool nomalloc_;
  bool enable_;
};
}  // namespace __tsan