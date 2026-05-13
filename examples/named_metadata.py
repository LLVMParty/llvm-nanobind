"""Create module named metadata and print LLVM IR.

Run from the repository root with:

    uv run python examples/named_metadata.py
"""

from __future__ import annotations

import llvm


def build_module() -> str:
    with llvm.create_context() as ctx:
        with ctx.create_module("named_metadata_example") as mod:
            tags = mod.named_metadata["example.tags"]
            tags.append(ctx.md_node([ctx.md_string("frontend"), ctx.md_string("demo")]))
            tags.append(ctx.md_node([ctx.md_string("purpose"), ctx.md_string("docs")]))

            # Inspect named metadata through the module mapping view.
            keys = list(mod.named_metadata)
            operand_count = len(mod.named_metadata["example.tags"])
            iterated_count = sum(1 for _ in mod.named_metadata["example.tags"])

            first_tag = mod.named_metadata["example.tags"][0]
            first_tag_strings = [operand.string for operand in first_tag.operands]

            assert keys == ["example.tags"]
            assert operand_count == 2
            assert iterated_count == 2
            assert first_tag_strings == ["frontend", "demo"]
            assert mod.verify(), mod.verification_error
            return (
                "named metadata:\n"
                f"  keys: {', '.join(keys)}\n"
                f"  example.tags operands: {operand_count}\n"
                f"  iterated operands: {iterated_count}\n"
                f"  first tag strings: {', '.join(first_tag_strings)}\n"
                "\nIR:\n"
                f"{mod}"
            )


def main() -> None:
    print(build_module(), end="")


if __name__ == "__main__":
    main()
