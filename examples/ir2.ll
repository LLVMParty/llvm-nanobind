; ModuleID = 'ir2.cpp'
source_filename = "ir2.cpp"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-pc-windows-msvc19.44.35222"

$printf = comdat any

$_vfprintf_l = comdat any

$__local_stdio_printf_options = comdat any

$"??_C@_0O@GEHPLBPJ@Hello?0?5world?$CB?$AA@" = comdat any

$"??_C@_0N@IHBFFJII@test1?3?5?$CFlli?6?$AA@" = comdat any

$"??_C@_0N@LOJIGFEN@test2?3?5?$CFlli?6?$AA@" = comdat any

$"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA" = comdat any

@"??_C@_0O@GEHPLBPJ@Hello?0?5world?$CB?$AA@" = linkonce_odr dso_local unnamed_addr constant [14 x i8] c"Hello, world!\00", comdat, align 1
@"??_C@_0N@IHBFFJII@test1?3?5?$CFlli?6?$AA@" = linkonce_odr dso_local unnamed_addr constant [13 x i8] c"test1: %lli\0A\00", comdat, align 1
@"??_C@_0N@LOJIGFEN@test2?3?5?$CFlli?6?$AA@" = linkonce_odr dso_local unnamed_addr constant [13 x i8] c"test2: %lli\0A\00", comdat, align 1
@"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA" = linkonce_odr dso_local global i64 0, comdat, align 8

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local i64 @test1_linear_flow(i64 noundef %n) #0 {
entry:
  %n.addr = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  %0 = load i64, ptr %n.addr, align 8
  %shr = lshr i64 %0, 3
  store i64 %shr, ptr %n.addr, align 8
  %1 = load i64, ptr %n.addr, align 8
  %mul = mul i64 %1, 3
  store i64 %mul, ptr %n.addr, align 8
  %2 = load i64, ptr %n.addr, align 8
  %add = add i64 %2, 1337
  store i64 %add, ptr %n.addr, align 8
  %3 = load i64, ptr %n.addr, align 8
  ret i64 %3
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local i64 @test2_if_else(i64 noundef %n) #0 {
entry:
  %retval = alloca i64, align 8
  %n.addr = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  %0 = load i64, ptr %n.addr, align 8
  %and = and i64 %0, 1
  %cmp = icmp eq i64 %and, 0
  br i1 %cmp, label %if.then, label %if.else

if.then:                                          ; preds = %entry
  %1 = load i64, ptr %n.addr, align 8
  %shr = lshr i64 %1, 1
  store i64 %shr, ptr %retval, align 8
  br label %return

if.else:                                          ; preds = %entry
  %2 = load i64, ptr %n.addr, align 8
  %shl = shl i64 %2, 1
  store i64 %shl, ptr %retval, align 8
  br label %return

return:                                           ; preds = %if.else, %if.then
  %3 = load i64, ptr %retval, align 8
  ret i64 %3
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local i64 @test3_complex_cfg(i64 noundef %n, i64 noundef %m) #0 {
entry:
  %m.addr = alloca i64, align 8
  %n.addr = alloca i64, align 8
  store i64 %m, ptr %m.addr, align 8
  store i64 %n, ptr %n.addr, align 8
  %0 = load i64, ptr %n.addr, align 8
  %and = and i64 %0, 3
  %cmp = icmp eq i64 %and, 0
  br i1 %cmp, label %if.then, label %if.else7

if.then:                                          ; preds = %entry
  %1 = load i64, ptr %n.addr, align 8
  %add = add i64 %1, 5
  store i64 %add, ptr %n.addr, align 8
  %2 = load i64, ptr %n.addr, align 8
  %3 = load i64, ptr %m.addr, align 8
  %cmp1 = icmp ult i64 %2, %3
  br i1 %cmp1, label %if.then2, label %if.else

if.then2:                                         ; preds = %if.then
  %4 = load i64, ptr %n.addr, align 8
  %5 = load i64, ptr %n.addr, align 8
  %add3 = add i64 %4, %5
  %6 = load i64, ptr %m.addr, align 8
  %add4 = add i64 %6, %add3
  store i64 %add4, ptr %m.addr, align 8
  br label %if.end

if.else:                                          ; preds = %if.then
  %7 = load i64, ptr %m.addr, align 8
  %8 = load i64, ptr %m.addr, align 8
  %add5 = add i64 %7, %8
  %9 = load i64, ptr %n.addr, align 8
  %add6 = add i64 %9, %add5
  store i64 %add6, ptr %n.addr, align 8
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then2
  br label %if.end13

if.else7:                                         ; preds = %entry
  %10 = load i64, ptr %n.addr, align 8
  %mul = mul i64 %10, 2
  %11 = load i64, ptr %m.addr, align 8
  %cmp8 = icmp ult i64 %mul, %11
  br i1 %cmp8, label %if.then9, label %if.else10

if.then9:                                         ; preds = %if.else7
  %12 = load i64, ptr %n.addr, align 8
  %13 = load i64, ptr %m.addr, align 8
  %sub = sub i64 %13, %12
  store i64 %sub, ptr %m.addr, align 8
  br label %if.end12

if.else10:                                        ; preds = %if.else7
  %14 = load i64, ptr %m.addr, align 8
  %15 = load i64, ptr %n.addr, align 8
  %sub11 = sub i64 %15, %14
  store i64 %sub11, ptr %n.addr, align 8
  br label %if.end12

if.end12:                                         ; preds = %if.else10, %if.then9
  br label %if.end13

if.end13:                                         ; preds = %if.end12, %if.end
  %16 = load i64, ptr %n.addr, align 8
  %shl = shl i64 %16, 5
  %17 = load i64, ptr %m.addr, align 8
  %xor = xor i64 %shl, %17
  ret i64 %xor
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local i64 @test4_simple_loop(i64 noundef %n) #0 {
entry:
  %n.addr = alloca i64, align 8
  %factorial = alloca i64, align 8
  %i = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  store i64 1, ptr %factorial, align 8
  store i64 2, ptr %i, align 8
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i64, ptr %i, align 8
  %1 = load i64, ptr %n.addr, align 8
  %cmp = icmp ule i64 %0, %1
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %2 = load i64, ptr %i, align 8
  %3 = load i64, ptr %factorial, align 8
  %mul = mul i64 %3, %2
  store i64 %mul, ptr %factorial, align 8
  br label %for.inc

for.inc:                                          ; preds = %for.body
  %4 = load i64, ptr %i, align 8
  %inc = add i64 %4, 1
  store i64 %inc, ptr %i, align 8
  br label %for.cond, !llvm.loop !9

for.end:                                          ; preds = %for.cond
  %5 = load i64, ptr %factorial, align 8
  ret i64 %5
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local i64 @test5_simple_loop(i64 noundef %n) #0 {
entry:
  %retval = alloca i64, align 8
  %n.addr = alloca i64, align 8
  %x = alloca i64, align 8
  %i = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  store i64 1, ptr %x, align 8
  store i64 0, ptr %i, align 8
  br label %for.cond

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i64, ptr %i, align 8
  %1 = load i64, ptr %n.addr, align 8
  %cmp = icmp ult i64 %0, %1
  br i1 %cmp, label %for.body, label %for.end

for.body:                                         ; preds = %for.cond
  %2 = load i64, ptr %i, align 8
  %and = and i64 %2, 2
  %cmp1 = icmp eq i64 %and, 0
  br i1 %cmp1, label %if.then, label %if.else

if.then:                                          ; preds = %for.body
  store i64 100, ptr %x, align 8
  br label %if.end

if.else:                                          ; preds = %for.body
  store i64 200, ptr %x, align 8
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then
  br label %for.inc

for.inc:                                          ; preds = %if.end
  %3 = load i64, ptr %i, align 8
  %inc = add i64 %3, 1
  store i64 %inc, ptr %i, align 8
  br label %for.cond, !llvm.loop !11

for.end:                                          ; preds = %for.cond
  %4 = load i64, ptr %n.addr, align 8
  %cmp2 = icmp ugt i64 %4, 0
  br i1 %cmp2, label %land.lhs.true, label %if.end5

land.lhs.true:                                    ; preds = %for.end
  %5 = load i64, ptr %x, align 8
  %cmp3 = icmp eq i64 %5, 1
  br i1 %cmp3, label %if.then4, label %if.end5

if.then4:                                         ; preds = %land.lhs.true
  store i64 4919, ptr %retval, align 8
  br label %return

if.end5:                                          ; preds = %land.lhs.true, %for.end
  %6 = load i64, ptr %x, align 8
  store i64 %6, ptr %retval, align 8
  br label %return

return:                                           ; preds = %if.end5, %if.then4
  %7 = load i64, ptr %retval, align 8
  ret i64 %7
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local i64 @test6_nested(i64 noundef %n) #0 {
entry:
  %n.addr = alloca i64, align 8
  %x = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  store i64 0, ptr %x, align 8
  %0 = load i64, ptr %n.addr, align 8
  %and = and i64 %0, 2
  %cmp = icmp eq i64 %and, 0
  br i1 %cmp, label %if.then, label %if.else3

if.then:                                          ; preds = %entry
  %1 = load i64, ptr %n.addr, align 8
  %shr = lshr i64 %1, 32
  %cmp1 = icmp eq i64 %shr, 0
  br i1 %cmp1, label %if.then2, label %if.else

if.then2:                                         ; preds = %if.then
  store i64 1, ptr %x, align 8
  br label %if.end

if.else:                                          ; preds = %if.then
  store i64 2, ptr %x, align 8
  br label %if.end

if.end:                                           ; preds = %if.else, %if.then2
  br label %if.end9

if.else3:                                         ; preds = %entry
  %2 = load i64, ptr %n.addr, align 8
  %shr4 = lshr i64 %2, 16
  %cmp5 = icmp eq i64 %shr4, 0
  br i1 %cmp5, label %if.then6, label %if.else7

if.then6:                                         ; preds = %if.else3
  store i64 3, ptr %x, align 8
  br label %if.end8

if.else7:                                         ; preds = %if.else3
  store i64 4, ptr %x, align 8
  br label %if.end8

if.end8:                                          ; preds = %if.else7, %if.then6
  br label %if.end9

if.end9:                                          ; preds = %if.end8, %if.end
  %3 = load i64, ptr %x, align 8
  ret i64 %3
}

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define dso_local i64 @test10_switch(i64 noundef %n) #0 {
entry:
  %retval = alloca i64, align 8
  %n.addr = alloca i64, align 8
  store i64 %n, ptr %n.addr, align 8
  %0 = load i64, ptr %n.addr, align 8
  switch i64 %0, label %sw.default [
    i64 1, label %sw.bb
    i64 2, label %sw.bb1
    i64 3, label %sw.bb2
    i64 4, label %sw.bb3
    i64 5, label %sw.bb4
    i64 6, label %sw.bb5
    i64 7, label %sw.bb7
    i64 8, label %sw.bb8
    i64 9, label %sw.bb9
    i64 10, label %sw.bb10
    i64 11, label %sw.bb11
  ]

sw.bb:                                            ; preds = %entry
  store i64 1234, ptr %retval, align 8
  br label %return

sw.bb1:                                           ; preds = %entry
  store i64 4567, ptr %retval, align 8
  br label %return

sw.bb2:                                           ; preds = %entry
  store i64 8901, ptr %retval, align 8
  br label %return

sw.bb3:                                           ; preds = %entry
  %1 = load i64, ptr %n.addr, align 8
  %2 = load i64, ptr %n.addr, align 8
  %add = add i64 %1, %2
  store i64 %add, ptr %retval, align 8
  br label %return

sw.bb4:                                           ; preds = %entry
  %3 = load i64, ptr %n.addr, align 8
  %mul = mul i64 %3, 55
  store i64 %mul, ptr %retval, align 8
  br label %return

sw.bb5:                                           ; preds = %entry
  %4 = load i64, ptr %n.addr, align 8
  %add6 = add i64 %4, 68
  store i64 %add6, ptr %retval, align 8
  br label %return

sw.bb7:                                           ; preds = %entry
  store i64 8473, ptr %retval, align 8
  br label %return

sw.bb8:                                           ; preds = %entry
  %5 = load i64, ptr %n.addr, align 8
  %xor = xor i64 8473, %5
  store i64 %xor, ptr %retval, align 8
  br label %return

sw.bb9:                                           ; preds = %entry
  %6 = load i64, ptr %n.addr, align 8
  %and = and i64 6748, %6
  store i64 %and, ptr %retval, align 8
  br label %return

sw.bb10:                                          ; preds = %entry
  store i64 948, ptr %retval, align 8
  br label %return

sw.bb11:                                          ; preds = %entry
  store i64 4827, ptr %retval, align 8
  br label %return

sw.default:                                       ; preds = %entry
  %7 = load i64, ptr %n.addr, align 8
  %and12 = and i64 %7, 4919
  store i64 %and12, ptr %retval, align 8
  br label %return

return:                                           ; preds = %sw.default, %sw.bb11, %sw.bb10, %sw.bb9, %sw.bb8, %sw.bb7, %sw.bb5, %sw.bb4, %sw.bb3, %sw.bb2, %sw.bb1, %sw.bb
  %8 = load i64, ptr %retval, align 8
  ret i64 %8
}

; Function Attrs: mustprogress noinline optnone uwtable
define dso_local void @"?test@@YAXXZ"() #1 {
entry:
  %call = call i32 @puts(ptr noundef @"??_C@_0O@GEHPLBPJ@Hello?0?5world?$CB?$AA@")
  %call1 = call i64 @test1_linear_flow(i64 noundef 42)
  %call2 = call i32 (ptr, ...) @printf(ptr noundef @"??_C@_0N@IHBFFJII@test1?3?5?$CFlli?6?$AA@", i64 noundef %call1)
  %call3 = call i64 @test2_if_else(i64 noundef 8)
  %call4 = call i32 (ptr, ...) @printf(ptr noundef @"??_C@_0N@LOJIGFEN@test2?3?5?$CFlli?6?$AA@", i64 noundef %call3)
  ret void
}

declare dso_local i32 @puts(ptr noundef) #2

; Function Attrs: mustprogress noinline optnone uwtable
define linkonce_odr dso_local i32 @printf(ptr noundef %_Format, ...) #1 comdat {
entry:
  %_Format.addr = alloca ptr, align 8
  %_Result = alloca i32, align 4
  %_ArgList = alloca ptr, align 8
  store ptr %_Format, ptr %_Format.addr, align 8
  call void @llvm.va_start.p0(ptr %_ArgList)
  %0 = load ptr, ptr %_ArgList, align 8
  %1 = load ptr, ptr %_Format.addr, align 8
  %call = call ptr @__acrt_iob_func(i32 noundef 1)
  %call1 = call i32 @_vfprintf_l(ptr noundef %call, ptr noundef %1, ptr noundef null, ptr noundef %0)
  store i32 %call1, ptr %_Result, align 4
  call void @llvm.va_end.p0(ptr %_ArgList)
  %2 = load i32, ptr %_Result, align 4
  ret i32 %2
}

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.va_start.p0(ptr) #3

; Function Attrs: mustprogress noinline optnone uwtable
define linkonce_odr dso_local i32 @_vfprintf_l(ptr noundef %_Stream, ptr noundef %_Format, ptr noundef %_Locale, ptr noundef %_ArgList) #1 comdat {
entry:
  %_ArgList.addr = alloca ptr, align 8
  %_Locale.addr = alloca ptr, align 8
  %_Format.addr = alloca ptr, align 8
  %_Stream.addr = alloca ptr, align 8
  store ptr %_ArgList, ptr %_ArgList.addr, align 8
  store ptr %_Locale, ptr %_Locale.addr, align 8
  store ptr %_Format, ptr %_Format.addr, align 8
  store ptr %_Stream, ptr %_Stream.addr, align 8
  %0 = load ptr, ptr %_ArgList.addr, align 8
  %1 = load ptr, ptr %_Locale.addr, align 8
  %2 = load ptr, ptr %_Format.addr, align 8
  %3 = load ptr, ptr %_Stream.addr, align 8
  %call = call ptr @__local_stdio_printf_options()
  %4 = load i64, ptr %call, align 8
  %call1 = call i32 @__stdio_common_vfprintf(i64 noundef %4, ptr noundef %3, ptr noundef %2, ptr noundef %1, ptr noundef %0)
  ret i32 %call1
}

declare dso_local ptr @__acrt_iob_func(i32 noundef) #2

; Function Attrs: nocallback nofree nosync nounwind willreturn
declare void @llvm.va_end.p0(ptr) #3

declare dso_local i32 @__stdio_common_vfprintf(i64 noundef, ptr noundef, ptr noundef, ptr noundef, ptr noundef) #2

; Function Attrs: mustprogress noinline nounwind optnone uwtable
define linkonce_odr dso_local ptr @__local_stdio_printf_options() #0 comdat {
entry:
  ret ptr @"?_OptionsStorage@?1??__local_stdio_printf_options@@9@4_KA"
}

attributes #0 = { mustprogress noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress noinline optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nocallback nofree nosync nounwind willreturn }

!llvm.dbg.cu = !{!0}
!llvm.linker.options = !{!2}
!llvm.module.flags = !{!3, !4, !5, !6, !7}
!llvm.ident = !{!8}

!0 = distinct !DICompileUnit(language: DW_LANG_C_plus_plus_14, file: !1, producer: "clang version 21.1.8", isOptimized: false, runtimeVersion: 0, emissionKind: NoDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "ir2.cpp", directory: "C:\\CodeBlocks\\llvm-nanobind\\examples")
!2 = !{!"/FAILIFMISMATCH:\22_CRT_STDIO_ISO_WIDE_SPECIFIERS=0\22"}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 1, !"wchar_size", i32 2}
!5 = !{i32 8, !"PIC Level", i32 2}
!6 = !{i32 7, !"uwtable", i32 2}
!7 = !{i32 1, !"MaxTLSAlign", i32 65536}
!8 = !{!"clang version 21.1.8"}
!9 = distinct !{!9, !10}
!10 = !{!"llvm.loop.mustprogress"}
!11 = distinct !{!11, !10}
