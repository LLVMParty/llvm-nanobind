; ModuleID = 'tests\golden\ollvm_obf\loop-to-recursion_simple-counter\input.ll'
source_filename = "tests\\golden\\ollvm_obf\\loop-to-recursion_simple-counter\\input.ll"

define i64 @sum_loop(i64 %n) {
entry:
  %0 = call i64 @0(i64 0, i64 0, i64 %n)
  br label %exit

exit:                                             ; preds = %entry
  ret i64 %0
}

define internal i64 @0(i64 %0, i64 %1, i64 %2) {
  %4 = add i64 %1, %0
  %5 = add i64 %0, 1
  %6 = icmp slt i64 %5, %2
  br i1 %6, label %8, label %7

7:                                                ; preds = %3
  ret i64 %4

8:                                                ; preds = %3
  %9 = musttail call i64 @0(i64 %5, i64 %4, i64 %2)
  ret i64 %9
}
