; ModuleID = 'tests\golden\ollvm_obf\if-convert_diamond\input.ll'
source_filename = "tests\\golden\\ollvm_obf\\if-convert_diamond\\input.ll"

define i32 @diamond(i32 %x) {
entry:
  %cmp = icmp sgt i32 %x, 0
  %a = add i32 %x, 11
  %b = add i32 %x, 17
  %0 = select i1 %cmp, i32 %a, i32 %b
  br label %merge

merge:                                            ; preds = %entry
  ret i32 %0
}
