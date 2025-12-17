#!/usr/bin/env -S uv run
"""
Test: What does func.type actually return?

Run with:
    cat test_syncscope_crash.bc | uv run test_type_crash2.py
"""

import llvm

print("Loading bitcode...")
with llvm.create_context() as ctx:
    buf = llvm.create_memory_buffer_with_stdin()
    src_mod = llvm.parse_bitcode_in_context(ctx, buf)

    src_func = src_mod.get_function("test")
    print(f"Source function: {src_func}")

    func_type = src_func.type
    print(f"func.type: {func_type}")
    print(f"type(func.type): {type(func_type)}")

    # What should we use instead?
    # In the C code they use LLVMGlobalGetValueType(Cur)
    # which is different from LLVMTypeOf(Cur)

    # Let me check if there's a method for that
    print("\nLooking for value_type or global_value_type methods...")
    methods = [m for m in dir(src_func) if "type" in m.lower()]
    for m in methods:
        print(f"  {m}")
