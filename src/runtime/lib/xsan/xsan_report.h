#include "sanitizer_common/sanitizer_symbolizer.h"

namespace __xsan {
void CorrectGlobalVariableDesc([[maybe_unused]] const uptr addr,
                               [[maybe_unused]] DataInfo &desc);
}