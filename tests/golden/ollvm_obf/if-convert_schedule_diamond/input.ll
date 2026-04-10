define i32 @diamond(i32 %x) {
entry:
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %then, label %else
then:
  %a = add i32 %x, 11
  br label %merge
else:
  %b = add i32 %x, 17
  br label %merge
merge:
  %phi = phi i32 [ %a, %then ], [ %b, %else ]
  %r = add i32 %phi, 1
  ret i32 %r
}
