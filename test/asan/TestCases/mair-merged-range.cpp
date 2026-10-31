// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s
//
// XFAIL: *

struct S { int a, b, c; };

int read3(S *s) { return s->a + s->b + s->c; }

// The three 4-byte loads sit at s+0, s+4, s+8: contiguous and equally wide, so
// locationmerge could fold them into one 12-byte observation.
//
// It never gets the chance. MAIROptimizationConfig::forAddressSanitizer() sets
// AllowRelocation=false, which leaves every observation non-movable, and
// SimplifyGraphPass only contracts a dominating/post-dominating pair when the
// dominating one holds movable observations only. Every load therefore keeps
// its own program point and locationmerge never sees two observations to
// merge, so the backend gate on Obs.getInstructions().size() != 1 is not even
// reached: three separate per-field checks come out instead.
//
// The backend side is already widened to accept multi-source observations, so
// as soon as the graph produces one it is materialized as a single check over
// the whole range. 12 is not a power of two, so instrumentUnusualSizeOrAlignment
// probes the first and the last byte and reports __asan_report_load_n(addr, 12)
// for both of them.
//
// CHECK-LABEL: define {{.*}} @_Z5read3P1S
// CHECK-COUNT-2: call void @__asan_report_load_n({{.*}}, i64 12)
// CHECK-NOT: call void @__asan_report_load4
