/**
 This file provide the empty definitions temporarily.

 Some original sanitizer source code references these functions,
    but they are not used by XSan.
*/

#include "xsan_internal.h"

/// TODO: Delegate these function implemantations to XSan or remove 
/// all the references to these symbols.
namespace __asan {

void InitializeAsanInterceptors() {}
void ReplaceSystemMalloc() {}

}  // namespace __asan

namespace __sanitizer {
    class LibIgnore; 
}

namespace __tsan{


class ThreadState;

/* In tsan_interceptors_posix.cpp */
void PlatformCleanUpThreadState(ThreadState *thr) {}
__sanitizer::LibIgnore *libignore() { return nullptr; }
void ProcessPendingSignalsImpl(ThreadState *thr) {}
void InitializeInterceptors() {}
void InitializeLibIgnore() {}
}

/* In tsan_interceptors_posix.cpp */
extern "C" void __tsan_setjmp(uptr) { }
