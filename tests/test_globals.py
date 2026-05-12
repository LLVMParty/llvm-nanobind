#!/usr/bin/env -S uv run
"""
Test: test_globals
Tests LLVM global variable operations

Python equivalent of tests/test_globals.cpp
Must produce identical output to the C++ version.
"""

import llvm


def linkage_name(linkage):
    names = {
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
    return names.get(linkage, "unknown")


def visibility_name(vis):
    names = {
        llvm.Visibility.Default: "default",
        llvm.Visibility.Hidden: "hidden",
        llvm.Visibility.Protected: "protected",
    }
    return names.get(vis, "unknown")


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_globals") as mod:
            i8 = ctx.types.i8
            i32 = ctx.types.i32
            i64 = ctx.types.i64
            f64 = ctx.types.f64
            ptr = ctx.types.ptr

            # ==========================================
            # Basic global variable
            # ==========================================
            global_counter = mod.add_global(i32, "counter")
            global_counter.initializer = i32.constant(0)

            # ==========================================
            # Constant global
            # ==========================================
            global_const = mod.add_global(i32, "magic_number")
            global_const.initializer = i32.constant(42)
            global_const.is_global_constant = True

            # ==========================================
            # Global with alignment
            # ==========================================
            global_aligned = mod.add_global(i64, "aligned_var")
            global_aligned.initializer = i64.constant(0)
            global_aligned.alignment = 16

            # ==========================================
            # Global with linkage
            # ==========================================
            global_internal = mod.add_global(i32, "internal_var")
            global_internal.initializer = i32.constant(100)
            global_internal.linkage = llvm.Linkage.Internal

            global_private = mod.add_global(i32, "private_var")
            global_private.initializer = i32.constant(200)
            global_private.linkage = llvm.Linkage.Private

            global_weak = mod.add_global(i32, "weak_var")
            global_weak.initializer = i32.constant(300)
            global_weak.linkage = llvm.Linkage.WeakAny

            # ==========================================
            # Global with visibility
            # ==========================================
            global_hidden = mod.add_global(i32, "hidden_var")
            global_hidden.initializer = i32.constant(0)
            global_hidden.visibility = llvm.Visibility.Hidden

            # ==========================================
            # Global with section
            # ==========================================
            global_section = mod.add_global(i32, "section_var")
            global_section.initializer = i32.constant(0)
            global_section.section = ".mydata"

            # ==========================================
            # Thread-local global
            # ==========================================
            global_tls = mod.add_global(i32, "tls_var")
            global_tls.initializer = i32.constant(0)
            global_tls.is_thread_local = True

            # ==========================================
            # Externally initialized global (no initializer)
            # ==========================================
            global_extern = mod.add_global(i32, "extern_var")
            global_extern.is_externally_initialized = True

            # ==========================================
            # Global in address space
            # ==========================================
            global_addrspace = mod.add_global_in_address_space(i32, "addrspace_var", 1)
            global_addrspace.initializer = i32.constant(0)

            # ==========================================
            # Global to be deleted
            # ==========================================
            global_delete = mod.add_global(i32, "to_be_deleted")
            global_delete.initializer = i32.constant(999)

            # Count globals before deletion
            count_before = len(mod.globals)

            # Delete the global
            global_delete.delete_global()

            # Count globals after deletion
            count_after = len(mod.globals)

            # Get global by name
            found_counter = mod.get_global("counter")
            found_nonexist = mod.get_global("nonexistent")

            # Get initializer
            init = global_const.initializer

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.verification_error}")
                return 1

            # Print diagnostic comments
            print("; Test: test_globals")
            print(";")
            print("; Global variable properties:")
            print(";")
            print("; counter:")
            print(
                f";   is constant: {'yes' if global_counter.is_global_constant else 'no'}"
            )
            print(f";   linkage: {linkage_name(global_counter.linkage)}")
            print(";")
            print("; magic_number:")
            print(
                f";   is constant: {'yes' if global_const.is_global_constant else 'no'}"
            )
            print(f";   has initializer: {'yes' if init else 'no'}")
            if init:
                print(f";   initializer value: {init.const_zext_value}")
            else:
                print(";   initializer value: None")
            print(";")
            print("; aligned_var:")
            print(f";   alignment: {global_aligned.alignment}")
            print(";")
            print("; internal_var:")
            print(f";   linkage: {linkage_name(global_internal.linkage)}")
            print(";")
            print("; hidden_var:")
            print(f";   visibility: {visibility_name(global_hidden.visibility)}")
            print(";")
            print("; section_var:")
            print(f";   section: {global_section.section}")
            print(";")
            print("; tls_var:")
            print(
                f";   is thread local: {'yes' if global_tls.is_thread_local else 'no'}"
            )
            print(";")
            print("; extern_var:")
            print(
                f";   is externally initialized: {'yes' if global_extern.is_externally_initialized else 'no'}"
            )
            print(";")
            print("; Lookup tests:")
            print(f";   found 'counter': {'yes' if found_counter else 'no'}")
            print(f";   found 'nonexistent': {'yes' if found_nonexist else 'no'}")
            print(";")
            print("; Global counts:")
            print(f";   before deletion: {count_before}")
            print(f";   after deletion: {count_after}")
            print(";")
            print("; All globals:")
            for g in mod.globals:
                print(f";   - {g.name}")
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
