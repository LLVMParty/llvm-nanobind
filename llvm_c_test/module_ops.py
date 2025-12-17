"""
Implementation of module operations commands.

Includes:
- --module-dump (and variants)
- --module-list-functions
- --module-list-globals
"""

import sys
import llvm


def module_dump(lazy=False, new=False):
    """
    Parse bitcode from stdin and print IR.

    Args:
        lazy: If True, use lazy loading
        new: If True, use new API (diagnostic handler)
    """
    try:
        # Read stdin into memory buffer
        membuf = llvm.create_memory_buffer_with_stdin()

        # Parse bitcode
        ctx = llvm.global_context()
        mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=lazy, new_api=new)

        # Print module IR
        print(mod.to_string(), end="")

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


def module_list_functions():
    """List functions in module with basic block and instruction counts."""
    try:
        # Read and parse module
        membuf = llvm.create_memory_buffer_with_stdin()
        ctx = llvm.global_context()
        mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=False)

        # Iterate through functions
        for func in mod.functions:
            if func.is_declaration():
                print(f"FunctionDeclaration: {func.name}")
            else:
                bb_count = func.basic_block_count
                print(f"FunctionDefinition: {func.name} [#bb={bb_count}]")

                # Count instructions and find calls
                nisn = 0
                nbb = 0

                bb = func.first_basic_block
                while bb is not None:
                    nbb += 1

                    inst = bb.first_instruction
                    while inst is not None:
                        nisn += 1

                        # Check if it's a call instruction
                        if inst.is_a_call_inst():
                            # Get the called function (last operand)
                            num_ops = inst.get_num_operands()
                            if num_ops > 0:
                                callee = inst.get_operand(num_ops - 1)
                                print(f" calls: {callee.name}")

                        inst = inst.next_instruction

                    bb = bb.next_block

                print(f" #isn: {nisn}")
                print(f" #bb: {nbb}\n")

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


def module_list_globals():
    """List global variables in module."""
    try:
        # Read and parse module
        membuf = llvm.create_memory_buffer_with_stdin()
        ctx = llvm.global_context()
        mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=False)

        # Iterate through globals
        for g in mod.globals:
            ty = g.type
            ty_str = str(ty)

            if g.is_declaration():
                print(f"GlobalDeclaration: {g.name} {ty_str}")
            else:
                print(f"GlobalDefinition: {g.name} {ty_str}")

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
