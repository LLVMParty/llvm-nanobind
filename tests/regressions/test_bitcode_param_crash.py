#!/usr/bin/env -S uv run
"""
Test: Does calling first_param() on a bitcode-loaded function work?

This test verifies that getting parameters from a function loaded from
bitcode works correctly without crashing.
"""

import llvm
from pathlib import Path

print("Loading bitcode from file...")
bitcode_path = Path(__file__).parent / "syncscope.bc"
with open(bitcode_path, "rb") as f:
    bitcode = f.read()

with llvm.create_context() as ctx:
    with ctx.parse_bitcode_from_bytes(bitcode) as mod:
        print("Bitcode loaded successfully")

        func = mod.get_function("test")
        assert func is not None, "Function 'test' not found in module"
        print(f"Function loaded: {func}")
        print(f"Function has {func.param_count} parameters")

        print("\nCalling first_param()...")
        try:
            param = func.first_param()
            print(f"SUCCESS: first_param = {param}")
        except Exception as e:
            print(f"FAILED: {e}")
