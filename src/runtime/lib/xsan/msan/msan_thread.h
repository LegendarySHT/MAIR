#ifndef MSAN_THREAD_H
#define MSAN_THREAD_H

#include "../xsan_hooks_types.h"
#include "sanitizer_common/sanitizer_internal_defs.h"

namespace __msan {

struct MsanThread {
  static THREADLOCAL MsanThread *msan_current_thread;

  __xsan::XsanThreadQueryKey xsan_key;
  unsigned in_signal_handler_ = 0;

  bool InSignalHandler() { return in_signal_handler_; }
  void EnterSignalHandler() { in_signal_handler_++; }
  void LeaveSignalHandler() { in_signal_handler_--; }
};

ALWAYS_INLINE MsanThread *GetCurrentThread() {
  return MsanThread::msan_current_thread;
}

}  // namespace __msan

#endif
