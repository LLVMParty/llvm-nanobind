#!/usr/bin/env -S uv run
"""
Final minimal syncscope test - demonstrates the remaining crash.

With the TypeCloner fix (using global_get_value_type), we can now:
1. Load a module with custom syncscopes ✓
2. Create a destination function with the correct type ✓
3. Clone atomic instructions with custom syncscopes ✓
4. But CRASH when trying to print the module ✗

Run with:
    cat test_syncscope_crash.bc | uv run test_syncscope_final.py
"""

import llvm
import sys

with llvm.create_context() as ctx:
    bitcode = sys.stdin.buffer.read()
    with ctx.parse_bitcode_from_bytes(bitcode) as src:
        src_func = src.get_function("test")
        assert src_func is not None, "Function 'test' not found"
        src_bb = src_func.first_basic_block
        assert src_bb is not None, "Function must have at least one basic block"
        src_inst = src_bb.first_instruction
        assert src_inst is not None, "Basic block must have at least one instruction"

        # Get syncscope ID from source
        scope_id = src_inst.get_atomic_sync_scope_id()
        print(f"Source syncscope ID: {scope_id}")

        # Get function type before src closes
        func_ty = src_func.global_get_value_type()

    # Create destination module in same context
    with ctx.create_module("dest") as dst:
        # Use global_get_value_type() instead of .type (BUG #1 FIX)
        dst_func = dst.add_function("test", func_ty)

        with ctx.create_builder() as builder:
            bb = dst_func.append_basic_block("entry", ctx)
            builder.position_at_end(bb)

            # Create atomic with same syncscope ID
            ptr = builder.alloca(ctx.int8_type(), "ptr")
            val = llvm.const_int(ctx.int8_type(), 0)

            atomic = builder.atomic_rmw_sync_scope(
                llvm.AtomicRMWBinOp.Xchg,
                ptr,
                val,
                llvm.AtomicOrdering.AcquireRelease,
                scope_id,  # Using scope ID from source
            )
            atomic.set_volatile(True)
            atomic.set_alignment(8)

            builder.ret_void()

        print("Module created successfully")
        print("Trying to print module...")

        # THIS CRASHES! (BUG #2)
        print(str(dst))
