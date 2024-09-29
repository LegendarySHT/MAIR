//===-- xsan_activation.cpp -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan activation/deactivation logic.
//===----------------------------------------------------------------------===//

#include "xsan_activation.h"

#include <sanitizer_common/sanitizer_common.h>
#include <sanitizer_common/sanitizer_flags.h>

#include "asan/orig/asan_activation.h"
#include "xsan_internal.h"

namespace __xsan {

static bool xsan_is_deactivated;

void XsanDeactivate() {
  CHECK(!xsan_is_deactivated);

  VReport(1, "Deactivating XSan\n");

  xsan_is_deactivated = true;
}

void XsanActivate() {
  __asan::AsanActivate();

  Printf("XSAN Activating. Just for test, pending to be removed.\n");
  /// FIXME: xsan_is_deactivated is always false,
  //        so the following code is never executed in preinit.
  //       ASan faces the same problem.
  if (!xsan_is_deactivated)
    return;
  VReport(1, "Activating XSan\n");

  xsan_is_deactivated = false;
}

}  // namespace __xsan
