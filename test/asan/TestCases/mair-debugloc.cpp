// RUN: env MAIR=1 %clangxx -asan -O1 -g -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=MAIR
// RUN: env -u MAIR %clangxx -asan -O1 -g -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=LEGACY

struct S { int a, b, c; };

int read3(S *s) { return s->a + s->b + s->c; }

// A check must name the access it guards, not the place where it happens to be
// inserted. instrumentAddress builds the reporting call from OrigIns -- the
// observation's first source instruction -- and copies that instruction's
// DebugLoc onto it; the shadow load, the compare and the branch of the same
// check come from one IRBuilder and share the location. MAIR sets no debug
// location of its own and needs none, because it hands instrumentMAIRObservation
// the source and passes that on as OrigIns.
//
// MAIR folds the three loads into one 12-byte check, so the whole check carries
// a single location: the first source's. The legacy path checks each load where
// it stands and reports the same line three times. "Same line as the access" is
// what this test pins. The interesting failure mode is a merged check placed at
// a program point whose anchor is not one of the accesses -- the two probes would
// then carry the anchor's line, or none at all.
//
// MAIR-LABEL: define {{.*}} @_Z5read3P1S
// MAIR-NOT: load i32
// MAIR: %[[SHADOW:[0-9]+]] = load i8, ptr {{%[0-9]+}}, align 1, !dbg ![[MLOC:[0-9]+]]
// MAIR: call void @__asan_report_load_n({{.*}}, i64 12) #{{[0-9]+}}, !dbg ![[MLOC]]
// MAIR-NEXT: unreachable
// MAIR: call void @__asan_report_load_n({{.*}}, i64 12) #{{[0-9]+}}, !dbg ![[MLOC]]
// MAIR-DAG: ![[MLOC]] = !DILocation(line: 6, column: 29, scope:
// MAIR-NOT: call void @__asan_report_load4

// The legacy path keeps one check per load. All three still report the line of
// the statement they stand on, which is what MAIR's single check has to match.
//
// LEGACY-LABEL: define {{.*}} @_Z5read3P1S
// LEGACY: call void @__asan_report_load4({{.*}}) #{{[0-9]+}}, !dbg ![[L1:[0-9]+]]
// LEGACY-COUNT-2: call void @__asan_report_load4({{.*}}) #{{[0-9]+}}, !dbg
// LEGACY-NOT: call void @__asan_report_load_n
// LEGACY: ![[L1]] = !DILocation(line: 6, column: 29, scope:

// The legacy line unsets MAIR rather than merely not setting it: a suite-wide
// MAIR=1 would otherwise run both prefixes through MAIR, and the baseline would
// stop being a baseline. Note that the expected line numbers below are tied to
// this file, so comments here stay at the bottom.
