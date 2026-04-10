define i64 @f(i64 %x, i64 %y) #0 {
entry:
  %a = and i64 %x, 255
  %b = xor i64 %a, %y
  ret i64 %b
}
attributes #0 = { "target-features"="+bmi,+bmi2" }
