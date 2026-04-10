; ModuleID = 'tests\golden\ollvm_obf\loop-to-recursion_multi-exit-rejected\input.ll'
source_filename = "tests\\golden\\ollvm_obf\\loop-to-recursion_multi-exit-rejected\\input.ll"

define i32 @f(i32 %n) {
entry:
  br label %header

header:                                           ; preds = %latch, %entry
  %i = phi i32 [ 0, %entry ], [ %next, %latch ]
  %cmp0 = icmp slt i32 %i, %n
  br i1 %cmp0, label %latch, label %exit

latch:                                            ; preds = %header
  %next = add i32 %i, 1
  %cmp1 = icmp slt i32 %next, %n
  br i1 %cmp1, label %header, label %exit

exit:                                             ; preds = %latch, %header
  %r = phi i32 [ %i, %header ], [ %next, %latch ]
  ret i32 %r
}
