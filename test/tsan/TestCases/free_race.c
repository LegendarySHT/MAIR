// RUN: %clang_tsan -O1 %s -o %t
// RUN: %deflake %run %t | FileCheck %s --check-prefix=CHECK-NOZUPP
/// In Xsan, Tsan's suppressions don't work for ASan's check! And thus the test must exit with abnormal status.
// RUN: %env_tsan_opts=suppressions='%s.supp':print_suppressions=1 %run %t 2>&1 | FileCheck %s --check-prefix=CHECK-SUPP || printenv XSAN_IN_TSAN_TEST >/dev/null

#include "test.h"

int *mem;
pthread_mutex_t mtx;

void *Thread1(void *x) {
  pthread_mutex_lock(&mtx);
  free(mem);
  pthread_mutex_unlock(&mtx);
  barrier_wait(&barrier);
  return NULL;
}

__attribute__((noinline)) void *Thread2(void *x) {
  barrier_wait(&barrier);
  pthread_mutex_lock(&mtx);
  mem[0] = 42;
  pthread_mutex_unlock(&mtx);
  return NULL;
}

int main() {
  barrier_init(&barrier, 2);
  mem = (int*)malloc(100);
  pthread_mutex_init(&mtx, 0);
  pthread_t t;
  pthread_create(&t, NULL, Thread1, NULL);
  Thread2(0);
  pthread_join(t, NULL);
  pthread_mutex_destroy(&mtx);
  return 0;
}

// CHECK-NOZUPP: {{WARNING: ThreadSanitizer: heap-use-after-free|ERROR: AddressSanitizer: heap-use-after-free}}
// CHECK-NOZUPP:   {{Write of size 4 at .* by main thread.*:|WRITE of size 4 at .* thread T0}}
// CHECK-NOZUPP:     #0 Thread2
// CHECK-NOZUPP:     #1 main
// CHECK-NOZUPP:   {{Previous write of size 8 at .* by thread T1.*:|freed by thread T1 here:}}
// CHECK-NOZUPP:     #0 free
// CHECK-NOZUPP:     #{{(1|2)}} Thread1
// CHECK-NOZUPP: SUMMARY: {{ThreadSanitizer|XSan}}: heap-use-after-free{{.*}}Thread2

/// TSan's suppressions don't work for ASan's check!
// CHECK-SUPP:   {{ThreadSanitizer: Matched 1 suppressions|ERROR: AddressSanitizer: heap-use-after-free}}
// CHECK-SUPP:    {{1 race:\^Thread2\$|WRITE of size 4 at .* thread T0}}
