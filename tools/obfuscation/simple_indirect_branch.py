#!/usr/bin/env -S uv run
"""
Simple Indirect Branching Pass

Converts direct branches into indirect branches through a jump table
stored on the stack. This breaks static analysis tools without affecting
runtime performance significantly.

The transformation:
    if (x == 2) goto LABEL_A
               goto LABEL_B
becomes:
    @stack jump_table = {&LABEL_B, &LABEL_A}
    goto jump_table[!(x == 2)]

Usage:
    uv run tools/obfuscation/simple_indirect_branch.py [options] < input.ll > output.ll
    uv run tools/obfuscation/simple_indirect_branch.py [options] -o output.ll input.ll

Options:
    --chance N        Percentage chance to transform each branch (default: 50)
    --iterations N    Number of times to run the pass (default: 1)
    -o FILE           Output file (default: stdout)
"""

import argparse
import random
import sys

import llvm


def compute_fake_index(builder: llvm.Builder, index: llvm.Value) -> llvm.Value:
    """
    Obfuscate the index computation using MBA (mixed boolean arithmetic).
    This applies: index ^ rand ^ rand = index (but with obfuscated XOR).
    """
    int_ty = index.type
    bit_width = int_ty.int_width

    rand_val = random.getrandbits(bit_width)
    rand_const = int_ty.constant(rand_val)

    # First XOR: index ^ rand
    xor1 = builder.xor(index, rand_const, "sibr.xor1")

    # Obfuscate: a ^ b = (~a & b) | (a & ~b)
    not_xor1 = builder.not_(xor1, "sibr.not1")
    not_rand = builder.not_(rand_const, "sibr.not_rand")

    left = builder.and_(not_xor1, rand_const, "sibr.left")
    right = builder.and_(xor1, not_rand, "sibr.right")

    result = builder.or_(left, right, "sibr.idx")
    return result


def transform_branch(
    func: llvm.Function, branch: llvm.Value, builder: llvm.Builder
) -> bool:
    """Transform a branch instruction into an indirect branch."""

    # Get successor blocks
    successors = list(branch.successors)
    if len(successors) == 0:
        return False

    is_conditional = len(successors) == 2

    # Create jump table type - array of 2 block address pointers
    ctx = func.context
    ptr_ty = ctx.types.ptr
    i32_ty = ctx.types.i32

    # Position at function entry for alloca
    entry_bb = list(func.basic_blocks)[0]
    first_inst = list(entry_bb.instructions)[0]
    builder.position_before(first_inst)

    # Create stack-allocated jump table
    array_ty = ctx.types.array(ptr_ty, 2)
    jump_table = builder.alloca(array_ty, name="sibr.table")

    # Position before the branch for the rest
    builder.position_before(branch)

    if is_conditional:
        # Store successors: [false_target, true_target]
        # So index 0 = false branch, index 1 = true branch
        # We'll use NOT(condition) as index
        false_bb = successors[1]
        true_bb = successors[0]

        # GEP to table[0] and store false target
        idx0 = builder.gep(
            array_ty, jump_table, [i32_ty.constant(0), i32_ty.constant(0)], "sibr.slot0"
        )
        false_addr = func.block_address(false_bb)
        builder.store(false_addr, idx0)

        # GEP to table[1] and store true target
        idx1 = builder.gep(
            array_ty, jump_table, [i32_ty.constant(0), i32_ty.constant(1)], "sibr.slot1"
        )
        true_addr = func.block_address(true_bb)
        builder.store(true_addr, idx1)

        # Get condition and invert it
        condition = branch.condition
        inverted = builder.not_(condition, "sibr.not_cond")

        # Zero-extend to i32 for indexing
        index = builder.zext(inverted, i32_ty, "sibr.idx")

        # Apply fake index obfuscation
        obf_index = compute_fake_index(builder, index)

        # Load target address
        target_gep = builder.gep(
            array_ty, jump_table, [i32_ty.constant(0), obf_index], "sibr.target_ptr"
        )
        target_addr = builder.load(ptr_ty, target_gep, "sibr.target")

        # Create indirect branch
        indir_br = builder.indirect_br(target_addr, 2)
        indir_br.add_destination(true_bb)
        indir_br.add_destination(false_bb)

    else:
        # Unconditional branch - still use table for obfuscation
        target_bb = successors[0]

        # Store target at table[0]
        idx0 = builder.gep(
            array_ty, jump_table, [i32_ty.constant(0), i32_ty.constant(0)], "sibr.slot0"
        )
        target_addr = func.block_address(target_bb)
        builder.store(target_addr, idx0)

        # Load from table[0]
        loaded_addr = builder.load(ptr_ty, idx0, "sibr.target")

        # Create indirect branch
        indir_br = builder.indirect_br(loaded_addr, 1)
        indir_br.add_destination(target_bb)

    # Remove original branch
    branch.erase_from_parent()

    return True


def run_on_function(func: llvm.Function, chance: int, iterations: int) -> None:
    """Apply simple indirect branching to a function."""

    if len(list(func.basic_blocks)) < 2:
        return

    for _ in range(iterations):
        # Collect branch instructions
        branches = []
        for bb in func.basic_blocks:
            for inst in bb.instructions:
                if inst.opcode == llvm.Opcode.Br:
                    if random.randint(1, 100) <= chance:
                        branches.append(inst)

        # Transform each branch
        for branch in branches:
            bb = branch.block
            with bb.create_builder() as builder:
                transform_branch(func, branch, builder)


def main():
    parser = argparse.ArgumentParser(
        description="Convert direct branches to indirect branches via stack jump tables"
    )
    parser.add_argument("input", nargs="?", help="Input LLVM IR file (default: stdin)")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument(
        "--chance",
        type=int,
        default=50,
        help="Percentage chance to transform each branch",
    )
    parser.add_argument(
        "--iterations", type=int, default=1, help="Number of pass iterations"
    )
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

                    run_on_function(func, args.chance, args.iterations)

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
