// RUN: env MAIR=1 %clangxx_tsan -O1 -S -emit-llvm %s -o - | FileCheck %s

// XFAIL: *

// TSan's MAIR path currently looks at every access on its own, so the three
// 4-byte loads below still produce three checks. The target is one check that
// covers the whole 12-byte region, the way ASan's merged observation reports
// once (see test/asan/TestCases/mair-merged-range.cpp).
//
// The three loads sit at s+0, s+4 and s+8: contiguous and equally wide, so
// LocationMergePass would fold them into one 12-byte observation. Two things
// keep that observation out of TSan today, and both have to go:
//
//  1. MAOpt is built with KeepFullDebug=true, which selects the Og pipeline
//     (buildMAIRGraphOgPipeline in MAIRPasses.cpp). That pipeline has no
//     LocationMergePass, so TSan never even forms the merged observation:
//     instrumentMAIRObservation is handed three single-source, 4-byte
//     observations. ASan passes KeepFullDebug=false and gets the O1 pipeline,
//     which is why the ASan test could assert a 12-byte range in the first
//     place.
//  2. With LocationMergePass running, CanInstrumentViaMAIR would reject the
//     three-source observation on `Obs.getInstructions().size() != 1`, and
//     CanUseOptimizedSite would reject it again on `Size != SourceSize`
//     (12 != 4), falling back to per-source instrumentation.
//
// 12 is not a power of two, so widening the check to __tsan_read16 would
// report bytes the program never touches. __tsan_read_range(address, 12)
// already exists and is what the memory-intrinsic path uses, so it is the
// natural lowering for a merged range; this test only pins that exactly one
// check covers the region, not which spelling is chosen for it.

struct S { int a, b, c; };

int read3(S *s) { return s->a + s->b + s->c; }

// CHECK-LABEL: define {{.*}} @_Z5read3P1S
// CHECK-NOT: call void @__tsan_read16
// CHECK: call void @__tsan_read
// CHECK-NOT: call void @__tsan_read
