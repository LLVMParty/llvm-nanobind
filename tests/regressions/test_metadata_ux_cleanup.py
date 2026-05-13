"""
Regression tests for the Pythonic metadata/debug-info API.

These cover:
- Value.metadata mapping view
- Module.named_metadata mapping/list view
- Module.module_flags view
- Builder.debug_location(...) context manager
- DIBuilder convenience recipes for files, compile units, functions, and locals
"""

import llvm


def test_value_metadata_mapping_set_get_delete():
    with llvm.create_context() as ctx:
        with ctx.create_module("metadata_mapping") as mod:
            i32 = ctx.types.i32
            fn = mod.add_function("f", ctx.types.function(i32, [i32]))
            entry = fn.append_basic_block("entry")

            with entry.create_builder() as builder:
                inst = builder.add(fn.get_param(0), i32.constant(1), "sum")
                marker = ctx.md_node([ctx.md_string("example.metadata")])

                inst.metadata["example.kind"] = marker
                assert "example.kind" in inst.metadata
                assert inst.metadata.get("example.kind") == marker
                assert inst.metadata["example.kind"] == marker

                try:
                    _ = inst.metadata["missing.kind"]
                except KeyError:
                    pass
                else:
                    raise AssertionError("expected KeyError for missing metadata")

                builder.ret(inst)

            ir_with_metadata = str(mod)
            assert "!example.kind" in ir_with_metadata

            del inst.metadata["example.kind"]
            assert inst.metadata.get("example.kind") is None
            assert "!example.kind" not in str(mod)
            assert mod.verify(), mod.verification_error


def test_named_metadata_view_append_iterate_and_get():
    with llvm.create_context() as ctx:
        with ctx.create_module("named_metadata_view") as mod:
            node = ctx.md_node([ctx.md_string("named")])

            assert "custom.named" not in mod.named_metadata
            assert mod.named_metadata.get("custom.named") is None

            mod.named_metadata["custom.named"].append(node)

            assert "custom.named" in mod.named_metadata
            named = mod.named_metadata["custom.named"]
            assert len(named) == 1
            assert named[0] == node
            assert list(named) == [node]
            assert mod.named_metadata.get("custom.named") is not None
            assert "custom.named" in mod.named_metadata.keys()
            assert "custom.named" in list(mod.named_metadata)
            assert "!custom.named = !{" in str(mod)


def test_md_string_value_round_trips_to_python_string():
    with llvm.create_context() as ctx:
        md_string = ctx.md_string("hello metadata")
        assert isinstance(md_string.kind, int)
        assert md_string.is_string
        assert not md_string.is_node
        assert not md_string.is_value
        assert md_string.string == "hello metadata"

        md_node = ctx.md_node([md_string])
        assert isinstance(md_node.kind, int)
        assert not md_node.is_string
        assert md_node.is_node
        assert not md_node.is_value
        assert len(md_node.operands) == 1
        assert md_node.operands[0].string == "hello metadata"

        try:
            _ = md_node.string
        except llvm.LLVMAssertionError as exc:
            assert "not an MDString" in str(exc)
        else:
            raise AssertionError("expected non-MDString string to fail")

        try:
            _ = md_string.operands
        except llvm.LLVMAssertionError as exc:
            assert "not an MDNode" in str(exc)
        else:
            raise AssertionError("expected non-MDNode operands to fail")

        value_md = ctx.types.i32.constant(7).as_metadata()
        assert value_md.is_value
        try:
            _ = value_md.value
        except NotImplementedError as exc:
            assert "ValueAsMetadata" in str(exc)
        else:
            raise AssertionError("expected Metadata.value to be unimplemented")


def test_metadata_attachment_rejects_cross_context_metadata():
    with llvm.create_context() as ctx1:
        with llvm.create_context() as ctx2:
            with ctx1.create_module("metadata_context_mismatch") as mod:
                i32 = ctx1.types.i32
                fn = mod.add_function("f", ctx1.types.function(i32, [i32]))
                entry = fn.append_basic_block("entry")

                with entry.create_builder() as builder:
                    inst = builder.add(fn.get_param(0), i32.constant(1), "sum")
                    foreign = ctx2.md_node([ctx2.md_string("foreign")])

                    try:
                        inst.metadata["example.foreign"] = foreign
                    except llvm.LLVMAssertionError as exc:
                        assert "same context" in str(exc)
                    else:
                        raise AssertionError("expected cross-context metadata error")

                    builder.ret(inst)

                assert "example.foreign" not in str(mod)
                assert mod.verify(), mod.verification_error


def test_metadata_map_rejects_use_after_module_disposed():
    ctx_mgr = llvm.create_context()
    ctx = ctx_mgr.__enter__()
    try:
        mod_mgr = ctx.create_module("metadata_map_lifetime")
        mod = mod_mgr.__enter__()
        fn = mod.add_function("f", ctx.types.function(ctx.types.void, []))
        metadata_view = fn.metadata

        mod_mgr.__exit__(None, None, None)

        try:
            metadata_view.get("example.kind")
        except llvm.LLVMMemoryError as exc:
            assert "module was disposed" in str(exc)
        else:
            raise AssertionError("expected disposed-module metadata view error")
    finally:
        ctx_mgr.__exit__(None, None, None)


def test_metadata_copy_to_replaces_raw_kind_id_copying():
    with llvm.create_context() as ctx:
        with ctx.create_module("metadata_copy") as mod:
            i32 = ctx.types.i32
            fn = mod.add_function("f", ctx.types.function(i32, [i32]))
            entry = fn.append_basic_block("entry")

            with entry.create_builder() as builder:
                first = builder.add(fn.get_param(0), i32.constant(1), "first")
                second = builder.add(first, i32.constant(2), "second")
                marker = ctx.md_node([ctx.md_string("copied")])
                first.metadata["example.copy"] = marker

                first.metadata.copy_to(second)
                builder.ret(second)

            assert second.metadata.get("example.copy") == marker
            assert str(mod).count("!example.copy") == 2
            assert mod.verify(), mod.verification_error


def test_metadata_mapping_works_on_detached_instruction():
    with llvm.create_context() as ctx:
        with ctx.create_module("detached_metadata") as mod:
            fn = mod.add_function("f", ctx.types.function(ctx.types.void, []))
            entry = fn.append_basic_block("entry")

            with entry.create_builder() as builder:
                ret = builder.ret_void()

            ret.remove_from_parent()
            marker = ctx.md_node([ctx.md_string("detached")])
            ret.metadata["example.detached"] = marker
            assert ret.metadata["example.detached"] == marker
            ret.delete_instruction()


def test_module_flags_view_add_get_and_iterate_keys():
    with llvm.create_context() as ctx:
        with ctx.create_module("module_flags_view") as mod:
            version = ctx.types.i32.constant(3).as_metadata()

            mod.module_flags.add(
                "Debug Info Version", llvm.ModuleFlagBehavior.Warning, version
            )

            assert "Debug Info Version" in mod.module_flags
            assert mod.module_flags.get("Debug Info Version") == version
            assert mod.module_flags["Debug Info Version"] == version
            assert "Debug Info Version" in mod.module_flags.keys()
            assert "Debug Info Version" in list(mod.module_flags)
            assert "!llvm.module.flags" in str(mod)


def test_debug_location_manager_rejects_disposed_builder():
    with llvm.create_context() as ctx:
        with ctx.create_module("debug_location_lifetime") as mod:
            i32 = ctx.types.i32
            fn = mod.add_function("f", ctx.types.function(i32, []))
            entry = fn.append_basic_block("entry")
            loc = ctx.md_node([])

            with entry.create_builder() as builder:
                manager = builder.debug_location(loc)
                builder.ret(i32.constant(0))

            try:
                manager.__enter__()
            except llvm.LLVMMemoryError as exc:
                assert "builder was disposed" in str(exc)
            else:
                raise AssertionError("expected disposed-builder debug location error")


def test_debug_location_context_manager_and_dibuilder_recipes():
    with llvm.create_context() as ctx:
        with ctx.create_module("debug_location_context") as mod:
            mod.is_new_dbg_info_format = True
            i32 = ctx.types.i32
            fn = mod.add_function("add", ctx.types.function(i32, [i32, i32]))

            with mod.create_dibuilder() as dib:
                file = dib.file("main.c", ".")
                compile_unit = dib.compile_unit(
                    language=llvm.DwarfLanguage.C,
                    file=file,
                    producer="llvm-nanobind-test",
                )
                subprogram = dib.function(
                    fn,
                    name="add",
                    file=file,
                    line=1,
                    return_type=i32,
                    param_types=[i32, i32],
                )
                local = dib.local_variable(subprogram, "tmp", file, 2, i32)

                assert compile_unit is not None
                assert subprogram is not None
                assert local is not None

                entry = fn.append_basic_block("entry")
                with entry.create_builder() as builder:
                    with builder.debug_location(line=2, column=5, scope=subprogram):
                        sum_inst = builder.add(fn.get_param(0), fn.get_param(1), "sum")

                    loc = ctx.debug_location(line=3, column=7, scope=subprogram)
                    with builder.debug_location(loc):
                        result = builder.add(sum_inst, i32.constant(0), "result")

                    builder.ret(result)

                dib.finalize()

            ir = str(mod)
            assert "define i32 @add" in ir
            assert "!dbg" in ir
            assert "line: 2, column: 5" in ir
            assert "line: 3, column: 7" in ir
            assert "!llvm.dbg.cu" in ir
            assert mod.verify(), mod.verification_error


def test_redundant_low_level_metadata_apis_are_removed():
    assert not hasattr(llvm, "get_md_kind_id")
    assert not hasattr(llvm, "ValueMetadataEntries")
    assert not hasattr(llvm, "NamedMDNode")

    assert not hasattr(llvm.Context, "get_md_kind_id")
    assert not hasattr(llvm.Context, "create_debug_location")

    assert not hasattr(llvm.Value, "set_metadata")
    assert not hasattr(llvm.Value, "global_copy_all_metadata")
    assert not hasattr(llvm.Value, "instruction_get_all_metadata_other_than_debug_loc")

    assert not hasattr(llvm.Metadata, "as_value")
    assert not hasattr(llvm.Metadata, "string_value")
    assert not hasattr(llvm.Metadata, "__len__")
    assert not hasattr(llvm.Metadata, "__getitem__")
    assert not hasattr(llvm.Metadata, "__iter__")

    assert not hasattr(llvm.Module, "first_named_metadata")
    assert not hasattr(llvm.Module, "last_named_metadata")
    assert not hasattr(llvm.Module, "get_named_metadata")
    assert not hasattr(llvm.Module, "add_named_metadata")
    assert not hasattr(llvm.Module, "get_named_metadata_num_operands")
    assert not hasattr(llvm.Module, "get_named_metadata_operands")
    assert not hasattr(llvm.Module, "add_named_metadata_operand")
    assert not hasattr(llvm.Module, "add_module_flag")
    assert not hasattr(llvm.Module, "get_module_flag")

    assert not hasattr(llvm.DIBuilder, "create_file")
    assert not hasattr(llvm.DIBuilder, "create_compile_unit")

    # Advanced DIBuilder methods remain public because the convenience recipes do
    # not cover custom debug types, manual subroutine types, or record insertion.
    assert hasattr(llvm.DIBuilder, "create_function")
    assert hasattr(llvm.DIBuilder, "create_auto_variable")
    assert hasattr(llvm.DIBuilder, "create_struct_type")


if __name__ == "__main__":
    test_value_metadata_mapping_set_get_delete()
    print("test_value_metadata_mapping_set_get_delete: PASSED")

    test_named_metadata_view_append_iterate_and_get()
    print("test_named_metadata_view_append_iterate_and_get: PASSED")

    test_md_string_value_round_trips_to_python_string()
    print("test_md_string_value_round_trips_to_python_string: PASSED")

    test_metadata_attachment_rejects_cross_context_metadata()
    print("test_metadata_attachment_rejects_cross_context_metadata: PASSED")

    test_metadata_map_rejects_use_after_module_disposed()
    print("test_metadata_map_rejects_use_after_module_disposed: PASSED")

    test_metadata_copy_to_replaces_raw_kind_id_copying()
    print("test_metadata_copy_to_replaces_raw_kind_id_copying: PASSED")

    test_metadata_mapping_works_on_detached_instruction()
    print("test_metadata_mapping_works_on_detached_instruction: PASSED")

    test_module_flags_view_add_get_and_iterate_keys()
    print("test_module_flags_view_add_get_and_iterate_keys: PASSED")

    test_debug_location_manager_rejects_disposed_builder()
    print("test_debug_location_manager_rejects_disposed_builder: PASSED")

    test_debug_location_context_manager_and_dibuilder_recipes()
    print("test_debug_location_context_manager_and_dibuilder_recipes: PASSED")

    test_redundant_low_level_metadata_apis_are_removed()
    print("test_redundant_low_level_metadata_apis_are_removed: PASSED")
