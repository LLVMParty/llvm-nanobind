"""
Regression tests for exception-first accessors that previously returned None.

These APIs now raise LLVMAssertionError with actionable guidance instead of
returning None for missing state:
- Function.entry_block / first_basic_block / last_basic_block
- Function personality/prefix/prologue accessors
"""

import llvm


def _assert_raises_assertion(fn, expected_substrings):
    try:
        fn()
        assert False, "Expected LLVMAssertionError"
    except llvm.LLVMAssertionError as exc:
        msg = str(exc)
        for part in expected_substrings:
            assert part in msg, f"Expected {part!r} in error message: {msg!r}"


def test_function_block_accessors_require_definition():
    with llvm.create_context() as ctx:
        with ctx.create_module("m") as mod:
            fn_ty = ctx.types.function(ctx.types.void, [])
            decl = mod.add_function("decl", fn_ty)

            assert decl.is_declaration
            assert decl.basic_block_count == 0

            _assert_raises_assertion(
                lambda: decl.entry_block, ["entry_block", "is_declaration"]
            )
            _assert_raises_assertion(
                lambda: decl.first_basic_block,
                ["first_basic_block", "basic_block_count"],
            )
            _assert_raises_assertion(
                lambda: decl.last_basic_block,
                ["last_basic_block", "basic_block_count"],
            )


def test_function_block_accessors_work_on_definition():
    with llvm.create_context() as ctx:
        with ctx.create_module("m") as mod:
            fn_ty = ctx.types.function(ctx.types.void, [])
            fn = mod.add_function("f", fn_ty)
            bb = fn.append_basic_block("entry")

            assert not fn.is_declaration
            assert fn.entry_block == bb
            assert fn.first_basic_block == bb
            assert fn.last_basic_block == bb


def test_function_metadata_accessors_require_has_checks():
    with llvm.create_context() as ctx:
        with ctx.create_module("m") as mod:
            fn_ty = ctx.types.function(ctx.types.void, [])
            fn = mod.add_function("f", fn_ty)

            assert not fn.has_personality_fn
            assert not fn.has_prefix_data
            assert not fn.has_prologue_data

            _assert_raises_assertion(
                lambda: fn.personality_fn, ["personality_fn", "has_personality_fn"]
            )
            _assert_raises_assertion(
                lambda: fn.prefix_data, ["prefix_data", "has_prefix_data"]
            )
            _assert_raises_assertion(
                lambda: fn.prologue_data, ["prologue_data", "has_prologue_data"]
            )


def test_function_metadata_accessors_work_when_present():
    with llvm.create_context() as ctx:
        with ctx.create_module("m") as mod:
            void_ty = ctx.types.void
            i32 = ctx.types.i32
            fn_ty = ctx.types.function(void_ty, [])
            personality_ty = ctx.types.function(i32, [], vararg=True)

            personality = mod.add_function("__personality", personality_ty)
            fn = mod.add_function("f", fn_ty)

            fn.personality_fn = personality
            fn.prefix_data = ctx.types.ptr.null()
            fn.prologue_data = ctx.types.ptr.null()

            assert fn.has_personality_fn
            assert fn.personality_fn == personality
            assert fn.personality_fn == personality
            assert fn.has_prefix_data
            assert fn.prefix_data is not None
            assert fn.has_prologue_data
            assert fn.prologue_data is not None


if __name__ == "__main__":
    test_function_block_accessors_require_definition()
    print("test_function_block_accessors_require_definition: PASSED")
    test_function_block_accessors_work_on_definition()
    print("test_function_block_accessors_work_on_definition: PASSED")
    test_function_metadata_accessors_require_has_checks()
    print("test_function_metadata_accessors_require_has_checks: PASSED")
    test_function_metadata_accessors_work_when_present()
    print("test_function_metadata_accessors_work_when_present: PASSED")
