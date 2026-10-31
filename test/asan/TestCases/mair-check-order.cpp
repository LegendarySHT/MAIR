// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s

int f(int *p, int *q) { return *p + *q; }

// The property that matters is that a check never runs after the access it
// guards. AddressSanitizer pins its observations for exactly that reason, so the
// contraction of the two loads into the entry program point hoists the later one
// rather than sinking the earlier one. Both checks therefore come out ahead of
// both loads, which keeps the property: neither load is ever executed without
// its check having run first.
//
// CHECK-LABEL: define {{.*}} @_Z1f
// CHECK-NOT: load i32
// CHECK-COUNT-2: call void @__asan_report_load4
// CHECK-COUNT-2: load i32
