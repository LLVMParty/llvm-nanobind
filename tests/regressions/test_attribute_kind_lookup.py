"""
Regression tests for the public attribute API.

Enum attribute kind IDs are LLVM-build-specific, so the public API should use
attribute names and slot accessors instead of exposing numeric kind IDs.
"""

import llvm


def test_attribute_enum_factory_for_noreturn():
    with llvm.create_context() as ctx:
        with ctx.create_module("attr_lookup") as mod:
            void = ctx.types.void
            fn_ty = ctx.types.function(void, [])
            fn = mod.add_function("abort_like", fn_ty)

            attr = llvm.Attribute.enum(ctx, "noreturn")
            assert attr.value == 0

            fn.attributes.add(attr)
            assert fn.attributes.get("noreturn") is not None

            ir = str(mod)
            assert "noreturn" in ir
            assert mod.verify(), mod.verification_error


def test_attribute_factories_and_function_accessors():
    with llvm.create_context() as ctx:
        with ctx.create_module("attr_accessors") as mod:
            i32 = ctx.types.i32
            ptr = ctx.types.ptr
            fn_ty = ctx.types.function(i32, [ptr, ptr])
            fn = mod.add_function("f", fn_ty)

            fn.attributes.add("noreturn")
            assert "noreturn" in fn.attributes
            assert len(fn.attributes) == 1

            cold = llvm.Attribute.enum(ctx, "cold")
            fn.attributes.add(cold)
            assert "cold" in fn.attributes

            fn.attributes.add_string("frame-pointer", "all")
            assert "frame-pointer" in fn.attributes
            frame_pointer_attr = fn.attributes.get("frame-pointer")
            assert frame_pointer_attr is not None
            assert frame_pointer_attr.string_value == "all"
            fn.attributes.remove("frame-pointer")
            assert "frame-pointer" not in fn.attributes

            fn.return_attributes.add("zeroext")
            assert "zeroext" in fn.return_attributes

            fn.param_attributes(0).add("nonnull")
            fn.param_attributes(0).add("align", 16)
            assert "nonnull" in fn.param_attributes(0)
            assert "align" in fn.param_attributes(0)

            byval = llvm.Attribute.type(ctx, "byval", i32)
            fn.param_attributes(1).add(byval)
            assert "byval" in fn.param_attributes(1)

            attrs = list(fn.attributes)
            assert len(attrs) == 2
            assert all(attr.is_enum_attribute for attr in attrs)

            ir = str(mod)
            assert "noreturn" in ir
            assert "zeroext" in ir
            assert "nonnull" in ir
            assert "byval" in ir
            assert mod.verify(), mod.verification_error


def test_callsite_attribute_accessors():
    with llvm.create_context() as ctx:
        with ctx.create_module("callsite_attr_accessors") as mod:
            void = ctx.types.void
            ptr = ctx.types.ptr
            callee_ty = ctx.types.function(void, [ptr])
            callee = mod.add_function("callee", callee_ty)
            caller_ty = ctx.types.function(void, [ptr])
            caller = mod.add_function("caller", caller_ty)
            entry = caller.append_basic_block("entry")

            with entry.create_builder() as builder:
                call = builder.call(callee_ty, callee, [caller.get_param(0)], "")
                call.callsite_attributes.add("noreturn")
                call.callsite_param_attributes(0).add("nonnull")
                call.callsite_param_attributes(0).add("align", 16)
                builder.ret_void()

            assert "noreturn" in call.callsite_attributes
            assert "nonnull" in call.callsite_param_attributes(0)
            assert "align" in call.callsite_param_attributes(0)
            assert len(call.callsite_attributes) == 1

            call.callsite_attributes.remove("noreturn")
            assert "noreturn" not in call.callsite_attributes

            ir = str(mod)
            assert "nonnull" in ir
            assert "align" in ir
            assert mod.verify(), mod.verification_error


def test_attribute_enum_unknown_name_raises():
    with llvm.create_context() as ctx:
        try:
            llvm.Attribute.enum(ctx, "not-a-real-llvm-attribute")
        except llvm.LLVMAssertionError as exc:
            assert "Unknown enum attribute kind" in str(exc)
        else:
            raise AssertionError("expected LLVMAssertionError")


if __name__ == "__main__":
    test_attribute_enum_factory_for_noreturn()
    print("test_attribute_enum_factory_for_noreturn: PASSED")

    test_attribute_factories_and_function_accessors()
    print("test_attribute_factories_and_function_accessors: PASSED")

    test_callsite_attribute_accessors()
    print("test_callsite_attribute_accessors: PASSED")

    test_attribute_enum_unknown_name_raises()
    print("test_attribute_enum_unknown_name_raises: PASSED")
