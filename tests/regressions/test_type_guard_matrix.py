"""
Comprehensive guard matrix for non-Value Type APIs.

This suite verifies that Type accessors reject wrong-type usage with
actionable LLVMAssertionError messages instead of hard-crashing.
"""

import llvm


def assert_llvm_assertion(action, expected_substring: str):
    try:
        action()
        assert False, "Expected llvm.LLVMAssertionError"
    except llvm.LLVMAssertionError as e:
        msg = str(e).lower()
        assert expected_substring.lower() in msg, (
            f"Expected '{expected_substring}' in error message, got: {e}"
        )


def test_type_guard_matrix_negative():
    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        f32 = ctx.types.f32
        fn_ty = ctx.types.function(i32, [i32], False)
        arr_ty = ctx.types.array(i32, 4)
        vec_ty = ctx.types.vector(i32, 4)
        literal_struct = ctx.types.struct([i32], packed=False)
        named_struct_with_body = ctx.types.struct([i32], name="NamedWithBody", packed=False)
        opaque_struct = ctx.types.opaque_struct("OpaqueS")

        cases = [
            (lambda: f32.int_width, "integer type"),
            (lambda: i32.is_packed_struct, "struct type"),
            (lambda: i32.is_opaque_struct, "struct type"),
            (lambda: i32.is_literal_struct, "struct type"),
            (lambda: i32.struct_name, "struct type"),
            (lambda: i32.get_struct_element_type(0), "struct type"),
            (lambda: i32.struct_element_count, "struct type"),
            (lambda: i32.set_body([i32]), "struct type"),
            (
                lambda: literal_struct.set_body([i32]),
                "identified (named) struct type",
            ),
            (
                lambda: named_struct_with_body.set_body([i32]),
                "opaque struct type",
            ),
            (
                lambda: named_struct_with_body.get_struct_element_type(1),
                "out of range",
            ),
            (lambda: i32.is_vararg, "function type"),
            (lambda: i32.return_type, "function type"),
            (lambda: i32.param_count, "function type"),
            (lambda: i32.param_types, "function type"),
            (lambda: i32.is_opaque_pointer, "pointer type"),
            (lambda: i32.pointer_address_space, "pointer type"),
            (lambda: i32.element_type, "does not have an element type"),
            (lambda: i32.array_length, "array type"),
            (lambda: i32.vector_size, "vector type"),
            (lambda: i32.target_ext_type_name, "target extension type"),
            (lambda: i32.target_ext_type_num_type_params, "target extension type"),
            (lambda: i32.target_ext_type_num_int_params, "target extension type"),
            (lambda: i32.get_target_ext_type_type_param(0), "target extension type"),
            (lambda: i32.get_target_ext_type_int_param(0), "target extension type"),
            (lambda: f32.constant(1, False), "integer type"),
            (lambda: i32.real_constant(1.0), "floating-point type"),
            (lambda: i32.constant_from_string("10", 1), "between 2 and 36"),
            (lambda: f32.constant_from_string("10", 10), "integer type"),
            (lambda: i32.real_constant_from_string("1.0"), "floating-point type"),
        ]

        for action, expected in cases:
            assert_llvm_assertion(action, expected)

        # Target extension index guards (if supported by this LLVM build).
        target_ext = None
        try:
            target_ext = ctx.types.target_ext("llvm_nanobind.test", [i32], [7])
        except Exception:
            target_ext = None
        if target_ext is not None:
            assert_llvm_assertion(
                lambda: target_ext.get_target_ext_type_type_param(1),
                "out of range",
            )
            assert_llvm_assertion(
                lambda: target_ext.get_target_ext_type_int_param(1),
                "out of range",
            )

        # Keep a few correctly-typed values live to ensure no accidental
        # poisoning from negative-path checks.
        assert fn_ty.param_count == 1
        assert arr_ty.array_length == 4
        assert vec_ty.vector_size == 4
        assert opaque_struct.is_struct


def test_type_guard_matrix_positive():
    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        f32 = ctx.types.f32
        ptr = ctx.types.ptr
        fn_ty = ctx.types.function(i32, [i32], False)
        arr_ty = ctx.types.array(i32, 4)
        vec_ty = ctx.types.vector(i32, 8)
        opaque_struct = ctx.types.opaque_struct("S")

        assert i32.int_width == 32
        assert fn_ty.param_count == 1
        assert len(fn_ty.param_types) == 1
        assert fn_ty.return_type == i32
        assert ptr.pointer_address_space == 0
        assert arr_ty.array_length == 4
        assert arr_ty.element_type == i32
        assert vec_ty.vector_size == 8
        assert vec_ty.element_type == i32

        opaque_struct.set_body([i32, f32])
        assert opaque_struct.struct_element_count == 2
        assert opaque_struct.get_struct_element_type(0) == i32
        assert opaque_struct.get_struct_element_type(1) == f32
        assert opaque_struct.struct_name == "S"
        assert not opaque_struct.is_opaque_struct

        assert i32.constant(7, False).is_constant
        assert i32.constant_from_string("123", 10).is_constant
        assert f32.real_constant(1.25).is_constant
        assert f32.real_constant_from_string("2.5").is_constant
        assert ctx.types.bf16.real_constant(1.25).is_constant
        assert ctx.types.bf16.real_constant_from_string("2.5").is_constant
        assert ctx.types.x86_fp80.real_constant(1.25).is_constant
        assert ctx.types.x86_fp80.real_constant_from_string("2.5").is_constant
        assert ctx.types.ppc_fp128.real_constant(1.25).is_constant
        assert ctx.types.ppc_fp128.real_constant_from_string("2.5").is_constant

        target_ext = None
        try:
            target_ext = ctx.types.target_ext("llvm_nanobind.test", [i32], [7])
        except Exception:
            target_ext = None
        if target_ext is not None:
            assert target_ext.target_ext_type_num_type_params == 1
            assert target_ext.target_ext_type_num_int_params == 1
            assert target_ext.get_target_ext_type_type_param(0) == i32
            assert target_ext.get_target_ext_type_int_param(0) == 7


if __name__ == "__main__":
    test_type_guard_matrix_negative()
    print("test_type_guard_matrix_negative: PASSED")

    test_type_guard_matrix_positive()
    print("test_type_guard_matrix_positive: PASSED")
