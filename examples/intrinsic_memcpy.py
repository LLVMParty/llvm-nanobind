"""Build a call to an LLVM intrinsic by name.

This example uses ``Builder.intrinsic`` for ``llvm.memcpy``. The
``overloaded_types`` list selects the intrinsic declaration:

- destination pointer type,
- source pointer type,
- length integer type.

Run from the repository root with:

    uv run python examples/intrinsic_memcpy.py
"""

from __future__ import annotations

import llvm


def build_module() -> str:
    with llvm.create_context() as ctx:
        void = ctx.types.void
        ptr = ctx.types.ptr
        i64 = ctx.types.i64
        i1 = ctx.types.i1

        with ctx.create_module("intrinsic_memcpy_example") as mod:
            fn_ty = ctx.types.function(void, [ptr, ptr, i64])
            fn = mod.add_function("copy_bytes", fn_ty)
            dst = fn.get_param(0)
            src = fn.get_param(1)
            count = fn.get_param(2)

            entry = fn.append_basic_block("entry")
            with entry.create_builder() as builder:
                builder.intrinsic(
                    "llvm.memcpy",
                    [dst, src, count, i1.constant(False)],
                    overloaded_types=[ptr, ptr, i64],
                )
                builder.ret_void()

            assert mod.verify(), mod.verification_error
            return str(mod)


def main() -> None:
    print(build_module(), end="")


if __name__ == "__main__":
    main()
