#!/usr/bin/env -S uv run
"""
Test: Does using func.type from a bitcode-loaded function cause issues?

Run with:
    cat test_syncscope_crash.bc | uv run test_type_crash.py
"""

import llvm
import sys

print("Loading bitcode...")
with llvm.create_context() as ctx:
    bitcode = sys.stdin.buffer.read()
    with ctx.parse_bitcode_from_bytes(bitcode) as src_mod:
        src_func = src_mod.get_function("test")
        assert src_func is not None, "Function 'test' not found"
        print(f"Source function: {src_func}")
        print(f"Source function type: {src_func.type}")

        # Save the type before src_mod closes
        func_type = src_func.type

    # Create a new module in the SAME context
    with ctx.create_module("dest") as dst_mod:
        print("\nCreating destination function with src_func.type...")
        dst_func = dst_mod.add_function("test_clone", func_type)

        print("Destination function created (not printing it yet)")
        print("Calling dst_func.first_param()...")

        # Does this crash?
        param = dst_func.first_param()

        print("SUCCESS: got param")
        print("Now trying to print dst_func...")
        print(f"{dst_func}")
