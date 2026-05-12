#!/usr/bin/env -S uv run
"""
Control Flow Flattening Pass

Flattens the control flow of functions by introducing a dispatcher that
controls which block executes next based on a state variable.

The transformation:
    LABEL_A: bool b = x == 2;
             IF EQ: goto LABEL_B
             goto LABEL_C
    LABEL_B: do_stuff()
    LABEL_C: do_other_stuff()
             goto LABEL_A

becomes:
    int state = 0;
    DISPATCHER: switch(state) {
        case STATE_A: goto LABEL_A;
        case STATE_B: goto LABEL_B;
        case STATE_C: goto LABEL_C;
    }
    LABEL_A: bool b = x == 2;
             state = b ? STATE_B : STATE_C;
             goto DISPATCHER;
    LABEL_B: do_stuff()
             goto DISPATCHER;
    LABEL_C: do_other_stuff()
             state = STATE_A;
             goto DISPATCHER;

Usage:
    uv run tools/obfuscation/control_flow_flattening.py [options] < input.ll > output.ll
    uv run tools/obfuscation/control_flow_flattening.py [options] -o output.ll input.ll

Options:
    --iterations N      Number of times to run the pass (default: 1)
    --use-globals       Store state values in global variables (harder to analyze)
    --shuffle           Shuffle condition blocks order (default: on)
    --no-shuffle        Don't shuffle condition blocks
    -o FILE             Output file (default: stdout)
"""

import argparse
import random
import sys

import llvm


def generate_unique_state(existing_states: set[int]) -> int:
    """Generate a unique random state value (uses 32-bit for compatibility)."""
    max_val = 0x7FFFFFFF  # Max signed 32-bit
    min_val = 0x000F0000  # Avoid small values

    while True:
        state = random.randint(min_val, max_val)
        if state not in existing_states:
            existing_states.add(state)
            return state


def run_on_function(
    func: llvm.Function,
    iterations: int,
    use_globals: bool,
    shuffle: bool,
) -> None:
    """Apply control flow flattening to a function."""

    for _ in range(iterations):
        flatten_function(func, use_globals, shuffle)


def demote_phi_to_stack(func: llvm.Function) -> None:
    """
    Demote all PHI nodes in a function to stack variables.

    For each PHI node:
    1. Create an alloca in the entry block
    2. Store each incoming value at the end of its predecessor block
    3. Load from the alloca where the PHI was
    4. Replace uses of PHI with the load
    5. Delete the PHI
    """
    entry_bb = list(func.basic_blocks)[0]

    # Collect all PHI nodes first
    phi_nodes = []
    for bb in func.basic_blocks:
        for inst in bb.instructions:
            if inst.opcode == llvm.Opcode.PHI:
                phi_nodes.append(inst)

    if not phi_nodes:
        return

    # Get first instruction in entry for alloca placement
    entry_first = list(entry_bb.instructions)[0]

    for phi in phi_nodes:
        phi_bb = phi.block
        phi_type = phi.type

        # Create alloca at entry
        with entry_bb.create_builder() as builder:
            builder.position_before(entry_first)
            alloca = builder.alloca(phi_type, name=f"{phi.name}.demoted")

        # Store each incoming value at the end of its predecessor
        for i in range(phi.num_incoming):
            incoming_val = phi.get_incoming_value(i)
            incoming_bb = phi.get_incoming_block(i)

            # Find terminator of incoming block
            incoming_term = incoming_bb.terminator

            with incoming_bb.create_builder() as builder:
                builder.position_before(incoming_term)
                builder.store(incoming_val, alloca)

        # Load the value where the PHI was
        with phi_bb.create_builder() as builder:
            builder.position_before(phi)
            load = builder.load(phi_type, alloca, phi.name)

        # Replace uses of PHI with the load
        phi.replace_all_uses_with(load)

        # Delete the PHI
        phi.erase_from_parent()


def flatten_function(func: llvm.Function, use_globals: bool, shuffle: bool) -> None:
    """Flatten control flow of a single function."""

    blocks = list(func.basic_blocks)
    if len(blocks) < 2:
        return

    # Demote PHI nodes to stack variables first
    demote_phi_to_stack(func)

    ctx = func.context
    mod = func.module

    # Use i32 for state values (simpler and more compatible)
    int_ty = ctx.types.i32

    entry_bb = blocks[0]
    original_blocks = blocks[1:]  # Blocks to flatten (excluding entry)

    if not original_blocks:
        return

    # Generate unique state for each block
    states: set[int] = set()
    block_state_map: dict[llvm.BasicBlock, int] = {}

    for bb in original_blocks:
        state = generate_unique_state(states)
        block_state_map[bb] = state

    # Create state variable at entry
    first_inst = list(entry_bb.instructions)[0]
    with entry_bb.create_builder() as builder:
        builder.position_before(first_inst)
        state_var = builder.alloca(int_ty, name="cff.state")
        builder.store(int_ty.constant(0), state_var)

    # Create dispatcher block
    dispatch_bb = func.append_basic_block("cff.dispatch")

    # Create condition check blocks
    cond_blocks = []
    for i, bb in enumerate(original_blocks):
        cond_bb = func.append_basic_block(f"cff.cond.{i}")
        cond_blocks.append(cond_bb)

    # Shuffle condition blocks if requested
    if shuffle:
        combined = list(zip(cond_blocks, original_blocks))
        random.shuffle(combined)
        cond_blocks, original_blocks_shuffled = zip(*combined)
        cond_blocks = list(cond_blocks)
        original_blocks = list(original_blocks_shuffled)

    # Create default block (loops back to dispatcher)
    default_bb = func.append_basic_block("cff.default")
    with default_bb.create_builder() as builder:
        builder.br(dispatch_bb)

    # Fill dispatcher: branch to first condition
    with dispatch_bb.create_builder() as builder:
        builder.br(cond_blocks[0])

    # Fill condition blocks
    for i, (cond_bb, target_bb) in enumerate(zip(cond_blocks, original_blocks)):
        with cond_bb.create_builder() as builder:
            target_state = block_state_map[target_bb]

            # Load current state
            current_state = builder.load(int_ty, state_var, "cff.state.val")

            # Get target state value (optionally from global)
            if use_globals:
                gv = mod.add_global(int_ty, f"__cff_state_{target_state}")
                gv.initializer = int_ty.constant(target_state)
                gv.linkage = llvm.Linkage.Private
                target_val = builder.load(int_ty, gv, "cff.target")
            else:
                target_val = int_ty.constant(target_state)

            # Compare
            cmp = builder.icmp(
                llvm.IntPredicate.EQ, current_state, target_val, "cff.cmp"
            )

            # Branch to target or next condition
            if i < len(cond_blocks) - 1:
                next_cond = cond_blocks[i + 1]
            else:
                next_cond = default_bb

            builder.cond_br(cmp, target_bb, next_cond)

    # Modify original blocks to update state and go to dispatcher
    # Also include entry block
    all_blocks_to_modify = original_blocks + [entry_bb]

    for bb in all_blocks_to_modify:
        instructions = list(bb.instructions)
        if not instructions:
            continue

        terminator = instructions[-1]

        if terminator.opcode == llvm.Opcode.Br:
            successors = list(terminator.successors)

            if len(successors) == 1:
                # Unconditional branch
                target = successors[0]
                if target in block_state_map:
                    with bb.create_builder() as builder:
                        builder.position_before(terminator)
                        builder.store(
                            int_ty.constant(block_state_map[target]), state_var
                        )
                        builder.br(dispatch_bb)
                    terminator.erase_from_parent()

            elif len(successors) == 2:
                # Conditional branch
                true_bb = successors[0]
                false_bb = successors[1]

                if true_bb in block_state_map and false_bb in block_state_map:
                    condition = terminator.condition

                    # Create state-setting blocks
                    true_state_bb = func.append_basic_block("cff.true_state")
                    false_state_bb = func.append_basic_block("cff.false_state")

                    with true_state_bb.create_builder() as builder:
                        builder.store(
                            int_ty.constant(block_state_map[true_bb]), state_var
                        )
                        builder.br(dispatch_bb)

                    with false_state_bb.create_builder() as builder:
                        builder.store(
                            int_ty.constant(block_state_map[false_bb]), state_var
                        )
                        builder.br(dispatch_bb)

                    # Replace original branch
                    with bb.create_builder() as builder:
                        builder.position_before(terminator)
                        builder.cond_br(condition, true_state_bb, false_state_bb)

                    terminator.erase_from_parent()


def main():
    parser = argparse.ArgumentParser(
        description="Flatten control flow in LLVM IR using a state-machine dispatcher"
    )
    parser.add_argument("input", nargs="?", help="Input LLVM IR file (default: stdin)")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
    parser.add_argument(
        "--iterations", type=int, default=1, help="Number of pass iterations"
    )
    parser.add_argument(
        "--use-globals",
        action="store_true",
        help="Store state values in global variables",
    )
    parser.add_argument(
        "--shuffle",
        action="store_true",
        default=True,
        help="Shuffle condition block order (default)",
    )
    parser.add_argument(
        "--no-shuffle",
        action="store_false",
        dest="shuffle",
        help="Don't shuffle condition blocks",
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

                    run_on_function(
                        func, args.iterations, args.use_globals, args.shuffle
                    )

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
