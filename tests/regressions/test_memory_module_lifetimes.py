"""
Regression tests for module-owned wrapper lifetime guards.

Root cause: many wrappers kept only an LLVM context token while holding raw
references to values, blocks, uses, operand bundles, metadata views, or borrowed
modules owned by an LLVMModuleRef. Disposing the module while the context stayed
alive left those raw references dangling. Each wrapper must reject use through a
Python LLVMMemoryError before calling LLVM-C on freed module-owned storage.
"""

from pathlib import Path

import llvm


def expect_memory_error(action, expected: str):
    try:
        action()
    except llvm.LLVMMemoryError as exc:
        assert expected.lower() in str(exc).lower(), str(exc)
    else:
        raise AssertionError("expected LLVMMemoryError")


def expect_assertion(action, expected: str):
    try:
        action()
    except llvm.LLVMAssertionError as exc:
        assert expected.lower() in str(exc).lower(), str(exc)
    else:
        raise AssertionError("expected LLVMAssertionError")


def test_module_owned_wrappers_reject_use_after_module_disposed():
    ctx_mgr = llvm.create_context()
    ctx = ctx_mgr.__enter__()
    try:
        mod_mgr = ctx.create_module("module_lifetimes")
        mod = mod_mgr.__enter__()

        i32 = ctx.types.i32
        fn = mod.add_function("f", ctx.types.function(i32, [i32]))
        borrowed_mod = fn.module
        param = fn.get_param(0)
        attr_view = fn.attributes
        bb = fn.append_basic_block("entry")
        bb_value = bb.as_value()
        builder_manager = bb.create_builder()
        operand_bundle = llvm.create_operand_bundle("deopt", [param], ctx)

        with bb.create_builder() as builder:
            inst = builder.add(param, i32.constant(1), "sum")
            builder.ret(inst)

        uses = param.uses
        assert uses
        use = uses[0]
        metadata_view = inst.metadata
        value_metadata = param.as_metadata()
        with mod.create_dibuilder() as dib:
            di_file = dib.file("x.c", ".")

        mod_mgr.__exit__(None, None, None)

        expect_memory_error(lambda: fn.name, "module was disposed")
        expect_memory_error(lambda: param.name, "module was disposed")
        expect_memory_error(lambda: bb.name, "module was disposed")
        expect_memory_error(lambda: bb_value.name, "module was disposed")
        expect_memory_error(lambda: inst.name, "module was disposed")
        expect_memory_error(lambda: use.user, "module was disposed")
        expect_memory_error(lambda: attr_view.get("noreturn"), "module was disposed")
        expect_memory_error(lambda: borrowed_mod.name, "module has been disposed")
        expect_memory_error(lambda: builder_manager.__enter__(), "module has been disposed")
        expect_memory_error(lambda: operand_bundle.num_args, "module was disposed")
        expect_memory_error(lambda: metadata_view.get("dbg"), "module was disposed")
        expect_memory_error(lambda: value_metadata.kind, "module was disposed")
        expect_memory_error(lambda: di_file.kind, "module was disposed")
    finally:
        ctx_mgr.__exit__(None, None, None)


def test_context_manager_results_reject_use_after_exit():
    ctx_mgr = llvm.create_context()
    ctx = ctx_mgr.__enter__()
    ctx_mgr.__exit__(None, None, None)
    expect_memory_error(lambda: ctx.types.i32, "context")

    with llvm.create_context() as live_ctx:
        mod_mgr = live_ctx.create_module("escaped_module")
        mod = mod_mgr.__enter__()
        mod_mgr.__exit__(None, None, None)
        expect_memory_error(lambda: mod.name, "module")

        mod_mgr = live_ctx.create_module("escaped_builder")
        mod = mod_mgr.__enter__()
        fn = mod.add_function("f", live_ctx.types.function(live_ctx.types.void, []))
        bb = fn.append_basic_block("entry")
        builder_mgr = bb.create_builder()
        builder = builder_mgr.__enter__()
        builder_mgr.__exit__(None, None, None)
        expect_memory_error(lambda: builder.ret_void(), "builder")

        dib_mgr = mod.create_dibuilder()
        dib = dib_mgr.__enter__()
        dib_mgr.__exit__(None, None, None)
        expect_memory_error(lambda: dib.file("x.c", "."), "dibuilder")
        mod_mgr.__exit__(None, None, None)


def test_binary_manager_result_rejects_use_after_exit():
    bitcode_path = Path(__file__).parent / "factorial.bc"
    binary_mgr = llvm.BinaryManager.from_bytes(bitcode_path.read_bytes())
    binary = binary_mgr.__enter__()
    binary_mgr.__exit__(None, None, None)
    expect_memory_error(lambda: binary.type, "binary")


def test_builder_debug_location_rejects_cross_context_metadata():
    with llvm.create_context() as ctx1:
        with llvm.create_context() as ctx2:
            with ctx1.create_module("debug_location_context_mismatch") as mod:
                fn = mod.add_function("f", ctx1.types.function(ctx1.types.void, []))
                bb = fn.append_basic_block("entry")
                local_scope = ctx1.md_node([])
                foreign_scope = ctx2.md_node([])
                foreign_inlined_at = ctx2.md_node([])

                with bb.create_builder() as builder:
                    expect_assertion(
                        lambda: builder.debug_location(
                            line=1, column=1, scope=foreign_scope
                        ),
                        "same context",
                    )
                    expect_assertion(
                        lambda: builder.debug_location(
                            line=1,
                            column=1,
                            scope=local_scope,
                            inlined_at=foreign_inlined_at,
                        ),
                        "same context",
                    )
                    expect_assertion(
                        lambda: builder.debug_location(foreign_scope),
                        "same context",
                    )
                    expect_assertion(
                        lambda: ctx1.debug_location(
                            line=1, column=1, scope=foreign_scope
                        ),
                        "same context",
                    )
                    builder.ret_void()


if __name__ == "__main__":
    test_module_owned_wrappers_reject_use_after_module_disposed()
    print("test_module_owned_wrappers_reject_use_after_module_disposed: PASSED")

    test_context_manager_results_reject_use_after_exit()
    print("test_context_manager_results_reject_use_after_exit: PASSED")

    test_binary_manager_result_rejects_use_after_exit()
    print("test_binary_manager_result_rejects_use_after_exit: PASSED")

    test_builder_debug_location_rejects_cross_context_metadata()
    print("test_builder_debug_location_rejects_cross_context_metadata: PASSED")
