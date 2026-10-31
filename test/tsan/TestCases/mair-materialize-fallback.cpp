// MAIR is an optimization on top of the legacy instrumentation, never a
// correctness requirement. When the graph cannot be materialized the function
// must still be instrumented, one access at a time, so the check count has to
// come back to what a build without MAIR produces.
//
// RUN: env MAIR=1 %clangxx_tsan -O1 -S -emit-llvm %s -o - | FileCheck %s --check-prefix=MAIR
// RUN: env MAIR=1 %clangxx_tsan -O1 -mllvm -mair-force-tsan-fallback -S -emit-llvm %s -o - | FileCheck %s --check-prefix=LEGACY
// RUN: env -u MAIR %clangxx_tsan -O1 -S -emit-llvm %s -o - | FileCheck %s --check-prefix=LEGACY
//
// The last two RUN lines share one prefix on purpose: "MAIR gave up" and "MAIR
// was never asked" have to be the same instrumentation, otherwise the fallback
// is silently dropping or doubling a check. The third line unsets MAIR instead
// of leaving it empty because isMAIROptEnabled() tests for the mere presence of
// the variable.
//
// -mair-force-tsan-fallback is the only way to reach the recovery path today:
// after C1 moved every rejection into preflight, the edge sites liom and lpoc
// create are provably critical and the plan provably splits each edge once, so
// no input in the suite trips it. Forcing the failure keeps the recovery path
// from rotting until an input shows up that really cannot be lowered. The name
// differs from ASan's -mair-force-fallback because XSanInstPass links both
// sanitizers into one module and cl::opt refuses a duplicate registration.

struct S { int a, b, c; };

int read3(S *s) { return s->a + s->b + s->c; }

// With MAIR the three contiguous 4-byte loads at s+0, s+4 and s+8 become one
// 12-byte observation. 12 is not a power of two, so the region is described by
// __tsan_read_range, which takes the exact width.
//
// MAIR-LABEL: define {{.*}} @_Z5read3P1S
// MAIR-NOT: call void @__tsan_read4
// MAIR: call void @__tsan_read_range({{.*}}, i64 12)
// MAIR-NOT: call void @__tsan_read

// Falling back loses the merge and nothing else: one 4-byte check per load, in
// the same order and still ahead of the load it guards.
//
// LEGACY-LABEL: define {{.*}} @_Z5read3P1S
// LEGACY: call void @__tsan_read4
// LEGACY: load i32
// LEGACY: call void @__tsan_read4
// LEGACY: load i32
// LEGACY: call void @__tsan_read4
// LEGACY: load i32
// LEGACY-NOT: call void @__tsan_read_range

void store2(int *p) { p[0] = 1; p[1] = 2; }

// Two adjacent 4-byte stores merge into one 8-byte region. 8 is a standard
// width, but the stores only guarantee 4-byte alignment, so the merged check
// is __tsan_unaligned_write8.
//
// MAIR-LABEL: define {{.*}} @_Z6store2Pi
// MAIR-NOT: call void @__tsan_write4
// MAIR: call void @__tsan_unaligned_write8
// MAIR-NOT: call void @__tsan_write

// LEGACY-LABEL: define {{.*}} @_Z6store2Pi
// LEGACY: call void @__tsan_write4
// LEGACY: store i32
// LEGACY: call void @__tsan_write4
// LEGACY: store i32
// LEGACY-NOT: call void @__tsan_write_range
