; RUN: opt -load-pass-plugin=libXSANMopIR.so -passes="mop-ir,print<mop-ir>" -disable-output < %s | FileCheck %s
; 复杂冗余MOP检测用例

define void @redundant_complex(i32* %p, i32* %q) {
entry:
  store i32 1, i32* %p, align 4           ; MOP1
  store i32 2, i32* %p, align 4           ; MOP2 (冗余)
  call void @side_effect()                ; 干扰调用
  store i32 3, i32* %p, align 4           ; MOP3
  store i32 4, i32* %q, align 4           ; MOP4
  store i32 5, i32* %q, align 4           ; MOP5 (冗余)
  %cond = icmp eq i32* %p, %q
  br i1 %cond, label %bb1, label %bb2
bb1:
  store i32 6, i32* %p, align 4           ; MOP6
  br label %exit
bb2:
  store i32 7, i32* %p, align 4           ; MOP7
  br label %exit
exit:
  ret void
}

declare void @side_effect()

; CHECK-LABEL: @redundant_complex
; CHECK: MOP redundant: store i32 2, i32* %p, align 4
; CHECK: MOP redundant: store i32 5, i32* %q, align 4
