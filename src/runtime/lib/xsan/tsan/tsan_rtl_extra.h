/*
 *  This file provide the implementations of some symbols defined in
 * tsan_interceptors_posix.cpp.
 *    - libignore
 *    - ProcessPendingSignalsImpl
 *    - InitializeInterceptors : this is provided by xsan_disability_dummy.cpp
 *    - InitializeLibIgnore
 *    - __tsan_setjmp
 */

#pragma once

#include "interception/interception.h"
#include "sanitizer_common/sanitizer_errno.h"
#include "tsan_rtl.h"
#include "tsan_suppressions.h"


#ifdef __mips__
const int kSigCount = 129;
#else
const int kSigCount = 65;
#endif

namespace __tsan {


// The sole reason tsan wraps atexit callbacks is to establish synchronization
// between callback setup and callback execution.
struct AtExitCtx {
  void (*f)();
  void *arg;
  uptr pc;
};

// InterceptorContext holds all global data required for interceptors.
// It's explicitly constructed in InitializeInterceptors with placement new
// and is never destroyed. This allows usage of members with non-trivial
// constructors and destructors.
struct InterceptorContext {
  // The object is 64-byte aligned, because we want hot data to be located
  // in a single cache line if possible (it's accessed in every interceptor).
  ALIGNED(64) LibIgnore libignore;
  __sanitizer_sigaction sigactions[kSigCount];
#if !SANITIZER_APPLE && !SANITIZER_NETBSD
  unsigned finalize_key;
#endif

  Mutex atexit_mu;
  Vector<struct AtExitCtx *> AtExitStack;

  InterceptorContext()
      : libignore(LINKER_INITIALIZED),
        atexit_mu(MutexTypeAtExit),
        AtExitStack() {}
};


InterceptorContext *interceptor_ctx();

}  // namespace __tsan