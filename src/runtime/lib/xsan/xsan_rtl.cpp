#include "xsan_activation.h"

// ---------------------- Interface ---------------- {{{1
using namespace __xsan;

// Initialize as requested from instrumented application code.
// We use this call as a trigger to wake up ASan from deactivated state.
void __xsan_init() {
  XsanActivate();
}