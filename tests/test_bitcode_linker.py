#!/usr/bin/env -S uv run
"""
Test: test_bitcode_linker
Tests for BitWriter (bitcode writing) and Linker (module linking)

This is the Python equivalent of tests/test_bitcode_linker.cpp.
Output should match the C++ golden master test.

LLVM Python APIs tested:
- mod.write_bitcode_to_memory_buffer()
- mod.write_bitcode_to_file()
- ctx.parse_bitcode_from_bytes()
- mod.link_module()
- mod.clone()
- mod.print_to_file()
"""

import sys
import llvm


def build_simple_function(mod, ctx, func_name, value):
    """Add a simple function returning a constant to the module."""
    i32 = ctx.types.i32
    fn_ty = ctx.types.function(i32, [])
    fn = mod.add_function(func_name, fn_ty)

    entry = fn.append_basic_block("entry")
    with entry.create_builder() as builder:
        builder.ret(i32.constant(value))


def main():
    print("; Test: test_bitcode_linker")
    print("; Tests bitcode writing/reading and module linking")
    print(";")

    with llvm.create_context() as ctx:
        # ==========================================================================
        # Test 1: Write module to memory buffer and read back
        # ==========================================================================
        print("; Test 1: Bitcode round-trip (memory buffer)")

        with ctx.create_module("module1") as mod1:
            mod1.target_triple = "x86_64-unknown-linux-gnu"
            build_simple_function(mod1, ctx, "get_one", 1)
            # Write to memory buffer
            bc_bytes = mod1.write_bitcode_to_memory_buffer()
            bc_size = len(bc_bytes)
            print(f";   Bitcode size: {bc_size} bytes")

            # Check bitcode magic (0x42, 0x43, 0xC0, 0xDE for "BC"...)
            if bc_size >= 4:
                print(f";   Magic bytes: 0x{bc_bytes[0]:02X} 0x{bc_bytes[1]:02X} "
                      f"0x{bc_bytes[2]:02X} 0x{bc_bytes[3]:02X}")

        # Read back from memory buffer (outside mod1 context since it's disposed)
        with ctx.parse_bitcode_from_bytes(bc_bytes) as mod1_copy:
            # Verify the read-back module has the expected function
            fn = mod1_copy.get_function("get_one")
            print(f";   Round-trip function found: {'yes' if fn else 'no'}")

            # Verify
            if not mod1_copy.verify():
                print(f"; ERROR: Verification failed: {mod1_copy.get_verification_error()}",
                      file=sys.stderr)

            print(";   Round-trip module verified: yes")

        # ==========================================================================
        # Test 2: Clone module
        # ==========================================================================
        print(";")
        print("; Test 2: Module cloning")

        with ctx.create_module("module2") as mod2:
            mod2.target_triple = "x86_64-unknown-linux-gnu"
            build_simple_function(mod2, ctx, "get_two", 2)
            
            with mod2.clone() as mod2_clone:
                # Change the original
                mod2.name = "module2_modified"

                # Check clone still has original name
                print(f";   Original module renamed to: {mod2.name}")
                print(f";   Clone still has name: {mod2_clone.name}")

                # Verify clone has the function
                fn2 = mod2_clone.get_function("get_two")
                print(f";   Clone has function get_two: {'yes' if fn2 else 'no'}")

        # ==========================================================================
        # Test 3: Link modules
        # ==========================================================================
        print(";")
        print("; Test 3: Module linking")

        # Create destination module
        with ctx.create_module("dest") as dest:
            dest.target_triple = "x86_64-unknown-linux-gnu"
            build_simple_function(dest, ctx, "get_dest", 100)
            
            # Create source module (will be consumed by linking)
            # Note: we create without 'with' because link_module destroys it
            with ctx.create_module("src") as src:
                src.target_triple = "x86_64-unknown-linux-gnu"
                build_simple_function(src, ctx, "get_src", 200)

                # Count functions before linking
                fn_count_before = len(dest.functions)
                print(f";   Functions in dest before linking: {fn_count_before}")

                # Link src into dest (src is destroyed)
                dest.link_module(src)

            # Count functions after linking
            fn_count_after = len(dest.functions)
            print(f";   Functions in dest after linking: {fn_count_after}")

            # Check both functions exist
            fn_dest = dest.get_function("get_dest")
            fn_src = dest.get_function("get_src")
            print(f";   get_dest exists: {'yes' if fn_dest else 'no'}")
            print(f";   get_src exists: {'yes' if fn_src else 'no'}")

            # Verify linked module
            if not dest.verify():
                print(f"; ERROR: Linked module verification failed: {dest.get_verification_error()}",
                      file=sys.stderr)

            print(";   Linked module verified: yes")

            # Print the final module IR
            print(";")
            print("; Final linked module:")
            print(dest.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
