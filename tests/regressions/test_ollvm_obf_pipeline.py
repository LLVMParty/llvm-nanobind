"""Smoke tests for tools/obfuscation/ollvm_obf.py."""

from pathlib import Path
import llvm

from tools.obfuscation.ollvm_obf import PipelineOptions, apply_pipeline, main


def test_ollvm_if_convert_eliminates_phi_and_introduces_select() -> None:
    ir = r'''
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
      ret i32 %phi
    }
    '''

    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir) as mod:
            apply_pipeline(mod, PipelineOptions(if_convert=True, verify_each=True))
            text = mod.to_string()
            assert mod.verify(), mod.get_verification_error()
            assert " phi " not in text
            assert "select i1" in text


def test_ollvm_supported_pipeline_smoke() -> None:
    ir = r'''
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
      %cmp = icmp sgt i32 %n, 0
      br i1 %cmp, label %then, label %else
    then:
      %a = add i32 %n, 5
      br label %merge
    else:
      %b = add i32 %n, 9
      br label %merge
    merge:
      %phi = phi i32 [ %a, %then ], [ %b, %else ]
      %call = call i32 @helper(i32 %phi, i32 5)
      ret i32 %call
    }
    '''

    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir) as mod:
            apply_pipeline(
                mod,
                PipelineOptions(
                    code_clone=True,
                    substitute=True,
                    opaque_predicates=True,
                    bogus_control_flow=True,
                    const_unfold=True,
                    schedule_instructions=True,
                    verify_each=True,
                    seed=123,
                ),
            )
            assert mod.verify(), mod.get_verification_error()


def test_ollvm_flatten_smoke() -> None:
    ir = r'''
    define i32 @f(i32 %x) {
    entry:
      %cmp = icmp eq i32 %x, 0
      br i1 %cmp, label %then, label %else
    then:
      %a = add i32 %x, 1
      br label %merge
    else:
      %b = add i32 %x, 2
      br label %merge
    merge:
      %v = phi i32 [ %a, %then ], [ %b, %else ]
      %r = add i32 %v, 3
      ret i32 %r
    }
    '''

    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir) as mod:
            apply_pipeline(mod, PipelineOptions(flatten=True, verify_each=True, seed=7))
            text = mod.to_string()
            assert mod.verify(), mod.get_verification_error()
            assert "cff.dispatch" in text
            assert "cff.state" in text


def test_ollvm_remaining_passes_smoke() -> None:
    string_ir = r'''
    @.str = private constant [6 x i8] c"hello\00"
    define ptr @f() {
    entry:
      %p = getelementptr [6 x i8], ptr @.str, i32 0, i32 0
      ret ptr %p
    }
    '''

    with llvm.create_context() as ctx:
        with ctx.parse_ir(string_ir) as mod:
            apply_pipeline(mod, PipelineOptions(string_encrypt=True, verify_each=True, seed=1))
            assert mod.verify(), mod.get_verification_error()
            text = mod.to_string()
            assert "__ollvm_str_enc_0" in text
            assert "__ollvm_str_key_0" in text

    bmi_ir = r'''
    define i32 @f(i32 %x, i32 %y) #0 {
    entry:
      %a = and i32 %x, 255
      %b = xor i32 %a, %y
      ret i32 %b
    }
    attributes #0 = { "target-features"="+bmi,+bmi2" }
    '''

    with llvm.create_context() as ctx:
        with ctx.parse_ir(bmi_ir) as mod:
            apply_pipeline(mod, PipelineOptions(bmi_mutate=True, verify_each=True, seed=7))
            assert mod.verify(), mod.get_verification_error()
            text = mod.to_string()
            assert "@llvm.x86.bmi.bzhi.32(" in text
            assert ".i32(" not in text

    misc_ir = r'''
    define internal i32 @helper(i32 %x, i32 %y) {
    entry:
      %tmp = add i32 %x, %y
      ret i32 %tmp
    }

    define i32 @main(i32 %n) {
    entry:
      %s = alloca i32
      store i32 %n, ptr %s
      %lv = load i32, ptr %s
      %cmp = icmp sgt i32 %lv, 0
      br i1 %cmp, label %body, label %else
    body:
      %a = add i32 %lv, 1
      %b = mul i32 %a, 2
      %c = add i32 %b, 3
      br label %merge
    else:
      %d = call i32 @helper(i32 %lv, i32 9)
      br label %merge
    merge:
      %phi = phi i32 [ %c, %body ], [ %d, %else ]
      %res = add i32 %phi, 5
      ret i32 %res
    }
    '''

    with llvm.create_context() as ctx:
        with ctx.parse_ir(misc_ir) as mod:
            apply_pipeline(
                mod,
                PipelineOptions(
                    outline_functions=True,
                    stack_randomize=True,
                    arith_encode=True,
                    reg_pressure=True,
                    vectorize=True,
                    verify_each=True,
                    seed=123,
                ),
            )
            assert mod.verify(), mod.get_verification_error()


def test_ollvm_loop_to_recursion_smoke() -> None:
    ir = r'''
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
    '''

    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir) as mod:
            apply_pipeline(mod, PipelineOptions(loop_to_recursion=True, verify_each=True, seed=7))
            text = mod.to_string()
            assert mod.verify(), mod.get_verification_error()
            assert "musttail call" in text
            assert "@sum_loop.recur" in text



def test_ollvm_loop_to_recursion_skips_multi_exit_loop() -> None:
    ir = r'''
    define i32 @f(i32 %n) {
    entry:
      br label %header
    header:
      %i = phi i32 [ 0, %entry ], [ %next, %latch ]
      %cmp0 = icmp slt i32 %i, %n
      br i1 %cmp0, label %latch, label %exit
    latch:
      %next = add i32 %i, 1
      %cmp1 = icmp slt i32 %next, %n
      br i1 %cmp1, label %header, label %exit
    exit:
      %r = phi i32 [ %i, %header ], [ %next, %latch ]
      ret i32 %r
    }
    '''

    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir) as mod:
            before = mod.to_string()
            apply_pipeline(mod, PipelineOptions(loop_to_recursion=True, verify_each=True, seed=7))
            after = mod.to_string()
            assert mod.verify(), mod.get_verification_error()
            assert "musttail call" not in after
            assert "@f.recur" not in after
            assert after == before


def test_ollvm_cli_vectorize_no_longer_reports_unsupported(tmp_path: Path) -> None:
    input_path = tmp_path / "input.ll"
    input_path.write_text(
        """
        define i32 @f(i32 %x, i32 %y) {
        entry:
          %a = add i32 %x, %y
          ret i32 %a
        }
        """,
        encoding="utf-8",
    )
    assert main(["--vectorize", str(input_path)]) == 0


if __name__ == "__main__":
    import pytest

    raise SystemExit(pytest.main([__file__]))
