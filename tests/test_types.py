#!/usr/bin/env -S uv run
"""
Test: test_types
Tests LLVM Type creation and inspection via Python bindings.

This is the Python equivalent of tests/test_types.cpp.
Output should match the C++ golden master test.

LLVM APIs covered (via Python bindings):
- Integer types: int1_type(), int8_type(), int16_type(), int32_type(), int64_type(), int128_type(), int_type()
- Floating point: half_type(), bfloat_type(), float_type(), double_type()
- Other types: void_type(), pointer_type(), array_type(), vector_type(), function_type()
- Struct types: struct_type(), named_struct_type(), set_body()
- Type inspection: kind, int_width, is_sized, is_packed_struct, is_opaque_struct, struct_name
"""

import sys
import llvm


def type_kind_name(kind: llvm.TypeKind) -> str:
    """Convert TypeKind enum to the string name used in C++ test."""
    mapping = {
        llvm.TypeKind.Void: "void",
        llvm.TypeKind.Half: "half",
        llvm.TypeKind.Float: "float",
        llvm.TypeKind.Double: "double",
        llvm.TypeKind.FP128: "fp128",
        llvm.TypeKind.Label: "label",
        llvm.TypeKind.Integer: "integer",
        llvm.TypeKind.Function: "function",
        llvm.TypeKind.Struct: "struct",
        llvm.TypeKind.Array: "array",
        llvm.TypeKind.Pointer: "pointer",
        llvm.TypeKind.Vector: "vector",
        llvm.TypeKind.Metadata: "metadata",
        llvm.TypeKind.Token: "token",
        llvm.TypeKind.ScalableVector: "scalable_vector",
        llvm.TypeKind.BFloat: "bfloat",
    }
    return mapping.get(kind, "unknown")


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_types") as mod:
            # Integer types
            i1 = ctx.int1_type()
            i8 = ctx.int8_type()
            i16 = ctx.int16_type()
            i32 = ctx.int32_type()
            i64 = ctx.int64_type()
            i128 = ctx.int128_type()
            i256 = ctx.int_type(256)

            # Floating point types
            f16 = ctx.half_type()
            bf16 = ctx.bfloat_type()
            f32 = ctx.float_type()
            f64 = ctx.double_type()

            # Void type
            void_ty = ctx.void_type()

            # Pointer type (opaque pointer)
            ptr = ctx.pointer_type(0)

            # Array type
            arr_i32_10 = ctx.array_type(i32, 10)

            # Vector type
            vec_i32_4 = ctx.vector_type(i32, 4)

            # Function type: i32(i32, i32)
            func_ty = ctx.function_type(i32, [i32, i32], vararg=False)

            # Function type with varargs: i32(i32, ...)
            vararg_func_ty = ctx.function_type(i32, [i32], vararg=True)

            # Anonymous struct type: {i32, f64}
            anon_struct = ctx.struct_type([i32, f64], packed=False)

            # Packed anonymous struct type: <{i8, i32}>
            packed_struct = ctx.struct_type([i8, i32], packed=True)

            # Named struct type
            named_struct = ctx.named_struct_type("MyStruct")
            named_struct.set_body([i32, ptr, f64], packed=False)

            # Opaque struct (no body set)
            opaque_struct = ctx.named_struct_type("OpaqueStruct")

            # Add global variables to make types visible in output
            mod.add_global(i32, "global_i32")
            mod.add_global(arr_i32_10, "global_arr")
            mod.add_global(vec_i32_4, "global_vec")
            mod.add_global(anon_struct, "global_anon_struct")
            mod.add_global(packed_struct, "global_packed_struct")
            mod.add_global(named_struct, "global_named_struct")

            # Add function declarations to show function type
            mod.add_function("example_func", func_ty)
            mod.add_function("example_vararg_func", vararg_func_ty)

            # Verify module
            if not mod.verify():
                print(
                    f"; Verification failed: {mod.get_verification_error()}",
                    file=sys.stderr,
                )
                return 1

            # Print diagnostic comments
            print("; Test: test_types")
            print(";")
            print("; Integer types:")
            print(f";   i1 width: {i1.int_width}, kind: {type_kind_name(i1.kind)}")
            print(f";   i8 width: {i8.int_width}, kind: {type_kind_name(i8.kind)}")
            print(f";   i16 width: {i16.int_width}, kind: {type_kind_name(i16.kind)}")
            print(f";   i32 width: {i32.int_width}, kind: {type_kind_name(i32.kind)}")
            print(f";   i64 width: {i64.int_width}, kind: {type_kind_name(i64.kind)}")
            print(
                f";   i128 width: {i128.int_width}, kind: {type_kind_name(i128.kind)}"
            )
            print(
                f";   i256 width: {i256.int_width}, kind: {type_kind_name(i256.kind)}"
            )
            print(";")
            print("; Floating point types:")
            print(f";   half kind: {type_kind_name(f16.kind)}")
            print(f";   bfloat kind: {type_kind_name(bf16.kind)}")
            print(f";   float kind: {type_kind_name(f32.kind)}")
            print(f";   double kind: {type_kind_name(f64.kind)}")
            print(";")
            print("; Other types:")
            print(
                f";   void kind: {type_kind_name(void_ty.kind)}, sized: {'yes' if void_ty.is_sized else 'no'}"
            )
            print(
                f";   pointer kind: {type_kind_name(ptr.kind)}, sized: {'yes' if ptr.is_sized else 'no'}"
            )
            print(
                f";   array kind: {type_kind_name(arr_i32_10.kind)}, sized: {'yes' if arr_i32_10.is_sized else 'no'}"
            )
            print(
                f";   vector kind: {type_kind_name(vec_i32_4.kind)}, sized: {'yes' if vec_i32_4.is_sized else 'no'}"
            )
            print(
                f";   function kind: {type_kind_name(func_ty.kind)}, sized: {'yes' if func_ty.is_sized else 'no'}"
            )
            print(";")
            print("; Struct types:")
            print(
                f";   anon_struct kind: {type_kind_name(anon_struct.kind)}, packed: {'yes' if anon_struct.is_packed_struct else 'no'}"
            )
            print(
                f";   packed_struct kind: {type_kind_name(packed_struct.kind)}, packed: {'yes' if packed_struct.is_packed_struct else 'no'}"
            )

            named_struct_name = named_struct.struct_name
            print(
                f";   named_struct name: {named_struct_name}, opaque: {'yes' if named_struct.is_opaque_struct else 'no'}"
            )

            opaque_struct_name = opaque_struct.struct_name
            print(
                f";   opaque_struct name: {opaque_struct_name}, opaque: {'yes' if opaque_struct.is_opaque_struct else 'no'}"
            )

            # Print type strings
            print(";")
            print("; Type strings:")
            print(f";   i32: {i32}")
            print(f";   [10 x i32]: {arr_i32_10}")
            print(f";   func type: {func_ty}")

            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
