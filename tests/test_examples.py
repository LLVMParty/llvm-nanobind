"""Smoke tests for public example scripts.

These keep README-linked examples honest as the bindings evolve.
"""

from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


def run_example(path: str) -> str:
    result = subprocess.run(
        [sys.executable, path],
        cwd=PROJECT_ROOT,
        text=True,
        capture_output=True,
        check=True,
    )
    return result.stdout


def assert_quick_start_output(output: str) -> None:
    assert "define i32 @get_answer()" in output
    assert "ret i32 42" in output


def test_quick_start_example() -> None:
    output = run_example("examples/quick_start.py")
    assert_quick_start_output(output)


def test_readme_quick_start_snippet() -> None:
    readme = (PROJECT_ROOT / "README.md").read_text(encoding="utf-8")
    match = re.search(r"```python\n(?P<code>.*?get_answer.*?)\n```", readme, re.DOTALL)
    assert match is not None, "README quick start Python snippet not found"

    result = subprocess.run(
        [sys.executable, "-c", match.group("code")],
        cwd=PROJECT_ROOT,
        text=True,
        capture_output=True,
        check=True,
    )
    assert_quick_start_output(result.stdout)


def test_transform_replace_add_example() -> None:
    output = run_example("examples/transform_replace_add.py")
    assert "define i32 @add_values(i32 %x, i32 %y)" in output
    assert "%sum.rhs.neg = sub i32 0, %y" in output
    assert "%sum.repl = sub i32 %x, %sum.rhs.neg" in output
    assert "ret i32 %sum.repl" in output
    assert " add i32 " not in output


def test_transform_replace_add_preserves_rhs_operand() -> None:
    from examples.transform_replace_add import transform

    output = transform(
        """
        define i32 @f(i32 %x, i32 %y) {
        entry:
          %sum = add i32 %x, %y
          ret i32 %sum
        }
        """
    )
    assert "%sum.rhs.neg = sub i32 0, %y" in output
    assert "%sum.repl = sub i32 %x, %sum.rhs.neg" in output


def test_transform_replace_add_leaves_no_wrap_adds_alone() -> None:
    from examples.transform_replace_add import transform

    output = transform(
        """
        define i32 @f(i32 %x, i32 %y) {
        entry:
          %sum = add nsw i32 %x, %y
          ret i32 %sum
        }
        """
    )
    assert "%sum = add nsw i32 %x, %y" in output


def test_intrinsic_memcpy_example() -> None:
    output = run_example("examples/intrinsic_memcpy.py")
    assert "define void @copy_bytes" in output
    assert "call void @llvm.memcpy.p0.p0.i64" in output


def test_optimize_module_example() -> None:
    output = run_example("examples/optimize_module.py")
    assert "define i32 @add_then_simplify" in output
    assert "ret i32 %x" in output
    assert "alloca" not in output


def test_emit_object_assembly_example() -> None:
    output = run_example("examples/emit_object_assembly.py")
    if output.startswith("skipped:"):
        assert "host code generation is unavailable" in output
        return
    assert "target:" in output
    assert "object bytes:" in output
    assert "binary type:" in output
    assert "assembly preview:" in output


def test_jit_add_example() -> None:
    output = run_example("examples/jit_add.py")
    if output.startswith("skipped:"):
        assert "host JIT is unavailable" in output
        return
    assert "add_i32(40, 2) = 42" in output
    assert "call_python(5) = 15" in output
