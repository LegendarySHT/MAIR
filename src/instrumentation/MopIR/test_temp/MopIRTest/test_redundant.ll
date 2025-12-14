define void @redundant_store(i32* %p) {
entry:
  store i32 42, i32* %p, align 4         ; MOP1
  store i32 43, i32* %p, align 4         ; MOP2 (应被判定为冗余)
  ret void
}