"""
Implementation of attribute testing commands.

Includes:
- --test-function-attributes
- --test-callsite-attributes
"""

import sys
import llvm


def test_function_attributes():
    """Test function attribute enumeration (no output expected)."""
    try:
        # Read and parse module
        membuf = llvm.create_memory_buffer_with_stdin()
        ctx = llvm.global_context()
        mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=True)

        # Iterate through functions
        for func in mod.functions:
            # Read attributes at different indices
            param_count = func.param_count

            # Test function index and all parameter indices
            # AttributeFunctionIndex is -1, so iterate from -1 to param_count
            idx = llvm.AttributeFunctionIndex
            while idx <= param_count:
                attr_count = llvm.get_attribute_count_at_index(func, idx)
                idx += 1
                # The C version allocates and frees but doesn't use the attributes
                # We just check the count is valid
                if attr_count < 0:
                    raise ValueError(f"Invalid attribute count: {attr_count}")

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


def test_callsite_attributes():
    """Test callsite attribute enumeration (no output expected)."""
    try:
        # Read and parse module
        membuf = llvm.create_memory_buffer_with_stdin()
        ctx = llvm.global_context()
        mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=True)

        # Iterate through functions
        for func in mod.functions:
            param_count = func.param_count

            # Iterate through basic blocks
            bb = func.first_basic_block
            while bb is not None:
                # Iterate through instructions
                inst = bb.first_instruction
                while inst is not None:
                    # Check if it's a call instruction
                    if inst.is_a_call_inst():
                        # Read call site attributes at different indices
                        idx = llvm.AttributeFunctionIndex
                        while idx <= param_count:
                            attr_count = llvm.get_callsite_attribute_count(inst, idx)
                            if attr_count < 0:
                                raise ValueError(
                                    f"Invalid attribute count: {attr_count}"
                                )
                            idx += 1

                    inst = inst.next_instruction

                bb = bb.next_block

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
