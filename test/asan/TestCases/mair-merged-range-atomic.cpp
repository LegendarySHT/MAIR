// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s

#include <atomic>

struct A { std::atomic<int> a, b, c; };

// AddressSanitizer's MAIR configuration sets IgnoreAtomic, so an atomic access
// is mergeable: the check only asks whether the address range is addressable,
// which has nothing to do with atomicity. Three adjacent atomic loads therefore
// fold into one 12-byte read, exactly like the plain-int case in
// mair-merged-range.cpp.
//
// CHECK-LABEL: define {{.*}} @_Z5read3P1A
// CHECK-NOT: call void @__asan_report_load4
// CHECK-COUNT-2: call void @__asan_report_load_n({{.*}}, i64 12)
// CHECK-NOT: call void @__asan_report
// CHECK-COUNT-3: load atomic i32

int read3(A *p) {
  return p->a.load(std::memory_order_relaxed) +
         p->b.load(std::memory_order_relaxed) +
         p->c.load(std::memory_order_relaxed);
}
