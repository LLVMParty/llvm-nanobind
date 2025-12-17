#!/usr/bin/env -S uv run
"""
Test: test_builder_casts
Tests LLVM Builder cast operations

Python equivalent of tests/test_builder_casts.cpp
Must produce identical output to the C++ version.
"""

import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_builder_casts") as mod:
            i8 = ctx.int8_type()
            i16 = ctx.int16_type()
            i32 = ctx.int32_type()
            i64 = ctx.int64_type()
            f32 = ctx.float_type()
            f64 = ctx.double_type()
            ptr = ctx.pointer_type()
            void_ty = ctx.void_type()

            with ctx.create_builder() as builder:
                # ==========================================
                # Function 1: Integer casts
                # ==========================================
                int_cast_ty = ctx.function_type(i8, [i64])
                int_cast_func = mod.add_function("integer_casts", int_cast_ty)
                i64_val = int_cast_func.get_param(0)
                i64_val.name = "val"

                int_entry = int_cast_func.append_basic_block("entry", ctx)
                builder.position_at_end(int_entry)

                # Truncate i64 -> i32 -> i16 -> i8
                trunc_32 = builder.trunc(i64_val, i32, "trunc_32")
                trunc_16 = builder.trunc(trunc_32, i16, "trunc_16")
                trunc_8 = builder.trunc(trunc_16, i8, "trunc_8")

                # Zero extend i8 -> i16 -> i32 -> i64
                zext_16 = builder.zext(trunc_8, i16, "zext_16")
                zext_32 = builder.zext(zext_16, i32, "zext_32")
                zext_64 = builder.zext(zext_32, i64, "zext_64")

                # Sign extend i8 -> i16 -> i32 -> i64
                sext_16 = builder.sext(trunc_8, i16, "sext_16")
                sext_32 = builder.sext(sext_16, i32, "sext_32")
                sext_64 = builder.sext(sext_32, i64, "sext_64")

                # IntCast2 (auto-selects trunc/zext/sext)
                intcast_unsigned = builder.int_cast2(
                    i64_val, i32, False, "intcast_unsigned"
                )
                intcast_signed = builder.int_cast2(trunc_8, i32, True, "intcast_signed")

                builder.ret(trunc_8)

                # ==========================================
                # Function 2: Float casts
                # ==========================================
                fp_cast_ty = ctx.function_type(f32, [f64])
                fp_cast_func = mod.add_function("float_casts", fp_cast_ty)
                f64_val = fp_cast_func.get_param(0)
                f64_val.name = "val"

                fp_entry = fp_cast_func.append_basic_block("entry", ctx)
                builder.position_at_end(fp_entry)

                # FP truncate f64 -> f32
                fptrunc = builder.fptrunc(f64_val, f32, "fptrunc")

                # FP extend f32 -> f64
                fpext = builder.fpext(fptrunc, f64, "fpext")

                builder.ret(fptrunc)

                # ==========================================
                # Function 3: Int <-> Float casts
                # ==========================================
                mixed_ty = ctx.function_type(void_ty, [i32, f64])
                mixed_func = mod.add_function("int_float_casts", mixed_ty)
                int_param = mixed_func.get_param(0)
                fp_param = mixed_func.get_param(1)
                int_param.name = "i"
                fp_param.name = "f"

                mixed_entry = mixed_func.append_basic_block("entry", ctx)
                builder.position_at_end(mixed_entry)

                # Int -> Float
                uitofp = builder.uitofp(int_param, f64, "uitofp")
                sitofp = builder.sitofp(int_param, f64, "sitofp")

                # Float -> Int
                fptoui = builder.fptoui(fp_param, i32, "fptoui")
                fptosi = builder.fptosi(fp_param, i32, "fptosi")

                builder.ret_void()

                # ==========================================
                # Function 4: Pointer casts
                # ==========================================
                ptr_ty = ctx.function_type(void_ty, [ptr, i64])
                ptr_func = mod.add_function("pointer_casts", ptr_ty)
                ptr_param = ptr_func.get_param(0)
                int_for_ptr = ptr_func.get_param(1)
                ptr_param.name = "p"
                int_for_ptr.name = "addr"

                ptr_entry = ptr_func.append_basic_block("entry", ctx)
                builder.position_at_end(ptr_entry)

                # Pointer -> Int
                ptrtoint = builder.ptrtoint(ptr_param, i64, "ptrtoint")

                # Int -> Pointer
                inttoptr = builder.inttoptr(int_for_ptr, ptr, "inttoptr")

                builder.ret_void()

                # ==========================================
                # Function 5: Bitcast
                # ==========================================
                vec_i32 = ctx.vector_type(i32, 4)
                vec_f32 = ctx.vector_type(f32, 4)

                bitcast_ty = ctx.function_type(vec_f32, [vec_i32])
                bitcast_func = mod.add_function("bitcast_example", bitcast_ty)
                vec_param = bitcast_func.get_param(0)
                vec_param.name = "v"

                bitcast_entry = bitcast_func.append_basic_block("entry", ctx)
                builder.position_at_end(bitcast_entry)

                bitcast = builder.bitcast(vec_param, vec_f32, "bitcast")
                builder.ret(bitcast)

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_builder_casts")
            print(";")
            print("; Cast operations demonstrated:")
            print(";   Integer: trunc, zext, sext, intcast2")
            print(";   Float: fptrunc, fpext")
            print(";   Int<->Float: uitofp, sitofp, fptoui, fptosi")
            print(";   Pointer: ptrtoint, inttoptr")
            print(";   Reinterpret: bitcast")
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
