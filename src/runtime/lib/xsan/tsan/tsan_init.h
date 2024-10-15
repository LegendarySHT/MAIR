#pragma once

namespace __tsan {

// Initialization before flag initialization
void TsanInitFromXsanEarly();
// Initialization before xsan_is_running = false;
void TsanInitFromXsan();
}  // namespace __tsan