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

DECLARE_REAL(void *, memcpy, void *to, const void *from, SIZE_T size)
DECLARE_REAL(void *, memset, void *block, int c, SIZE_T size)

#endif  // ASAN_MEMINTRIN_H
