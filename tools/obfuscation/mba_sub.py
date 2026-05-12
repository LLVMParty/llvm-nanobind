#!/usr/bin/env -S uv run
"""
Mixed Boolean Arithmetic Substitution Pass

Replaces simple arithmetic and logical operations with mathematically
equivalent but more complex expressions.

Substitutions:
- Sub: X - Y == (X ^ -Y) + 2*(X & -Y)
- Add: Multiple variants including random constant obfuscation
- Xor: (~a & b) | (a & ~b), (a | b) & ~(a & b), (a + b) - 2*(a & b), etc.
- Mul: (((b | c) * (b & c)) + ((b & ~c) * (c & ~b)))
- Or:  ~(~a & ~b), a ^ b ^ (a & b), (a + b) - (a & b)

Usage:
    uv run tools/obfuscation/mba_sub.py [options] < input.ll > output.ll
    uv run tools/obfuscation/mba_sub.py [options] -o output.ll input.ll

Options:
    --iterations N    Number of times to run the pass (default: 1)
    -o FILE           Output file (default: stdout)
"""

import argparse
import random
import sys
import llvm


# =============================================================================
# MBA Substitution Functions
# =============================================================================


def obfuscate_sub(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    X - Y == (X ^ -Y) + 2*(X & -Y)
    """
    neg_b = builder.neg(b, "mba.neg")
    xor_val = builder.xor(a, neg_b, "mba.xor")
    and_val = builder.and_(a, neg_b, "mba.and")

    # Get the type for constant 2
    ty = a.type
    two = ty.constant(2)

    mul_val = builder.mul(two, and_val, "mba.mul")
    result = builder.add(xor_val, mul_val, name or "mba.sub")
    return result


def obfuscate_add_v1(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    x + y = ~(x + ((-x) + ((-x) + (~y))))
    """
    neg_a = builder.neg(a, "mba.neg_a")
    not_b = builder.not_(b, "mba.not_b")

    inner = builder.add(neg_a, not_b, "mba.inner1")
    middle = builder.add(neg_a, inner, "mba.inner2")
    outer = builder.add(a, middle, "mba.inner3")
    result = builder.not_(outer, name or "mba.add")
    return result


def obfuscate_add_v2(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    r = rand(); c = b + r; a = a + c; a = a - r
    """
    ty = a.type
    r = ty.constant(random.randint(0, 2**32 - 1))

    c = builder.add(b, r, "mba.c")
    a_plus_c = builder.add(a, c, "mba.a_plus_c")
    result = builder.sub(a_plus_c, r, name or "mba.add")
    return result


def obfuscate_add(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """Randomly choose an add obfuscation."""
    choice = random.randint(0, 1)
    if choice == 0:
        return obfuscate_add_v1(builder, a, b, name)
    else:
        return obfuscate_add_v2(builder, a, b, name)


def obfuscate_xor_v1(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    a ^ b = (~a & b) | (a & ~b)
    """
    not_a = builder.not_(a, "mba.not_a")
    not_b = builder.not_(b, "mba.not_b")

    left = builder.and_(not_a, b, "mba.left")
    right = builder.and_(a, not_b, "mba.right")
    result = builder.or_(left, right, name or "mba.xor")
    return result


def obfuscate_xor_v2(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    a ^ b = (a | b) & ~(a & b)
    """
    or_val = builder.or_(a, b, "mba.or")
    and_val = builder.and_(a, b, "mba.and")
    not_and = builder.not_(and_val, "mba.not_and")
    result = builder.and_(or_val, not_and, name or "mba.xor")
    return result


def obfuscate_xor_v3(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    a ^ b = (a + b) - 2 * (a & b)
    """
    ty = a.type
    two = ty.constant(2)

    add_val = builder.add(a, b, "mba.add")
    and_val = builder.and_(a, b, "mba.and")
    mul_val = builder.mul(two, and_val, "mba.mul")
    result = builder.sub(add_val, mul_val, name or "mba.xor")
    return result


def obfuscate_xor_v4(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    a ^ b = ~(~a & ~b) & ~(a & b)
    """
    not_a = builder.not_(a, "mba.not_a")
    not_b = builder.not_(b, "mba.not_b")

    and_nots = builder.and_(not_a, not_b, "mba.and_nots")
    not_and_nots = builder.not_(and_nots, "mba.not_and_nots")

    and_ab = builder.and_(a, b, "mba.and_ab")
    not_and_ab = builder.not_(and_ab, "mba.not_and_ab")

    result = builder.and_(not_and_nots, not_and_ab, name or "mba.xor")
    return result


def obfuscate_xor(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """Randomly choose an xor obfuscation."""
    choice = random.randint(0, 3)
    if choice == 0:
        return obfuscate_xor_v1(builder, a, b, name)
    elif choice == 1:
        return obfuscate_xor_v2(builder, a, b, name)
    elif choice == 2:
        return obfuscate_xor_v3(builder, a, b, name)
    else:
        return obfuscate_xor_v4(builder, a, b, name)


def obfuscate_mul(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    b * c = (((b | c) * (b & c)) + ((b & ~c) * (c & ~b)))
    """
    not_a = builder.not_(a, "mba.not_a")
    not_b = builder.not_(b, "mba.not_b")

    or_ab = builder.or_(a, b, "mba.or_ab")
    and_ab = builder.and_(a, b, "mba.and_ab")
    mul1 = builder.mul(or_ab, and_ab, "mba.mul1")

    a_and_not_b = builder.and_(a, not_b, "mba.a_and_not_b")
    b_and_not_a = builder.and_(b, not_a, "mba.b_and_not_a")
    mul2 = builder.mul(a_and_not_b, b_and_not_a, "mba.mul2")

    result = builder.add(mul1, mul2, name or "mba.mul")
    return result


def obfuscate_or_v1(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    a | b = ~(~a & ~b)
    """
    not_a = builder.not_(a, "mba.not_a")
    not_b = builder.not_(b, "mba.not_b")
    and_val = builder.and_(not_a, not_b, "mba.and")
    result = builder.not_(and_val, name or "mba.or")
    return result


def obfuscate_or_v2(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    a | b = a ^ b ^ (a & b)
    """
    xor_ab = builder.xor(a, b, "mba.xor_ab")
    and_ab = builder.and_(a, b, "mba.and_ab")
    result = builder.xor(xor_ab, and_ab, name or "mba.or")
    return result


def obfuscate_or_v3(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """
    a | b = (a + b) - (a & b)
    """
    add_ab = builder.add(a, b, "mba.add_ab")
    and_ab = builder.and_(a, b, "mba.and_ab")
    result = builder.sub(add_ab, and_ab, name or "mba.or")
    return result


def obfuscate_or(
    builder: llvm.Builder, a: llvm.Value, b: llvm.Value, name: str = ""
) -> llvm.Value:
    """Randomly choose an or obfuscation."""
    choice = random.randint(0, 2)
    if choice == 0:
        return obfuscate_or_v1(builder, a, b, name)
    elif choice == 1:
        return obfuscate_or_v2(builder, a, b, name)
    else:
        return obfuscate_or_v3(builder, a, b, name)


# =============================================================================
# Pass Implementation
# =============================================================================


def run_on_basic_block(bb: llvm.BasicBlock) -> None:
    """Apply MBA substitutions to all eligible instructions in a basic block."""

    # Map opcodes to their obfuscation functions
    obfuscators = {
        llvm.Opcode.Sub: obfuscate_sub,
        llvm.Opcode.Add: obfuscate_add,
        llvm.Opcode.Xor: obfuscate_xor,
        llvm.Opcode.Mul: obfuscate_mul,
        llvm.Opcode.Or: obfuscate_or,
    }

    # Collect instructions to transform (avoid modifying while iterating)
    to_transform = []
    for inst in bb.instructions:
        if inst.opcode in obfuscators:
            # Only transform binary integer operations
            if inst.type.kind == llvm.TypeKind.Integer:
                to_transform.append((inst, obfuscators[inst.opcode]))

    # Transform each instruction
    for inst, obfuscator in to_transform:
        # Get operands - use get_operand() instead of .operands
        if inst.num_operands != 2:
            continue

        a = inst.get_operand(0)
        b = inst.get_operand(1)

        # Create builder positioned before this instruction
        with bb.create_builder() as builder:
            builder.position_before(inst)

            # Create obfuscated replacement
            replacement = obfuscator(builder, a, b, inst.name)

            # Replace uses and remove original
            inst.replace_all_uses_with(replacement)
            inst.erase_from_parent()


def run_on_function(func: llvm.Function, iterations: int) -> None:
    """Apply MBA substitutions to a function."""
    for _ in range(iterations):
        for bb in func.basic_blocks:
            run_on_basic_block(bb)


def main():
    parser = argparse.ArgumentParser(
        description="Apply Mixed Boolean Arithmetic substitutions to LLVM IR"
    )
    parser.add_argument("input", nargs="?", help="Input LLVM IR file (default: stdin)")
    parser.add_argument("-o", "--output", help="Output file (default: stdout)")
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

                    run_on_function(func, args.iterations)

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
