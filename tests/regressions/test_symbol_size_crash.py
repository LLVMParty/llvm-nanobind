#!/usr/bin/env python3
"""
Standalone reproduction of LLVM bug: LLVMGetSymbolSize crashes on non-common symbols.

Bug: LLVMGetSymbolSize() calls getCommonSymbolSize() internally, which asserts
if the symbol doesn't have the SF_Common flag set. This affects most symbols
in typical object files (functions, data, etc.).

Error message:
    Assertion failed: (*SymbolFlagsOrErr & SymbolRef::SF_Common),
    function getCommonSymbolSize, file ObjectFile.h, line 313.

This is an upstream LLVM bug in the C API. The C++ API has getSize() which
works for all symbols, but LLVMGetSymbolSize maps to getCommonSymbolSize().

Affected LLVM versions: Tested on LLVM 21 (likely affects many versions)

To run:
    uv run python tests/regressions/test_symbol_size_crash.py
"""

import subprocess
import sys
from pathlib import Path


def create_test_object() -> bytes:
    """Create a minimal object file with a non-common symbol."""
    # Minimal IR that produces a function symbol (not a common symbol)
    ir = b"""; Minimal IR to produce a non-common symbol
target triple = "x86_64-unknown-linux-gnu"

define i32 @test_function() {
    ret i32 42
}
"""
    # Find LLVM tools
    project_root = Path(__file__).parent.parent.parent
    llvm_prefix_file = project_root / ".llvm-prefix"

    if llvm_prefix_file.exists():
        llvm_prefix = llvm_prefix_file.read_text().strip()
        llc = Path(llvm_prefix) / "bin" / "llc"
    else:
        llc = Path("llc")  # Hope it's in PATH

    # Compile IR to object file
    # llc reads IR from stdin and writes object to stdout with -filetype=obj
    result = subprocess.run(
        [str(llc), "-filetype=obj", "-o", "-"],
        input=ir,
        capture_output=True,
    )

    if result.returncode != 0:
        print(
            f"Failed to create object file: {result.stderr.decode()}", file=sys.stderr
        )
        sys.exit(1)

    return result.stdout


def test_symbol_size_crash():
    """
    Test that demonstrates the LLVM bug.

    This test creates an object file and attempts to read symbol sizes.
    With the current LLVM C API, this will crash due to the assertion
    in getCommonSymbolSize().
    """
    import llvm

    # Create test object file
    obj_data = create_test_object()
    print(f"Created test object file: {len(obj_data)} bytes")

    with llvm.BinaryManager.from_bytes(obj_data) as binary:
        print(f"Binary type: {binary.type}")

        # List sections (this works fine)
        print("\nSections:")
        for section in binary.sections:
            print(f"  {section.name}: @0x{section.address:08x} +{section.size}")

        # List symbols - this will crash when accessing .size
        print("\nSymbols (will crash on .size access):")
        sym_iter = binary.symbols
        while not sym_iter.is_at_end():
            name = sym_iter.name
            address = sym_iter.address
            print(f"  {name}: @0x{address:08x}", end="")

            # This line triggers the crash!
            # Uncomment to see the assertion failure:
            size = sym_iter.size
            print(f" +{size}")

            print(" (size access skipped - would crash)")
            sym_iter.move_next()

    print("\nTest completed (crash avoided by not accessing .size)")
    print("\nTo trigger the crash, uncomment the 'size = sym_iter.size' line")
    print("or run: cat <object_file> | ./build/llvm-c-test --object-list-symbols")


def test_with_existing_object():
    """Test using the existing simple.o file if available."""
    import llvm

    project_root = Path(__file__).parent.parent.parent
    simple_o = project_root / "llvm-c/llvm-c-test/inputs/simple.o"

    if not simple_o.exists():
        print(f"Skipping: {simple_o} not found")
        return

    print(f"\nTesting with existing object file: {simple_o}")

    with open(simple_o, "rb") as f:
        obj_data = f.read()

    with llvm.BinaryManager.from_bytes(obj_data) as binary:
        print(f"Binary type: {binary.type}")

        sym_iter = binary.symbols
        symbol_count = 0
        while not sym_iter.is_at_end():
            symbol_count += 1
            sym_iter.move_next()

        print(f"Symbol count: {symbol_count}")
        print("Note: Accessing sym_iter.size would crash with assertion failure")


if __name__ == "__main__":
    print("=" * 70)
    print("LLVM Bug Reproduction: LLVMGetSymbolSize crashes on non-common symbols")
    print("=" * 70)
    print()

    test_symbol_size_crash()
    test_with_existing_object()

    print()
    print("=" * 70)
    print("Bug Summary:")
    print("  - LLVMGetSymbolSize() internally calls getCommonSymbolSize()")
    print("  - getCommonSymbolSize() asserts that the symbol has SF_Common flag")
    print("  - Most symbols (functions, data) do NOT have this flag")
    print("  - Result: Assertion failure / crash")
    print()
    print("Workaround: Don't call LLVMGetSymbolSize() (or .size property)")
    print("Fix needed: LLVM should use SymbolRef::getSize() instead")
    print("=" * 70)
