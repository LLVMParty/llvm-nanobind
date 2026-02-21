#!/usr/bin/env -S uv run
"""
Regression test for DIBuilder metadata ID assignment.

The C version produces function debug info with `!dbg !44`, while the Python
version produces `!dbg !45`. This indicates a difference in the order of
metadata creation between the two implementations.

Expected (C):
    define i64 @foo(i64 %0, i64 %1, <10 x i64> %2) !dbg !44 {

Actual (Python):
    define i64 @foo(i64 %0, i64 %1, <10 x i64> %2) !dbg !45 {

Root cause: The metadata IDs are assigned in order of creation. A difference
of 1 suggests one extra metadata node is being created before the function's
debug info in the Python version, OR the order of creating certain metadata
is different.

This is a subtle bug that affects test output but may not affect functionality.
However, it indicates the Python implementation doesn't exactly replicate the
C implementation's metadata creation order.

Affects lit tests:
- debug_info_new_format.ll

Fix: Compare the C and Python debuginfo.py implementations carefully to find
where the extra metadata is being created or where order differs. The C test
is in llvm-c/llvm-c-test/debuginfo.c.
"""

import subprocess
import sys


def get_function_dbg_id(output: str) -> str | None:
    """Extract the !dbg ID from the function definition line."""
    for line in output.split("\n"):
        if line.startswith("define i64 @foo("):
            # Find !dbg !XX
            if "!dbg !" in line:
                start = line.index("!dbg !") + len("!dbg !")
                end = start
                while end < len(line) and line[end].isdigit():
                    end += 1
                return line[start:end]
    return None


def run_python_dibuilder():
    """Run Python --test-dibuilder and capture output."""
    result = subprocess.run(
        ["uv", "run", "llvm-c-test", "--test-dibuilder"],
        capture_output=True,
        text=True,
    )
    return result.stdout


def run_c_dibuilder():
    """Run C --test-dibuilder and capture output."""
    result = subprocess.run(
        ["./build/llvm-c-test", "--test-dibuilder"],
        capture_output=True,
        text=True,
    )
    return result.stdout


def test_dibuilder_metadata_ids():
    """Compare metadata IDs between C and Python implementations."""
    print("Running Python DIBuilder test...")
    py_output = run_python_dibuilder()
    py_id = get_function_dbg_id(py_output)
    print(f"Python function !dbg ID: !{py_id}")

    print("\nRunning C DIBuilder test...")
    c_output = run_c_dibuilder()
    c_id = get_function_dbg_id(c_output)
    print(f"C function !dbg ID: !{c_id}")

    if py_id == c_id:
        print("\nPASS: Metadata IDs match!")
        return True
    else:
        print(f"\nFAIL: Metadata IDs differ: Python !{py_id} vs C !{c_id}")
        diff = abs(int(py_id or 0) - int(c_id or 0))
        print(f"  Difference: {diff} metadata node(s)")
        return False


def test_subprogram_metadata_location():
    """Show where the DISubprogram metadata appears in the output."""
    print("\nAnalyzing Python DIBuilder metadata structure...")
    py_output = run_python_dibuilder()

    # Find DISubprogram for foo
    for i, line in enumerate(py_output.split("\n")):
        line = line.strip()
        if "DISubprogram" in line and 'name: "foo"' in line:
            print(f"DISubprogram for 'foo' at line {i + 1}:")
            print(f"  {line[:100]}...")
            # Extract the metadata ID
            if line.startswith("!"):
                meta_id = line.split()[0]
                print(f"  Metadata ID: {meta_id}")

    print("\nAnalyzing C DIBuilder metadata structure...")
    c_output = run_c_dibuilder()

    for i, line in enumerate(c_output.split("\n")):
        line = line.strip()
        if "DISubprogram" in line and 'name: "foo"' in line:
            print(f"DISubprogram for 'foo' at line {i + 1}:")
            print(f"  {line[:100]}...")
            if line.startswith("!"):
                meta_id = line.split()[0]
                print(f"  Metadata ID: {meta_id}")


def count_metadata_before_subprogram():
    """Count metadata nodes before the DISubprogram to identify the difference."""
    print("\nCounting metadata nodes before DISubprogram...")

    py_output = run_python_dibuilder()
    c_output = run_c_dibuilder()

    def count_before_subprogram(output: str) -> int:
        count = 0
        for line in output.split("\n"):
            line = line.strip()
            if line.startswith("!") and "=" in line:
                if "DISubprogram" in line and 'name: "foo"' in line:
                    return count
                count += 1
        return count

    py_count = count_before_subprogram(py_output)
    c_count = count_before_subprogram(c_output)

    print(f"Python: {py_count} metadata nodes before DISubprogram")
    print(f"C:      {c_count} metadata nodes before DISubprogram")
    print(f"Diff:   {py_count - c_count}")


if __name__ == "__main__":
    print("=" * 60)
    print("DIBuilder Metadata ID Regression Test")
    print("=" * 60)
    print()

    ids_match = test_dibuilder_metadata_ids()
    test_subprogram_metadata_location()
    count_metadata_before_subprogram()

    print()
    print("=" * 60)
    if ids_match:
        print("RESULT: Metadata IDs match - test passes")
    else:
        print("RESULT: Metadata IDs differ - needs investigation")
        print()
        print("To fix debug_info_new_format.ll, the Python implementation")
        print("needs to create metadata in the same order as the C version.")
    print("=" * 60)

    sys.exit(0 if ids_match else 1)
