; ModuleID = 'tests\golden\ollvm_obf\code-clone_internal-helper\input.ll'
source_filename = "tests\\golden\\ollvm_obf\\code-clone_internal-helper\\input.ll"

define internal i32 @helper(i32 %x, i32 %y) {
entry:
  %0 = add i32 %x, %y
  %1 = xor i32 %0, 7
  %2 = or i32 %1, 3
  %3 = add i32 %2, 1
  %4 = and i32 %3, 255
  ret i32 %4
}

define i32 @main(i32 %n) {
entry:
  %call = call i32 @helper(i32 %n, i32 5)
  ret i32 %call
}
