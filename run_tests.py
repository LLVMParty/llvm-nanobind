#!/usr/bin/env python3
"""
Golden master test runner for llvm-nanobind.

Runs C++ test executables from build/ directory and saves their stdout
as golden master outputs in tests/output/. Then runs corresponding Python
tests (if they exist) and compares their output to the C++ golden master.

Each test outputs valid LLVM IR with diagnostic comments to stdout.
The outputs must be deterministic (no timestamps, etc.) for comparison.
"""

import os
import subprocess
import sys
from pathlib import Path

BUILD_DIR = Path("build")
OUTPUT_DIR = Path("tests/output")

# List of expected C++ test executables
TESTS = [
    "test_context",
    "test_module",
    "test_types",
    "test_function",
    "test_basic_block",
    "test_builder_arithmetic",
    "test_builder_memory",
    "test_builder_control_flow",
    "test_builder_casts",
    "test_builder_cmp",
    "test_constants",
    "test_globals",
    "test_phi",
    "test_factorial",
    "test_struct",
]

# Mapping of C++ test names to Python test files (only tests with Python equivalents)
PYTHON_TESTS = {
    "test_context": Path("test_context.py"),
    "test_module": Path("test_module.py"),
    "test_types": Path("test_types.py"),
    "test_function": Path("test_function.py"),
    "test_basic_block": Path("test_basic_block.py"),
    "test_builder_arithmetic": Path("test_builder_arithmetic.py"),
    "test_builder_memory": Path("test_builder_memory.py"),
    "test_builder_control_flow": Path("test_builder_control_flow.py"),
    "test_builder_casts": Path("test_builder_casts.py"),
    "test_builder_cmp": Path("test_builder_cmp.py"),
    "test_constants": Path("test_constants.py"),
    "test_globals": Path("test_globals.py"),
    "test_phi": Path("test_phi.py"),
    "test_factorial": Path("test_factorial.py"),
    "test_struct": Path("test_struct.py"),
}


def run_cpp_test(name: str) -> tuple[str, str, int]:
    """Run a C++ test executable and return (stdout, stderr, returncode)."""
    exe = BUILD_DIR / name
    if sys.platform == "win32":
        exe = exe.with_suffix(".exe")

    if not exe.exists():
        return "", f"Executable not found: {exe}", -1

    result = subprocess.run(
        [str(exe)],
        capture_output=True,
    )
    # Decode with error handling for any binary data in output
    try:
        stdout = result.stdout.decode("utf-8")
    except UnicodeDecodeError:
        stdout = result.stdout.decode("utf-8", errors="replace")
    try:
        stderr = result.stderr.decode("utf-8")
    except UnicodeDecodeError:
        stderr = result.stderr.decode("utf-8", errors="replace")

    return stdout, stderr, result.returncode


def run_python_test(script: Path) -> tuple[str, str, int]:
    """Run a Python test script and return (stdout, stderr, returncode)."""
    if not script.exists():
        return "", f"Python test not found: {script}", -1

    result = subprocess.run(
        [sys.executable, str(script)],
        capture_output=True,
        env={**os.environ, "PYTHONPATH": str(BUILD_DIR)},
    )
    try:
        stdout = result.stdout.decode("utf-8")
    except UnicodeDecodeError:
        stdout = result.stdout.decode("utf-8", errors="replace")
    try:
        stderr = result.stderr.decode("utf-8")
    except UnicodeDecodeError:
        stderr = result.stderr.decode("utf-8", errors="replace")

    return stdout, stderr, result.returncode


def save_output(name: str, stdout: str):
    """Save test output to file (raw stdout only, no headers)."""
    output_file = OUTPUT_DIR / f"{name}.ll"
    with open(output_file, "w") as f:
        f.write(stdout)


def main():
    if not BUILD_DIR.exists():
        print(f"Build directory '{BUILD_DIR}' not found.")
        print("Run: cmake -B build -G Ninja && cmake --build build")
        sys.exit(1)

    # Create output directory
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

    cpp_passed = 0
    cpp_failed = 0
    cpp_results = []

    py_passed = 0
    py_failed = 0
    py_skipped = 0
    py_results = []

    # Run C++ tests
    print("=" * 60)
    print("Running C++ tests (golden masters)")
    print("=" * 60)

    for test in TESTS:
        stdout, stderr, code = run_cpp_test(test)

        if code == 0:
            status = "PASS"
            cpp_passed += 1
        else:
            status = "FAIL"
            cpp_failed += 1

        cpp_results.append((test, status, code, stdout))
        save_output(test, stdout)

        # Print progress
        print(f"[{status}] {test}")
        if code != 0 and stderr:
            for line in stderr.splitlines()[:5]:  # First 5 lines of error
                print(f"       {line}")

    print()
    print("=" * 60)
    print("Running Python tests and comparing to golden masters")
    print("=" * 60)

    # Run Python tests and compare
    for test in TESTS:
        if test not in PYTHON_TESTS:
            py_skipped += 1
            continue

        script = PYTHON_TESTS[test]
        py_stdout, py_stderr, py_code = run_python_test(script)

        # Find the corresponding C++ output
        cpp_stdout = None
        for t, s, c, out in cpp_results:
            if t == test:
                cpp_stdout = out
                break

        if py_code != 0:
            status = "FAIL"
            py_failed += 1
            reason = "non-zero exit"
        elif cpp_stdout is None:
            status = "FAIL"
            py_failed += 1
            reason = "no C++ output to compare"
        elif py_stdout != cpp_stdout:
            status = "FAIL"
            py_failed += 1
            reason = "output differs from C++"
        else:
            status = "PASS"
            py_passed += 1
            reason = ""

        py_results.append((test, status, reason))

        # Print progress
        if reason:
            print(f"[{status}] {test} ({reason})")
        else:
            print(f"[{status}] {test}")

        if status == "FAIL" and py_stderr:
            for line in py_stderr.splitlines()[:5]:
                print(f"       {line}")

        # Show diff if output differs
        if status == "FAIL" and reason == "output differs from C++" and cpp_stdout:
            print("       --- First difference ---")
            cpp_lines = cpp_stdout.splitlines()
            py_lines = py_stdout.splitlines()
            for i, (cpp_line, py_line) in enumerate(zip(cpp_lines, py_lines)):
                if cpp_line != py_line:
                    print(f"       Line {i + 1}:")
                    print(f"       C++: {cpp_line[:60]}")
                    print(f"       Py:  {py_line[:60]}")
                    break
            else:
                if len(cpp_lines) != len(py_lines):
                    print(
                        f"       Line count: C++={len(cpp_lines)}, Py={len(py_lines)}"
                    )

    # Summary
    print()
    print("=" * 60)
    print("Summary")
    print("=" * 60)
    print()
    print(f"C++ tests:    {cpp_passed} passed, {cpp_failed} failed out of {len(TESTS)}")
    print(
        f"Python tests: {py_passed} passed, {py_failed} failed, {py_skipped} skipped out of {len(TESTS)}"
    )
    print(f"Golden masters saved to: {OUTPUT_DIR}/")
    print()

    # C++ results table
    print("C++ Test Results:")
    print("-" * 50)
    print(f"{'Test':<30} {'Status':<6} {'Exit Code'}")
    print("-" * 50)
    for test, status, code, _ in cpp_results:
        print(f"{test:<30} {status:<6} {code}")

    # Python results table (only if there are any)
    if py_results:
        print()
        print("Python Test Results:")
        print("-" * 50)
        print(f"{'Test':<30} {'Status':<6} {'Notes'}")
        print("-" * 50)
        for test, status, reason in py_results:
            print(f"{test:<30} {status:<6} {reason}")

    # Exit with failure if any test failed
    if cpp_failed > 0 or py_failed > 0:
        sys.exit(1)
    sys.exit(0)


if __name__ == "__main__":
    main()
