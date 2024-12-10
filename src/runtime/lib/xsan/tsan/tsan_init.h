#pragma once

namespace __tsan {

// Initialization before flag initialization
void TsanInitFromXsanEarly();
// Initialization before xsan_is_running = false;
void TsanInitFromXsan();
// Initialization after TSan has been fully initialized.
void TsanInitFromXsanLate();

}  // namespace __tsan