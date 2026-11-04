// MAIR is an optimization on top of the legacy instrumentation, never a
// correctness requirement. When the graph cannot be materialized the function
// must still be instrumented, one access at a time, so the check count has to
// come back to what a build without MAIR produces.
//
// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=MAIR
// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -mllvm -mair-force-fallback -S -emit-llvm %s -o - | FileCheck %s --check-prefix=LEGACY
// RUN: env -u MAIR %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=LEGACY
//
// The last two RUN lines share one prefix on purpose: "MAIR gave up" and "MAIR
// was never asked" have to be the same instrumentation, otherwise the fallback
// is silently dropping or doubling a check.
//
// -mair-force-fallback is the only way to reach the recovery path today: after
// C1 moved every rejection into preflight, the edge sites liom and lpoc create
// are provably critical and the plan provably splits each edge once, so no
// input in the suite trips it. Forcing the failure keeps the recovery path from
// rotting until an input shows up that really cannot be lowered.

struct S { int a, b, c; };

int read3(S *s) { return s->a + s->b + s->c; }

// With MAIR the three contiguous 4-byte loads at s+0, s+4, s+8 become one
// 12-byte observation. 12 is not a power of two, so the check probes the first
// and the last byte through __asan_report_load_n(addr, 12).
//
// MAIR-LABEL: define {{.*}} @_Z5read3P1S
// MAIR-NOT: load i32
// MAIR-COUNT-2: call void @__asan_report_load_n({{.*}}, i64 12)
// MAIR-NOT: call void @__asan_report_load4
// MAIR-COUNT-3: load i32
// MAIR-NOT: call void @__asan_report_load

// Falling back loses the merge and nothing else: one 4-byte check per load, in
// the same order and still ahead of the load it guards.
//
// LEGACY-LABEL: define {{.*}} @_Z5read3P1S
// LEGACY: call void @__asan_report_load4
// LEGACY: load i32
// LEGACY: call void @__asan_report_load4
// LEGACY: load i32
// LEGACY: call void @__asan_report_load4
// LEGACY: load i32
// LEGACY-NOT: call void @__asan_report_load

// The last line unsets MAIR rather than merely not setting it: a suite-wide
// MAIR=1 would otherwise run the baseline through MAIR as well.
