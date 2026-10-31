// RUN: env MAIR=1 %clangxx_tsan -O1 -S -emit-llvm %s -o - | FileCheck %s

// A merged observation covers a contiguous region built from several accesses,
// and TSan has to check that region exactly once. Two things had to change for
// that to happen:
//
//  1. MAOpt is built with KeepFullDebug=false, which selects the O1 pipeline.
//     The Og pipeline has no LocationMergePass, so TSan never formed a merged
//     observation and instrumented every access on its own.
//  2. The preflight gate and the backend accept multi-source observations, and
//     a region whose width is not a power of two is described by
//     __tsan_read_range. Rounding up to __tsan_read16 would report four bytes
//     the program never touches.
//
// The three loads sit at s+0, s+4 and s+8, so they merge into a 12-byte region,
// which has no __tsan_readN entry point of its own.

struct S { int a, b, c; };

int read3(S *s) { return s->a + s->b + s->c; }

// CHECK-LABEL: define {{.*}} @_Z5read3P1S
// CHECK-NOT: call void @__tsan_read16
// CHECK: call void @__tsan_read
// CHECK-NOT: call void @__tsan_read

void store2(int *p) { p[0] = 1; p[1] = 2; }

// Two adjacent 4-byte stores merge into one 8-byte region as well. 8 is a
// standard width, so this region does have a __tsan_writeN entry point, but
// __tsan_write8 expects an 8-aligned address and the stores only guarantee 4,
// so the unaligned spelling is the correct one here.
//
// CHECK-LABEL: define {{.*}} @_Z6store2Pi
// CHECK-NOT: call void @__tsan_write
// CHECK: call void @__tsan_unaligned_write8
// CHECK-NOT: call void @__tsan_write
