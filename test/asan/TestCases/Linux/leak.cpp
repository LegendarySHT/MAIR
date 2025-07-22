// Minimal test for LeakSanitizer+{{AddressSanitizer|XSan}}.
// REQUIRES: leak-detection
// - `-fno-sanitize=undefined`: UBSan's pointer sanitizing makes the returned heap
//                              address remains on a register when LSan starts. 
// RUN: %clangxx_asan -fno-sanitize=undefined  %s -o %t
// RUN: %env_asan_opts=detect_leaks=1 not %run %t  2>&1 | FileCheck %s
// RUN: not %run %t  2>&1 | FileCheck %s
// RUN: %env_asan_opts=detect_leaks=0     %run %t
//
#include <stdio.h>
int *t;

int main(int argc, char **argv) {
  t = new int[argc - 1] - 100000;
  printf("t: %p\n", t);
}
// CHECK: LeakSanitizer: detected memory leaks
