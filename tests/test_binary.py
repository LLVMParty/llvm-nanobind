#!/usr/bin/env python3
"""
Tests for the Binary/Object File API.

These tests verify:
1. Basic functionality of BinaryManager.from_bytes and BinaryManager.from_file
2. Memory safety with validity tokens
3. Iterator behavior after binary disposal
4. Error handling
"""

import sys
from pathlib import Path

import llvm


def test_create_binary_from_bytes():
    """Test creating a binary from bytes."""
    # Read the test object file
    obj_path = (
        Path(__file__).parent.parent
        / "llvm-c"
        / "llvm-c-test"
        / "inputs"
        / "Inputs"
        / "simple.o"
    )
    if not obj_path.exists():
        print(f"SKIP: Test object file not found: {obj_path}")
        return

    data = obj_path.read_bytes()

    with llvm.BinaryManager.from_bytes(data) as binary:
        # Check binary type
        assert binary.type == llvm.BinaryType.ELF64L, (
            f"Expected ELF64L, got {binary.type}"
        )

        # Count sections
        section_count = 0
        for sect in binary.sections:
            section_count += 1
            # Access properties to ensure they work
            _ = sect.name
            _ = sect.address
            _ = sect.size

        assert section_count > 0, "Expected at least one section"
        print(f"  Found {section_count} sections")

    print("PASS: test_create_binary_from_bytes")


def test_create_binary_from_file():
    """Test creating a binary from a file path."""
    obj_path = (
        Path(__file__).parent.parent
        / "llvm-c"
        / "llvm-c-test"
        / "inputs"
        / "Inputs"
        / "simple.o"
    )
    if not obj_path.exists():
        print(f"SKIP: Test object file not found: {obj_path}")
        return

    with llvm.BinaryManager.from_file(str(obj_path)) as binary:
        assert binary.type == llvm.BinaryType.ELF64L

        # Iterate sections
        section_names = []
        for sect in binary.sections:
            section_names.append(sect.name)

        assert ".text" in section_names, f"Expected .text section, got {section_names}"
        print(f"  Sections: {section_names}")

    print("PASS: test_create_binary_from_file")


def test_section_iterator_after_binary_disposed():
    """Test that section iterator raises error after binary is disposed."""
    obj_path = (
        Path(__file__).parent.parent
        / "llvm-c"
        / "llvm-c-test"
        / "inputs"
        / "Inputs"
        / "simple.o"
    )
    if not obj_path.exists():
        print(f"SKIP: Test object file not found: {obj_path}")
        return

    data = obj_path.read_bytes()

    # Get iterator inside context, try to use outside
    sect_iter = None
    with llvm.BinaryManager.from_bytes(data) as binary:
        sect_iter = binary.sections
        # Iterator should work inside the context
        assert not sect_iter.is_at_end()
        _ = sect_iter.name

    # After context exit, iterator should raise error
    try:
        _ = sect_iter.name
        assert False, "Expected LLVMMemoryError after binary disposed"
    except llvm.LLVMMemoryError as e:
        assert "disposed" in str(e).lower() or "invalid" in str(e).lower()
        print(f"  Got expected error: {e}")

    print("PASS: test_section_iterator_after_binary_disposed")


def test_symbol_iterator_after_binary_disposed():
    """Test that symbol iterator raises error after binary is disposed."""
    obj_path = (
        Path(__file__).parent.parent
        / "llvm-c"
        / "llvm-c-test"
        / "inputs"
        / "Inputs"
        / "simple.o"
    )
    if not obj_path.exists():
        print(f"SKIP: Test object file not found: {obj_path}")
        return

    data = obj_path.read_bytes()

    sym_iter = None
    with llvm.BinaryManager.from_bytes(data) as binary:
        sym_iter = binary.symbols
        # Don't access it - just get it

    # After context exit, iterator should raise error
    try:
        _ = sym_iter.is_at_end()
        assert False, "Expected LLVMMemoryError after binary disposed"
    except llvm.LLVMMemoryError as e:
        assert "disposed" in str(e).lower() or "invalid" in str(e).lower()
        print(f"  Got expected error: {e}")

    print("PASS: test_symbol_iterator_after_binary_disposed")


def test_invalid_binary_bytes():
    """Test error handling for invalid binary data."""
    try:
        with llvm.BinaryManager.from_bytes(b"not a valid object file") as binary:
            pass
        assert False, "Expected LLVMError for invalid data"
    except llvm.LLVMError as e:
        print(f"  Got expected error: {e}")

    print("PASS: test_invalid_binary_bytes")


def test_nonexistent_file():
    """Test error handling for non-existent file."""
    try:
        with llvm.BinaryManager.from_file("/nonexistent/path/to/file.o") as binary:
            pass
        assert False, "Expected LLVMError for non-existent file"
    except llvm.LLVMError as e:
        print(f"  Got expected error: {e}")

    print("PASS: test_nonexistent_file")


def test_pythonic_iteration():
    """Test that Pythonic for loops work correctly."""
    obj_path = (
        Path(__file__).parent.parent
        / "llvm-c"
        / "llvm-c-test"
        / "inputs"
        / "Inputs"
        / "simple.o"
    )
    if not obj_path.exists():
        print(f"SKIP: Test object file not found: {obj_path}")
        return

    data = obj_path.read_bytes()

    with llvm.BinaryManager.from_bytes(data) as binary:
        # Test that we can iterate multiple times
        count1 = sum(1 for _ in binary.sections)
        count2 = sum(1 for _ in binary.sections)

        # Each call to binary.sections creates a new iterator
        # So both counts should be the same
        assert count1 == count2, f"Expected same count, got {count1} and {count2}"
        print(f"  Section count (both iterations): {count1}")

    print("PASS: test_pythonic_iteration")


def test_section_contents():
    """Test accessing section contents as bytes."""
    obj_path = (
        Path(__file__).parent.parent
        / "llvm-c"
        / "llvm-c-test"
        / "inputs"
        / "Inputs"
        / "simple.o"
    )
    if not obj_path.exists():
        print(f"SKIP: Test object file not found: {obj_path}")
        return

    data = obj_path.read_bytes()

    with llvm.BinaryManager.from_bytes(data) as binary:
        for sect in binary.sections:
            if sect.name == ".text":
                contents = sect.contents
                assert isinstance(contents, bytes)
                assert len(contents) == sect.size
                print(f"  .text section: {sect.size} bytes")
                break

    print("PASS: test_section_contents")


def test_double_enter():
    """Test that entering a binary manager twice raises error."""
    obj_path = (
        Path(__file__).parent.parent
        / "llvm-c"
        / "llvm-c-test"
        / "inputs"
        / "Inputs"
        / "simple.o"
    )
    if not obj_path.exists():
        print(f"SKIP: Test object file not found: {obj_path}")
        return

    data = obj_path.read_bytes()
    manager = llvm.BinaryManager.from_bytes(data)

    with manager as binary:
        pass  # First enter/exit

    # Now the manager is disposed, trying to enter again should fail
    try:
        with manager as binary:
            pass
        assert False, "Expected error on second enter"
    except llvm.LLVMMemoryError as e:
        print(f"  Got expected error: {e}")

    print("PASS: test_double_enter")


def test_dispose_before_enter():
    """Test dispose() without entering the context."""
    obj_path = (
        Path(__file__).parent.parent
        / "llvm-c"
        / "llvm-c-test"
        / "inputs"
        / "Inputs"
        / "simple.o"
    )
    if not obj_path.exists():
        print(f"SKIP: Test object file not found: {obj_path}")
        return

    data = obj_path.read_bytes()
    manager = llvm.BinaryManager.from_bytes(data)

    # Dispose without entering - should work
    manager.dispose()

    # Now try to enter - should fail
    try:
        with manager as binary:
            pass
        assert False, "Expected error after dispose"
    except llvm.LLVMMemoryError as e:
        print(f"  Got expected error: {e}")

    print("PASS: test_dispose_before_enter")


def main():
    """Run all tests."""
    print("Running Binary API tests...")
    print()

    test_create_binary_from_bytes()
    test_create_binary_from_file()
    test_section_iterator_after_binary_disposed()
    test_symbol_iterator_after_binary_disposed()
    test_invalid_binary_bytes()
    test_nonexistent_file()
    test_pythonic_iteration()
    test_section_contents()
    test_double_enter()
    test_dispose_before_enter()

    print()
    print("All tests passed!")


if __name__ == "__main__":
    main()
