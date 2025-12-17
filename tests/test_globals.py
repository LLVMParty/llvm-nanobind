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
            i8 = ctx.int8_type()
            i32 = ctx.int32_type()
            i64 = ctx.int64_type()
            f64 = ctx.double_type()
            ptr = ctx.pointer_type()

            # ==========================================
            # Basic global variable
            # ==========================================
            global_counter = mod.add_global(i32, "counter")
            global_counter.set_initializer(llvm.const_int(i32, 0))

            # ==========================================
            # Constant global
            # ==========================================
            global_const = mod.add_global(i32, "magic_number")
            global_const.set_initializer(llvm.const_int(i32, 42))
            global_const.set_constant(True)

            # ==========================================
            # Global with alignment
            # ==========================================
            global_aligned = mod.add_global(i64, "aligned_var")
            global_aligned.set_initializer(llvm.const_int(i64, 0))
            global_aligned.set_alignment(16)

            # ==========================================
            # Global with linkage
            # ==========================================
            global_internal = mod.add_global(i32, "internal_var")
            global_internal.set_initializer(llvm.const_int(i32, 100))
            global_internal.set_linkage(llvm.Linkage.Internal)

            global_private = mod.add_global(i32, "private_var")
            global_private.set_initializer(llvm.const_int(i32, 200))
            global_private.set_linkage(llvm.Linkage.Private)

            global_weak = mod.add_global(i32, "weak_var")
            global_weak.set_initializer(llvm.const_int(i32, 300))
            global_weak.set_linkage(llvm.Linkage.WeakAny)

            # ==========================================
            # Global with visibility
            # ==========================================
            global_hidden = mod.add_global(i32, "hidden_var")
            global_hidden.set_initializer(llvm.const_int(i32, 0))
            global_hidden.set_visibility(llvm.Visibility.Hidden)

            # ==========================================
            # Global with section
            # ==========================================
            global_section = mod.add_global(i32, "section_var")
            global_section.set_initializer(llvm.const_int(i32, 0))
            global_section.set_section(".mydata")

            # ==========================================
            # Thread-local global
            # ==========================================
            global_tls = mod.add_global(i32, "tls_var")
            global_tls.set_initializer(llvm.const_int(i32, 0))
            global_tls.set_thread_local(True)

            # ==========================================
            # Externally initialized global (no initializer)
            # ==========================================
            global_extern = mod.add_global(i32, "extern_var")
            global_extern.set_externally_initialized(True)

            # ==========================================
            # Global in address space
            # ==========================================
            global_addrspace = mod.add_global_in_address_space(i32, "addrspace_var", 1)
            global_addrspace.set_initializer(llvm.const_int(i32, 0))

            # ==========================================
            # Global to be deleted
            # ==========================================
            global_delete = mod.add_global(i32, "to_be_deleted")
            global_delete.set_initializer(llvm.const_int(i32, 999))

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
            init = global_const.get_initializer()

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_globals")
            print(";")
            print("; Global variable properties:")
            print(";")
            print("; counter:")
            print(
                f";   is constant: {'yes' if global_counter.is_global_constant() else 'no'}"
            )
            print(f";   linkage: {linkage_name(global_counter.get_linkage())}")
            print(";")
            print("; magic_number:")
            print(
                f";   is constant: {'yes' if global_const.is_global_constant() else 'no'}"
            )
            print(f";   has initializer: {'yes' if init else 'no'}")
            if init:
                print(f";   initializer value: {llvm.const_int_get_zext_value(init)}")
            else:
                print(";   initializer value: None")
            print(";")
            print("; aligned_var:")
            print(f";   alignment: {global_aligned.get_alignment()}")
            print(";")
            print("; internal_var:")
            print(f";   linkage: {linkage_name(global_internal.get_linkage())}")
            print(";")
            print("; hidden_var:")
            print(f";   visibility: {visibility_name(global_hidden.get_visibility())}")
            print(";")
            print("; section_var:")
            print(f";   section: {global_section.get_section()}")
            print(";")
            print("; tls_var:")
            print(
                f";   is thread local: {'yes' if global_tls.is_thread_local() else 'no'}"
            )
            print(";")
            print("; extern_var:")
            print(
                f";   is externally initialized: {'yes' if global_extern.is_externally_initialized() else 'no'}"
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
