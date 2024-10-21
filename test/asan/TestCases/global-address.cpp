// RUN: %clangxx_asan -o %t %s
// RUN: not %run %t 2>&1 | FileCheck %s
#include <sanitizer/allocator_interface.h>

int g_i = 42;
int main() {
  // CHECK: {{AddressSanitizer|XSan}}: attempting to call __sanitizer_get_allocated_size() for pointer which is not owned
  // CHECK-NOT: {{AddressSanitizer|XSan}}:DEADLYSIGNAL
  // CHECK: SUMMARY: {{AddressSanitizer|XSan}}: bad-__sanitizer_get_allocated_size
  // CHECK-NOT: {{AddressSanitizer|XSan}}:DEADLYSIGNAL
  return (int)__sanitizer_get_allocated_size(&g_i);
}
