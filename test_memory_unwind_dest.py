"""
Test for memory safety when accessing unwind destinations.

This test verifies that get_unwind_dest returns None for instructions
that don't have an unwind destination (like cleanupret without unwind).
"""

import llvm


def test_get_unwind_dest_returns_none_when_not_present():
    """get_unwind_dest should return None when there's no unwind dest."""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as m:
            # Create a function with exception handling
            void_ty = ctx.void_type()
            i8_ptr_ty = ctx.pointer_type(0)
            token_ty = ctx.token_type()

            # Create personality function type
            i32_ty = ctx.int32_type()
            personality_ty = ctx.function_type(i32_ty, [], True)
            personality_fn = m.add_function("__personality", personality_ty)

            # Create main function
            fn_ty = ctx.function_type(void_ty, [])
            fn = m.add_function("test_cleanup", fn_ty)
            fn.set_personality_fn(personality_fn)

            entry = fn.append_basic_block("entry", ctx)
            cleanup_block = fn.append_basic_block("cleanup", ctx)

            with ctx.create_builder() as builder:
                # Entry block - just branch to cleanup for simplicity
                builder.position_at_end(entry)
                builder.br(cleanup_block)

                # Cleanup block with cleanuppad and cleanupret (no unwind)
                builder.position_at_end(cleanup_block)
                # Create a cleanup pad with no parent (use a constant none token)
                none_token = llvm.const_null(token_ty)
                cleanup_pad = builder.cleanup_pad(none_token, [], "pad")

                # Create cleanupret WITHOUT unwind destination
                cleanup_ret = builder.cleanup_ret(cleanup_pad, None)

                # Verify that get_unwind_dest returns None
                unwind_dest = cleanup_ret.get_unwind_dest()
                assert unwind_dest is None, f"Expected None, got {unwind_dest}"


def test_get_unwind_dest_returns_block_when_present():
    """get_unwind_dest should return the block when it exists."""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as m:
            void_ty = ctx.void_type()
            i32_ty = ctx.int32_type()

            # Create personality function
            personality_ty = ctx.function_type(i32_ty, [], True)
            personality_fn = m.add_function("__personality", personality_ty)

            # Create main function
            fn_ty = ctx.function_type(void_ty, [])
            fn = m.add_function("test_invoke", fn_ty)
            fn.set_personality_fn(personality_fn)

            entry = fn.append_basic_block("entry", ctx)
            normal = fn.append_basic_block("normal", ctx)
            unwind = fn.append_basic_block("unwind", ctx)

            # Create a simple function to invoke
            callee_ty = ctx.function_type(void_ty, [])
            callee = m.add_function("may_throw", callee_ty)

            with ctx.create_builder() as builder:
                builder.position_at_end(entry)
                # Use invoke_with_operand_bundles with empty bundles
                invoke = builder.invoke_with_operand_bundles(
                    callee_ty, callee, [], normal, unwind, [], ""
                )

                builder.position_at_end(normal)
                builder.ret_void()

                builder.position_at_end(unwind)
                landing = builder.landing_pad(
                    ctx.struct_type([ctx.pointer_type(0), i32_ty]), 0, "lp"
                )
                landing.set_cleanup(True)
                builder.ret_void()

                # Verify get_unwind_dest returns the unwind block
                result = invoke.get_unwind_dest()
                assert result is not None, "Expected unwind block"
                assert result.name == "unwind", (
                    f"Expected 'unwind', got '{result.name}'"
                )


if __name__ == "__main__":
    test_get_unwind_dest_returns_none_when_not_present()
    print("test_get_unwind_dest_returns_none_when_not_present: PASSED")

    test_get_unwind_dest_returns_block_when_present()
    print("test_get_unwind_dest_returns_block_when_present: PASSED")
