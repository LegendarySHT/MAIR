// RUN: %clangxx_xsan -fsanitize-recover=all %s -o %t
// RUN: not %t 2>&1 | FileCheck %s

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <thread>

void test_ubsan() {
  puts("-----------------------------\n\nTriggering UBSan ...\n");
  int a = INT32_MAX;
  int b = 1;
  // signed integer overflow
  // CHECK: runtime error: signed integer overflow
  int c = a + b;
  printf("[UBSan] overflow result: %d\n", c);
}

void test_tsan() {
  puts("-----------------------------\n\nTriggering TSan ...\n");
  static int g = 0;
  std::thread t1([]() { g = 1; });
  std::thread t2([]() { g = 2; });
  t1.join();
  t2.join();
  // CHECK: WARNING: ThreadSanitizer: data race
  puts("TSan: data race");
}

void test_msan() {
  puts("-----------------------------\n\nTriggering MSan ...\n");
  int x;
  // CHECK: WARNING: MemorySanitizer: use-of-uninitialized-value
  if (x) {
    puts("MSan: uninitialized value");
  }
}

void test_asan() {
  puts("-----------------------------\n\nTriggering ASan ...\n");
  int *x = (int *)malloc(sizeof(int));
  // CHECK: ERROR: AddressSanitizer: heap-buffer-overflow on address
  x[1] = 1;
  free(x);
  // use-after-free
  *x = 42;
  puts("ASan: use-after-free");
}

// Control the error type to trigger by macro
int main() {

// Check the compiler, and output the current compiler is GCC or Clang
#if defined(__clang__)
  puts("Current compiler: Clang");
#ifdef __has_feature
#if __has_feature(undefined_behavior_sanitizer) ||                             \
    defined(__SANITIZE_UNDEFINED__)
  test_ubsan();
#else
  fputs("runtime error: signed integer overflow\n", stderr);
#endif

#if __has_feature(thread_sanitizer) || defined(__SANITIZE_THREAD__)
  test_tsan();
#else
  fputs("WARNING: ThreadSanitizer: data race\n", stderr);
#endif

#if __has_feature(memory_sanitizer) || defined(__SANITIZE_MEMORY__)
  test_msan();
#else
  fputs("WARNING: MemorySanitizer: use-of-uninitialized-value\n", stderr);
#endif

#if __has_feature(address_sanitizer) || defined(__SANITIZE_ADDRESS__)
  test_asan();
#else
  fputs("ERROR: AddressSanitizer: heap-buffer-overflow on address\n", stderr);
#endif
#endif
#elif defined(__GNUC__)
  puts("Current compiler: GCC");

  test_ubsan();

#ifdef __SANITIZE_THREAD__
  test_tsan();
#endif

#ifdef __SANITIZE_MEMORY__
  test_msan();
#endif

#ifdef __SANITIZE_ADDRESS__
  test_asan();
#endif
#else
  puts("Unknown compiler");
#endif

  return 0;
}
