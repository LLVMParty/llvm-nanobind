#!/usr/bin/env -S uv run
"""
Test: Does calling first_param() on a bitcode-loaded function work?

Run with:
    cat test_syncscope_crash.bc | uv run test_bitcode_param_crash.py
"""

import llvm
import sys

print("Loading bitcode from stdin...")
with llvm.create_context() as ctx:
    bitcode = sys.stdin.buffer.read()
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
