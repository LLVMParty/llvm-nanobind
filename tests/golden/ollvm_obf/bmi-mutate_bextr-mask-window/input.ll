define i32 @f(i32 %x) #0 {
entry:
  %a = and i32 %x, 8160
  ret i32 %a
}
attributes #0 = { "target-features"="+bmi,+bmi2" }
