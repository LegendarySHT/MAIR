; 简单的测试 IR，包含基本的 Load/Store 操作

define i32 @test_simple(i32* %ptr, i32 %val) {
entry:
  ; Load 操作
  %0 = load i32, i32* %ptr

  ; Store 操作
  store i32 %val, i32* %ptr

  ; 另一个 Load
  %1 = load i32, i32* %ptr

  ret i32 %1
}

define void @test_loop(i32* %arr, i32 %n) {
entry:
  br label %loop

loop:
  %i = phi i32 [ 0, %entry ], [ %i.next, %loop ]
  %cmp = icmp slt i32 %i, %n
  br i1 %cmp, label %loop.body, label %exit

loop.body:
  ; 循环中的 Load
  %idx = getelementptr i32, i32* %arr, i32 %i
  %val = load i32, i32* %idx
  
  ; 循环中的 Store
  %val2 = add i32 %val, 1
  store i32 %val2, i32* %idx
  
  %i.next = add i32 %i, 1
  br label %loop

exit:
  ret void
}
