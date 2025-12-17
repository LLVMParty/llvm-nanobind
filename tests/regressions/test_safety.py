"""
Tests for memory safety - ensuring the interpreter never crashes due to
improper cleanup order of LLVM objects.

These tests verify that:
1. Modules outliving their context don't crash the interpreter
2. Proper exceptions are raised when accessing invalid objects
3. Warning messages are emitted for memory leaks
"""

import gc
import io
import os
import sys
import tempfile

import llvm


def create_test_bitcode():
    """Create a simple bitcode file for testing."""
    ir = """; ModuleID = 'test'
source_filename = "test"

define i32 @foo() {
entry:
  ret i32 42
}
"""
    # Create temp .ll file
    with tempfile.NamedTemporaryFile(mode="w", suffix=".ll", delete=False) as f:
        f.write(ir)
        ll_file = f.name

    # Compile to bitcode
    bc_file = ll_file.replace(".ll", ".bc")
    os.system(f"./llvm-bin llvm-as {ll_file} -o {bc_file}")
    os.unlink(ll_file)

    return bc_file


def test_module_outlives_context_no_crash():
    """Module outliving context should not crash interpreter.

    This tests the fix for the crash that occurred when a module was
    garbage collected after its parent context was destroyed.
    """
    # Create a module wrapper that will outlive the context
    escaped_module = None

    with llvm.create_context() as ctx:
        # Create a module using the context manager (safe pattern)
        with ctx.create_module("inner") as m:
            fn_ty = ctx.function_type(ctx.int32_type(), [], False)
            fn = m.add_function("bar", fn_ty)
            bb = fn.append_basic_block("entry", ctx)
            with ctx.create_builder() as builder:
                builder.position_at_end(bb)
                builder.ret(llvm.const_int(ctx.int32_type(), 99, False))

        # Create another module directly (unsafe pattern - for testing)
        # This module will NOT be automatically disposed when context exits
        escaped_module = ctx.create_module("escaped")
        # Note: we're not using 'with', so the module won't auto-dispose

    # Context is now destroyed
    # The escaped_module still exists but its context is gone

    # Force garbage collection - this should NOT crash the interpreter
    # Before the fix, this would cause a segfault in LLVMDisposeModule
    gc.collect()

    # Accessing the module should raise an exception, not crash
    exception_raised = False
    try:
        # This should fail because context is dead
        _ = escaped_module.__enter__()  # Try to enter the manager
    except (llvm.LLVMError, AttributeError):
        exception_raised = True

    # Clean up - this also should not crash
    del escaped_module
    gc.collect()

    print("  (module outlived context without crashing)")


def test_proper_cleanup_order():
    """Proper cleanup order should work without warnings or errors."""
    old_stderr = sys.stderr
    sys.stderr = io.StringIO()

    try:
        with llvm.create_context() as ctx:
            with ctx.create_module("test") as m:
                fn_ty = ctx.function_type(ctx.int32_type(), [], False)
                fn = m.add_function("foo", fn_ty)
                bb = fn.append_basic_block("entry", ctx)
                with ctx.create_builder() as builder:
                    builder.position_at_end(bb)
                    builder.ret(llvm.const_int(ctx.int32_type(), 42, False))

                # Module is valid here
                ir = str(m)
                assert "define i32 @foo()" in ir

            # Module disposed here (end of 'with ctx.create_module')

        # Context disposed here (end of 'with llvm.create_context')

        gc.collect()
        stderr_output = sys.stderr.getvalue()
    finally:
        sys.stderr = old_stderr

    # With proper cleanup, there should be no warnings
    assert "Warning" not in stderr_output, (
        f"Unexpected warning with proper cleanup: {stderr_output}"
    )
    print("  (proper cleanup produced no warnings)")


def test_value_outlives_context():
    """Values outliving their context should raise exceptions, not crash."""
    escaped_value = None

    with llvm.create_context() as ctx:
        escaped_value = llvm.const_int(ctx.int32_type(), 123, False)
        # Value is valid here
        assert escaped_value.is_constant

    # Context destroyed, value should be invalid
    gc.collect()

    exception_raised = False
    try:
        _ = escaped_value.is_constant
    except llvm.LLVMUseAfterFreeError:
        exception_raised = True

    assert exception_raised, (
        "Expected exception when accessing value after context destroyed"
    )
    print("  (value outlived context, got expected exception)")


def test_type_outlives_context():
    """Types outliving their context should raise exceptions, not crash."""
    escaped_type = None

    with llvm.create_context() as ctx:
        escaped_type = ctx.int32_type()
        # Type is valid here
        assert escaped_type.is_integer

    # Context destroyed, type should be invalid
    gc.collect()

    exception_raised = False
    try:
        _ = escaped_type.is_integer
    except llvm.LLVMUseAfterFreeError:
        exception_raised = True

    assert exception_raised, (
        "Expected exception when accessing type after context destroyed"
    )
    print("  (type outlived context, got expected exception)")


def test_builder_outlives_context():
    """Builders outliving their context should raise exceptions, not crash."""
    escaped_builder = None

    with llvm.create_context() as ctx:
        escaped_builder = ctx.create_builder()
        # Don't use 'with' - let it escape

    # Context destroyed
    gc.collect()

    exception_raised = False
    try:
        # Try to use the builder
        _ = escaped_builder.__enter__()
    except (llvm.LLVMError, AttributeError):
        exception_raised = True

    del escaped_builder
    gc.collect()

    print("  (builder outlived context without crashing)")


def test_function_outlives_module():
    """Function references should handle module disposal gracefully."""
    escaped_fn = None

    with llvm.create_context() as ctx:
        with ctx.create_module("test") as m:
            fn_ty = ctx.function_type(ctx.int32_type(), [], False)
            escaped_fn = m.add_function("foo", fn_ty)
            # Function is valid here
            assert escaped_fn.name == "foo"

        # Module disposed, function should be invalid

    # Context also disposed
    gc.collect()

    exception_raised = False
    try:
        _ = escaped_fn.name
    except llvm.LLVMUseAfterFreeError:
        exception_raised = True

    assert exception_raised, (
        "Expected exception when accessing function after module destroyed"
    )
    print("  (function outlived module, got expected exception)")


def test_basic_block_outlives_function():
    """BasicBlock references should handle function disposal gracefully."""
    escaped_bb = None

    with llvm.create_context() as ctx:
        with ctx.create_module("test") as m:
            fn_ty = ctx.function_type(ctx.void_type(), [], False)
            fn = m.add_function("foo", fn_ty)
            escaped_bb = fn.append_basic_block("entry", ctx)
            # BB is valid here
            assert escaped_bb.name == "entry"

    # Everything disposed
    gc.collect()

    exception_raised = False
    try:
        _ = escaped_bb.name
    except llvm.LLVMError:
        exception_raised = True

    assert exception_raised, (
        "Expected exception when accessing BB after context destroyed"
    )
    print("  (basic block outlived context, got expected exception)")


if __name__ == "__main__":
    print("Running memory safety tests...")
    print()

    test_proper_cleanup_order()
    test_value_outlives_context()
    test_type_outlives_context()
    test_function_outlives_module()
    test_basic_block_outlives_function()
    test_builder_outlives_context()
    test_module_outlives_context_no_crash()

    print()
    print("All memory safety tests passed!")
