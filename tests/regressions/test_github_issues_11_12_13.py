"""Regression coverage for GitHub issues 11, 12, and 13."""

import llvm


def test_issue_11_types_aliases_share_context_type_factory() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("types_aliases") as mod:
            i32 = ctx.types.i32
            fn = mod.add_function("f", ctx.types.function(i32, [i32]))
            bb = fn.append_basic_block("entry")
            with bb.create_builder() as b:
                value = b.add(fn.get_param(0), i32.constant(1), "value")
                b.ret(value)

            assert ctx.types == mod.types
            assert ctx.types == fn.types
            assert ctx.types == bb.types
            assert ctx.types == value.types
            assert mod.types.i64 == ctx.types.i64
            assert fn.types.function(i32, [i32]) == ctx.types.function(i32, [i32])


def test_issue_12_function_create_builder_creates_entry_block() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("function_create_builder") as mod:
            i64 = mod.types.i64
            fn = mod.add_function("lift2", mod.types.function(i64, [i64, i64]))

            with fn.create_builder() as ir:
                ir.ret(i64.constant(0))

            assert fn.basic_block_count == 1
            assert fn.entry_block.name == "entry"
            assert mod.verify(), mod.verification_error
            assert "define i64 @lift2" in str(mod)
            assert "ret i64 0" in str(mod)


def test_issue_13_alloca_count_overload_replaces_array_alloca() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("alloca_count_overload") as mod:
            void = mod.types.void
            i32 = mod.types.i32
            fn = mod.add_function("f", mod.types.function(void, []))

            with fn.create_builder() as b:
                scalar = b.alloca(i32, "scalar")
                array = b.alloca(i32, i32.constant(4), "array")
                b.ret_void()

            assert not hasattr(llvm.Builder, "array_alloca")
            assert not scalar.is_array_allocation
            assert array.is_array_allocation
            ir = str(mod)
            assert "%scalar = alloca i32" in ir
            assert "%array = alloca i32, i32 4" in ir
            assert mod.verify(), mod.verification_error


if __name__ == "__main__":
    test_issue_11_types_aliases_share_context_type_factory()
    test_issue_12_function_create_builder_creates_entry_block()
    test_issue_13_alloca_count_overload_replaces_array_alloca()
    print("test_github_issues_11_12_13: PASSED")
