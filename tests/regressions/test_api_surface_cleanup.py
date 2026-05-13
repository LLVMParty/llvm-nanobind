"""
Regression coverage for API surface cleanup helpers.

Covers:
- Module add_* reuse_existing=True defaults and explicit raw insertion escape hatch
- Named struct reuse_existing=True defaults and explicit raw insertion escape hatch
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


def test_module_add_reuse_existing_semantics() -> None:
    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        i64 = ctx.types.i64
        void = ctx.types.void
        fn_ty = ctx.types.function(void, [])
        other_fn_ty = ctx.types.function(i32, [])
        resolver_ty = ctx.types.function(i32, [])

        with ctx.create_module("reuse_existing") as mod:
            fn = mod.add_function("f", fn_ty)
            assert mod.add_function("f", fn_ty) == fn
            assert len([f for f in mod.functions if f.name.startswith("f")]) == 1
            expect_raises(lambda: mod.add_function("f", other_fn_ty), "different type")
            raw_fn = mod.add_function("f", fn_ty, reuse_existing=False)
            assert raw_fn.name != "f"

            g = mod.add_global(i32, "g")
            assert mod.add_global(i32, "g") == g
            expect_raises(lambda: mod.add_global(i64, "g"), "different type")
            expect_raises(
                lambda: mod.add_global_in_address_space(i32, "g", 1),
                "address space",
            )
            raw_g = mod.add_global(i32, "g", reuse_existing=False)
            assert raw_g.name != "g"

            expect_raises(lambda: mod.add_function("g", fn_ty), "global variable")
            expect_raises(lambda: mod.add_global(i32, "f"), "function")

            alias = mod.add_alias(i32, 0, g, "alias")
            assert mod.add_alias(i32, 0, g, "alias") == alias
            expect_raises(lambda: mod.add_alias(i64, 0, g, "alias"), "different type")
            raw_alias = mod.add_alias(i32, 0, g, "alias", reuse_existing=False)
            assert raw_alias.name != "alias"

            resolver = mod.add_function("resolver", resolver_ty)
            other_resolver = mod.add_function("other_resolver", resolver_ty)
            ifunc = mod.add_global_ifunc("ifunc", resolver_ty, 0, resolver)
            assert mod.add_global_ifunc("ifunc", resolver_ty, 0, resolver) == ifunc
            expect_raises(
                lambda: mod.add_global_ifunc("ifunc", resolver_ty, 1, resolver),
                "address space",
            )
            expect_raises(
                lambda: mod.add_global_ifunc("ifunc", resolver_ty, 0, other_resolver),
                "different resolver",
            )
            raw_ifunc = mod.add_global_ifunc(
                "ifunc", resolver_ty, 0, resolver, reuse_existing=False
            )
            assert raw_ifunc.name != "ifunc"


def test_named_struct_reuse_existing_semantics() -> None:
    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        i64 = ctx.types.i64

        pair = ctx.types.struct("Pair", [i32, i64], packed=False)
        assert pair.struct_name == "Pair"
        assert not pair.is_opaque_struct
        assert ctx.types.struct("Pair", [i32, i64], packed=False) == pair
        expect_raises(
            lambda: ctx.types.struct("Pair", [i32], packed=False),
            "different body",
        )
        expect_raises(
            lambda: ctx.types.struct("Pair", [i32, i64], packed=True),
            "different packing",
        )

        raw_pair = ctx.types.struct(
            "Pair", [i32, i64], packed=False, reuse_existing=False
        )
        assert raw_pair != pair
        assert raw_pair.struct_name != "Pair"

        forward = ctx.types.opaque_struct("Forward")
        assert forward.is_opaque_struct
        assert ctx.types.opaque_struct("Forward") == forward
        completed = ctx.types.struct("Forward", [i32])
        assert completed == forward
        assert not completed.is_opaque_struct

        raw_forward = ctx.types.opaque_struct("Forward", reuse_existing=False)
        assert raw_forward != forward
        assert raw_forward.struct_name != "Forward"


def test_member_factories_and_builder_navigation() -> None:
    removed_globals = [
        "AttributeFunctionIndex",
        "AttributeReturnIndex",
        "last_enum_attribute_kind",
        "block_address",
        "const_data_array",
        "const_gep_with_no_wrap_flags",
        "const_int_of_arbitrary_precision",
        "const_ptr_auth",
        "const_string",
        "const_struct",
        "const_vector",
        "create_binary_from_bytes",
        "create_binary_from_file",
        "create_disasm_cpu_features",
        "create_target_data",
        "create_target_machine",
        "di_file_get_directory",
        "di_file_get_filename",
        "di_file_get_source",
        "di_global_variable_expression_get_expression",
        "di_global_variable_expression_get_variable",
        "di_location_get_column",
        "di_location_get_inlined_at",
        "di_location_get_line",
        "di_location_get_scope",
        "di_scope_get_file",
        "di_subprogram_get_line",
        "di_subprogram_replace_type",
        "di_type_get_name",
        "di_variable_get_file",
        "di_variable_get_line",
        "di_variable_get_scope",
        "dibuilder_create_debug_location",
        "get_cast_opcode",
        "get_debug_metadata_version",
        "get_default_target_triple",
        "get_first_dbg_record",
        "get_host_cpu_features",
        "get_host_cpu_name",
        "get_inline_asm",
        "get_md_kind_id",
        "initialize_all_asm_parsers",
        "initialize_all_asm_printers",
        "initialize_all_disassemblers",
        "initialize_all_target_infos",
        "initialize_all_target_mcs",
        "initialize_all_targets",
        "initialize_native_asm_parser",
        "initialize_native_asm_printer",
        "initialize_native_disassembler",
        "initialize_native_target",
        "get_di_node_tag",
        "get_last_dbg_record",
        "get_last_enum_attribute_kind",
        "get_module_debug_metadata_version",
        "get_next_dbg_record",
        "get_previous_dbg_record",
        "get_target_from_name",
        "get_target_from_triple",
        "intrinsic_get_type",
        "lookup_enum_attribute_kind",
        "replace_md_node_operand_with",
        "run_passes",
        "strip_module_debug_info",
        "NamedMDNode",
        "ValueMetadataEntries",
    ]
    for name in removed_globals:
        assert not hasattr(llvm, name), name

    removed_class_methods = {
        llvm.Context: [
            "get_diagnostics",
            "get_md_kind_id",
            "create_debug_location",
            "create_enum_attribute",
            "create_string_attribute",
            "create_type_attribute",
        ],
        llvm.Module: [
            "get_verification_error",
            "first_named_metadata",
            "last_named_metadata",
            "get_named_metadata",
            "add_named_metadata",
            "get_named_metadata_num_operands",
            "get_named_metadata_operands",
            "add_named_metadata_operand",
            "add_module_flag",
            "get_module_flag",
        ],
        llvm.Function: [
            "get_personality_fn",
            "set_personality_fn",
            "get_gc",
            "set_gc",
            "get_attribute_count",
            "get_enum_attribute",
            "add_attribute",
            "get_attributes",
            "get_string_attribute",
            "remove_enum_attribute",
            "remove_string_attribute",
        ],
        llvm.Attribute: ["kind"],
        llvm.AttributeAccessor: ["get_enum", "remove_enum"],
        llvm.DIBuilder: ["create_file", "create_compile_unit"],
        llvm.Metadata: ["as_value", "string_value", "__len__", "__getitem__", "__iter__"],
        llvm.Value: [
            "set_constant",
            "set_comdat",
            "set_thread_local",
            "set_externally_initialized",
            "set_volatile",
            "set_inst_alignment",
            "set_global_ifunc_resolver",
            "set_personality_fn",
            "set_prefix_data",
            "set_prologue_data",
            "set_nsw",
            "set_nuw",
            "set_exact",
            "set_nneg",
            "set_is_disjoint",
            "set_icmp_same_sign",
            "set_ordering",
            "set_atomic_sync_scope_id",
            "set_weak",
            "set_tail_call_kind",
            "set_called_operand",
            "set_cleanup",
            "set_fast_math_flags",
            "get_callsite_attribute_count",
            "get_callsite_enum_attribute",
            "add_callsite_attribute",
            "set_metadata",
            "global_copy_all_metadata",
            "instruction_get_all_metadata_other_than_debug_loc",
        ],
    }
    for cls, names in removed_class_methods.items():
        for name in names:
            assert not hasattr(cls, name), f"{cls.__name__}.{name}"

    assert isinstance(llvm.default_target_triple, str)
    assert isinstance(llvm.host_cpu_name, str)
    assert isinstance(llvm.host_cpu_features, str)
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
                expect_raises(lambda: ret.has_dbg_records, "LLVM-C has no safe")

            with mod.create_builder(ret) as before_ret:
                assert before_ret.insert_block == bb

            assert not hasattr(mod, "get_or_insert_named_metadata")
            named_md = mod.named_metadata["llvm.nanobind.surface"]
            assert mod.named_metadata["llvm.nanobind.surface"] is not None
            assert len(named_md) == 0

            assert not hasattr(mod, "get_or_insert_comdat")
            comdat = mod.add_comdat("surface_comdat")
            comdat.selection_kind = llvm.ComdatSelectionKind.ExactMatch
            assert (
                mod.add_comdat("surface_comdat").selection_kind
                == llvm.ComdatSelectionKind.ExactMatch
            )

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
    test_module_add_reuse_existing_semantics()
    test_named_struct_reuse_existing_semantics()
    test_member_factories_and_builder_navigation()
    print("test_api_surface_cleanup: PASSED")
