; RUN: mair-opt -passes="simplifygraph,locationmerge" %s | FileCheck %s
; RUN: mair-opt -passes="simplifygraph,locationmerge" --disable-output %s

; Two observations only merge when they name the same memory object, and the
; graph interns one object per base Value. An address can still be written down
; with two Values: a phi that selects the same pointer on every incoming edge is
; the common one, and so is a select. Until location merging asks AliasAnalysis,
; those two spellings stay apart and each check is emitted on its own.
;
; Only MustAlias is accepted. A phi that picks between two different offsets of
; the same object (PartialAlias-shaped) or between two unrelated pointers
; (MayAlias) must keep its two observations: a merged observation is checked as
; one region, so merging on anything weaker than "the same address" would make
; the check cover bytes no source access ever touched.

define void @merge_through_phi(ptr %p, i1 %c) {
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

; CHECK-LABEL: mair graph of fn @merge_through_phi {
; CHECK: o: read ptr %p, 4
; CHECK-NOT: o: read ptr

; The phi-based load comes first here. The two bases are still the same address,
; but %q is only available after %p, so the merged observation has to be renamed
; to %p -- the observation is materialized at the program point, and a base the
; point cannot name would make it unmovable and, on a bb.entry site, unusable.

define void @merge_through_phi_reversed(ptr %p, i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %join
b:
  br label %join
join:
  %q = phi ptr [ %p, %a ], [ %p, %b ]
  %v1 = load i32, ptr %q, align 4
  %v0 = load i32, ptr %p, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @merge_through_phi_reversed {
; CHECK: o: read ptr %p, 4
; CHECK-NOT: o: read ptr

; %q is either %p or %p+4 here, so the two loads do not touch the same address.

define void @keep_phi_with_two_offsets(ptr %p, i1 %c) {
entry:
  %p4 = getelementptr i8, ptr %p, i64 4
  br i1 %c, label %a, label %b
a:
  br label %join
b:
  br label %join
join:
  %q = phi ptr [ %p, %a ], [ %p4, %b ]
  %v0 = load i32, ptr %p, align 4
  %v1 = load i32, ptr %q, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @keep_phi_with_two_offsets {
; CHECK-DAG: o: read ptr %p, 4
; CHECK-DAG: o: read ptr %q, 4
; CHECK-NOT: o: read ptr

; Two unrelated arguments: AliasAnalysis can only say MayAlias.

define void @keep_phi_of_two_arguments(ptr %p, ptr %r, i1 %c) {
entry:
  br i1 %c, label %a, label %b
a:
  br label %join
b:
  br label %join
join:
  %q = phi ptr [ %p, %a ], [ %r, %b ]
  %v0 = load i32, ptr %p, align 4
  %v1 = load i32, ptr %q, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @keep_phi_of_two_arguments {
; CHECK-DAG: o: read ptr %p, 4
; CHECK-DAG: o: read ptr %q, 4
; CHECK-NOT: o: read ptr
