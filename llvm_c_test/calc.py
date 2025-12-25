"""
Implementation of --calc command.

RPN calculator that generates LLVM IR.
"""

import llvm
from .helpers import tokenize_stdin


def op_to_opcode(op):
    """Convert operator character to LLVM opcode."""
    ops = {
        "+": llvm.Opcode.Add,
        "-": llvm.Opcode.Sub,
        "*": llvm.Opcode.Mul,
        "/": llvm.Opcode.SDiv,
        "&": llvm.Opcode.And,
        "|": llvm.Opcode.Or,
        "^": llvm.Opcode.Xor,
    }
    if op not in ops:
        raise ValueError(f"Unknown operation: {op}")
    return ops[op]


def build_from_tokens(tokens, builder, param, i64_ty):
    """
    Build IR from RPN token list.

    Returns the final value on success, None on error.
    """
    stack = []

    for tok in tokens:
        if tok in ["+", "-", "*", "/", "&", "|", "^"]:
            # Binary operation
            if len(stack) < 2:
                print("stack underflow")
                return None

            # Pop two operands (note: order matters for non-commutative ops)
            rhs = stack.pop()
            lhs = stack.pop()

            # Build binary operation
            result = builder.binop(op_to_opcode(tok), lhs, rhs, "")
            stack.append(result)

        elif tok == "@":
            # Load from memory: pop offset, compute GEP, load
            if len(stack) < 1:
                print("stack underflow")
                return None

            offset = stack.pop()
            ptr = builder.gep(i64_ty, param, [offset], "")
            value = builder.load(i64_ty, ptr, "")
            stack.append(value)

        else:
            # Number - parse and push constant
            try:
                val = int(tok, 0)  # Supports hex, octal, decimal
            except ValueError:
                print("error parsing number")
                return None

            const = i64_ty.constant(val, sign_extend=True)
            stack.append(const)

    if len(stack) < 1:
        print("stack underflow at return")
        return None

    # Build return instruction
    builder.ret(stack[-1])
    return stack[-1]


def handle_line(tokens):
    """Process a single line of input."""
    name = tokens[0]
    expr_tokens = tokens[1:]

    # Create module
    with llvm.create_context() as ctx:
        with ctx.create_module(name) as mod:
            # Create function type: i64(i64*)
            i64_ty = ctx.types.i64
            i64_ptr_ty = ctx.types.ptr()
            func_ty = ctx.types.function(i64_ty, [i64_ptr_ty], vararg=False)

            # Add function
            func = mod.add_function(name, func_ty)
            param = func.get_param(0)
            param.name = "in"

            # Create entry block
            entry = func.append_basic_block("entry")

            # Create builder and build IR
            with entry.create_builder() as builder:
                result = build_from_tokens(expr_tokens, builder, param, i64_ty)

                if result:
                    # Print module
                    print(mod.to_string(), end="")


def calc():
    """Main calc command handler."""
    tokenize_stdin(handle_line)
    return 0
