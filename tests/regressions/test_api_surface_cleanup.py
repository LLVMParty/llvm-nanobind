"""
Regression coverage for API surface cleanup helpers.

Covers:
- Module add_* get_or_insert=True defaults and explicit raw insertion escape hatch
- New member/factory APIs that replace module-level helpers
- Builder parent navigation helpers and Module.create_builder()
"""

import llvm


def expect_raises(fn, message_part: str) -> None:
    try:
        fn()
    except Exception as exc:
        assert message_part in str(exc), str(exc)
    else:
        raise AssertionError("expected exception")


def test_module_add_get_or_insert_semantics() -> None:
    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        i64 = ctx.types.i64
        void = ctx.types.void
        fn_ty = ctx.types.function(void, [])
        other_fn_ty = ctx.types.function(i32, [])
        resolver_ty = ctx.types.function(i32, [])

        with ctx.create_module("get_or_insert") as mod:
            fn = mod.add_function("f", fn_ty)
            assert mod.add_function("f", fn_ty) == fn
            assert len([f for f in mod.functions if f.name.startswith("f")]) == 1
            expect_raises(lambda: mod.add_function("f", other_fn_ty), "different type")
            raw_fn = mod.add_function("f", fn_ty, get_or_insert=False)
            assert raw_fn.name != "f"

            g = mod.add_global(i32, "g")
            assert mod.add_global(i32, "g") == g
            expect_raises(lambda: mod.add_global(i64, "g"), "different type")
            expect_raises(
                lambda: mod.add_global_in_address_space(i32, "g", 1),
                "address space",
            )
            raw_g = mod.add_global(i32, "g", get_or_insert=False)
            assert raw_g.name != "g"

            expect_raises(lambda: mod.add_function("g", fn_ty), "global variable")
            expect_raises(lambda: mod.add_global(i32, "f"), "function")

            alias = mod.add_alias(i32, 0, g, "alias")
            assert mod.add_alias(i32, 0, g, "alias") == alias
            expect_raises(lambda: mod.add_alias(i64, 0, g, "alias"), "different type")

            resolver = mod.add_function("resolver", resolver_ty)
            ifunc = mod.add_global_ifunc("ifunc", resolver_ty, 0, resolver)
            assert mod.add_global_ifunc("ifunc", resolver_ty, 0, resolver) == ifunc
            expect_raises(
                lambda: mod.add_global_ifunc("ifunc", resolver_ty, 1, resolver),
                "address space",
            )


def test_member_factories_and_builder_navigation() -> None:
    llvm.initialize_all_target_infos()
    llvm.initialize_all_targets()
    llvm.initialize_all_target_mcs()
    llvm.initialize_all_asm_printers()
    llvm.initialize_all_asm_parsers()

    assert isinstance(llvm.default_target_triple, str)
    assert isinstance(llvm.host_cpu_name, str)
    assert isinstance(llvm.host_cpu_features, str)
    assert isinstance(llvm.last_enum_attribute_kind, int)
    assert isinstance(llvm.debug_metadata_version, int)

    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        i64 = ctx.types.i64
        ptr = ctx.types.ptr
        fn_ty = ctx.types.function(i32, [])

        assert i64.constant([1]).const_zext_value == 1
        assert i32.vector_const([i32.constant(1), i32.constant(2)]).is_constant
        assert i32.constant(1).get_cast_opcode(False, i64, False) == llvm.Opcode.ZExt
        assert ctx.get_intrinsic_type(llvm.lookup_intrinsic_id("llvm.memcpy"), [ptr, ptr, i64]).kind == llvm.TypeKind.Function
        asm_ty = ctx.types.function(ctx.types.void, [])
        assert asm_ty.inline_asm("nop", "", False, False, llvm.InlineAsmDialect.ATT, False).is_inline_asm

        with ctx.create_module("surface") as mod:
            fn = mod.add_function("f", fn_ty)
            bb = fn.append_basic_block("entry")
            assert bb.block_address().is_constant

            with mod.create_builder(bb) as builder:
                assert builder.function == fn
                assert builder.module == mod
                assert builder.context is not None
                ret = builder.ret(i32.constant(0))
                assert ret.first_dbg_record is None

            with mod.create_builder(ret) as before_ret:
                assert before_ret.insert_block == bb

            opts = llvm.PassBuilderOptions()
            mod.run_passes("default<O0>", options=opts)

            bitcode = mod.write_bitcode_to_memory_buffer()
            with llvm.BinaryManager.from_bytes(bitcode) as binary:
                assert binary.copy_to_memory_buffer()

    target = llvm.Target.from_triple(llvm.default_target_triple)
    assert target is not None
    tm = llvm.TargetMachine.create(target, llvm.default_target_triple)
    td = tm.create_data_layout()
    assert str(llvm.TargetData.create(str(td))) == str(td)

    dis = llvm.Disasm.create(llvm.default_target_triple)
    assert hasattr(dis, "is_valid")


if __name__ == "__main__":
    test_module_add_get_or_insert_semantics()
    test_member_factories_and_builder_navigation()
    print("test_api_surface_cleanup: PASSED")
