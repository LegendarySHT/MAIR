// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s

struct S { int a, b, c; };

int read3(S *s) { return s->a + s->b + s->c; }

// The three 4-byte loads sit at s+0, s+4, s+8: contiguous and equally wide, so
// locationmerge can fold them into one 12-byte observation.
//
// AddressSanitizer pins its observations, because a check must stay ahead of the
// access it guards and sinking would move it behind. SimplifyGraphPass therefore
// contracts the dominating/post-dominating pair by hoisting instead, which keeps
// every check ahead of every load and is what finally gives locationmerge two
// observations at one program point. The three loads become one 12-byte read.
//
// 12 is not a power of two, so instrumentUnusualSizeOrAlignment probes the first
// and the last byte and reports __asan_report_load_n(addr, 12) for both of them.
//
// CHECK-LABEL: define {{.*}} @_Z5read3P1S
// CHECK-NOT: load i32
// CHECK-COUNT-2: call void @__asan_report_load_n({{.*}}, i64 12)
// CHECK-NOT: call void @__asan_report_load4
// CHECK-COUNT-3: load i32
