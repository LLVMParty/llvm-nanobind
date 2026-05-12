"""
Regression tests for the API UX cleanup helpers.

These tests cover the high-level APIs from devdocs/api-ux-cleanup:
- Builder.intrinsic(...)
- Module.optimize(...)
- TargetMachine.host()
- Module.emit_object()/emit_assembly()
- JIT through LLVM-C ORC LLJIT
"""

import ctypes

import llvm


def _simple_i32_module(name: str = "simple"):
    ctx_mgr = llvm.create_context()
    ctx = ctx_mgr.__enter__()
    mod_mgr = ctx.create_module(name)
    mod = mod_mgr.__enter__()

    i32 = ctx.types.i32
    fn = mod.add_function("answer", ctx.types.function(i32, []))
    entry = fn.append_basic_block("entry")
    with entry.create_builder() as builder:
        builder.ret(builder.add(i32.constant(40), i32.constant(2), "sum"))

    return ctx_mgr, mod_mgr, ctx, mod


def _close_module(ctx_mgr, mod_mgr):
    try:
        mod_mgr.__exit__(None, None, None)
    finally:
        ctx_mgr.__exit__(None, None, None)


def _host_target_machine_or_skip():
    try:
        return llvm.TargetMachine.host()
    except llvm.LLVMError as exc:
        print(f"host target unavailable, skipping target-dependent check: {exc}")
        return None


def _host_jit_or_skip():
    try:
        return llvm.JIT.host()
    except llvm.LLVMError as exc:
        print(f"host JIT unavailable, skipping JIT check: {exc}")
        return None


def test_builder_intrinsic_non_overloaded():
    with llvm.create_context() as ctx:
        with ctx.create_module("intrinsic_trap") as mod:
            fn = mod.add_function("f", ctx.types.function(ctx.types.void, []))
            entry = fn.append_basic_block("entry")
            with entry.create_builder() as builder:
                builder.intrinsic("llvm.trap", [])
                builder.unreachable()

            ir = str(mod)
            assert "call void @llvm.trap()" in ir
            assert mod.verify(), mod.verification_error


def test_builder_intrinsic_overloaded_float():
    with llvm.create_context() as ctx:
        with ctx.create_module("intrinsic_sqrt") as mod:
            f64 = ctx.types.f64
            fn = mod.add_function("mysqrt", ctx.types.function(f64, [f64]))
            x = fn.get_param(0)
            entry = fn.append_basic_block("entry")
            with entry.create_builder() as builder:
                y = builder.intrinsic(
                    "llvm.sqrt", [x], overloaded_types=[f64], name_hint="y"
                )
                builder.ret(y)

            ir = str(mod)
            assert "@llvm.sqrt.f64" in ir
            assert mod.verify(), mod.verification_error


def test_builder_intrinsic_memcpy():
    with llvm.create_context() as ctx:
        with ctx.create_module("intrinsic_memcpy") as mod:
            void = ctx.types.void
            ptr = ctx.types.ptr
            i64 = ctx.types.i64
            i1 = ctx.types.i1
            fn = mod.add_function("copy", ctx.types.function(void, [ptr, ptr, i64]))
            dst = fn.get_param(0)
            src = fn.get_param(1)
            n = fn.get_param(2)
            entry = fn.append_basic_block("entry")
            with entry.create_builder() as builder:
                builder.intrinsic(
                    "llvm.memcpy",
                    [dst, src, n, i1.constant(False)],
                    overloaded_types=[ptr, ptr, i64],
                )
                builder.ret_void()

            ir = str(mod)
            assert "@llvm.memcpy" in ir
            assert mod.verify(), mod.verification_error


def test_builder_intrinsic_errors_are_clear():
    with llvm.create_context() as ctx:
        with ctx.create_module("intrinsic_errors") as mod:
            f64 = ctx.types.f64
            fn = mod.add_function("f", ctx.types.function(f64, [f64]))
            x = fn.get_param(0)
            entry = fn.append_basic_block("entry")
            with entry.create_builder() as builder:
                try:
                    builder.intrinsic("llvm.not_a_real_intrinsic", [])
                except llvm.LLVMAssertionError as exc:
                    assert "Unknown LLVM intrinsic" in str(exc)
                else:
                    raise AssertionError("expected unknown intrinsic error")

                try:
                    builder.intrinsic("llvm.sqrt", [x])
                except llvm.LLVMAssertionError as exc:
                    assert "overloaded_types" in str(exc)
                else:
                    raise AssertionError("expected overloaded_types error")

                builder.ret(x)


def test_module_optimize_success_and_failure():
    ctx_mgr, mod_mgr, _ctx, mod = _simple_i32_module("optimize_helper")
    try:
        assert mod.verify(), mod.verification_error
        mod.optimize("default<O0>")
        assert mod.verify(), mod.verification_error

        try:
            mod.optimize("not-a-real-pass")
        except llvm.LLVMError as exc:
            message = str(exc)
            assert "not-a-real-pass" in message
            assert "Failed to optimize" in message
        else:
            raise AssertionError("expected invalid pipeline error")
    finally:
        _close_module(ctx_mgr, mod_mgr)


def test_module_optimize_with_target_machine():
    tm = _host_target_machine_or_skip()
    if tm is None:
        return

    ctx_mgr, mod_mgr, _ctx, mod = _simple_i32_module("optimize_with_tm")
    try:
        mod.optimize("default<O0>", target_machine=tm)
        assert mod.verify(), mod.verification_error
    finally:
        _close_module(ctx_mgr, mod_mgr)


def test_emit_object_and_assembly_convenience():
    tm = _host_target_machine_or_skip()
    if tm is None:
        return

    ctx_mgr, mod_mgr, _ctx, mod = _simple_i32_module("emit_helper")
    try:
        mod.optimize("default<O0>", target_machine=tm)

        obj = mod.emit_object(target_machine=tm)
        asm = mod.emit_assembly(target_machine=tm)
        obj_host = mod.emit_object()
        asm_host = mod.emit_assembly()

        assert isinstance(obj, bytes) and len(obj) > 0
        assert isinstance(asm, bytes) and len(asm) > 0
        assert isinstance(obj_host, bytes) and len(obj_host) > 0
        assert isinstance(asm_host, bytes) and len(asm_host) > 0

        with llvm.BinaryManager.from_bytes(obj) as binary:
            assert binary.type in {
                llvm.BinaryType.COFF,
                llvm.BinaryType.ELF32L,
                llvm.BinaryType.ELF32B,
                llvm.BinaryType.ELF64L,
                llvm.BinaryType.ELF64B,
                llvm.BinaryType.MachO32L,
                llvm.BinaryType.MachO32B,
                llvm.BinaryType.MachO64L,
                llvm.BinaryType.MachO64B,
                llvm.BinaryType.Wasm,
            }
    finally:
        _close_module(ctx_mgr, mod_mgr)


def test_jit_integer_function_and_module_transfer():
    jit = _host_jit_or_skip()
    if jit is None:
        return

    with jit:
        with llvm.create_context() as ctx:
            with ctx.create_module("jit_add") as mod:
                i32 = ctx.types.i32
                fn = mod.add_function("add_i32", ctx.types.function(i32, [i32, i32]))
                a = fn.get_param(0)
                b = fn.get_param(1)
                entry = fn.append_basic_block("entry")
                with entry.create_builder() as builder:
                    builder.ret(builder.add(a, b, "sum"))

                assert mod.verify(), mod.verification_error
                jit.add_module(mod)

                try:
                    _ = mod.name
                except llvm.LLVMMemoryError as exc:
                    assert "disposed" in str(exc)
                else:
                    raise AssertionError("expected module to be invalid after JIT transfer")

        address = jit.lookup("add_i32")
        assert isinstance(address, int) and address != 0

        add_i32 = jit.ctypes_function(
            "add_i32", ctypes.c_int32, [ctypes.c_int32, ctypes.c_int32]
        )
        assert add_i32(2, 3) == 5


def test_jit_missing_symbol_error():
    jit = _host_jit_or_skip()
    if jit is None:
        return

    with jit:
        try:
            jit.lookup("missing_symbol")
        except llvm.LLVMError as exc:
            assert "missing_symbol" in str(exc)
        else:
            raise AssertionError("expected missing symbol lookup to fail")


def test_jit_ctypes_function_keeps_jit_alive():
    jit = _host_jit_or_skip()
    if jit is None:
        return

    with llvm.create_context() as ctx:
        with ctx.create_module("jit_keepalive") as mod:
            i32 = ctx.types.i32
            fn = mod.add_function("add_i32_keepalive", ctx.types.function(i32, [i32, i32]))
            a = fn.get_param(0)
            b = fn.get_param(1)
            entry = fn.append_basic_block("entry")
            with entry.create_builder() as builder:
                builder.ret(builder.add(a, b, "sum"))

            jit.add_module(mod)
            add_i32 = jit.ctypes_function(
                "add_i32_keepalive", ctypes.c_int32, [ctypes.c_int32, ctypes.c_int32]
            )

    del jit
    assert add_i32(4, 6) == 10


def test_jit_callback_symbol():
    jit = _host_jit_or_skip()
    if jit is None:
        return

    callback_type = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.c_int32)

    @callback_type
    def py_inc(x):
        return x + 10

    with jit:
        jit.add_symbol("py_inc", py_inc)

        with llvm.create_context() as ctx:
            with ctx.create_module("jit_callback") as mod:
                i32 = ctx.types.i32
                callee = mod.add_function("py_inc", ctx.types.function(i32, [i32]))
                fn = mod.add_function("call_py_inc", ctx.types.function(i32, [i32]))
                x = fn.get_param(0)
                entry = fn.append_basic_block("entry")
                with entry.create_builder() as builder:
                    result = builder.call(callee, [x], "result")
                    builder.ret(result)

                assert mod.verify(), mod.verification_error
                jit.add_module(mod)

        call_py_inc = jit.ctypes_function(
            "call_py_inc", ctypes.c_int32, [ctypes.c_int32]
        )
        assert call_py_inc(5) == 15


if __name__ == "__main__":
    test_builder_intrinsic_non_overloaded()
    print("test_builder_intrinsic_non_overloaded: PASSED")

    test_builder_intrinsic_overloaded_float()
    print("test_builder_intrinsic_overloaded_float: PASSED")

    test_builder_intrinsic_memcpy()
    print("test_builder_intrinsic_memcpy: PASSED")

    test_builder_intrinsic_errors_are_clear()
    print("test_builder_intrinsic_errors_are_clear: PASSED")

    test_module_optimize_success_and_failure()
    print("test_module_optimize_success_and_failure: PASSED")

    test_module_optimize_with_target_machine()
    print("test_module_optimize_with_target_machine: PASSED")

    test_emit_object_and_assembly_convenience()
    print("test_emit_object_and_assembly_convenience: PASSED")

    test_jit_integer_function_and_module_transfer()
    print("test_jit_integer_function_and_module_transfer: PASSED")

    test_jit_missing_symbol_error()
    print("test_jit_missing_symbol_error: PASSED")

    test_jit_ctypes_function_keeps_jit_alive()
    print("test_jit_ctypes_function_keeps_jit_alive: PASSED")

    test_jit_callback_symbol()
    print("test_jit_callback_symbol: PASSED")
