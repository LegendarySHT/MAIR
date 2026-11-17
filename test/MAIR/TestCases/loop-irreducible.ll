; An irreducible or otherwise unrepresented cycle cannot be placed in the MAIR
; hierarchy. The function is skipped instead of aborting the compilation: the
; tool exits normally and emits no graph at all, so the sanitizer will use its
; legacy per-access instrumentation for this function.
;
; RUN: mair-opt %s | FileCheck --allow-empty %s
; RUN: mair-opt -passes=lpoc %s | FileCheck --allow-empty %s

; CHECK-NOT: mair graph
; CHECK-NOT: MAIR does not support irreducible or unrepresented cycles

; The nested cycle has two entries and cannot be represented by LoopInfo.
define void @keep_irreducible_period(ptr %p, i1 %entry.sel, i1 %cycle.sel,
                                     i1 %again) {
entry:
  br label %header

header:
  %i = phi i64 [ 0, %entry ], [ %next, %latch ]
  br i1 %again, label %dispatch, label %exit

dispatch:
  br i1 %entry.sel, label %a, label %b

a:
  br i1 %cycle.sel, label %b, label %latch

b:
  br i1 %cycle.sel, label %a, label %latch

latch:
  %pi = getelementptr i8, ptr %p, i64 %i
  %v = load i8, ptr %pi, align 1
  %next = add i64 %i, 1
  br label %header

exit:
  ret void
}
