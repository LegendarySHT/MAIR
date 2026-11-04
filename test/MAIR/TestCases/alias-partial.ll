; RUN: mair-opt -passes="simplifygraph,locationmerge" %s | FileCheck %s
; RUN: mair-opt -passes="simplifygraph,locationmerge" --disable-output %s

; PartialAlias: the two addresses are known to lie inside one object but not to
; be the same address. That is exactly the answer that must not be accepted as
; MustAlias -- rebasing the second observation onto the first base would make
; the check start where the first access starts, and the offsets it dropped are
; the ones the original access was about.
;
; Both shapes below share a property that makes a wrong answer observable rather
; than merely conservative: the second base is at offset zero relative to itself
; and the first access covers four bytes, so an accepted PartialAlias produces
; two overlapping four-byte regions that collapse into one observation.

define void @keep_select_of_two_offsets(ptr %p, i1 %c) {
entry:
  %p4 = getelementptr i8, ptr %p, i64 4
  %q = select i1 %c, ptr %p, ptr %p4
  %v0 = load i32, ptr %p, align 4
  %v1 = load i32, ptr %q, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @keep_select_of_two_offsets {
; CHECK-DAG: o: read ptr %p, 4
; CHECK-DAG: o: read ptr %q, 4
; CHECK-NOT: o: read ptr

; The same judgement through a phi that never names %p: both incoming edges are
; offsets of the object, so %q is at best partially aliasing %p. A phi whose
; arms are two different fields is the shape a frontend emits for
; "s->a or s->b", which is why it is worth pinning separately from the select.

define void @keep_phi_of_two_offsets(ptr %p, i1 %c) {
entry:
  %p4 = getelementptr i8, ptr %p, i64 4
  %p8 = getelementptr i8, ptr %p, i64 8
  br i1 %c, label %a, label %b
a:
  br label %join
b:
  br label %join
join:
  %q = phi ptr [ %p4, %a ], [ %p8, %b ]
  %v0 = load i32, ptr %p, align 4
  %v1 = load i32, ptr %q, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @keep_phi_of_two_offsets {
; CHECK-DAG: o: read ptr %p, 4
; CHECK-DAG: o: read ptr %q, 4
; CHECK-NOT: o: read ptr

; The boundary in the other direction: a phi that picks the same pointer on
; every edge is MustAlias even though the observation is written down against
; %q, and the two loads do merge. Keeping this next to the two negatives is the
; point -- MustAlias has to be wide enough to cover a second spelling of one
; address and narrow enough to reject two addresses of one object.

define void @merge_phi_of_one_pointer(ptr %p, i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %join
b:
  br label %join
join:
  %q = phi ptr [ %p, %a ], [ %p, %b ]
  %v0 = load i32, ptr %p, align 4
  %v1 = load i32, ptr %q, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @merge_phi_of_one_pointer {
; CHECK: o: read ptr %p, 4
; CHECK-NOT: o: read ptr
