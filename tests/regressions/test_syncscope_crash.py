#!/usr/bin/env -S uv run
"""
Minimal reproduction - closer to echo.py behavior.

This uses the actual function parameter like echo.py does.
Demonstrates cloning atomic instructions with custom syncscopes.
"""

import sys
import llvm
from pathlib import Path

with llvm.create_context() as ctx:
    # Load source module from file
    bitcode_path = Path(__file__).parent / "syncscope.bc"
    with open(bitcode_path, "rb") as f:
        bitcode = f.read()
    with ctx.parse_bitcode_from_bytes(bitcode) as src:
        print("Source module loaded", file=sys.stderr)

        # Get the syncscope ID from source
        src_func = src.get_function("test")
        assert src_func is not None, "Function 'test' not found"
        src_bb = src_func.first_basic_block
        assert src_bb is not None, "Function must have at least one basic block"
        src_inst = src_bb.first_instruction
        assert src_inst is not None, "Basic block must have at least one instruction"
        sync_scope_id = src_inst.get_atomic_sync_scope_id()

        print(f"Source sync_scope_id={sync_scope_id}", file=sys.stderr)

        # Print source module (should work)
        print("Source module:", file=sys.stderr)
        print(str(src)[:200], file=sys.stderr)

        # Get references we need before src closes
        module_name = src.name
        source_filename = src.source_filename
        target_triple = src.target_triple
        data_layout = src.data_layout
        func_ty = src_func.type
        src_param = src_func.first_param()
        assert src_param is not None, "Function must have at least one parameter"

    # Create destination module in same context
    with ctx.create_module(module_name) as dst:
        # Copy module properties
        dst.source_filename = source_filename
        dst.target_triple = target_triple
        dst.data_layout = data_layout

        # Clone the function
        dst_func = dst.add_function("test", func_ty)

        # Clone the basic block and instruction
        with ctx.create_builder() as builder:
            bb = dst_func.append_basic_block("", ctx)
            builder.position_at_end(bb)

            # Get destination parameter
            dst_param = dst_func.first_param()
            assert dst_param is not None, "Function must have at least one parameter"

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
