//===-- asan_posix.cpp ----------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// Posix-specific details.
//===----------------------------------------------------------------------===//

#include "sanitizer_common/sanitizer_platform.h"
#include "sanitizer_common/sanitizer_stackdepot.h"
#include "xsan_thread.h"
#if SANITIZER_POSIX

#  include <pthread.h>
#  include <signal.h>
#  include <stdlib.h>
#  include <sys/resource.h>
#  include <sys/time.h>
#  include <unistd.h>

#  include "sanitizer_common/sanitizer_libc.h"
#  include "sanitizer_common/sanitizer_posix.h"
#  include "sanitizer_common/sanitizer_procmaps.h"

#include "xsan_allocator.h"
#include "xsan_hooks.h"

namespace __xsan {

/// FIXME: how about COMMON_SYSCALL_PRE_FORK / COMMON_SYSCALL_POST_FORK ?

static void BeforeFork() SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  __xsan::OnForkBefore();
  // Allocator is shared resource, and lock it in XSan.
  xsanThreadArgRetval().Lock();
  allocator()->ForceLock();
  // StackDepot is shared resource, and lock it in XSan.
  StackDepotLockBeforeFork();
}

static void AfterFork(bool fork_child) SANITIZER_NO_THREAD_SAFETY_ANALYSIS {
  StackDepotUnlockAfterFork(fork_child);
  allocator()->ForceUnlock();
  xsanThreadArgRetval().Unlock();
  __xsan::OnForkAfter(fork_child);
}

void InstallAtForkHandler() {
#  if SANITIZER_SOLARIS || SANITIZER_NETBSD || SANITIZER_APPLE || \
      (SANITIZER_LINUX && SANITIZER_SPARC)
  // While other Linux targets use clone in internal_fork which doesn't
  // trigger pthread_atfork handlers, Linux/sparc64 uses __fork, causing a
  // hang.
  return;  // FIXME: Implement FutexWait.
#  endif
  int res = pthread_atfork(
      &BeforeFork, []() { AfterFork(/* fork_child= */ false); },
      []() { AfterFork(/* fork_child= */ true); });
  if (res) {
    Printf("XSan: failed to setup atfork callbacks\n");
    Die();
  }
}



}  // namespace __xsan

#endif  // SANITIZER_POSIX
