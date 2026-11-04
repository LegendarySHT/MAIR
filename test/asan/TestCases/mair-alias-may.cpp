// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=MAIR
// RUN: env -u MAIR %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=LEGACY

// Two arguments that AliasAnalysis can only call MayAlias must keep one check
// each. MAIR is allowed to fold checks whose addresses it proves identical, and
// folding these two would not save work -- it would drop the access through %b:
// a single check on %a covers four bytes, and the eight-byte range that a wrong
// merge would produce starts where %a starts, so the guard would sit on the
// wrong address as well as on the wrong size.
//
// The count is therefore the baseline count, not a reduction. The graph-level
// negative lives in test/MAIR/TestCases/alias-may.ll, because this file only
// sees what survives the frontend; here the point is that the instrumented
// output still covers both loads.

int sum2(int *a, int *b) { return *a + *b; }

// MAIR hoists both checks ahead of both loads, so the load lines come after the
// reports. The legacy pipeline interleaves them instead, which is why the
// baseline below only pins the report count.

// MAIR-LABEL: define {{.*}}@_Z4sum2PiS_(
// MAIR-COUNT-2: call void @__asan_report_load4
// MAIR-NOT: call void @__asan_report_load8
// MAIR: load i32, ptr %a, align 4
// MAIR: load i32, ptr %b, align 4

// LEGACY-LABEL: define {{.*}}@_Z4sum2PiS_(
// LEGACY-COUNT-2: call void @__asan_report_load4
// LEGACY-NOT: call void @__asan_report_load8
