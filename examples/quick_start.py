"""Build a tiny LLVM module with llvm-nanobind.

Run from the repository root with:

    uv run python examples/quick_start.py
"""

from __future__ import annotations

import llvm


def build_module() -> str:
    """Return LLVM IR for a function that returns 42."""
    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        fn_type = ctx.types.function(i32, [])

        with ctx.create_module("example") as mod:
            fn = mod.add_function("get_answer", fn_type)
            entry = fn.append_basic_block("entry")

            with entry.create_builder() as builder:
                builder.ret(i32.constant(42))

            assert mod.verify(), mod.verification_error
            return str(mod)


def main() -> None:
    print(build_module(), end="")


if __name__ == "__main__":
    main()
