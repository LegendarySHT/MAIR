// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s

struct S {
  int a __attribute__((aligned(8)));
  int b;
};

// Two adjacent 4-byte loads merge into one 8-byte read, but their alignments
// differ: s->a is 8-aligned, s->b is only 4-aligned. The merged range inherits
// the weaker one, so doInstrumentAddress must not take the single-check path --
// an 8-byte access at 4-byte alignment straddles two shadow granules, and one
// shadow byte would miss a violation in the second half.
//
// This is the test that distinguishes taking the minimum from trusting the first
// source: with Alignment = 8 the range would be checked once and report
// __asan_report_load8; taking the minimum keeps it on the two-probe path and
// reports __asan_report_load_n(addr, 8) twice.
//
// CHECK-LABEL: define {{.*}} @_Z5read2P1S
// CHECK-NOT: call void @__asan_report_load8
// CHECK-COUNT-2: call void @__asan_report_load_n({{.*}}, i64 8)
// CHECK-NOT: call void @__asan_report
// CHECK-COUNT-2: load i32

int read2(S *s) { return s->a + s->b; }
