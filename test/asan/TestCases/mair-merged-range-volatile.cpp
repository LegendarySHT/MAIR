// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s

struct S { volatile int a, b, c; };

// A volatile access must keep its own check at its own position: merging would
// move a check across a volatile operation, which is not allowed to be reordered
// or coalesced. Observation::isVolatile() makes the observation unmergeable and
// CanInstrumentViaMAIR rejects every volatile source, so all three loads go back
// to per-source instrumentation and keep their own __asan_report_load4.
//
// CHECK-LABEL: define {{.*}} @_Z5read3P1S
// CHECK-NOT: call void @__asan_report_load_n
// CHECK: call void @__asan_report_load4
// CHECK: load volatile i32
// CHECK: call void @__asan_report_load4
// CHECK: load volatile i32
// CHECK: call void @__asan_report_load4
// CHECK: load volatile i32
// CHECK-NOT: call void @__asan_report

int read3(S *s) { return s->a + s->b + s->c; }
