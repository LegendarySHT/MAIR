// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s

struct S { int a, b; };

// A read of s->a followed by a write of s->b covers s[0,4) and s[4,8): two
// adjacent accesses of equal width, so locationmerge folds them into one
// 8-byte observation with AccessKind::ReadWrite.
//
// Its alignment is the minimum over both sources, 4, which is below one shadow
// granule. doInstrumentAddress therefore declines the single-check path and
// probes the first and the last byte instead, so the merged range reports
// __asan_report_store_n(addr, 8) twice rather than __asan_report_store4 once.
// That is deliberate: claiming 8-byte alignment here would read one shadow byte
// for a range that straddles two, and miss a violation in the second half.
//
// Both probes are inserted ahead of the load, which is what keeps the check in
// front of every access it guards.
//
// CHECK-LABEL: define {{.*}} @_Z5copy2P1S
// CHECK-COUNT-2: call void @__asan_report_store_n({{.*}}, i64 8)
// CHECK-NOT: call void @__asan_report
// CHECK: load i32
// CHECK: store i32

void copy2(S *s) {
  int *dst = &s->b;
  int t = s->a;
  *dst = t;
}

// Single-source ReadWrite: an atomicrmw both reads and writes, so the
// observation is ReadWrite with one source. `IsWrite = Kind != Read` makes it
// report as a write, which matches what the legacy path does for the same
// instruction -- it classifies an atomicrmw as a write too. Report kind is the
// only difference between the load and store entry points; both run the same
// shadow check, so this is not a detection gap.
//
// CHECK-LABEL: define {{.*}} @_Z3addPi
// CHECK: call void @__asan_report_store4
// CHECK-NOT: call void @__asan_report
// CHECK: atomicrmw add

int add(int *p) { return __atomic_fetch_add(p, 1, __ATOMIC_SEQ_CST); }
