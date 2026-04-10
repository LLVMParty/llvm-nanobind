; ModuleID = 'tests\golden\ollvm_obf\bmi-mutate_bextr-mask-window\input.ll'
source_filename = "tests\\golden\\ollvm_obf\\bmi-mutate_bextr-mask-window\\input.ll"

define i32 @f(i32 %x) #0 {
entry:
  %0 = call i32 @llvm.x86.bmi.bextr.32(i32 %x, i32 2053)
  %1 = shl i32 %0, 5
  ret i32 %1
}

; Function Attrs: nocallback nofree nosync nounwind willreturn memory(none)
declare i32 @llvm.x86.bmi.bextr.32(i32, i32) #1

attributes #0 = { "target-features"="+bmi,+bmi2" }
attributes #1 = { nocallback nofree nosync nounwind willreturn memory(none) }
