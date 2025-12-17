#!/usr/bin/env -S uv run
"""
Test: Does using func.type from a bitcode-loaded function cause issues?

This test demonstrates a potential issue where using the wrong type attribute
can lead to crashes when cloning functions.
"""

import llvm
from pathlib import Path

print("Loading bitcode...")
bitcode_path = Path(__file__).parent / "syncscope.bc"
with open(bitcode_path, "rb") as f:
    bitcode = f.read()

with llvm.create_context() as ctx:
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
