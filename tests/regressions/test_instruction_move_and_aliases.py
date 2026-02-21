"""
Regression tests for instruction ergonomics:
- Value.is_terminator (renamed from is_terminator_inst)
- Value.parent alias for instruction parent block
- Instruction movement helpers: move_before/move_after
"""

import llvm


def test_instruction_aliases_and_movement():
    with llvm.create_context() as ctx:
        with ctx.create_module("inst_move_alias") as mod:
            i32 = ctx.types.i32
            void_ty = ctx.types.void
            fn_ty = ctx.types.function(void_ty, [i32, i32, i32], vararg=False)
            fn = mod.add_function("f", fn_ty)

            entry = fn.append_basic_block("entry")
            exit_bb = fn.append_basic_block("exit")

            with entry.create_builder() as builder:
                p0 = fn.get_param(0)
                p1 = fn.get_param(1)
                p2 = fn.get_param(2)
                a = builder.add(p0, p1, "a")
                b = builder.add(p1, p2, "b")
                builder.br(exit_bb)

                builder.position_at_end(exit_bb)
                x = builder.add(p0, p2, "x")
                builder.ret_void()

            # Renamed property should exist.
            term = entry.terminator
            assert term.is_terminator

            # Old name should be gone (project policy: no backward compatibility).
            try:
                _ = term.is_terminator_inst  # type: ignore[attr-defined]
                assert False, "Expected AttributeError for removed property"
            except AttributeError:
                pass

            # parent is an alias for instruction parent block.
            assert term.parent == entry

            # Reorder within the same block.
            entry_instrs = list(entry.instructions)
            assert entry_instrs[0] == a
            assert entry_instrs[1] == b
            assert entry_instrs[2].opcode_name == "br"

            b.move_before(a, preserve=True)
            entry_instrs = list(entry.instructions)
            assert entry_instrs[0] == b
            assert entry_instrs[1] == a
            assert entry_instrs[2].opcode_name == "br"

            b.move_after(a, preserve=False)
            entry_instrs = list(entry.instructions)
            assert entry_instrs[0] == a
            assert entry_instrs[1] == b
            assert entry_instrs[2].opcode_name == "br"

            # Move across blocks.
            b.move_before(x)
            entry_instrs = list(entry.instructions)
            exit_instrs = list(exit_bb.instructions)
            assert len(entry_instrs) == 2
            assert entry_instrs[0] == a
            assert entry_instrs[1].opcode_name == "br"
            assert exit_instrs[0] == b
            assert exit_instrs[1] == x
            assert exit_instrs[2].opcode_name == "ret"
            assert b.parent == exit_bb

            b.move_after(x)
            exit_instrs = list(exit_bb.instructions)
            assert exit_instrs[0] == x
            assert exit_instrs[1] == b
            assert exit_instrs[2].opcode_name == "ret"
            assert b.parent == exit_bb

            # Cannot move after terminators.
            try:
                a.move_after(entry.terminator)
                assert False, "Expected an exception when moving after terminator"
            except Exception as e:
                assert "terminator" in str(e).lower(), f"Unexpected error: {e}"

            assert mod.verify(), mod.get_verification_error()


def test_instruction_move_rejects_cross_context():
    with llvm.create_context() as ctx1:
        with ctx1.create_module("m1") as m1:
            i32_1 = ctx1.types.i32
            fn_ty_1 = ctx1.types.function(ctx1.types.void, [i32_1], vararg=False)
            f1 = m1.add_function("f1", fn_ty_1)
            bb1 = f1.append_basic_block("bb1")
            with bb1.create_builder() as b1:
                p = f1.get_param(0)
                inst1 = b1.add(p, i32_1.constant(1), "inst1")
                b1.ret_void()

            with llvm.create_context() as ctx2:
                with ctx2.create_module("m2") as m2:
                    i32_2 = ctx2.types.i32
                    fn_ty_2 = ctx2.types.function(ctx2.types.void, [i32_2], vararg=False)
                    f2 = m2.add_function("f2", fn_ty_2)
                    bb2 = f2.append_basic_block("bb2")
                    with bb2.create_builder() as b2:
                        q = f2.get_param(0)
                        inst2 = b2.add(q, i32_2.constant(1), "inst2")
                        b2.ret_void()

                    try:
                        inst1.move_before(inst2)
                        assert False, "Expected move_before to reject cross-context move"
                    except Exception as e:
                        assert "different contexts" in str(e), f"Unexpected error: {e}"

                    try:
                        inst1.move_after(inst2)
                        assert False, "Expected move_after to reject cross-context move"
                    except Exception as e:
                        assert "different contexts" in str(e), f"Unexpected error: {e}"


def test_instruction_move_rejects_invalid_placement():
    with llvm.create_context() as ctx:
        with ctx.create_module("m") as m:
            i1 = ctx.types.i1
            i32 = ctx.types.i32
            fn_ty = ctx.types.function(i32, [i1, i32, i32], vararg=False)
            fn = m.add_function("f", fn_ty)

            entry = fn.append_basic_block("entry")
            left = fn.append_basic_block("left")
            right = fn.append_basic_block("right")
            merge = fn.append_basic_block("merge")

            with entry.create_builder() as b:
                cond = fn.get_param(0)
                b.cond_br(cond, left, right)

                b.position_at_end(left)
                left_add = b.add(fn.get_param(1), fn.get_param(2), "left_add")
                b.br(merge)

                b.position_at_end(right)
                b.br(merge)

                b.position_at_end(merge)
                phi = b.phi(i32, "p")
                phi.add_incoming(fn.get_param(1), left)
                phi.add_incoming(fn.get_param(2), right)
                merge_add = b.add(phi, i32.constant(1), "merge_add")
                b.ret(merge_add)

            # Non-PHI before PHI is invalid.
            try:
                left_add.move_before(phi)
                assert False, "Expected rejection for non-PHI insertion before PHIs"
            except Exception as e:
                assert "non-PHI" in str(e) or "PHI" in str(e), f"Unexpected error: {e}"

            # PHI after non-PHI is invalid.
            try:
                phi.move_after(merge_add)
                assert False, "Expected rejection for PHI insertion outside PHI prefix"
            except Exception as e:
                assert "PHI" in str(e), f"Unexpected error: {e}"

            # Terminator cannot be moved before a non-end insertion point.
            try:
                term = merge.terminator
                assert term is not None
                term.move_before(merge_add)
                assert False, "Expected rejection for terminator move_before"
            except Exception as e:
                assert "Terminator" in str(e) or "terminator" in str(e), (
                    f"Unexpected error: {e}"
                )

            assert m.verify(), m.get_verification_error()


def test_landingpad_move_rejected_and_ir_unchanged():
    """LandingPad must remain first non-PHI; invalid move must not mutate IR."""
    with llvm.create_context() as ctx:
        with ctx.create_module("landingpad_move") as m:
            void_ty = ctx.types.void
            i32_ty = ctx.types.i32

            # Personality function required for EH function.
            personality_ty = ctx.types.function(i32_ty, [], True)
            personality_fn = m.add_function("__personality", personality_ty)

            fn_ty = ctx.types.function(void_ty, [])
            fn = m.add_function("f", fn_ty)
            fn.set_personality_fn(personality_fn)

            entry = fn.append_basic_block("entry")
            normal = fn.append_basic_block("normal")
            unwind = fn.append_basic_block("unwind")

            callee_ty = ctx.types.function(void_ty, [])
            callee = m.add_function("may_throw", callee_ty)

            with entry.create_builder() as b:
                b.invoke_with_operand_bundles(callee_ty, callee, [], normal, unwind, [], "")

                b.position_at_end(normal)
                b.ret_void()

                b.position_at_end(unwind)
                lp_ty = ctx.types.struct([ctx.types.ptr, i32_ty])
                landing = b.landing_pad(lp_ty, 0, "lp")
                landing.set_cleanup(True)
                payload = b.extract_value(landing, 0, "payload")
                b.ret_void()

            before = m.to_string()
            unwind_before = [inst.opcode_name for inst in unwind.instructions]
            assert unwind_before[0] == "landingpad", unwind_before
            assert unwind_before[1] == "extractvalue", unwind_before
            assert payload.parent == unwind

            # Try to move landingpad after extractvalue: invalid by placement rule.
            try:
                landing.move_after(payload)
                assert False, "Expected rejection when moving landingpad off first non-PHI slot"
            except Exception as e:
                assert "LandingPad" in str(e) or "landing" in str(e).lower(), (
                    f"Unexpected error: {e}"
                )

            # Must remain unchanged after failed operation.
            after = m.to_string()
            unwind_after = [inst.opcode_name for inst in unwind.instructions]
            assert unwind_after == unwind_before, (
                f"Block mutated on failed move: {unwind_before} -> {unwind_after}"
            )
            assert after == before, "Module IR changed after rejected move"
            assert m.verify(), m.get_verification_error()


if __name__ == "__main__":
    test_instruction_aliases_and_movement()
    print("test_instruction_aliases_and_movement: PASSED")

    test_instruction_move_rejects_cross_context()
    print("test_instruction_move_rejects_cross_context: PASSED")

    test_instruction_move_rejects_invalid_placement()
    print("test_instruction_move_rejects_invalid_placement: PASSED")

    test_landingpad_move_rejected_and_ir_unchanged()
    print("test_landingpad_move_rejected_and_ir_unchanged: PASSED")
