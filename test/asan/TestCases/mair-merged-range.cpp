// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s
//
// XFAIL: *

struct S { int a, b, c; };

int read3(S *s) { return s->a + s->b + s->c; }

// The three 4-byte field loads sit at s+0, s+4, s+8: same width, contiguous
// addresses. `locationmerge` therefore folds them into a single 12-byte
// observation (verified with `mair-opt -passes=simplifygraph,locationmerge`:
// "o: read ptr %0, 12").
//
// Today the merged observation is rejected by the backend gate
// (Obs.getInstructions().size() != 1), so AsanAddressSanitizer falls back to
// per-source instrumentation and emits three separate __asan_report_load4
// calls. Once the gate accepts multi-source observations the merged range must
// be materialized as ONE check covering 12 bytes. 12 is not a power of two, so
// the report goes through instrumentUnusualSizeOrAlignment and becomes
// __asan_report_load_n(addr, 12) rather than __asan_report_load16.
//
// CHECK-LABEL: define {{.*}} @_Z5read3P1S
// CHECK: call void @__asan_report_load_n({{.*}}, i64 12)
// CHECK-NOT: call void @__asan_report_
// CHECK: ret i32
