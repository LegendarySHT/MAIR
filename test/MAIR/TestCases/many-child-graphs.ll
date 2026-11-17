; A function whose graph tree is larger than MAIRGraph::graphs()'s inline
; capacity. `graphs()` returns the list by value, so walking it children-first
; through llvm::reverse must not do so on the temporary: for nine graphs the
; buffer lives on the heap and is freed before the first iteration, which used
; to make MAIRGraph::build dereference a stale pointer and crash.
;
; RUN: mair-opt -passes=simplifygraph %s | FileCheck %s
; RUN: mair-opt -passes=simplifygraph --disable-output %s

; The root graph plus one graph per loop: nine in total, one past the inline
; capacity of MAIRGraph::graphs(). Every loop graph must be present in the dump.
; CHECK-LABEL: mair graph of fn @many_loops {
; CHECK-COUNT-8: of loop

define i32 @many_loops(ptr %p, i32 %n) {
entry:
  br label %loop0
loop0:
  %i0 = phi i32 [ 0, %entry ], [ %i0.n, %loop0 ]
  %v0 = load i32, ptr %p, align 4
  %i0.n = add i32 %i0, 1
  %c0 = icmp slt i32 %i0.n, %n
  br i1 %c0, label %loop0, label %loop1
loop1:
  %i1 = phi i32 [ 0, %loop0 ], [ %i1.n, %loop1 ]
  %v1 = load i32, ptr %p, align 4
  %i1.n = add i32 %i1, 1
  %c1 = icmp slt i32 %i1.n, %n
  br i1 %c1, label %loop1, label %loop2
loop2:
  %i2 = phi i32 [ 0, %loop1 ], [ %i2.n, %loop2 ]
  %v2 = load i32, ptr %p, align 4
  %i2.n = add i32 %i2, 1
  %c2 = icmp slt i32 %i2.n, %n
  br i1 %c2, label %loop2, label %loop3
loop3:
  %i3 = phi i32 [ 0, %loop2 ], [ %i3.n, %loop3 ]
  %v3 = load i32, ptr %p, align 4
  %i3.n = add i32 %i3, 1
  %c3 = icmp slt i32 %i3.n, %n
  br i1 %c3, label %loop3, label %loop4
loop4:
  %i4 = phi i32 [ 0, %loop3 ], [ %i4.n, %loop4 ]
  %v4 = load i32, ptr %p, align 4
  %i4.n = add i32 %i4, 1
  %c4 = icmp slt i32 %i4.n, %n
  br i1 %c4, label %loop4, label %loop5
loop5:
  %i5 = phi i32 [ 0, %loop4 ], [ %i5.n, %loop5 ]
  %v5 = load i32, ptr %p, align 4
  %i5.n = add i32 %i5, 1
  %c5 = icmp slt i32 %i5.n, %n
  br i1 %c5, label %loop5, label %loop6
loop6:
  %i6 = phi i32 [ 0, %loop5 ], [ %i6.n, %loop6 ]
  %v6 = load i32, ptr %p, align 4
  %i6.n = add i32 %i6, 1
  %c6 = icmp slt i32 %i6.n, %n
  br i1 %c6, label %loop6, label %loop7
loop7:
  %i7 = phi i32 [ 0, %loop6 ], [ %i7.n, %loop7 ]
  %v7 = load i32, ptr %p, align 4
  %i7.n = add i32 %i7, 1
  %c7 = icmp slt i32 %i7.n, %n
  br i1 %c7, label %loop7, label %exit
exit:
  ret i32 %v7
}
