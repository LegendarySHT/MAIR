; RUN: mair-opt -passes="simplifygraph,locationmerge" %s | FileCheck %s
; RUN: mair-opt -passes="simplifygraph,locationmerge" --disable-output %s

; NoAlias: two distinct stack objects, and one stack object against an argument
; that cannot have been derived from it. AliasAnalysis proves the addresses
; differ, so the observations stay apart.
;
; These two are the last line rather than the finest-grained check: a MustAlias
; that accepted PartialAlias or MayAlias still answers NoAlias correctly and
; leaves them green. They fail when the alias query is dropped altogether, which
; is the regression that would also let every other negative through -- with two
; offsets of zero, merging here folds two 4-byte loads into one check on %a.

define void @keep_two_alloca_objects() {
entry:
  %a = alloca i32, align 4
  %b = alloca i32, align 4
  %v0 = load i32, ptr %a, align 4
  %v1 = load i32, ptr %b, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @keep_two_alloca_objects {
; CHECK-DAG: o: read ptr %a, 4
; CHECK-DAG: o: read ptr %b, 4
; CHECK-NOT: o: read ptr

; An alloca is not captured here, so a pointer that arrived as an argument
; cannot name it.

define void @keep_alloca_and_argument(ptr %p) {
entry:
  %a = alloca i32, align 4
  %v0 = load i32, ptr %a, align 4
  %v1 = load i32, ptr %p, align 4
  ret void
}

; CHECK-LABEL: mair graph of fn @keep_alloca_and_argument {
; CHECK-DAG: o: read ptr %a, 4
; CHECK-DAG: o: read ptr %p, 4
; CHECK-NOT: o: read ptr
