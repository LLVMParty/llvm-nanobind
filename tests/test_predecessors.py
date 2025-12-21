#!/usr/bin/env -S uv run
"""
Test: test_predecessors
Tests LLVM BasicBlock predecessors and successors via Python bindings.

This is the Python equivalent of tests/test_predecessors.cpp.
Output should match the C++ golden master test.

LLVM APIs covered (via Python bindings):
- BasicBlock.successors property
- BasicBlock.predecessors property
- Value.uses property (list of Use objects)
- Value.users property (list of user Values)
- Use.user property
- Value.is_terminator_inst property
- Value.instruction_parent property
"""

import sys
import llvm


def get_predecessors_via_uses(bb: llvm.BasicBlock) -> list[llvm.BasicBlock]:
    """
    Get predecessors using pythonic iteration over uses.
    Uses the .uses property to iterate over Use objects.
    """
    result = []
    block_value = bb.as_value()

    for use in block_value.uses:
        user = use.user
        if user.is_terminator_inst:
            pred = user.instruction_parent
            result.append(pred)

    return result


def get_predecessors_via_users(bb: llvm.BasicBlock) -> list[llvm.BasicBlock]:
    """
    Get predecessors using pythonic iteration over users directly.
    This is the most concise style using the .users property.
    """
    result = []
    block_value = bb.as_value()

    for user in block_value.users:
        if user.is_terminator_inst:
            pred = user.instruction_parent
            result.append(pred)

    return result


def verify_predecessors(bb: llvm.BasicBlock, name: str) -> None:
    """Verify that all three methods of getting predecessors produce the same result."""
    preds_property = [b.name for b in bb.predecessors]
    preds_via_uses = [b.name for b in get_predecessors_via_uses(bb)]
    preds_via_users = [b.name for b in get_predecessors_via_users(bb)]

    assert preds_property == preds_via_uses, (
        f"{name}: predecessors property should match .uses iteration"
    )
    assert preds_property == preds_via_users, (
        f"{name}: predecessors property should match .users iteration"
    )


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_predecessors") as mod:
            i1 = ctx.types.i1
            i32 = ctx.types.i32
            void_ty = ctx.types.void

            # ==========================================
            # Function: diamond pattern (tests multiple predecessors)
            # void diamond(i1 cond)
            # entry -> (if_true | if_false) -> merge
            # ==========================================
            diamond_ty = ctx.types.function(void_ty, [i1], vararg=False)
            diamond_func = mod.add_function("diamond", diamond_ty)

            cond = diamond_func.get_param(0)
            cond.name = "cond"

            entry = diamond_func.append_basic_block("entry", ctx)
            if_true = diamond_func.append_basic_block("if_true", ctx)
            if_false = diamond_func.append_basic_block("if_false", ctx)
            merge = diamond_func.append_basic_block("merge", ctx)

            with ctx.create_builder() as builder:
                # Entry: conditional branch to if_true or if_false
                builder.position_at_end(entry)
                builder.cond_br(cond, if_true, if_false)

                # True branch: branch to merge
                builder.position_at_end(if_true)
                builder.br(merge)

                # False branch: branch to merge
                builder.position_at_end(if_false)
                builder.br(merge)

                # Merge: return
                builder.position_at_end(merge)
                builder.ret_void()

            # ==========================================
            # Function: loop pattern (tests self-referential predecessor)
            # void loop(i32 n)
            # entry -> loop -> (loop | exit)
            # ==========================================
            loop_ty = ctx.types.function(void_ty, [i32], vararg=False)
            loop_func = mod.add_function("loop", loop_ty)

            n = loop_func.get_param(0)
            n.name = "n"

            loop_entry = loop_func.append_basic_block("entry", ctx)
            loop_body = loop_func.append_basic_block("loop", ctx)
            loop_exit = loop_func.append_basic_block("exit", ctx)

            with ctx.create_builder() as builder:
                # Entry: branch to loop
                builder.position_at_end(loop_entry)
                builder.br(loop_body)

                # Loop: PHI, compare, conditional branch back to loop or to exit
                builder.position_at_end(loop_body)
                i_phi = builder.phi(i32, "i")
                new_i = builder.add(i_phi, i32.constant(1), "new_i")
                loop_cond = builder.icmp(llvm.IntPredicate.SLT, new_i, n, "loop_cond")
                builder.cond_br(loop_cond, loop_body, loop_exit)

                # Add incoming values to PHI
                i_phi.add_incoming(i32.constant(0), loop_entry)
                i_phi.add_incoming(new_i, loop_body)

                # Exit: return
                builder.position_at_end(loop_exit)
                builder.ret_void()

            # Verify module
            if not mod.verify():
                print(
                    f"; Verification failed: {mod.get_verification_error()}",
                    file=sys.stderr,
                )
                return 1

            # Print diagnostic comments
            print("; Test: test_predecessors")
            print(";")
            print("; Diamond pattern:")

            # Helper to format block list
            def format_blocks(blocks):
                return "[" + ", ".join(b.name for b in blocks) + "]"

            # Entry block
            print(";   entry:")
            print(f";     successors: {format_blocks(entry.successors)}")
            print(f";     predecessors: {format_blocks(entry.predecessors)}")
            verify_predecessors(entry, "entry")

            # if_true block
            print(";   if_true:")
            print(f";     successors: {format_blocks(if_true.successors)}")
            print(f";     predecessors: {format_blocks(if_true.predecessors)}")
            verify_predecessors(if_true, "if_true")

            # if_false block
            print(";   if_false:")
            print(f";     successors: {format_blocks(if_false.successors)}")
            print(f";     predecessors: {format_blocks(if_false.predecessors)}")
            verify_predecessors(if_false, "if_false")

            # merge block
            print(";   merge:")
            print(f";     successors: {format_blocks(merge.successors)}")
            print(f";     predecessors: {format_blocks(merge.predecessors)}")
            verify_predecessors(merge, "merge")

            print(";")
            print("; Loop pattern:")

            # loop_entry block
            print(";   entry:")
            print(f";     successors: {format_blocks(loop_entry.successors)}")
            print(f";     predecessors: {format_blocks(loop_entry.predecessors)}")
            verify_predecessors(loop_entry, "loop_entry")

            # loop_body block (has self-reference)
            print(";   loop:")
            print(f";     successors: {format_blocks(loop_body.successors)}")
            print(f";     predecessors: {format_blocks(loop_body.predecessors)}")
            verify_predecessors(loop_body, "loop_body")

            # loop_exit block
            print(";   exit:")
            print(f";     successors: {format_blocks(loop_exit.successors)}")
            print(f";     predecessors: {format_blocks(loop_exit.predecessors)}")
            verify_predecessors(loop_exit, "loop_exit")

            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
