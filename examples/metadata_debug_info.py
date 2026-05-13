"""Use Pythonic metadata and debug-info helpers.

This example shows:

- instruction metadata by kind name,
- named metadata as a module mapping,
- module flags as a keyed view,
- DIBuilder convenience methods,
- builder debug-location scopes.

Run from the repository root with:

    uv run python examples/metadata_debug_info.py
"""

from __future__ import annotations

import llvm


def build_module() -> str:
    with llvm.create_context() as ctx:
        with ctx.create_module("metadata_debug_info_example") as mod:
            mod.is_new_dbg_info_format = True
            i32 = ctx.types.i32
            fn = mod.add_function("add", ctx.types.function(i32, [i32, i32]))

            with mod.create_dibuilder() as dib:
                file = dib.file("main.c", ".")
                compile_unit = dib.compile_unit(
                    language=llvm.DwarfLanguage.C,
                    file=file,
                    producer="llvm-nanobind example",
                )
                subprogram = dib.function(
                    fn,
                    name="add",
                    file=file,
                    line=1,
                    return_type=i32,
                    param_types=[i32, i32],
                )
                _tmp_var = dib.local_variable(subprogram, "tmp", file, 2, i32)

                # Named metadata is a mapping from name to appendable list.
                mod.named_metadata["example.compile_units"].append(compile_unit)

                # Module flags are keyed by their string name.
                mod.module_flags.add(
                    "Example Metadata Version",
                    llvm.ModuleFlagBehavior.Warning,
                    i32.constant(1).as_metadata(),
                )

                entry = fn.append_basic_block("entry")
                with entry.create_builder() as builder:
                    with builder.debug_location(line=2, column=5, scope=subprogram):
                        result = builder.add(fn.get_param(0), fn.get_param(1), "result")

                    # Metadata kind lookup is internal to the mapping view.
                    result.metadata["example.note"] = ctx.md_node(
                        [ctx.md_string("created by metadata_debug_info.py")]
                    )
                    builder.ret(result)

                dib.finalize()

            assert mod.verify(), mod.verification_error
            return str(mod)


def main() -> None:
    print(build_module(), end="")


if __name__ == "__main__":
    main()
