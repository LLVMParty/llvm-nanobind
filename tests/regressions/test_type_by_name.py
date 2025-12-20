"""
Test for memory safety when looking up non-existent types.

This test verifies that looking up a type that doesn't exist returns None
rather than throwing an exception or causing memory issues.
"""

import llvm


def test_get_type_by_name_nonexistent_returns_none():
    """get_type_by_name should return None for non-existent types."""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as m:
            # Looking up a non-existent type should return None
            result = ctx.types.get("NonExistent")
            assert result is None, f"Expected None, got {result!r}"


def test_get_type_by_name_existing_returns_type():
    """get_type_by_name should return the type when it exists."""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as m:
            # Create a named struct
            struct = ctx.types.opaque_struct("MyStruct")
            struct.set_body([ctx.types.i32], False)

            # Looking up the existing type should return it
            result = ctx.types.get("MyStruct")
            assert result is not None, "Expected to find MyStruct"
            assert result.struct_name == "MyStruct"


if __name__ == "__main__":
    test_get_type_by_name_nonexistent_returns_none()
    print("test_get_type_by_name_nonexistent_returns_none: PASSED")

    test_get_type_by_name_existing_returns_type()
    print("test_get_type_by_name_existing_returns_type: PASSED")
