"""Second-tier semantic tests for ``tools/obfuscation/ollvm_obf.py``.

Unlike the canonical golden-master suite, these tests do not require the
transformed IR text to match. They compare runtime behavior of:

- the baseline input IR
- the Python/nanobind obfuscator output

This is useful for passes where IR shape legitimately differs while semantics
should still be preserved.
"""

from __future__ import annotations

from dataclasses import dataclass
from functools import lru_cache
from pathlib import Path
import shutil
import subprocess
import textwrap

import pytest

from tools.obfuscation.ollvm_obf import main as python_ollvm_obf


@dataclass(frozen=True, slots=True)
class SemanticCase:
    name: str
    args: tuple[str, ...]
    input_ir: str
    driver_c: str
    clang_flags: tuple[str, ...] = ()


SEMANTIC_CASES = [
    SemanticCase(
        name="substitute/value-mixing",
        args=("--substitute", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x, i32 %y) {
        entry:
          %a = add i32 %x, %y
          %b = xor i32 %a, 7
          %c = or i32 %b, 3
          %d = and i32 %c, 255
          ret i32 %d
        }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int, int);
        int main(void) {
          int xs[][2] = {{0, 0}, {1, 2}, {-3, 9}, {123, 77}, {-55, -13}};
          for (int i = 0; i < 5; ++i)
            printf("%d\n", f(xs[i][0], xs[i][1]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="const-unfold/add-xor-const",
        args=("--const-unfold", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x) {
        entry:
          %a = add i32 %x, 42
          %b = xor i32 %a, 99
          ret i32 %b
        }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int);
        int main(void) {
          int xs[] = {0, 1, -1, 77, 12345};
          for (int i = 0; i < 5; ++i)
            printf("%d\n", f(xs[i]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="string-encrypt/string-sum",
        args=("--string-encrypt", "--seed=7"),
        input_ir=r'''
        @.str = private constant [6 x i8] c"hello\00"
        define i32 @f() {
        entry:
          %p0 = getelementptr [6 x i8], ptr @.str, i32 0, i32 0
          %c0 = load i8, ptr %p0
          %p1 = getelementptr i8, ptr %p0, i32 1
          %c1 = load i8, ptr %p1
          %p2 = getelementptr i8, ptr %p0, i32 2
          %c2 = load i8, ptr %p2
          %z0 = zext i8 %c0 to i32
          %z1 = zext i8 %c1 to i32
          %z2 = zext i8 %c2 to i32
          %s0 = add i32 %z0, %z1
          %s1 = add i32 %s0, %z2
          ret i32 %s1
        }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(void);
        int main(void) {
          printf("%d\n", f());
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="flatten/diamond",
        args=("--flatten", "--seed=7"),
        input_ir=r'''
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
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int);
        int main(void) {
          int xs[] = {0, 1, -1, 77};
          for (int i = 0; i < 4; ++i)
            printf("%d\n", f(xs[i]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="flatten+opaque+bogus/diamond",
        args=("--flatten", "--opaque-predicates", "--bogus-control-flow", "--seed=7"),
        input_ir=r'''
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
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int);
        int main(void) {
          int xs[] = {0, 1, -1, 77};
          for (int i = 0; i < 4; ++i)
            printf("%d\n", f(xs[i]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="loop-to-recursion/simple-counter",
        args=("--loop-to-recursion", "--seed=7"),
        input_ir=r'''
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
        ''',
        driver_c=r'''
        #include <stdio.h>
        #include <inttypes.h>
        extern long long sum_loop(long long);
        int main(void) {
          long long xs[] = {0, 1, 2, 5, 9};
          for (int i = 0; i < 5; ++i)
            printf("%lld\n", sum_loop(xs[i]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="bmi-mutate/i32-and-mask-xor",
        args=("--bmi-mutate", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x, i32 %y) #0 {
        entry:
          %a = and i32 %x, 255
          %b = xor i32 %a, %y
          ret i32 %b
        }
        attributes #0 = { "target-features"="+bmi,+bmi2" }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int, int);
        int main(void) {
          int xs[][2] = {{0, 0}, {1, 2}, {-3, 9}, {123, 77}, {-55, -13}};
          for (int i = 0; i < 5; ++i)
            printf("%d\n", f(xs[i][0], xs[i][1]));
          return 0;
        }
        ''',
        clang_flags=("-mbmi", "-mbmi2"),
    ),
    SemanticCase(
        name="bmi-mutate/i64-and-mask-xor",
        args=("--bmi-mutate", "--seed=7"),
        input_ir=r'''
        define i64 @f(i64 %x, i64 %y) #0 {
        entry:
          %a = and i64 %x, 255
          %b = xor i64 %a, %y
          ret i64 %b
        }
        attributes #0 = { "target-features"="+bmi,+bmi2" }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern long long f(long long, long long);
        int main(void) {
          long long xs[][2] = {{0, 0}, {1, 2}, {-3, 9}, {123, 77}, {-55, -13}};
          for (int i = 0; i < 5; ++i)
            printf("%lld\n", f(xs[i][0], xs[i][1]));
          return 0;
        }
        ''',
        clang_flags=("-mbmi", "-mbmi2"),
    ),
    SemanticCase(
        name="vectorize/stack-and-arith",
        args=("--vectorize", "--vectorize-percent=100", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x, i32 %y) {
        entry:
          %s = alloca i32
          store i32 %x, ptr %s
          %lx = load i32, ptr %s
          %a = add i32 %lx, %y
          %b = mul i32 %a, 3
          ret i32 %b
        }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int, int);
        int main(void) {
          int xs[][2] = {{0, 0}, {1, 2}, {-3, 9}, {123, 77}, {-55, -13}};
          for (int i = 0; i < 5; ++i)
            printf("%d\n", f(xs[i][0], xs[i][1]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="vectorize/i64-lanes",
        args=("--vectorize", "--vectorize-percent=100", "--seed=7"),
        input_ir=r'''
        define i64 @f(i64 %x, i64 %y) {
        entry:
          %a = add i64 %x, %y
          %b = mul i64 %a, 3
          ret i64 %b
        }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern long long f(long long, long long);
        int main(void) {
          long long xs[][2] = {{0, 0}, {1, 2}, {-3, 9}, {123, 77}, {-55, -13}};
          for (int i = 0; i < 5; ++i)
            printf("%lld\n", f(xs[i][0], xs[i][1]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="vectorize/bitwise-lowering",
        args=("--vectorize", "--vectorize-bitwise", "--vectorize-percent=100", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x, i32 %y) {
        entry:
          %a = add i32 %x, %y
          %b = sub i32 %a, 3
          ret i32 %b
        }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int, int);
        int main(void) {
          int xs[][2] = {{0, 0}, {1, 2}, {-3, 9}, {123, 77}, {-55, -13}};
          for (int i = 0; i < 5; ++i)
            printf("%d\n", f(xs[i][0], xs[i][1]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="outline+arith+stack/frame-munging",
        args=("--outline-functions", "--arith-encode", "--stack-randomize", "--seed=123"),
        input_ir=r'''
        define i32 @f(i32 %x) {
        entry:
          %s = alloca i32
          store i32 %x, ptr %s
          br label %body
        body:
          %v = load i32, ptr %s
          %a = add i32 %v, 1
          %b = mul i32 %a, 2
          %c = add i32 %b, 3
          br label %exit
        exit:
          %r = add i32 %c, 4
          ret i32 %r
        }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int);
        int main(void) {
          int xs[] = {0, 1, -1, 11};
          for (int i = 0; i < 4; ++i)
            printf("%d\n", f(xs[i]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="reg-pressure/live-range-anchor",
        args=("--reg-pressure", "--seed=7"),
        input_ir=r'''
        define i32 @f(i32 %x) {
        entry:
          br label %left
        left:
          %a = add i32 %x, 1
          br label %merge
        merge:
          %r = add i32 %a, %x
          ret i32 %r
        }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int f(int);
        int main(void) {
          int xs[] = {0, 1, -1, 11};
          for (int i = 0; i < 4; ++i)
            printf("%d\n", f(xs[i]));
          return 0;
        }
        ''',
    ),
    SemanticCase(
        name="full-public-pipeline/mixed-module",
        args=(
            "--string-encrypt",
            "--code-clone",
            "--substitute",
            "--if-convert",
            "--loop-to-recursion",
            "--flatten",
            "--opaque-predicates",
            "--bogus-control-flow",
            "--bmi-mutate",
            "--const-unfold",
            "--schedule-instructions",
            "--outline-functions",
            "--arith-encode",
            "--stack-randomize",
            "--vectorize",
            "--vectorize-percent=100",
            "--reg-pressure",
            "--seed=123",
            "--verify-each",
        ),
        input_ir=r'''
        @.str = private constant [6 x i8] c"hello\00"

        define internal i32 @helper(i32 %x, i32 %y) #0 {
        entry:
          %a = and i32 %x, 255
          %b = xor i32 %a, %y
          %c = add i32 %b, 3
          %d = mul i32 %c, 2
          ret i32 %d
        }

        define internal i64 @sum_loop(i64 %n) {
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

        define i32 @entry_fn(i32 %n, i32 %m) #0 {
        entry:
          %slot = alloca i32
          store i32 %n, ptr %slot
          %lv = load i32, ptr %slot
          %cmp = icmp sgt i32 %lv, %m
          br i1 %cmp, label %then, label %else
        then:
          %p0 = getelementptr [6 x i8], ptr @.str, i32 0, i32 0
          %c0 = load i8, ptr %p0
          %z0 = zext i8 %c0 to i32
          %h1 = call i32 @helper(i32 %lv, i32 %z0)
          br label %merge
        else:
          %loop = call i64 @sum_loop(i64 5)
          %loop32 = trunc i64 %loop to i32
          %h2 = call i32 @helper(i32 %m, i32 %loop32)
          br label %merge
        merge:
          %phi = phi i32 [ %h1, %then ], [ %h2, %else ]
          %r = add i32 %phi, 1
          ret i32 %r
        }

        attributes #0 = { "target-features"="+bmi,+bmi2" }
        ''',
        driver_c=r'''
        #include <stdio.h>
        extern int entry_fn(int, int);
        int main(void) {
          int xs[][2] = {{0, 0}, {1, 2}, {-3, 9}, {123, 77}, {-55, -13}, {9, 1}};
          for (int i = 0; i < 6; ++i)
            printf("%d\n", entry_fn(xs[i][0], xs[i][1]));
          return 0;
        }
        ''',
        clang_flags=("-mbmi", "-mbmi2"),
    ),
]


@lru_cache(maxsize=1)
def find_clang() -> Path | None:
    for name in ("clang", "clang.exe"):
        path = shutil.which(name)
        if path:
            return Path(path)
    return None


@pytest.fixture(scope="module")
def clang_exe() -> Path:
    clang = find_clang()
    if clang is None:
        pytest.skip("clang not found in PATH; semantic parity tests require an external compiler")
    return clang


def run_python_tool(args: tuple[str, ...], input_path: Path, output_path: Path) -> None:
    rc = python_ollvm_obf([*args, str(input_path), "-o", str(output_path)])
    assert rc == 0


def compile_and_run(
    clang_exe: Path,
    ir_path: Path,
    driver_path: Path,
    exe_path: Path,
    extra_flags: tuple[str, ...],
) -> str:
    build = subprocess.run(
        [
            str(clang_exe),
            "-O0",
            "-Wno-override-module",
            *extra_flags,
            str(ir_path),
            str(driver_path),
            "-o",
            str(exe_path),
        ],
        capture_output=True,
        text=True,
    )
    assert build.returncode == 0, build.stderr

    run = subprocess.run([str(exe_path)], capture_output=True, text=True)
    assert run.returncode == 0, run.stderr
    return run.stdout


@pytest.mark.parametrize("case", SEMANTIC_CASES, ids=lambda case: case.name)
def test_ollvm_obf_semantic_preservation(
    clang_exe: Path, tmp_path: Path, case: SemanticCase
) -> None:
    input_path = tmp_path / "input.ll"
    driver_path = tmp_path / "driver.c"
    input_path.write_text(textwrap.dedent(case.input_ir).strip() + "\n", encoding="utf-8")
    driver_path.write_text(textwrap.dedent(case.driver_c).strip() + "\n", encoding="utf-8")

    py_ir = tmp_path / "py.ll"
    run_python_tool(case.args, input_path, py_ir)

    baseline_out = compile_and_run(
        clang_exe, input_path, driver_path, tmp_path / "baseline.exe", case.clang_flags
    )
    py_out = compile_and_run(
        clang_exe, py_ir, driver_path, tmp_path / "python.exe", case.clang_flags
    )

    assert py_out == baseline_out


def test_ollvm_obf_loop_to_recursion_rejects_multi_exit_case(
    clang_exe: Path, tmp_path: Path
) -> None:
    input_path = tmp_path / "input.ll"
    driver_path = tmp_path / "driver.c"
    input_path.write_text(
        textwrap.dedent(
            r'''
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
        ).strip()
        + "\n",
        encoding="utf-8",
    )
    driver_path.write_text(
        textwrap.dedent(
            r'''
            #include <stdio.h>
            extern int f(int);
            int main(void) {
              int xs[] = {0, 1, 2, 5, 9};
              for (int i = 0; i < 5; ++i)
                printf("%d\n", f(xs[i]));
              return 0;
            }
            '''
        ).strip()
        + "\n",
        encoding="utf-8",
    )

    py_ir = tmp_path / "py.ll"
    args = ("--loop-to-recursion", "--seed=7")
    run_python_tool(args, input_path, py_ir)

    assert ".recur" not in py_ir.read_text(encoding="utf-8")

    baseline_out = compile_and_run(clang_exe, input_path, driver_path, tmp_path / "baseline.exe", ())
    py_out = compile_and_run(clang_exe, py_ir, driver_path, tmp_path / "python.exe", ())

    assert py_out == baseline_out


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__]))
