; ModuleID = 'test_complex'
source_filename = "test_complex.c"

define void @test_complex() {
entry:
  %a = alloca i32
  %b = alloca i32
  %c = alloca i32
  store i32 0, i32* %a
  store i32 1, i32* %b
  br label %loop

loop:
  %i = phi i32 [0, %entry], [%next, %loop]
  %load_a = load i32, i32* %a
  %load_b = load i32, i32* %b
  %add = add i32 %load_a, %load_b
  store i32 %add, i32* %c
  %next = add i32 %i, 1
  %cond = icmp slt i32 %next, 10
  br i1 %cond, label %loop, label %exit

exit:
  ret void
}