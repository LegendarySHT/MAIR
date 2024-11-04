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

namespace __tsan {
/* In tsan_interceptors_posix.cpp */
// void InitializeInterceptors() {}
}  // namespace __tsan
