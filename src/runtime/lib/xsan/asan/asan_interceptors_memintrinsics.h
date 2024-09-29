//===-- asan_interceptors_memintrinsics.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===---------------------------------------------------------------------===//
//
// This file is a part of AddressSanitizer, an address sanity checker.
//
// ASan-private header for asan_interceptors_memintrinsics.cpp
//===---------------------------------------------------------------------===//
#ifndef ASAN_MEMINTRIN_H
#define ASAN_MEMINTRIN_H

#include "asan_interface_internal.h"
#include "asan_internal.h"
#include "asan_mapping.h"
#include "interception/interception.h"

DECLARE_REAL(void*, memcpy, void *to, const void *from, uptr size)
DECLARE_REAL(void*, memset, void *block, int c, uptr size)

namespace __asan {

// Return true if we can quickly decide that the region is unpoisoned.
// We assume that a redzone is at least 16 bytes.
static inline bool AsanQuickCheckForUnpoisonedRegion(uptr beg, uptr size) {
  if (size == 0) return true;
  if (size <= 32)
    return !AddressIsPoisoned(beg) &&
           !AddressIsPoisoned(beg + size - 1) &&
           !AddressIsPoisoned(beg + size / 2);
  if (size <= 64)
    return !AddressIsPoisoned(beg) &&
           !AddressIsPoisoned(beg + size / 4) &&
           !AddressIsPoisoned(beg + size - 1) &&
           !AddressIsPoisoned(beg + 3 * size / 4) &&
           !AddressIsPoisoned(beg + size / 2);
  return false;
}

// Behavior of functions like "memcpy" or "strcpy" is undefined
// if memory intervals overlap. We report error in this case.
// Macro is used to avoid creation of new frames.
static inline bool AsanRangesOverlap(const char *offset1, uptr length1,
                                     const char *offset2, uptr length2) {
  return !((offset1 + length1 <= offset2) || (offset2 + length2 <= offset1));
}


}  // namespace __asan

#endif  // ASAN_MEMINTRIN_H
