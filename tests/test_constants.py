#!/usr/bin/env -S uv run
"""
Test: test_constants
Tests LLVM constant value creation

Python equivalent of tests/test_constants.cpp
Must produce identical output to the C++ version.
"""

import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_constants") as mod:
            # Types
            i1 = ctx.int1_type()
            i8 = ctx.int8_type()
            i32 = ctx.int32_type()
            i64 = ctx.int64_type()
            i128 = ctx.int128_type()
            f32 = ctx.float_type()
            f64 = ctx.double_type()
            ptr = ctx.pointer_type()

            # ==========================================
            # Integer constants
            # ==========================================
            const_0 = llvm.const_int(i32, 0)
            const_42 = llvm.const_int(i32, 42)
            const_neg1 = llvm.const_int(i32, -1, sign_extend=True)
            const_max_u32 = llvm.const_int(i32, 0xFFFFFFFF)
            const_i64 = llvm.const_int(i64, 0x123456789ABCDEF0)

            # Arbitrary precision integer (128-bit) - two 64-bit words (little-endian)
            # words[] = {0xFFFFFFFFFFFFFFFFULL, 0x0000000000000001ULL}
            # Low word first (little-endian): 0xFFFFFFFFFFFFFFFF, 0x0000000000000001
            const_i128 = llvm.const_int_of_arbitrary_precision(
                i128, [0xFFFFFFFFFFFFFFFF, 0x0000000000000001]
            )

            # ==========================================
            # Floating point constants
            # ==========================================
            const_pi = llvm.const_real(f64, 3.14159265358979323846)
            const_e = llvm.const_real(f64, 2.71828182845904523536)
            const_f32 = llvm.const_real(f32, 1.5)

            # From string - not directly available, using const_real
            const_from_str = llvm.const_real(f64, 1.234567890123456789)

            # ==========================================
            # Special values
            # ==========================================
            null_i32 = llvm.const_null(i32)
            null_ptr = llvm.const_pointer_null(ptr)
            all_ones = llvm.const_all_ones(i32)
            undef_i32 = llvm.undef(i32)
            poison_i32 = llvm.poison(i32)

            # ==========================================
            # String constant
            # ==========================================
            str_val = "Hello, LLVM!"
            const_string = llvm.const_string(ctx, str_val, dont_null_terminate=False)
            const_string_no_null = llvm.const_string(
                ctx, str_val, dont_null_terminate=True
            )

            # ==========================================
            # Array constant
            # ==========================================
            arr_elems = [
                llvm.const_int(i32, 1),
                llvm.const_int(i32, 2),
                llvm.const_int(i32, 3),
                llvm.const_int(i32, 4),
                llvm.const_int(i32, 5),
            ]
            const_array = llvm.const_array(i32, arr_elems)

            # ==========================================
            # Anonymous struct constant
            # ==========================================
            struct_elems = [
                llvm.const_int(i32, 100),
                llvm.const_real(f64, 3.14),
                llvm.const_int(i64, 999),
            ]
            const_struct = llvm.const_struct(struct_elems, False, ctx)
            const_packed_struct = llvm.const_struct(struct_elems, True, ctx)

            # ==========================================
            # Named struct constant
            # ==========================================
            named_struct_ty = ctx.named_struct_type("Point")
            named_struct_ty.set_body([i32, i32], packed=False)

            point_vals = [
                llvm.const_int(i32, 10),
                llvm.const_int(i32, 20),
            ]
            const_named_struct = llvm.const_named_struct(named_struct_ty, point_vals)

            # ==========================================
            # Vector constant
            # ==========================================
            vec_elems = [
                llvm.const_int(i32, 1),
                llvm.const_int(i32, 2),
                llvm.const_int(i32, 3),
                llvm.const_int(i32, 4),
            ]
            const_vector = llvm.const_vector(vec_elems)

            # ==========================================
            # Add globals to expose constants in output
            # ==========================================
            g = mod.add_global(i32, "const_42")
            g.set_initializer(const_42)
            g.set_constant(True)

            g = mod.add_global(i32, "const_neg1")
            g.set_initializer(const_neg1)
            g.set_constant(True)

            g = mod.add_global(i64, "const_i64")
            g.set_initializer(const_i64)
            g.set_constant(True)

            g = mod.add_global(i128, "const_i128")
            g.set_initializer(const_i128)
            g.set_constant(True)

            g = mod.add_global(f64, "const_pi")
            g.set_initializer(const_pi)
            g.set_constant(True)

            g = mod.add_global(i32, "all_ones")
            g.set_initializer(all_ones)
            g.set_constant(True)

            g = mod.add_global(i32, "undef_val")
            g.set_initializer(undef_i32)

            g = mod.add_global(i32, "poison_val")
            g.set_initializer(poison_i32)

            # Get array type for const_string
            str_arr_ty = ctx.array_type(i8, len(str_val) + 1)
            g = mod.add_global(str_arr_ty, "hello_string")
            g.set_initializer(const_string)
            g.set_constant(True)

            arr_ty = ctx.array_type(i32, 5)
            g = mod.add_global(arr_ty, "const_array")
            g.set_initializer(const_array)
            g.set_constant(True)

            # Struct type for global
            anon_struct_ty = ctx.struct_type([i32, f64, i64], packed=False)
            g = mod.add_global(anon_struct_ty, "const_struct")
            g.set_initializer(const_struct)
            g.set_constant(True)

            g = mod.add_global(named_struct_ty, "const_point")
            g.set_initializer(const_named_struct)
            g.set_constant(True)

            vec_ty = ctx.vector_type(i32, 4)
            g = mod.add_global(vec_ty, "const_vector")
            g.set_initializer(const_vector)
            g.set_constant(True)

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_constants")
            print(";")
            print("; Integer constants:")
            print(f";   const_0 value (zext): {llvm.const_int_get_zext_value(const_0)}")
            print(
                f";   const_42 value (zext): {llvm.const_int_get_zext_value(const_42)}"
            )
            print(
                f";   const_neg1 value (sext): {llvm.const_int_get_sext_value(const_neg1)}"
            )
            print(
                f";   const_max_u32 value (zext): {llvm.const_int_get_zext_value(const_max_u32)}"
            )
            print(";")
            print("; Value checks:")
            print(
                f";   const_42 is constant: {'yes' if const_42.is_constant else 'no'}"
            )
            print(
                f";   null_i32 is null: {'yes' if llvm.value_is_null(null_i32) else 'no'}"
            )
            print(
                f";   null_ptr is null: {'yes' if llvm.value_is_null(null_ptr) else 'no'}"
            )
            print(f";   undef_i32 is undef: {'yes' if undef_i32.is_undef else 'no'}")
            print(
                f";   poison_i32 is poison: {'yes' if poison_i32.is_poison else 'no'}"
            )
            print(f";   const_42 is undef: {'yes' if const_42.is_undef else 'no'}")
            print(";")
            print("; Aggregate constants:")
            print(";   array with 5 i32 elements")
            print(";   struct with {i32, f64, i64}")
            print(";   named struct Point with {i32, i32}")
            print(";   vector with 4 x i32")
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
