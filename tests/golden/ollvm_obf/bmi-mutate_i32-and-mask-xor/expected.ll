; ModuleID = 'tests\golden\ollvm_obf\bmi-mutate_i32-and-mask-xor\input.ll'
source_filename = "tests\\golden\\ollvm_obf\\bmi-mutate_i32-and-mask-xor\\input.ll"

define i32 @f(i32 %x, i32 %y) #0 {
entry:
  %0 = call i32 @llvm.x86.bmi.bzhi.32(i32 %x, i32 8)
  %1 = xor i32 %0, -1
  %2 = and i32 %1, %y
  %3 = xor i32 %y, -1
  %4 = and i32 %3, %0
  %5 = or i32 %2, %4
  ret i32 %5
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare i32 @llvm.x86.bmi.bzhi.32(i32, i32) #1

attributes #0 = { "target-features"="+bmi,+bmi2" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(none) }
