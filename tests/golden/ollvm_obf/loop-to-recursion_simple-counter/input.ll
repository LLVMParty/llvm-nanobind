define i64 @sum_loop(i64 %n) {
entry:
  br label %header
header:
  %i = phi i64 [ 0, %entry ], [ %i.next, %header ]
  %sum = phi i64 [ 0, %entry ], [ %sum.next, %header ]
  %sum.next = add i64 %sum, %i
  %i.next = add i64 %i, 1
  %cmp = icmp slt i64 %i.next, %n
  br i1 %cmp, label %header, label %exit
exit:
  %ret = phi i64 [ %sum.next, %header ]
  ret i64 %ret
}
