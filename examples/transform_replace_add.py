"""Parse LLVM IR and replace every integer add with an equivalent sub.

This demonstrates the current transformation APIs:

- `inst.operands`
- `inst.replace_all_uses_with(...)`
- `inst.erase_from_parent()`

Run from the repository root with:

    uv run python examples/transform_replace_add.py
"""

from __future__ import annotations

import textwrap

import llvm


INPUT_IR = """
define i32 @add_one(i32 %x) {
entry:
  %sum = add i32 %x, 1
  ret i32 %sum
}
"""


def transform(ir_text: str) -> str:
    with llvm.create_context() as ctx:
        with ctx.parse_ir(textwrap.dedent(ir_text).strip() + "\n") as mod:
            for func in mod.functions:
                if func.is_declaration:
                    continue

                additions = [
                    inst
                    for bb in func.basic_blocks
                    for inst in bb.instructions
                    if inst.opcode == llvm.Opcode.Add
                ]

                for inst in additions:
                    lhs, rhs = inst.operands
                    with inst.create_builder() as builder:
                        replacement = builder.sub(lhs, rhs.type.constant(-1), inst.name + ".repl")
                    inst.replace_all_uses_with(replacement)
                    inst.erase_from_parent()

            assert mod.verify(), mod.get_verification_error()
            return str(mod)


def main() -> None:
    print(transform(INPUT_IR), end="")


if __name__ == "__main__":
    main()
