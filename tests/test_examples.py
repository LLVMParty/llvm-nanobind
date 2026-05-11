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
    assert "define i32 @add_one(i32 %x)" in output
    assert "%sum.repl = sub i32 %x, -1" in output
    assert "ret i32 %sum.repl" in output
    assert " add i32 " not in output
