#!/usr/bin/env -S uv run
"""
ACTUAL BUG: Crash when getting parameters from a freshly created function.

This has NOTHING to do with syncscope! The crash happens when you:
1. Clone a function from source to destination
2. Try to get parameters from the destination function
3. Before the function body is fully set up

Run:
    uv run test_function_param_crash.py
"""

import llvm

with llvm.create_context() as ctx:
    with ctx.create_module("test") as mod:
        # Create source function
        ptr_ty = ctx.types.ptr()
        func_ty = ctx.types.function(ctx.types.void, [ptr_ty])
        src_func = mod.add_function("source", func_ty)

        # Add a basic block to source
        with ctx.create_builder() as builder:
            bb = src_func.append_basic_block("entry", ctx)
            builder.position_at_end(bb)
            builder.ret_void()

        print("Source function created and works fine")
        print(f"Source first_param: {src_func.first_param()}")

        # Now create a destination function
        dst_func = mod.add_function("dest", func_ty)

        print("Destination function created")
        print("Trying to get dst_func.first_param()...")

        # THIS CRASHES!
        param = dst_func.first_param()

        print(f"Destination first_param: {param}")
