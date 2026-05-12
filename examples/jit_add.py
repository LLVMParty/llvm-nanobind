"""Compile LLVM IR in-process and call it from Python.

This example uses the LLVM-C ORC LLJIT wrapper. ``jit.add_module(mod)``
transfers the module into the JIT and invalidates the Python ``Module`` wrapper
on success.

Run from the repository root with:

    uv run python examples/jit_add.py
"""

from __future__ import annotations

import ctypes
from typing import cast

import llvm


CALLBACK_TYPE = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.c_int32)


@CALLBACK_TYPE
def py_add_ten(value: int) -> int:
    return value + 10


def add_functions_to_module(ctx: llvm.Context, mod: llvm.Module) -> None:
    i32 = ctx.types.i32

    add_fn = mod.add_function("add_i32", ctx.types.function(i32, [i32, i32]))
    lhs = add_fn.get_param(0)
    rhs = add_fn.get_param(1)
    add_entry = add_fn.append_basic_block("entry")
    with add_entry.create_builder() as builder:
        builder.ret(builder.add(lhs, rhs, "sum"))

    callback_decl = mod.add_function("py_add_ten", ctx.types.function(i32, [i32]))
    call_python_fn = mod.add_function("call_python", ctx.types.function(i32, [i32]))
    arg = call_python_fn.get_param(0)
    callback_entry = call_python_fn.append_basic_block("entry")
    with callback_entry.create_builder() as builder:
        result = builder.call(callback_decl, [arg], "result")
        builder.ret(result)


def run_jit() -> tuple[int, int]:
    with llvm.JIT.host() as jit:
        jit.add_symbol("py_add_ten", py_add_ten)

        with llvm.create_context() as ctx:
            with ctx.create_module("jit_example") as mod:
                add_functions_to_module(ctx, mod)
                assert mod.verify(), mod.verification_error
                jit.add_module(mod)

        add_i32 = jit.ctypes_function(
            "add_i32",
            restype=ctypes.c_int32,
            argtypes=[ctypes.c_int32, ctypes.c_int32],
        )
        call_python = jit.ctypes_function(
            "call_python",
            restype=ctypes.c_int32,
            argtypes=[ctypes.c_int32],
        )

        return cast(int, add_i32(40, 2)), cast(int, call_python(5))


def main() -> None:
    try:
        add_result, callback_result = run_jit()
    except llvm.LLVMError as exc:
        print(f"skipped: host JIT is unavailable: {exc}")
        return

    print(f"add_i32(40, 2) = {add_result}")
    print(f"call_python(5) = {callback_result}")


if __name__ == "__main__":
    main()
