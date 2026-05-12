"""Optimize a module with an LLVM PassBuilder pipeline string.

``Module.optimize`` mutates the module in place. The pipeline string is the same
string accepted by LLVM's PassBuilder, such as ``default<O0>`` or
``default<O2>``.

Run from the repository root with:

    uv run python examples/optimize_module.py
"""

from __future__ import annotations

import textwrap

import llvm


INPUT_IR = """
define i32 @add_then_simplify(i32 %x) {
entry:
  %tmp = alloca i32
  %zero = add i32 %x, 0
  store i32 %zero, ptr %tmp
  %loaded = load i32, ptr %tmp
  %result = mul i32 %loaded, 1
  ret i32 %result
}
"""


def optimize_ir(ir_text: str, pipeline: str = "default<O2>") -> str:
    with llvm.create_context() as ctx:
        with ctx.parse_ir(textwrap.dedent(ir_text).strip() + "\n") as mod:
            assert mod.verify(), mod.verification_error
            mod.optimize(pipeline)
            assert mod.verify(), mod.verification_error
            return str(mod)


def main() -> None:
    print(optimize_ir(INPUT_IR), end="")


if __name__ == "__main__":
    main()
