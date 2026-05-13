"""Optimize one function with an LLVM PassBuilder function pipeline string.

``Function.optimize`` mutates only that function in place. The pipeline string is
a function-level PassBuilder pipeline, such as ``mem2reg,instcombine,simplifycfg``.

Run from the repository root with:

    uv run python examples/optimize_function.py
"""

from __future__ import annotations

import textwrap

import llvm


INPUT_IR = """
define i32 @optimize_me(i32 %x) {
entry:
  %tmp = alloca i32
  %zero = add i32 %x, 0
  store i32 %zero, ptr %tmp
  %loaded = load i32, ptr %tmp
  %result = mul i32 %loaded, 1
  ret i32 %result
}

define i32 @leave_me_alone(i32 %x) {
entry:
  %result = add i32 %x, 0
  ret i32 %result
}
"""


def optimize_one_function(
    ir_text: str,
    function_name: str = "optimize_me",
    pipeline: str = "mem2reg,instcombine,simplifycfg",
) -> str:
    with llvm.create_context() as ctx:
        with ctx.parse_ir(textwrap.dedent(ir_text).strip() + "\n") as mod:
            assert mod.verify(), mod.verification_error
            fn = mod.get_function(function_name)
            assert fn is not None
            fn.optimize(pipeline)
            assert mod.verify(), mod.verification_error
            return str(mod)


def main() -> None:
    print(optimize_one_function(INPUT_IR), end="")


if __name__ == "__main__":
    main()
