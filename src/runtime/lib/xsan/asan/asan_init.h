#pragma once

namespace __asan {
// Initialization before xsan_is_running = false;
void AsanInitFromXsan();
// Initialization after xsan_is_running = false;
void AsanInitFromXsanLate();
} // namespace __asan