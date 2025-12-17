#!/usr/bin/env -S uv run
"""
Test: Does using func.type from a bitcode-loaded function cause issues?

Run with:
    cat test_syncscope_crash.bc | uv run test_type_crash.py
"""

import llvm

print("Loading bitcode...")
with llvm.create_context() as ctx:
    buf = llvm.create_memory_buffer_with_stdin()
    src_mod = llvm.parse_bitcode_in_context(ctx, buf)

    src_func = src_mod.get_function("test")
    print(f"Source function: {src_func}")
    print(f"Source function type: {src_func.type}")

    # Create a new module in the SAME context
    with ctx.create_module("dest") as dst_mod:
        print("\nCreating destination function with src_func.type...")
        dst_func = dst_mod.add_function("test_clone", src_func.type)

        print("Destination function created (not printing it yet)")
        print("Calling dst_func.first_param()...")

        # Does this crash?
        param = dst_func.first_param()

        print("SUCCESS: got param")
        print("Now trying to print dst_func...")
        print(f"{dst_func}")
