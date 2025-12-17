#!/usr/bin/env -S uv run
"""
Test: Does using global_get_value_type() fix the crash?

Run with:
    cat test_syncscope_crash.bc | uv run test_type_crash3.py
"""

import llvm

print("Loading bitcode...")
with llvm.create_context() as ctx:
    buf = llvm.create_memory_buffer_with_stdin()
    src_mod = llvm.parse_bitcode_in_context(ctx, buf)

    src_func = src_mod.get_function("test")
    print(f"Source function: {src_func}")

    # WRONG: func.type returns the pointer type
    print(f"\nfunc.type (WRONG): {src_func.type}")

    # CORRECT: global_get_value_type returns the actual function type
    func_type = src_func.global_get_value_type()
    print(f"global_get_value_type() (CORRECT): {func_type}")

    # Create a new module in the SAME context
    with ctx.create_module("dest") as dst_mod:
        print("\nCreating destination function with correct type...")
        dst_func = dst_mod.add_function("test_clone", func_type)

        print("Destination function created")
        print("Calling dst_func.first_param()...")

        # Should this work now?
        param = dst_func.first_param()

        print(f"SUCCESS: {param}")
        print("\nPrinting destination function:")
        print(dst_func)
