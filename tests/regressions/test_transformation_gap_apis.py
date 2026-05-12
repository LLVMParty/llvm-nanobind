"""Regression tests for transformation-oriented binding gaps."""

import llvm


def test_alloca_array_size_and_is_array_allocation() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("alloca_array_size") as mod:
            i32 = ctx.types.i32
            fn = mod.add_function("f", ctx.types.function(ctx.types.void, []))
            entry = fn.append_basic_block("entry")

            with entry.create_builder() as b:
                scalar = b.alloca(i32, "scalar")
                array = b.array_alloca(i32, i32.constant(4, False), "array")
                b.ret_void()

            assert scalar.allocated_type == i32
            assert scalar.array_size.is_constant_int
            assert scalar.array_size.const_zext_value == 1
            assert not scalar.is_array_allocation

            assert array.allocated_type == i32
            assert array.array_size.is_constant_int
            assert array.array_size.const_zext_value == 4
            assert array.is_array_allocation


def test_replace_uses_of_with_rewrites_only_selected_operands() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("replace_uses_of_with") as mod:
            i32 = ctx.types.i32
            fn = mod.add_function("f", ctx.types.function(i32, [i32]))
            x = fn.get_param(0)
            entry = fn.append_basic_block("entry")

            with entry.create_builder() as b:
                two = i32.constant(2, False)
                three = i32.constant(3, False)
                add = b.add(x, two, "sum")
                replacement = b.add(add, three, "replacement")
                mul = b.mul(add, add, "mul")
                mul.replace_uses_of_with(add, replacement)
                b.ret(mul)

            assert mod.verify(), mod.get_verification_error()
            text = mod.to_string()
            assert "mul i32 %replacement, %replacement" in text


def test_phi_get_incoming_value_for_block() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("phi_incoming_for_block") as mod:
            i1 = ctx.types.i1
            i32 = ctx.types.i32
            fn = mod.add_function("f", ctx.types.function(i32, [i1]))
            entry = fn.append_basic_block("entry")
            then = fn.append_basic_block("then")
            other = fn.append_basic_block("other")
            merge = fn.append_basic_block("merge")

            with entry.create_builder() as b:
                b.cond_br(fn.get_param(0), then, other)

            with then.create_builder() as b:
                b.br(merge)

            with other.create_builder() as b:
                b.br(merge)

            with merge.create_builder(first_non_phi=True) as b:
                phi = b.phi(i32, "v")
                phi.add_incoming(i32.constant(11, False), then)
                phi.add_incoming(i32.constant(17, False), other)
                b.ret(phi)

            assert phi.get_incoming_value_for_block(then).const_zext_value == 11
            assert phi.get_incoming_value_for_block(other).const_zext_value == 17


def test_basic_block_erase_from_parent() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("erase_basic_block") as mod:
            fn = mod.add_function("f", ctx.types.function(ctx.types.void, []))
            entry = fn.append_basic_block("entry")
            dead = fn.append_basic_block("dead")

            with entry.create_builder() as b:
                b.ret_void()
            with dead.create_builder() as b:
                b.unreachable()

            assert fn.basic_block_count == 2
            dead.erase_from_parent()
            assert fn.basic_block_count == 1
            assert mod.verify(), mod.get_verification_error()


def test_builder_vector_splat() -> None:
    with llvm.create_context() as ctx:
        with ctx.create_module("vector_splat") as mod:
            i32 = ctx.types.i32
            vec4 = i32.vector(4)
            fn = mod.add_function("f", ctx.types.function(i32, [i32]))
            entry = fn.append_basic_block("entry")

            with entry.create_builder() as b:
                splat = b.vector_splat(4, fn.get_param(0), "splat")
                lane2 = b.extract_element(splat, i32.constant(2, False), "lane2")
                b.ret(lane2)

            assert mod.verify(), mod.get_verification_error()
            text = mod.to_string()
            assert "shufflevector" in text
            assert str(vec4) in text


if __name__ == "__main__":
    import pytest

    raise SystemExit(pytest.main([__file__]))
