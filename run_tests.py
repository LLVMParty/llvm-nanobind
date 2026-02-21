#!/usr/bin/env -S uv run
"""
Golden master test runner for llvm-nanobind.

Runs C++ test executables from build/ directory and saves their stdout
as golden master outputs in tests/output/. Then runs corresponding Python
tests (if they exist) and compares their output to the C++ golden master.

Each test outputs valid LLVM IR with diagnostic comments to stdout.
The outputs must be deterministic (no timestamps, etc.) for comparison.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

BUILD_DIR = Path("build")
OUTPUT_DIR = Path("tests/output")
REGRESSIONS_DIR = Path("tests/regressions")


def coverage_wrap(name: str, args: list[str]) -> list[str]:
    """Wrap command with coverage if COVERAGE_RUN environment variable is set."""
    if os.environ.get("COVERAGE_RUN"):
        return ["-m", "coverage", "run", f"--data-file=.coverage.{name}"] + args
    return args


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
    "test_predecessors",
    "test_call_conv",
]

# Mapping of C++ test names to Python test files (only tests with Python equivalents)
PYTHON_TESTS = {
    "test_context": Path("tests/test_context.py"),
    "test_module": Path("tests/test_module.py"),
    "test_types": Path("tests/test_types.py"),
    "test_function": Path("tests/test_function.py"),
    "test_basic_block": Path("tests/test_basic_block.py"),
    "test_builder_arithmetic": Path("tests/test_builder_arithmetic.py"),
    "test_builder_memory": Path("tests/test_builder_memory.py"),
    "test_builder_control_flow": Path("tests/test_builder_control_flow.py"),
    "test_builder_casts": Path("tests/test_builder_casts.py"),
    "test_builder_cmp": Path("tests/test_builder_cmp.py"),
    "test_constants": Path("tests/test_constants.py"),
    "test_globals": Path("tests/test_globals.py"),
    "test_phi": Path("tests/test_phi.py"),
    "test_factorial": Path("tests/test_factorial.py"),
    "test_struct": Path("tests/test_struct.py"),
    "test_predecessors": Path("tests/test_predecessors.py"),
    "test_call_conv": Path("tests/test_call_conv.py"),
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

    # Build command with optional coverage wrapper
    cmd = coverage_wrap(script.stem, [str(script)])

    result = subprocess.run(
        [sys.executable] + cmd,
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


def run_regression_tests():
    """Run all Python regression tests from tests/regressions/."""
    if not REGRESSIONS_DIR.exists():
        print(f"Regression tests directory '{REGRESSIONS_DIR}' not found.")
        sys.exit(1)

    # Find all Python regression tests
    regression_tests = sorted(REGRESSIONS_DIR.glob("test_*.py"))

    if not regression_tests:
        print(f"No regression tests found in {REGRESSIONS_DIR}")
        sys.exit(1)

    print("=" * 60)
    print(f"Running {len(regression_tests)} regression tests")
    print("=" * 60)
    print()

    passed = 0
    failed = 0
    results = []

    for test_script in regression_tests:
        test_name = test_script.stem

        stdout, stderr, code = run_python_test(test_script)

        if code == 0:
            status = "PASS"
            passed += 1
        else:
            status = "FAIL"
            failed += 1

        results.append((test_name, status, code, stderr))

        # Print progress
        print(f"[{status}] {test_name}")

        # Show error output if failed
        if status == "FAIL" and stderr:
            for line in stderr.splitlines()[:10]:  # First 10 lines of error
                print(f"       {line}")

    # Summary
    print()
    print("=" * 60)
    print("Regression Test Summary")
    print("=" * 60)
    print(f"Passed: {passed}/{len(regression_tests)}")
    print(f"Failed: {failed}/{len(regression_tests)}")
    print()

    # Results table
    print("Results:")
    print("-" * 60)
    print(f"{'Test':<40} {'Status':<6} {'Exit Code'}")
    print("-" * 60)
    for test_name, status, code, _ in results:
        print(f"{test_name:<40} {status:<6} {code}")

    # Exit with failure if any test failed
    if failed > 0:
        sys.exit(1)
    sys.exit(0)


def main():
    parser = argparse.ArgumentParser(description="Run tests for llvm-nanobind bindings")
    parser.add_argument(
        "--regressions",
        action="store_true",
        help="Run regression tests from tests/regressions/",
    )
    args = parser.parse_args()

    if args.regressions:
        run_regression_tests()
        return

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
