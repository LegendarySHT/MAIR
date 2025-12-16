; ModuleID = 'test_recurrence'
source_filename = "test_recurrence"

define void @recurrence_simple(i32* %p) {
entry:
  ; first store dominates second
  store i32 1, i32* %p, align 4
  br label %cont

cont:
  store i32 2, i32* %p, align 4
  ret void
}
