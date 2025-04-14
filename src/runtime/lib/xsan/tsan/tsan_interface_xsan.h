#pragma once

#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __tsan {

void EnterSymbolizer();
void ExitSymbolizer();

struct alignas(SANITIZER_CACHE_LINE_SIZE) ThreadState;

ThreadState *cur_thread();
ThreadState *cur_thread_init();

// This creates 2 non-inlined specialized versions of MemoryAccessRange.
template <bool is_read>
void MemoryAccessRangeT(ThreadState *thr, uptr pc, uptr addr, uptr size);

}  // namespace __tsan
