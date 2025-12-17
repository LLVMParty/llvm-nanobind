#!/usr/bin/env -S uv run
"""
Test: test_function
Tests LLVM Function creation and properties via Python bindings.

This is the Python equivalent of tests/test_function.cpp.
Output should match the C++ golden master test.

LLVM APIs covered (via Python bindings):
- add_function(), get_function()
- param_count, get_param(), params
- name property (get/set)
- calling_conv property (get/set)
- linkage property (get/set)
- functions (module iteration)
- erase()
"""

import sys
import llvm


def linkage_name(linkage: llvm.Linkage) -> str:
    """Convert Linkage enum to the string name used in C++ test."""
    mapping = {
        llvm.Linkage.External: "external",
        llvm.Linkage.AvailableExternally: "available_externally",
        llvm.Linkage.LinkOnceAny: "linkonce",
        llvm.Linkage.LinkOnceODR: "linkonce_odr",
        llvm.Linkage.WeakAny: "weak",
        llvm.Linkage.WeakODR: "weak_odr",
        llvm.Linkage.Appending: "appending",
        llvm.Linkage.Internal: "internal",
        llvm.Linkage.Private: "private",
        llvm.Linkage.ExternalWeak: "extern_weak",
        llvm.Linkage.Common: "common",
    }
    return mapping.get(linkage, "unknown")


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_function") as mod:
            i32 = ctx.int32_type()
            i64 = ctx.int64_type()
            void_ty = ctx.void_type()
            ptr = ctx.pointer_type(0)

            # Function 1: void foo()
            foo_ty = ctx.function_type(void_ty, [], vararg=False)
            foo = mod.add_function("foo", foo_ty)

            # Function 2: i32 bar(i32, i32)
            bar_ty = ctx.function_type(i32, [i32, i32], vararg=False)
            bar = mod.add_function("bar", bar_ty)

            # Set parameter names
            bar_param0 = bar.get_param(0)
            bar_param1 = bar.get_param(1)
            bar_param0.name = "x"
            bar_param1.name = "y"

            # Function 3: i64 baz(ptr, i32, i64) with internal linkage
            # Internal linkage requires a body, so we add a simple one
            baz_ty = ctx.function_type(i64, [ptr, i32, i64], vararg=False)
            baz = mod.add_function("baz", baz_ty)
            baz.linkage = llvm.Linkage.Internal

            # Add a basic block with return to make it a valid definition
            with ctx.create_builder() as builder:
                baz_entry = baz.append_basic_block("entry", ctx)
                builder.position_at_end(baz_entry)
                builder.ret(llvm.const_int(i64, 0))

            # Function 4: varargs function - i32 printf(ptr, ...)
            printf_ty = ctx.function_type(i32, [ptr], vararg=True)
            printf_fn = mod.add_function("printf", printf_ty)

            # Function 5: Function with fastcc calling convention
            fastcc_ty = ctx.function_type(i32, [i32], vararg=False)
            fastcc_fn = mod.add_function("fastcc_func", fastcc_ty)
            fastcc_fn.calling_conv = llvm.CallConv.Fast.value

            # Function 6: Will be deleted
            delete_ty = ctx.function_type(void_ty, [], vararg=False)
            delete_fn = mod.add_function("to_be_deleted", delete_ty)

            # Get function by name
            found_bar = mod.get_function("bar")

            # Count functions before deletion
            count_before = len(mod.functions)

            # Delete the function
            delete_fn.erase()

            # Count functions after deletion
            count_after = len(mod.functions)

            # Verify module
            if not mod.verify():
                print(
                    f"; Verification failed: {mod.get_verification_error()}",
                    file=sys.stderr,
                )
                return 1

            # Print diagnostic comments
            print("; Test: test_function")
            print(";")

            # foo info
            print("; Function 'foo':")
            print(f";   name: {foo.name}")
            print(f";   param count: {foo.param_count}")
            print(f";   linkage: {linkage_name(foo.linkage)}")
            print(f";   calling conv: {foo.calling_conv} (C=0)")

            # bar info
            print(";")
            print("; Function 'bar':")
            print(f";   name: {bar.name}")
            print(f";   param count: {bar.param_count}")
            print(f";   found by name: {'yes' if found_bar is not None else 'no'}")

            # Get param names
            print(f";   param 0 name: {bar_param0.name}")
            print(f";   param 1 name: {bar_param1.name}")

            # baz info
            print(";")
            print("; Function 'baz':")
            print(f";   param count: {baz.param_count}")
            print(f";   linkage: {linkage_name(baz.linkage)}")

            # printf info
            print(";")
            print("; Function 'printf':")
            print(f";   param count: {printf_fn.param_count}")
            print(f";   is vararg: {'yes' if printf_ty.is_vararg else 'no'}")

            # fastcc info
            print(";")
            print("; Function 'fastcc_func':")
            print(f";   calling conv: {fastcc_fn.calling_conv} (FastCall=8)")

            # Function counts
            print(";")
            print(f"; Function count before deletion: {count_before}")
            print(f"; Function count after deletion: {count_after}")

            # List all functions
            print(";")
            print("; All functions:")
            for fn in mod.functions:
                print(f";   - {fn.name}")

            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
