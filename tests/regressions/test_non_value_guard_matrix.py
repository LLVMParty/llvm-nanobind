"""
Comprehensive guard matrix for non-Value wrapper APIs.

Covers:
- Builder instruction-only APIs.
- Function/block ownership checks (block_address, append_existing_basic_block).
- Function parameter attribute accessor bounds.
- Callsite parameter attribute accessor bounds.
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


def build_fixture(ctx: llvm.Context, mod: llvm.Module):
    i32 = ctx.types.i32
    void = ctx.types.void

    fn_ty = ctx.types.function(i32, [i32], False)
    fn = mod.add_function("f", fn_ty)
    entry = fn.append_basic_block("entry")
    with entry.create_builder() as b:
        arg0 = fn.get_param(0)
        add_inst = b.add(arg0, i32.constant(1, False), "add")
        b.ret(add_inst)

    first_inst = entry.first_instruction
    assert first_inst is not None
    detached_clone = first_inst.instruction_clone()

    other_fn = mod.add_function("g", fn_ty)
    other_entry = other_fn.append_basic_block("entry")
    with other_entry.create_builder() as b:
        b.ret(i32.constant(0, False))
    other_inst = other_entry.first_instruction
    assert other_inst is not None

    orphan_block = ctx.create_basic_block("orphan")
    fn_attr = llvm.Attribute.string(ctx, "test.attr", "1")
    call_attr = llvm.Attribute.string(ctx, "callsite.attr", "1")

    callee_ty = ctx.types.function(void, [i32], False)
    callee = mod.add_function("callee", callee_ty)
    with callee.append_basic_block("entry").create_builder() as b:
        b.ret_void()

    caller_ty = ctx.types.function(void, [i32], False)
    caller = mod.add_function("caller", caller_ty)
    caller_entry = caller.append_basic_block("entry")
    with caller_entry.create_builder() as b:
        call_inst = b.call(callee_ty, callee, [caller.get_param(0)], "")
        b.ret_void()

    return {
        "fn": fn,
        "entry": entry,
        "first_inst": first_inst,
        "detached_clone": detached_clone,
        "other_entry": other_entry,
        "other_inst": other_inst,
        "orphan_block": orphan_block,
        "fn_attr": fn_attr,
        "call_attr": call_attr,
        "call_inst": call_inst,
    }


def test_non_value_guard_matrix_negative():
    with llvm.create_context() as ctx:
        with ctx.create_module("guards") as mod:
            refs = build_fixture(ctx, mod)
            fn = refs["fn"]
            entry = refs["entry"]
            first_inst = refs["first_inst"]
            detached_clone = refs["detached_clone"]
            other_entry = refs["other_entry"]
            other_inst = refs["other_inst"]
            call_inst = refs["call_inst"]

            assert_llvm_assertion(
                lambda: ctx.create_builder(fn),
                "instruction value",
            )
            assert_llvm_assertion(
                lambda: ctx.create_builder(detached_clone),
                "instruction in a basic block",
            )

            assert_llvm_assertion(
                lambda: fn.get_param(0).create_builder(),
                "instruction value",
            )
            assert_llvm_assertion(
                lambda: detached_clone.create_builder(),
                "instruction in a basic block",
            )

            with entry.create_builder() as b:
                assert_llvm_assertion(
                    lambda: b.position_before(fn),
                    "instruction value",
                )
                assert_llvm_assertion(
                    lambda: b.position_before(detached_clone),
                    "instruction in a basic block",
                )
                assert_llvm_assertion(
                    lambda: b.position_at(entry, fn),
                    "instruction value",
                )
                assert_llvm_assertion(
                    lambda: b.position_at(entry, other_inst),
                    "belong to the provided basic block",
                )
                assert_llvm_assertion(
                    lambda: b.insert_into_builder_with_name(fn, "x"),
                    "instruction value",
                )
                assert_llvm_assertion(
                    lambda: b.add_metadata_to_inst(fn),
                    "instruction",
                )

            assert_llvm_assertion(
                lambda: fn.append_existing_basic_block(other_entry),
                "unattached basic block",
            )

            assert_llvm_assertion(
                lambda: fn.block_address(other_entry),
                "owned by this function",
            )
            assert_llvm_assertion(
                lambda: fn.block_address(other_entry),
                "owned by this function",
            )

            high = fn.param_count
            assert_llvm_assertion(
                lambda: fn.param_attributes(-1),
                "param index >= 0",
            )
            assert_llvm_assertion(
                lambda: fn.param_attributes(high),
                "out of range",
            )

            assert_llvm_assertion(
                lambda: call_inst.callsite_param_attributes(-1),
                "param index >= 0",
            )
            assert_llvm_assertion(
                lambda: call_inst.callsite_param_attributes(call_inst.num_arg_operands),
                "out of range",
            )

            assert_llvm_assertion(
                lambda: fn.first_dbg_record,
                "instruction value",
            )
            assert_llvm_assertion(
                lambda: fn.last_dbg_record,
                "instruction value",
            )

            # Drop the detached clone before module teardown; it still references
            # function arguments from this module.
            detached_clone.delete_instruction()

            # Keep the instruction alive to prevent fixture optimization.
            assert first_inst.is_instruction


def test_non_value_guard_matrix_positive():
    with llvm.create_context() as ctx:
        with ctx.create_module("guards_ok") as mod:
            refs = build_fixture(ctx, mod)
            fn = refs["fn"]
            entry = refs["entry"]
            first_inst = refs["first_inst"]
            detached_clone = refs["detached_clone"]
            orphan_block = refs["orphan_block"]
            fn_attr = refs["fn_attr"]
            call_attr = refs["call_attr"]
            call_inst = refs["call_inst"]

            before_blocks = fn.basic_block_count
            fn.append_existing_basic_block(orphan_block)
            assert fn.basic_block_count == before_blocks + 1

            assert fn.block_address(entry).is_constant
            assert entry.block_address().is_constant

            before_attrs = len(fn.attributes)
            fn.attributes.add(fn_attr)
            after_attrs = len(fn.attributes)
            assert after_attrs >= before_attrs + 1
            _ = fn.attributes.all()
            _ = fn.attributes.get("test.attr")
            fn.attributes.remove("test.attr")

            with ctx.create_builder(first_inst) as b:
                assert b.insert_block is not None

            with first_inst.create_builder() as b:
                assert b.insert_block is not None

            with entry.create_builder() as b:
                b.position_before(first_inst)
                b.position_at(entry, first_inst)
                b.insert_into_builder_with_name(detached_clone, "cloned_add")
                b.add_metadata_to_inst(first_inst)

            assert any(inst.name == "cloned_add" for inst in entry.instructions)

            before_call_attrs = len(call_inst.callsite_attributes)
            call_inst.callsite_attributes.add(call_attr)
            after_call_attrs = len(call_inst.callsite_attributes)
            assert after_call_attrs >= before_call_attrs + 1

            assert len(call_inst.callsite_return_attributes) >= 0
            assert len(call_inst.callsite_param_attributes(0)) >= 0


if __name__ == "__main__":
    test_non_value_guard_matrix_negative()
    print("test_non_value_guard_matrix_negative: PASSED")

    test_non_value_guard_matrix_positive()
    print("test_non_value_guard_matrix_positive: PASSED")
