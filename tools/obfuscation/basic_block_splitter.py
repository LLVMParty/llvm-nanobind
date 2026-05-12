#!/usr/bin/env -S uv run
"""
Basic Block Shuffler Pass

Shuffles basic blocks in functions to make control flow harder to follow.

Note: Full block splitting requires builder.split_block_before() which is not
currently available in the Python bindings. This pass shuffles existing blocks.

Usage:
    uv run tools/obfuscation/basic_block_splitter.py [options] < input.ll > output.ll
    uv run tools/obfuscation/basic_block_splitter.py [options] -o output.ll input.ll

Options:
    -o FILE           Output file (default: stdout)
"""

import argparse
import random
import sys

import llvm


def shuffle_blocks(func: llvm.Function) -> None:
    """Shuffle non-entry basic blocks in a function."""
    blocks = list(func.basic_blocks)

    if len(blocks) <= 1:
        return

    entry = blocks[0]
    other_blocks = blocks[1:]

    random.shuffle(other_blocks)

    # Move blocks to end in shuffled order
    for bb in other_blocks:
        bb.move_after(list(func.basic_blocks)[-1])


def main():
    parser = argparse.ArgumentParser(
        description="Shuffle basic blocks in LLVM IR functions"
    )
    parser.add_argument("input", nargs="?", help="Input LLVM IR file (default: stdin)")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument("--seed", type=int, help="Random seed for reproducibility")
    parser.add_argument(
        "--functions",
        help="Comma-separated list of function names to process (default: all)",
    )

    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    # Read input
    if args.input:
        with open(args.input, "r") as f:
            ir_text = f.read()
    else:
        ir_text = sys.stdin.read()

    with llvm.create_context() as ctx:
        # Parse the IR
        try:
            with ctx.parse_ir(ir_text) as mod:
                # Process functions
                target_funcs = None
                if args.functions:
                    target_funcs = set(args.functions.split(","))

                for func in mod.functions:
                    if func.is_declaration:
                        continue
                    if target_funcs and func.name not in target_funcs:
                        continue

                    shuffle_blocks(func)

                # Verify
                if not mod.verify():
                    print(
                        f"Error: Module verification failed: {mod.verification_error}",
                        file=sys.stderr,
                    )
                    return 1

                # Output
                output = mod.to_string()
                if args.output:
                    with open(args.output, "w") as f:
                        f.write(output)
                else:
                    print(output, end="")
        except llvm.LLVMError as e:
            print(f"Error parsing IR: {e}", file=sys.stderr)
            return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
