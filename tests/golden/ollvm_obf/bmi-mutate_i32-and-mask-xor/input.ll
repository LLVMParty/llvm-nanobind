define i32 @f(i32 %x, i32 %y) #0 {
entry:
  %a = and i32 %x, 255
  %b = xor i32 %a, %y
  ret i32 %b
}
attributes #0 = { "target-features"="+bmi,+bmi2" }
