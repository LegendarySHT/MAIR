// RUN: env MAIR=1 %clang_tsan -O1 -S -emit-llvm %s -o - | FileCheck %s
// The second line unsets MAIR instead of merely not setting it, so that the
// legacy baseline stays a baseline when the whole suite is run with MAIR=1.
// RUN: env -u MAIR %clang_tsan -O1 -S -emit-llvm %s -o - | FileCheck %s --check-prefix=LEGACY

// The runtime has one entry point per 1/2/4/8/16 bytes: __tsan_read1 ...
// __tsan_read16 and the matching __tsan_writeN. A packed 24-bit bitfield
// compiles to an i24 load or store, which has no entry point of its own, so
// the legacy path ignores the access completely -- getMemoryAccessFuncIndex
// returns -1 and instrumentLoadOrStore gives up. A race on such a field is
// therefore unreported today, and this file pins both halves of that:
//
//   * under MAIR the access is described by __tsan_read_range /
//     __tsan_write_range with the exact width, so it is checked;
//   * under the legacy path it is still not checked, which is the under-report
//     this change removes: two threads racing on such a field produce no
//     report without MAIR and a "Read of size 3" data race with it.
//
// The range entry point is not a new mechanism: a merged MAIR region whose
// width is not a power of two already lowers through it, and it reports the
// region it is given and nothing more, so 3 bytes stays 3 bytes.

struct __attribute__((packed)) S {
  unsigned char tag;
  unsigned int val : 24;
};

void set_val(struct S *s, unsigned v) { s->val = v; }

unsigned get_val(struct S *s) { return s->val; }

// CHECK-LABEL: define {{.*}} @set_val
// CHECK-COUNT-1: call void @__tsan_write_range(ptr {{.*}}, i64 3)
// CHECK-NOT: call void @__tsan_write
// CHECK-LABEL: define {{.*}} @get_val
// CHECK-COUNT-1: call void @__tsan_read_range(ptr {{.*}}, i64 3)
// CHECK-NOT: call void @__tsan_read

// LEGACY-LABEL: define {{.*}} @set_val
// LEGACY-NOT: call void @__tsan_write
// LEGACY-LABEL: define {{.*}} @get_val
// LEGACY-NOT: call void @__tsan_read
