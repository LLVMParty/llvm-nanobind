"""Emit object code and assembly from a module.

The module is optimized explicitly before emission. ``emit_object`` and
``emit_assembly`` perform code generation only.

Run from the repository root with:

    uv run python examples/emit_object_assembly.py
"""

from __future__ import annotations

import llvm


def emit_for_host() -> tuple[str, int, str, list[str]]:
    tm = llvm.TargetMachine.host()

    with llvm.create_context() as ctx:
        i32 = ctx.types.i32

        with ctx.create_module("emit_example") as mod:
            mod.target_triple = tm.triple
            mod.data_layout = str(tm.create_data_layout())

            fn = mod.add_function("get_answer", ctx.types.function(i32, []))
            entry = fn.append_basic_block("entry")
            with entry.create_builder() as builder:
                builder.ret(i32.constant(42))

            assert mod.verify(), mod.verification_error
            mod.optimize("default<O2>", target_machine=tm)

            obj = mod.emit_object(target_machine=tm)
            asm = mod.emit_assembly(target_machine=tm)

            with llvm.BinaryManager.from_bytes(obj) as binary:
                binary_type = binary.type.name

            asm_lines = asm.decode("utf-8", errors="replace").splitlines()
            preview = [line for line in asm_lines if line.strip()][:6]
            return tm.triple, len(obj), binary_type, preview


def main() -> None:
    try:
        triple, obj_size, binary_type, preview = emit_for_host()
    except llvm.LLVMError as exc:
        print(f"skipped: host code generation is unavailable: {exc}")
        return

    print(f"target: {triple}")
    print(f"object bytes: {obj_size}")
    print(f"binary type: {binary_type}")
    print("assembly preview:")
    for line in preview:
        print(f"  {line}")


if __name__ == "__main__":
    main()
