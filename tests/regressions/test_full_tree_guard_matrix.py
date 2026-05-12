"""
Full-tree regression matrix for remaining wrapper guard paths.

This complements value/type/non-value matrices with:
- helper API assertions (phi/switch/indirectbr, call builders, vector_const)
- manager state-machine guards (Context/Module/Builder/DIBuilder/Binary)
- wrapper lifetime guards (TypeFactory/Attribute/Comdat/NamedMDNode/Use/
  ValueMetadataEntries)
- attribute/operand-bundle/indexed metadata guards
- disassembler invalid-context guards
"""

from pathlib import Path

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


def assert_memory_error(action, expected_substring: str):
    try:
        action()
        assert False, "Expected llvm.LLVMMemoryError"
    except llvm.LLVMMemoryError as e:
        msg = str(e).lower()
        assert expected_substring.lower() in msg, (
            f"Expected '{expected_substring}' in error message, got: {e}"
        )


def assert_llvm_error(action, expected_substring: str):
    try:
        action()
        assert False, "Expected llvm.LLVMError"
    except llvm.LLVMError as e:
        msg = str(e).lower()
        assert expected_substring.lower() in msg, (
            f"Expected '{expected_substring}' in error message, got: {e}"
        )


def assert_out_of_range(action):
    try:
        action()
        assert False, "Expected out-of-range exception"
    except Exception as e:
        assert "out of range" in str(e).lower(), f"Unexpected message: {e}"


def test_helper_assertions_and_call_guards():
    with llvm.create_context() as ctx:
        with ctx.create_module("helpers") as mod:
            i1 = ctx.types.i1
            i32 = ctx.types.i32
            f32 = ctx.types.f32
            void = ctx.types.void

            # Function for PHI/switch/add_destination helper assertions.
            fn_ty = ctx.types.function(void, [i1, i32], False)
            fn = mod.add_function("f", fn_ty)
            entry = fn.append_basic_block("entry")
            left = fn.append_basic_block("left")
            right = fn.append_basic_block("right")
            merge = fn.append_basic_block("merge")

            with entry.create_builder() as b:
                b.cond_br(fn.get_param(0), left, right)

                b.position_at_end(left)
                add_inst = b.add(fn.get_param(1), i32.constant(1, False), "add")
                switch_inst = b.switch_(fn.get_param(1), merge, 1)
                switch_inst.add_case(i32.constant(0, False), merge)

                b.position_at_end(right)
                b.br(merge)

                b.position_at_end(merge)
                phi = b.phi(i32, "p")
                phi.add_incoming(fn.get_param(1), left)
                phi.add_incoming(fn.get_param(1), right)
                b.ret_void()

            assert_llvm_assertion(
                lambda: add_inst.add_incoming(i32.constant(0, False), merge),
                "requires a phi node",
            )
            assert_llvm_assertion(
                lambda: phi.add_incoming(f32.real_constant(1.0), merge),
                "type mismatch",
            )
            assert_llvm_assertion(
                lambda: add_inst.add_case(i32.constant(1, False), merge),
                "requires a switch instruction",
            )
            assert_llvm_assertion(
                lambda: switch_inst.add_case(add_inst, merge),
                "must be constant",
            )
            assert_llvm_assertion(
                lambda: add_inst.add_destination(merge),
                "requires an indirect branch instruction",
            )

            # Builder call-family assertions.
            callee_void_ty = ctx.types.function(void, [], False)
            callee_void = mod.add_function("callee_void", callee_void_ty)
            with callee_void.append_basic_block("entry").create_builder() as b:
                b.ret_void()

            call_fn_ty = ctx.types.function(void, [i32], False)
            call_fn = mod.add_function("call_fn", call_fn_ty)
            call_entry = call_fn.append_basic_block("entry")
            with call_entry.create_builder() as b:
                assert_llvm_assertion(
                    lambda: b.call(i32, callee_void, [], ""),
                    "non-function type",
                )
                assert_llvm_assertion(
                    lambda: b.call(callee_void_ty, callee_void, [], "named_void"),
                    "returning void",
                )
                assert_llvm_assertion(
                    lambda: b.call(call_fn.get_param(0), []),
                    "func must be a function",
                )
                assert_llvm_assertion(
                    lambda: b.call(callee_void, [], "named_void2"),
                    "returning void",
                )
                b.ret_void()

            assert_llvm_assertion(lambda: ctx.types.i32.vector_const([]), "empty vector")


def test_attribute_operand_bundle_and_entries_guards():
    with llvm.create_context() as ctx:
        with ctx.create_module("attrmd") as mod:
            i32 = ctx.types.i32
            enum_attr = ctx.create_enum_attribute(1, 0)
            string_attr = ctx.create_string_attribute("k", "v")

            assert_llvm_error(lambda: enum_attr.string_kind, "not a string attribute")
            assert_llvm_error(lambda: enum_attr.string_value, "not a string attribute")
            assert_llvm_error(lambda: string_attr.type_value, "not a type attribute")

            bundle = llvm.create_operand_bundle("deopt", [i32.constant(0, False)], ctx)
            assert bundle.num_args == 1
            assert_llvm_assertion(
                lambda: bundle.get_arg_at_index(1),
                "out of range",
            )

            fn_ty = ctx.types.function(i32, [i32], False)
            fn = mod.add_function("f", fn_ty)
            bb = fn.append_basic_block("entry")
            with bb.create_builder() as b:
                add_inst = b.add(fn.get_param(0), i32.constant(1, False), "sum")
                b.ret(add_inst)

            md = ctx.md_string("meta")
            kind = ctx.get_md_kind_id("llvm_nanobind.meta")
            add_inst.set_metadata(kind, md, ctx)
            entries = add_inst.instruction_get_all_metadata_other_than_debug_loc()
            assert len(entries) >= 1
            _ = entries.get_kind(0)
            _ = entries.get_metadata(0)
            assert_out_of_range(lambda: entries.get_kind(len(entries)))
            assert_out_of_range(lambda: entries.get_metadata(len(entries)))


def test_manager_state_machine_guards():
    cm = llvm.create_context()
    ctx = cm.__enter__()
    assert_memory_error(lambda: cm.__enter__(), "already entered")
    cm.__exit__(None, None, None)
    assert_memory_error(lambda: cm.__exit__(None, None, None), "not entered")

    with llvm.create_context() as ctx:
        mm = ctx.create_module("m")
        assert_memory_error(lambda: mm.dispose(), "not been created")
        mod = mm.__enter__()
        assert mod.name == "m"
        assert_memory_error(lambda: mm.dispose(), "cannot call dispose() after __enter__")
        mm.__exit__(None, None, None)
        assert_memory_error(lambda: mm.__exit__(None, None, None), "already been disposed")

    with llvm.create_context() as ctx:
        with ctx.create_module("m2") as mod:
            fn_ty = ctx.types.function(ctx.types.void, [], False)
            fn = mod.add_function("f", fn_ty)
            bb = fn.append_basic_block("entry")
            with bb.create_builder() as b:
                b.ret_void()

            bm = bb.create_builder()
            bm.dispose()
            assert_memory_error(lambda: bm.dispose(), "already been disposed")

            bm2 = bb.create_builder()
            _ = bm2.__enter__()
            assert_memory_error(
                lambda: bm2.dispose(),
                "cannot call dispose() after __enter__",
            )
            bm2.__exit__(None, None, None)
            assert_memory_error(
                lambda: bm2.__exit__(None, None, None),
                "already been disposed",
            )

            dm = mod.create_dibuilder()
            dm.dispose()
            assert_memory_error(lambda: dm.dispose(), "already been disposed")

            dm2 = mod.create_dibuilder()
            dib = dm2.__enter__()
            _ = dib.create_file("x.c", ".")
            assert_memory_error(
                lambda: dm2.dispose(),
                "cannot call dispose() after __enter__",
            )
            dm2.__exit__(None, None, None)
            assert_memory_error(
                lambda: dm2.__exit__(None, None, None),
                "already been disposed",
            )

    bitcode_path = Path(__file__).parent / "factorial.bc"
    bitcode = bitcode_path.read_bytes()

    bin_mgr = llvm.BinaryManager.from_bytes(bitcode)
    bin_mgr.dispose()
    assert_memory_error(lambda: bin_mgr.dispose(), "already been disposed")

    # Non-object binaries should reject object iterators instead of crashing.
    with llvm.BinaryManager.from_bytes(bitcode) as bitcode_binary:
        assert_llvm_assertion(lambda: bitcode_binary.sections, "object-file binary")
        assert_llvm_assertion(lambda: bitcode_binary.symbols, "object-file binary")

    object_file = Path("llvm-c/llvm-c-test/inputs/simple.o")
    if not object_file.exists():
        print(f"SKIP binary manager iterator checks: missing {object_file}")
        return

    bin_mgr2 = llvm.BinaryManager.from_file(object_file)
    binary = bin_mgr2.__enter__()
    sections = binary.sections
    symbols = binary.symbols
    assert_memory_error(
        lambda: bin_mgr2.dispose(),
        "cannot call dispose() after __enter__",
    )
    bin_mgr2.__exit__(None, None, None)
    assert_memory_error(
        lambda: bin_mgr2.__exit__(None, None, None),
        "already been disposed",
    )
    assert_memory_error(lambda: sections.is_at_end(), "after binary was disposed")
    assert_memory_error(lambda: symbols.is_at_end(), "after binary was disposed")


def test_lifetime_guards_for_remaining_wrappers():
    escaped = {}

    with llvm.create_context() as ctx:
        orphan = ctx.create_basic_block("orphan")
        assert_llvm_assertion(lambda: orphan.function, "no parent function")
        assert_llvm_assertion(lambda: orphan.module, "no parent function")
        assert_llvm_assertion(lambda: orphan.context, "no parent function")

    with llvm.create_context() as ctx:
        with ctx.create_module("lifetime") as mod:
            i32 = ctx.types.i32
            escaped["types"] = ctx.types
            escaped["attr"] = ctx.create_string_attribute("k", "v")
            escaped["comdat"] = mod.add_comdat("C")
            escaped["named_md"] = mod.add_named_metadata("llvm.nanobind.named")

            fn_ty = ctx.types.function(i32, [i32], False)
            fn = mod.add_function("f", fn_ty)
            bb = fn.append_basic_block("entry")
            with bb.create_builder() as b:
                add_inst = b.add(fn.get_param(0), i32.constant(1, False), "sum")
                b.ret(add_inst)

            uses = fn.get_param(0).uses
            assert len(uses) >= 1
            escaped["use"] = uses[0]

            md = ctx.md_string("meta")
            kind = ctx.get_md_kind_id("llvm_nanobind.lifetime")
            add_inst.set_metadata(kind, md, ctx)
            escaped["entries"] = add_inst.instruction_get_all_metadata_other_than_debug_loc()
            assert len(escaped["entries"]) >= 1

    assert_memory_error(lambda: escaped["types"].i32, "typefactory used after context")
    assert_memory_error(lambda: escaped["attr"].string_kind, "attribute used after context")
    assert_memory_error(lambda: escaped["comdat"].selection_kind, "comdat used after module")
    assert_memory_error(lambda: escaped["named_md"].name, "namedmdnode used after context")
    assert_memory_error(lambda: escaped["use"].user, "use used after context")
    assert_memory_error(
        lambda: escaped["entries"].get_kind(0),
        "valuemetadataentries used after context",
    )


def test_disasm_invalid_context_guard():
    dis = llvm.Disasm.create("zzzz-unknown-none")
    if dis.is_valid:
        dis = llvm.Disasm.create("definitely-not-a-real-triple")
    if dis.is_valid:
        print("SKIP: could not create an invalid disassembler context on this host")
        return

    assert_memory_error(
        lambda: dis.disasm_instruction(b"\x90", 0, 0),
        "null or invalid",
    )
    assert_memory_error(
        lambda: dis.set_options(llvm.DisasmOption_PrintImmHex),
        "null or invalid",
    )


if __name__ == "__main__":
    test_helper_assertions_and_call_guards()
    print("test_helper_assertions_and_call_guards: PASSED")

    test_attribute_operand_bundle_and_entries_guards()
    print("test_attribute_operand_bundle_and_entries_guards: PASSED")

    test_manager_state_machine_guards()
    print("test_manager_state_machine_guards: PASSED")

    test_lifetime_guards_for_remaining_wrappers()
    print("test_lifetime_guards_for_remaining_wrappers: PASSED")

    test_disasm_invalid_context_guard()
    print("test_disasm_invalid_context_guard: PASSED")
