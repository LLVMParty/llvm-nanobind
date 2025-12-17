#!/usr/bin/env -S uv run
"""
Test: test_builder_memory
Tests LLVM Builder memory operations

Python equivalent of tests/test_builder_memory.cpp
Must produce identical output to the C++ version.
"""

import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_builder_memory") as mod:
            i32 = ctx.int32_type()
            i64 = ctx.int64_type()
            ptr = ctx.pointer_type()
            void_ty = ctx.void_type()

            # Array type for array alloca test
            arr_ty = ctx.array_type(i32, 10)

            # Struct type for struct GEP test
            struct_ty = ctx.struct_type([i32, i64, i32], packed=False)

            # Function: void memory_ops()
            func_ty = ctx.function_type(void_ty, [])
            func = mod.add_function("memory_ops", func_ty)

            with ctx.create_builder() as builder:
                entry = func.append_basic_block("entry", ctx)
                builder.position_at_end(entry)

                # Basic alloca
                alloca_i32 = builder.alloca(i32, "local_i32")

                # Alloca with explicit alignment
                alloca_aligned = builder.alloca(i64, "local_aligned")
                alloca_aligned.set_inst_alignment(16)

                # Array alloca (dynamic size)
                array_size = llvm.const_int(i32, 5)
                array_alloca = builder.array_alloca(i32, array_size, "dynamic_array")

                # Static array alloca
                static_array = builder.alloca(arr_ty, "static_array")

                # Struct alloca
                struct_alloca = builder.alloca(struct_ty, "local_struct")

                # Store and load
                val = llvm.const_int(i32, 42)
                store = builder.store(val, alloca_i32)
                load = builder.load(i32, alloca_i32, "loaded")

                # Volatile load/store
                volatile_store = builder.store(val, alloca_i32)
                volatile_store.set_volatile(True)
                volatile_load = builder.load(i32, alloca_i32, "volatile_loaded")
                volatile_load.set_volatile(True)

                # Load with alignment
                aligned_load = builder.load(i64, alloca_aligned, "aligned_loaded")
                aligned_load.set_inst_alignment(16)

                # GEP into array (static)
                indices = [llvm.const_int(i64, 0), llvm.const_int(i64, 3)]
                gep = builder.gep(arr_ty, static_array, indices, "arr_elem")

                # Inbounds GEP
                inbounds_gep = builder.inbounds_gep(
                    arr_ty, static_array, indices, "arr_elem_inbounds"
                )

                # Struct GEP
                struct_gep_0 = builder.struct_gep(
                    struct_ty, struct_alloca, 0, "field_0"
                )
                struct_gep_1 = builder.struct_gep(
                    struct_ty, struct_alloca, 1, "field_1"
                )
                struct_gep_2 = builder.struct_gep(
                    struct_ty, struct_alloca, 2, "field_2"
                )

                # Store to struct fields
                builder.store(llvm.const_int(i32, 100), struct_gep_0)
                builder.store(llvm.const_int(i64, 200), struct_gep_1)
                builder.store(llvm.const_int(i32, 300), struct_gep_2)

                # Load from struct fields
                field_0_val = builder.load(i32, struct_gep_0, "field_0_val")
                field_1_val = builder.load(i64, struct_gep_1, "field_1_val")

                builder.ret_void()

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_builder_memory")
            print(";")
            print("; Memory operations demonstrated:")
            print(
                ";   alloca (i32, i64 with alignment, dynamic array, static array, struct)"
            )
            print(";   store (basic, volatile)")
            print(";   load (basic, volatile, aligned)")
            print(";   GEP (array indexing, inbounds)")
            print(";   struct GEP (field access)")
            print(";")
            print("; Alignment checks:")
            print(
                f";   alloca_aligned alignment: {alloca_aligned.get_inst_alignment()}"
            )
            print(f";   aligned_load alignment: {aligned_load.get_inst_alignment()}")
            print(";")
            print("; Volatile checks:")
            print(
                f";   volatile_store is volatile: {'yes' if volatile_store.get_volatile() else 'no'}"
            )
            print(
                f";   volatile_load is volatile: {'yes' if volatile_load.get_volatile() else 'no'}"
            )
            print(
                f";   regular store is volatile: {'yes' if store.get_volatile() else 'no'}"
            )
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
