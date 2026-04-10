define i32 @f(i32 %x, i32 %y, i32 %z) {
entry:
  %a = add i32 %x, %y
  %b = xor i32 %y, %z
  %c = or i32 %a, %b
  %d = add i32 %c, 1
  ret i32 %d
}
