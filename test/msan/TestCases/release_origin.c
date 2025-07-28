// Fixed RSS Overhead: MSan: 4MB, ASan: 1 MB, TSan: 13MB (10 MB for ctx_placeholder)
// Original RSS limit : 18 / 36 MB  --> New RSS limit : 32 / 50 MB
// However, to let -msan pass this test too, we choose to disable TSan,
// and update the RSS limit to 20 / 38 MB.
// RUN: %clang_msan -fno-sanitize=thread -fsanitize-memory-track-origins=0 -O0 %s -o %t && env ASAN_OPTIONS=quarantine_size_mb=0:thread_local_quarantine_size_kb=0 MSAN_OPTIONS=soft_rss_limit_mb=20:verbosity=1:allocator_may_return_null=1 %run %t 2>&1 | FileCheck %s -implicit-check-not="soft rss limit" -check-prefixes=CHECK,NOORIG
// RUN: %clang_msan -fno-sanitize=thread -fsanitize-memory-track-origins=2 -O0 %s -o %t && env ASAN_OPTIONS=quarantine_size_mb=0:thread_local_quarantine_size_kb=0 MSAN_OPTIONS=soft_rss_limit_mb=38:verbosity=1:allocator_may_return_null=1 %run %t 2>&1 | FileCheck %s -implicit-check-not="soft rss limit" -check-prefixes=CHECK,ORIGIN
#include <assert.h>
#include <sanitizer/allocator_interface.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

void *p;

int main(int argc, char **argv) {
  int s = 20 * 1024 * 1024;
  fprintf(stderr, "malloc\n");
  p = malloc(s);
  sleep(1);
  fprintf(stderr, "memset\n");
  memset(p, 1, s);
  sleep(1);
  fprintf(stderr, "free\n");
  free(p);
  sleep(1);
  return 0;
}

// CHECK-LABEL: malloc

// Non origin mode allocate ~20M for shadow.
// Origin mode allocate ~20M for shadow and ~20M for origin.
// CHECK: soft rss limit exhausted

// CHECK-LABEL: memset

// Memset reserve modified pages, frees ~20M for shadow. So not change in RSS for non-origin mode.
// Origin mode also frees ~20M of origins, so 'unexhausted'.
// ORIGIN: soft rss limit unexhausted

// CHECK-LABEL: free

// Now non-origin release all everything.
// NOORIG: soft rss limit unexhausted
