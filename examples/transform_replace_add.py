"""Parse LLVM IR and replace plain integer adds with equivalent subs.

The rewrite preserves both operands by converting ``x + y`` into
``x - (0 - y)``. Adds with ``nuw``/``nsw`` flags are intentionally left alone
because those flags carry poison semantics that this small example does not try
to reproduce.

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
define i32 @add_values(i32 %x, i32 %y) {
entry:
  %sum = add i32 %x, %y
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
                    if (
                        inst.opcode == llvm.Opcode.Add
                        and not inst.nsw
                        and not inst.nuw
                    )
                ]

                for inst in additions:
                    lhs, rhs = inst.operands
                    base_name = inst.name or "add"
                    with inst.create_builder() as builder:
                        neg_rhs = builder.neg(rhs, base_name + ".rhs.neg")
                        replacement = builder.sub(lhs, neg_rhs, base_name + ".repl")
                    inst.replace_all_uses_with(replacement)
                    inst.erase_from_parent()

            assert mod.verify(), mod.verification_error
            return str(mod)


def main() -> None:
    print(transform(INPUT_IR), end="")


if __name__ == "__main__":
    main()
