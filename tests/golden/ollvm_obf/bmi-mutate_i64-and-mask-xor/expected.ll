; ModuleID = 'tests\golden\ollvm_obf\bmi-mutate_i64-and-mask-xor\input.ll'
source_filename = "tests\\golden\\ollvm_obf\\bmi-mutate_i64-and-mask-xor\\input.ll"

define i64 @f(i64 %x, i64 %y) #0 {
entry:
  %0 = call i64 @llvm.x86.bmi.bzhi.64(i64 %x, i64 8)
  %1 = xor i64 %0, -1
  %2 = and i64 %1, %y
  %3 = xor i64 %y, -1
  %4 = and i64 %3, %0
  %5 = or i64 %2, %4
  ret i64 %5
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare i64 @llvm.x86.bmi.bzhi.64(i64, i64) #1

attributes #0 = { "target-features"="+bmi,+bmi2" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(none) }
