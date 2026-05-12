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
        bitcode = sys.stdin.buffer.read()
        ctx = llvm.global_context()

        with ctx.parse_bitcode_from_bytes(bitcode) as mod:
            # Iterate through functions
            for func in mod.functions:
                # Read attributes in all slots. The C version allocates and
                # frees but doesn't use the attributes; we just check counts.
                slots = [func.attributes, func.return_attributes]
                slots.extend(func.param_attributes(i) for i in range(func.param_count))
                for slot in slots:
                    attr_count = len(slot)
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
        bitcode = sys.stdin.buffer.read()
        ctx = llvm.global_context()

        with ctx.parse_bitcode_from_bytes(bitcode) as mod:
            # Iterate through functions
            for func in mod.functions:
                # Iterate through basic blocks
                bb = func.first_basic_block
                while bb is not None:
                    # Iterate through instructions
                    inst = bb.first_instruction
                    while inst is not None:
                        # Check if it's a call instruction
                        if inst.is_call_inst:
                            # Read call site attributes in all slots. The C
                            # version allocates and frees but doesn't use them.
                            slots = [
                                inst.callsite_attributes,
                                inst.callsite_return_attributes,
                            ]
                            slots.extend(
                                inst.callsite_param_attributes(i)
                                for i in range(inst.num_arg_operands)
                            )
                            for slot in slots:
                                attr_count = len(slot)
                                if attr_count < 0:
                                    raise ValueError(
                                        f"Invalid attribute count: {attr_count}"
                                    )

                        inst = inst.next_instruction

                    bb = bb.next_block

            return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
