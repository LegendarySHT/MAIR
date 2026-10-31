// RUN: env MAIR=build %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=BUILD
// RUN: env MAIR=1 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=FULL
// RUN: env MAIR=0 %clangxx -asan -O1 -mllvm -asan-opt=false -S -emit-llvm %s -o - | FileCheck %s --check-prefix=OFF

// The three levels of the MAIR switch on one access pattern.
//
// MAIR=build builds and materializes the graph but runs no transform pass,
// so every access keeps its own check: four load4 checks, exactly as the
// legacy path emits. Only the observation GEPs show that MAIR was engaged,
// which is what makes this level usable for an ablation.
//
// MAIR=1 runs the pipeline: the four adjacent loads merge into one 16-byte
// range. 16 is a power of two, but the alignment is only 4, so the check
// probes the first and the last byte and reports the full width through
// __asan_report_load_n(addr, 16) twice.
//
// MAIR=0 turns MAIR off, so no observation is built at all.

struct S {
  int a, b, c, d;
};

int read4(S *s) { return s->a + s->b + s->c + s->d; }

// BUILD-LABEL: define {{.*}} @_Z5read4P1S
// BUILD: %mair.observation.gep = getelementptr i8, ptr %s, i64 0
// BUILD-COUNT-4: call void @__asan_report_load4
// BUILD-NOT: call void @__asan_report_load_n

// FULL-LABEL: define {{.*}} @_Z5read4P1S
// FULL-COUNT-2: call void @__asan_report_load_n({{.*}}, i64 16)
// FULL-NOT: call void @__asan_report_load4

// OFF-LABEL: define {{.*}} @_Z5read4P1S
// OFF-NOT: mair.observation
// OFF-COUNT-4: call void @__asan_report_load4
