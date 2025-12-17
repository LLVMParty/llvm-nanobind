#!/usr/bin/env -S uv run
"""
Minimal reproduction of syncscope crash.

This script does the bare minimum:
1. Load a module with custom syncscope("agent")
2. Extract the syncscope ID from the atomic instruction
3. Create a new module in the same context
4. Create an atomic instruction using that syncscope ID
5. Try to print the module -> CRASH

Run with:
    cat test_syncscope_crash.bc | uv run test_syncscope_minimal.py
"""

import sys
import llvm

with llvm.create_context() as ctx:
    # Load source module from stdin
    bitcode = sys.stdin.buffer.read()
    with ctx.parse_bitcode_from_bytes(bitcode) as src:
        print("Source module loaded", file=sys.stderr)

        # Get the syncscope ID from the source atomic instruction
        src_func = src.get_function("test")
        assert src_func is not None, "Function 'test' not found"
        src_bb = src_func.first_basic_block
        assert src_bb is not None, "Function must have at least one basic block"
        src_inst = src_bb.first_instruction
        assert src_inst is not None, "Basic block must have at least one instruction"

        # Extract syncscope ID
        sync_scope_id = src_inst.get_atomic_sync_scope_id()
        print(f"Source atomic has sync_scope_id={sync_scope_id}", file=sys.stderr)

    # Create a NEW module in the SAME context
    with ctx.create_module("dest") as dst:
        print("Destination module created", file=sys.stderr)

        # Create a simple function
        i8 = ctx.int8_type()
        ptr_ty = ctx.pointer_type()
        func_ty = ctx.function_type(ctx.void_type(), [ptr_ty])
        dst_func = dst.add_function("test_clone", func_ty)

        with ctx.create_builder() as builder:
            bb = dst_func.append_basic_block("entry", ctx)
            builder.position_at_end(bb)

            # Create a dummy pointer value (alloca)
            ptr = builder.alloca(i8, "ptr")
            val = llvm.const_int(i8, 0)

            print(
                f"Creating atomic with sync_scope_id={sync_scope_id}", file=sys.stderr
            )

            # Create atomic with the syncscope ID from the source
            atomic = builder.atomic_rmw_sync_scope(
                llvm.AtomicRMWBinOp.Xchg,
                ptr,
                val,
                llvm.AtomicOrdering.AcquireRelease,
                sync_scope_id,  # <-- Using syncscope ID from source module
            )
            atomic.set_volatile(True)
            atomic.set_alignment(8)

            print("Atomic created", file=sys.stderr)

            builder.ret_void()

        print("Trying to print destination module...", file=sys.stderr)

        # This should crash!
        output = str(dst)

        print("SUCCESS! Module printed:", file=sys.stderr)
        print(output)
