#pragma once

#include "xsan_internal.h"

namespace __xsan {

/// The default alignment for heap allocations.
/// ASan's original alignment is 8 while TSan requires 16.
/// XSan is backward compatible to 16
const uptr kDefaultAlignment = 16;
}  // namespace __xsan