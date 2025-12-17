#!/usr/bin/env -S uv run
"""
Minimal reproduction - closer to echo.py behavior.

This uses the actual function parameter like echo.py does.

Run with:
    cat test_syncscope_crash.bc | uv run test_syncscope_minimal2.py
"""

import sys
import llvm

with llvm.create_context() as ctx:
    # Load source module from stdin
    buf = llvm.create_memory_buffer_with_stdin()
    src = llvm.parse_bitcode_in_context(ctx, buf)

    print("Source module loaded", file=sys.stderr)

    # Get the syncscope ID from source
    src_func = src.get_function("test")
    src_bb = src_func.first_basic_block
    src_inst = src_bb.first_instruction
    sync_scope_id = src_inst.get_atomic_sync_scope_id()

    print(f"Source sync_scope_id={sync_scope_id}", file=sys.stderr)

    # Print source module (should work)
    print("Source module:", file=sys.stderr)
    print(str(src)[:200], file=sys.stderr)

    # Create destination module in same context
    with ctx.create_module(src.name) as dst:
        # Copy module properties
        dst.source_filename = src.source_filename
        dst.target_triple = src.target_triple
        dst.data_layout = src.data_layout

        # Clone the function
        func_ty = src_func.type
        dst_func = dst.add_function("test", func_ty)

        # Clone the basic block and instruction
        with ctx.create_builder() as builder:
            bb = dst_func.append_basic_block("", ctx)
            builder.position_at_end(bb)

            # Get parameters from BOTH functions
            src_param = src_func.first_param()
            dst_param = dst_func.first_param()

            print(f"src_param={src_param}, dst_param={dst_param}", file=sys.stderr)

            # Clone the atomic instruction using the DESTINATION parameter
            # (This is what echo.py does via clone_value)
            i8 = ctx.int8_type()
            val = llvm.const_int(i8, 0)

            print(
                f"Creating atomic with dst_param and sync_scope_id={sync_scope_id}",
                file=sys.stderr,
            )

            atomic = builder.atomic_rmw_sync_scope(
                llvm.AtomicRMWBinOp.Xchg,
                dst_param,  # <-- Using dest function's parameter
                val,
                llvm.AtomicOrdering.AcquireRelease,
                sync_scope_id,
            )
            atomic.set_volatile(True)
            atomic.set_alignment(8)

            print("Atomic created", file=sys.stderr)

            builder.ret_void()

        print("Trying to print destination module...", file=sys.stderr)
        output = str(dst)

        print("SUCCESS!", file=sys.stderr)
        print(output)
