"""Attach custom metadata to an instruction and print LLVM IR.

Run from the repository root with:

    uv run python examples/instruction_metadata.py
"""

from __future__ import annotations

import llvm


def build_module() -> str:
    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        fn_type = ctx.types.function(i32, [i32])

        with ctx.create_module("instruction_metadata_example") as mod:
            fn = mod.add_function("bump", fn_type)
            entry = fn.append_basic_block("entry")

            with entry.create_builder() as builder:
                result = builder.add(fn.get_param(0), i32.constant(1), "result")

                # Metadata is addressed by its kind name. No metadata kind ID is
                # exposed in the Python API.
                note_text = ctx.md_string("created by instruction_metadata.py")
                note = ctx.md_node([note_text])
                result.metadata["example.note"] = note

                # Inspect the attached metadata through the same mapping view.
                attached_note = result.metadata["example.note"]
                has_note = "example.note" in result.metadata
                round_trip_matches = result.metadata.get("example.note") == note
                note_operand_count = len(attached_note.operands)
                note_text_value = attached_note.operands[0].string

                builder.ret(result)

            assert mod.verify(), mod.verification_error
            return (
                "instruction metadata:\n"
                f"  has example.note: {has_note}\n"
                f"  round trip matches: {round_trip_matches}\n"
                f"  note operands: {note_operand_count}\n"
                f"  note text: {note_text_value}\n"
                "\nIR:\n"
                f"{mod}"
            )


def main() -> None:
    print(build_module(), end="")


if __name__ == "__main__":
    main()
