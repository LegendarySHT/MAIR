//===----------------------------------------------------------------------===//
//
// This file is a part of XSanitizer, a sanitizer compositor.
//
// XSan-private header which defines various general utilities.
//===----------------------------------------------------------------------===//
#pragma once

#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_internal_defs.h>
#include <sanitizer_common/sanitizer_libc.h>
#include <sanitizer_common/sanitizer_stacktrace.h>

#include "xsan_common_defs.h"
#include "xsan_flags.h"
#include "xsan_interface_internal.h"
#include "xsan_platform.h"

// #if defined(__SANITIZE_ADDRESS__)
// #  error \
//       "The AddressSanitizer run-time should not be instrumented by
//       AddressSanitizer"
// #endif

// Build-time configuration options.

// If set, asan will intercept C++ exception api call(s).
#ifndef XSAN_HAS_EXCEPTIONS
#  define XSAN_HAS_EXCEPTIONS 1
#endif

// If set, values like allocator chunk size, as well as defaults for some flags
// will be changed towards less memory overhead.
#ifndef XSAN_LOW_MEMORY
#  if SANITIZER_IOS || SANITIZER_ANDROID
#    define XSAN_LOW_MEMORY 1
#  else
#    define XSAN_LOW_MEMORY 0
#  endif
#endif

#ifndef XSAN_DYNAMIC
#  ifdef PIC
#    define XSAN_DYNAMIC 1
#  else
#    define XSAN_DYNAMIC 0
#  endif
#endif

namespace __asan {
void StopInitOrderChecking();
void AsanOnDeadlySignal(int, void *siginfo, void *context);
}  // namespace __asan

// All internal functions in xsan reside inside the __xsan namespace
// to avoid namespace collisions with the user programs.
// Separate namespace also makes it simpler to distinguish the xsan run-time
// functions from the instrumented user code in a profile.
namespace __xsan {

class XsanThread;
using __sanitizer::StackTrace;
class XsanAllocator;


void XsanInitFromRtl();
bool TryXsanInitFromRtl();

// xsan_win.cpp
void InitializePlatformExceptionHandlers();
// Returns whether an address is a valid allocated system heap block.
// 'addr' must point to the beginning of the block.
bool IsSystemHeapAddress(uptr addr);

// xsan_rtl.cpp
void PrintAddressSpaceLayout();
void NORETURN ShowStatsAndAbort();

// xsan_shadow_setup.cpp
void InitializeShadowMemory();

// xsan_malloc_linux.cpp / xsan_malloc_mac.cpp
void ReplaceSystemMalloc();

// xsan_linux.cpp / xsan_mac.cpp / xsan_win.cpp
uptr FindDynamicShadowStart();
void *XsanDoesNotSupportStaticLinkage();
void XsanCheckDynamicRTPrereqs();
void XsanCheckIncompatibleRT();

// Unpoisons platform-specific stacks.
// Returns true if all stacks have been unpoisoned.
bool PlatformUnpoisonStacks();

// xsan_rtl.cpp
// Unpoison a region containing a stack.
// Performs a sanity check and warns if the bounds don't look right.
// The warning contains the type string to identify the stack type.
void UnpoisonStack(uptr bottom, uptr top, const char *type);

// xsan_thread.cpp
XsanThread *CreateMainThread();
// Initialize the main thread.
void InitializeMainThread();

// void XsanOnDeadlySignal(int, void *siginfo, void *context);

void SignContextStack(void *context);
void ReadContextStack(void *context, uptr *stack, uptr *ssize);
// void StopInitOrderChecking();

// Wrapper for TLS/TSD.
void XsanTSDInit(void (*destructor)(void *tsd));
void *XsanTSDGet();
void XsanTSDSet(void *tsd);
void XsanTSDDtor(void *tsd);
void PlatformTSDDtor(void *tsd);

void AppendToErrorMessageBuffer(const char *buffer);

void *XsanDlSymNext(const char *sym);

// Returns `true` iff most of ASan init process should be skipped due to the
// ASan library being loaded via `dlopen()`. Platforms may perform any
// `dlopen()` specific initialization inside this function.
bool HandleDlopenInit();

void InstallAtForkHandler();

#define XSAN_ON_ERROR() \
  if (&__xsan_on_error) \
  __xsan_on_error()

bool XsanInited();
// Used to avoid infinite recursion in __xsan_init().
extern bool xsan_in_init;

extern bool replace_intrin_cached;
extern void (*death_callback)(void);

}  // namespace __xsan
