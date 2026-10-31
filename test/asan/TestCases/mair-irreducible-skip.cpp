// An irreducible cycle cannot be represented by LoopInfo, so MAIR has to skip
// the whole function. Skipping is not a correctness loss: the legacy per-access
// instrumentation takes over, so the check count has to match a build without
// MAIR -- one report per load, and no merged range check.
//
// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s
// RUN: %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s
//
// Both RUN lines share one prefix on purpose: "MAIR skipped the function" and
// "MAIR was never asked" have to produce the same instrumentation, otherwise
// the skip is silently dropping or duplicating a check.

int irreducible_skip(int *p, int sel) {
  int r = p[0];
  if (sel)
    goto cycle;
loop:
  r += p[1];
cycle:
  r += p[2];
  if (r < 0)
    goto loop;
  return r;
}

// CHECK-LABEL: define {{.*}} @{{.*}}irreducible_skip
// CHECK: call void @__asan_report_load4
// CHECK: load i32
// CHECK: call void @__asan_report_load4
// CHECK: load i32
// CHECK: call void @__asan_report_load4
// CHECK: load i32
// CHECK-NOT: call void @__asan_report_load
// CHECK: ret i32
