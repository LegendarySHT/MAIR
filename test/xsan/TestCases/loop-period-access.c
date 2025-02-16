// RUN: %clangxx_xsan -O0 %s -o %t 
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR
// RUN: %clangxx_xsan -O1 %s -o %t 
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: %clangxx_xsan -O2 %s -o %t 
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: %clangxx_xsan -O3 %s -o %t 
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: %clangxx_xsan -O1 %s -o %t -DGRAINSIZE=2
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: %clangxx_xsan -O1 %s -o %t -DGRAINSIZE=4
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: %clangxx_xsan -O1 %s -o %t -DGRAINSIZE=8
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 

// RUN: %clangxx_xsan -O2 %s -o %t -DGRAINSIZE=8 -mllvm -xsan-loop-opt=no
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: %clangxx_xsan -O2 %s -o %t -DGRAINSIZE=8 -mllvm -xsan-loop-opt=range
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: %clangxx_xsan -O2 %s -o %t -DGRAINSIZE=8 -mllvm -xsan-loop-opt=period
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: %clangxx_xsan -O2 %s -o %t -DGRAINSIZE=8 -mllvm -xsan-loop-opt=full
// RUN: %run %t 2>&1 | FileCheck %s
// RUN: %run %t 1 2>&1 | FileCheck %s
// RUN: not %run %t 7 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 
// RUN: not %run %t 100 2>&1 | FileCheck %s --check-prefix=CHECK-ERROR 

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>



#define SIZE 6

volatile 
#if GRAINSIZE == 2
uint16_t
#elif GRAINSIZE == 4
uint32_t
#elif GRAINSIZE == 8
uint64_t
#else
uint8_t
#endif
global[SIZE];

int mystrlen(const char *s) {
  int len = 0;
  while (*s) {
    len++;
    s++;
  }
  return len;
}

void for_loop(int n) {
  for (int i = 0; i < n; i++) {
    global[i] = 0;
  }

  for (int i = 0; i < n; i++) {
    if (i >= SIZE) continue;
    global[i] = 0;
  }
}

void while_loop(int iter_count) {
  int n = iter_count;
  
  while (1) {
    if (--n >= SIZE) {
      continue;
    }
    if (n == -1) {
      break;
    }
    global[n] = 3;
  }
  
  n = iter_count;
  while (1) {
    global[n - 1] = 1;
    if (--n == 0) {
      break;
    }
  }

  n = iter_count;
  while (1) {
    if (--n == 0) {
      break;
    }
    global[n - 1] = 2;
  }

}

int main(int argc, char **argv) {
  int iter_count = argc == 1 ? SIZE : atoi(argv[1]);
  if (mystrlen(argv[0]) == 0) {
    exit(1);
  }
  while_loop(iter_count);
  for_loop(iter_count);
  printf("Done\n");
  return 0;
}

// CHECK-NOT: ERROR: AddressSanitizer:
// CHECK: Done
// CHECK-ERROR: AddressSanitizer: global-buffer-overflow on address