; RUN: mair-opt -passes="simplifygraph,locationmerge" %s | FileCheck %s
; RUN: mair-opt -passes="simplifygraph,locationmerge" --disable-output %s

; The negative half of the AliasAnalysis contract. Merging two observations
; produces one check covering one range, so accepting anything weaker than "the
; same address" lets a check guard bytes no source access touched -- a silent
; under-report, the one failure mode an instrumentation pass must not have.
;
; Two arguments are the plain MayAlias case: nothing in the callee says whether
; the caller passes one object twice. Both observations have to stay. They also
; make the failure visible: both bases sit at offset zero, so treating the pair
; as MustAlias folds the two 4-byte loads into a single 4-byte check on %p and
; drops the access through %r entirely.
;
; This is a graph-level test on purpose. At -O1 the frontend has already
; canonicalized the shapes AliasAnalysis is asked about here, so the sanitizer
; suites cannot tell a correct alias answer from a permissive one; see
; alias-merge.ll for the same note about the positive direction.

define void @may_alias_two_arguments(ptr %p, ptr %r) {
entry:
  %v0 = load i32, ptr %p, align 4
  %v1 = load i32, ptr %r, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @may_alias_two_arguments {
; CHECK-DAG: o: read ptr %p, 4
; CHECK-DAG: o: read ptr %r, 4
; CHECK-NOT: o: read ptr

; The control: when both arms of a select name the same pointer, the two
; observations do denote one address and must merge. A rule that only accepted
; identical Values would satisfy every negative above and still lose this.

define void @merge_select_of_one_pointer(ptr %p, i1 %c) {
entry:
  %q = select i1 %c, ptr %p, ptr %p
  %v0 = load i32, ptr %p, align 4
  %v1 = load i32, ptr %q, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @merge_select_of_one_pointer {
; CHECK: o: read ptr %p, 4
; CHECK-NOT: o: read ptr
