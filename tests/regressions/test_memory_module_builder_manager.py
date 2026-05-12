"""
Regression for Module.create_builder lifetime guards.

Root cause: Module.create_builder() stores raw basic-block/instruction refs in a
BuilderManager. If the module was disposed before entering that manager, the
manager previously only checked the context token, then created a builder at a
freed IR position. Accessing the resulting builder could segfault. Borrowed
modules returned from Builder.module had the same missing module-token guard.
"""

import llvm


def expect_memory_error(fn, message_part: str) -> None:
    try:
        fn()
    except llvm.LLVMMemoryError as exc:
        assert message_part in str(exc), str(exc)
    else:
        raise AssertionError("expected LLVMMemoryError")


def test_module_create_builder_manager_rejects_disposed_module() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("builder_lifetime") as mod:
            fn_ty = ctx.types.function(ctx.types.void, [])
            fn = mod.add_function("f", fn_ty)
            bb = fn.append_basic_block("entry")

            bb_manager = mod.create_builder(bb)
            with mod.create_builder(bb) as builder:
                ret = builder.ret_void()
            inst_manager = mod.create_builder(ret)

        expect_memory_error(bb_manager.__enter__, "module has been disposed")
        expect_memory_error(inst_manager.__enter__, "module has been disposed")


def test_builder_module_borrow_rejects_disposed_module() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("borrowed_builder_module") as mod:
            fn_ty = ctx.types.function(ctx.types.void, [])
            fn = mod.add_function("f", fn_ty)
            bb = fn.append_basic_block("entry")

            with mod.create_builder(bb) as builder:
                borrowed = builder.module
                assert borrowed == mod

        expect_memory_error(lambda: borrowed.name, "Module has been disposed")


if __name__ == "__main__":
    test_module_create_builder_manager_rejects_disposed_module()
    print("test_module_create_builder_manager_rejects_disposed_module: PASSED")
    test_builder_module_borrow_rejects_disposed_module()
    print("test_builder_module_borrow_rejects_disposed_module: PASSED")
