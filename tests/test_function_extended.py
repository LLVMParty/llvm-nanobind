#!/usr/bin/env -S uv run
"""
Test: test_function_extended
Tests for extended Function APIs including verification and intrinsics

This is the Python equivalent of tests/test_function_extended.cpp.
Output should match the C++ golden master test.

LLVM Python APIs tested:
- fn.verify(), fn.verify_and_print()
- fn.intrinsic_id, fn.is_intrinsic
- fn.has_personality_fn, fn.get_personality_fn(), fn.set_personality_fn()
- fn.get_gc(), fn.set_gc()
- mod.get_intrinsic_declaration()
- llvm.lookup_intrinsic_id()
"""

import sys
import llvm


def main():
    print("; Test: test_function_extended")
    print("; Tests extended Function APIs")
    print(";")

    with llvm.create_context() as ctx:
        with ctx.create_module("test_function_extended") as mod:
            mod.target_triple = "x86_64-unknown-linux-gnu"

            i32 = ctx.types.i32
            i64 = ctx.types.i64
            void_ty = ctx.types.void

            # ==========================================================================
            # Test 1: Function verification - valid function
            # ==========================================================================
            print("; Test 1: Function verification (valid function)")

            valid_ty = ctx.types.function(i32, [])
            valid_fn = mod.add_function("valid_function", valid_ty)

            valid_entry = valid_fn.append_basic_block("entry")
            with valid_entry.create_builder() as builder:
                builder.ret(i32.constant(42))

            valid_passed = valid_fn.verify()
            print(f";   valid_function verification passed: {'yes' if valid_passed else 'no'}")

            # ==========================================================================
            # Test 2: Function verification - invalid function (no terminator)
            # ==========================================================================
            print(";")
            print("; Test 2: Function verification (invalid function)")

            invalid_ty = ctx.types.function(i32, [])
            invalid_fn = mod.add_function("invalid_function", invalid_ty)

            invalid_entry = invalid_fn.append_basic_block("entry")
            with invalid_entry.create_builder() as builder:
                # Deliberately not adding a terminator - this makes the function invalid
                # We add an alloca just to have something in the block
                builder.alloca(i32, "x")

                invalid_passed = invalid_fn.verify()
                print(f";   invalid_function verification failed (expected): {'yes' if not invalid_passed else 'no'}")

                # Now fix it by adding a return
                builder.ret(i32.constant(0))
                invalid_passed = invalid_fn.verify()
                print(f";   After adding return, verification passed: {'yes' if invalid_passed else 'no'}")

            # ==========================================================================
            # Test 3: Intrinsic IDs
            # ==========================================================================
            print(";")
            print("; Test 3: Intrinsic IDs")

            # User functions have intrinsic ID 0
            valid_id = valid_fn.intrinsic_id
            print(f";   valid_function intrinsic ID: {valid_id} (0 = not intrinsic)")
            print(f";   valid_function is_intrinsic: {'yes' if valid_fn.is_intrinsic else 'no'}")

            # Look up an intrinsic ID by name
            memcpy_id = llvm.lookup_intrinsic_id("llvm.memcpy")
            print(f";   llvm.memcpy intrinsic ID: {memcpy_id}")

            # Get intrinsic declaration
            if memcpy_id != 0:
                ptr = ctx.types.ptr()
                memcpy_decl = mod.get_intrinsic_declaration(memcpy_id, [ptr, ptr, i64])
                memcpy_decl_id = memcpy_decl.intrinsic_id
                print(f";   memcpy declaration is_intrinsic: {'yes' if memcpy_decl_id != 0 else 'no'}")
                print(f";   memcpy declaration name: {memcpy_decl.name}")

            # ==========================================================================
            # Test 4: Personality function
            # ==========================================================================
            print(";")
            print("; Test 4: Personality function")

            # Create a personality function (like __gxx_personality_v0)
            personality_ty = ctx.types.function(i32, [], vararg=True)
            personality_fn = mod.add_function("__gxx_personality_v0", personality_ty)

            # Create a function that uses the personality
            with_personality_ty = ctx.types.function(void_ty, [])
            with_personality_fn = mod.add_function("with_personality", with_personality_ty)

            print(";   Before setting personality:")
            print(f";     has_personality_fn: {'yes' if with_personality_fn.has_personality_fn else 'no'}")

            with_personality_fn.set_personality_fn(personality_fn)

            print(";   After setting personality:")
            print(f";     has_personality_fn: {'yes' if with_personality_fn.has_personality_fn else 'no'}")

            got_personality = with_personality_fn.get_personality_fn()
            if got_personality:
                print(f";     personality fn name: {got_personality.name}")

            # Add entry block to with_personality function
            wp_entry = with_personality_fn.append_basic_block("entry")
            with wp_entry.create_builder() as builder:
                builder.ret_void()

            # ==========================================================================
            # Test 5: GC name
            # ==========================================================================
            print(";")
            print("; Test 5: GC name")

            gc_fn_ty = ctx.types.function(void_ty, [])
            gc_fn = mod.add_function("gc_function", gc_fn_ty)

            print(";   Before setting GC:")
            gc_before = gc_fn.get_gc()
            print(f";     GC name: {gc_before if gc_before else '(none)'}")

            gc_fn.set_gc("statepoint-example")

            print(";   After setting GC:")
            gc_after = gc_fn.get_gc()
            print(f";     GC name: {gc_after if gc_after else '(none)'}")

            # Add entry block
            gc_entry = gc_fn.append_basic_block("entry")
            with gc_entry.create_builder() as builder:
                builder.ret_void()

            # ==========================================================================
            # Print module
            # ==========================================================================
            print(";")
            print("; Module IR:")
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
