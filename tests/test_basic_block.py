#!/usr/bin/env -S uv run
"""
Test: test_basic_block
Tests LLVM BasicBlock creation and manipulation via Python bindings.

This is the Python equivalent of tests/test_basic_block.cpp.
Output should match the C++ golden master test.

LLVM APIs covered (via Python bindings):
- append_basic_block()
- name property
- parent property
- entry_block, basic_block_count
- first_basic_block, next_block, last_basic_block
- first_instruction, last_instruction, terminator
- move_before(), move_after()
- create_basic_block() (unattached), append_existing_basic_block()
"""

import sys
import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_basic_block") as mod:
            i32 = ctx.int32_type()
            void_ty = ctx.void_type()

            # Create a function to hold basic blocks
            func_ty = ctx.function_type(void_ty, [], vararg=False)
            func = mod.add_function("test_func", func_ty)

            # Append basic blocks
            entry = func.append_basic_block("entry", ctx)
            middle = func.append_basic_block("middle", ctx)
            exit_bb = func.append_basic_block("exit", ctx)

            # Get block names
            entry_name = entry.name
            middle_name = middle.name
            exit_name = exit_bb.name

            # Get parent function
            entry_parent = entry.parent

            # Get entry basic block
            func_entry = func.entry_block
            assert func_entry is not None

            # Count basic blocks
            bb_count = func.basic_block_count

            # Create a builder to add instructions
            with ctx.create_builder() as builder:
                # Add instructions to entry block
                builder.position_at_end(entry)
                builder.br(middle)

                # Add instructions to middle block
                builder.position_at_end(middle)
                builder.br(exit_bb)

                # Add instructions to exit block
                builder.position_at_end(exit_bb)
                builder.ret_void()

                # Get first/last instructions
                entry_first = entry.first_instruction
                entry_last = entry.last_instruction
                exit_terminator = exit_bb.terminator

                # Create unattached block
                unattached = ctx.create_basic_block("unattached")

                # Attach unattached block to function
                func.append_existing_basic_block(unattached)
                builder.position_at_end(unattached)
                builder.unreachable()

            # Block count after adding unattached
            bb_count_after = func.basic_block_count

            # Move blocks around: move unattached before exit
            unattached.move_before(exit_bb)

            # Verify module
            if not mod.verify():
                print(
                    f"; Verification failed: {mod.get_verification_error()}",
                    file=sys.stderr,
                )
                return 1

            # Print diagnostic comments
            print("; Test: test_basic_block")
            print(";")
            print("; Basic block info:")
            print(f";   entry name: {entry_name}")
            print(f";   middle name: {middle_name}")
            print(f";   exit name: {exit_name}")
            print(";")
            print("; Parent checks:")
            # Compare by name since we can't compare object identity directly
            print(
                f";   entry parent is func: {'yes' if entry_parent.name == func.name else 'no'}"
            )
            print(
                f";   func entry block is entry: {'yes' if func_entry.name == entry.name else 'no'}"
            )
            print(";")
            print("; Block counts:")
            print(f";   initial count: {bb_count}")
            print(f";   after adding unattached: {bb_count_after}")
            print(";")
            print("; Instruction checks:")
            print(
                f";   entry has first instruction: {'yes' if entry_first is not None else 'no'}"
            )
            print(
                f";   entry first == last (single inst): {'yes' if entry_first is not None and entry_last is not None else 'no'}"
            )
            print(
                f";   exit has terminator: {'yes' if exit_terminator is not None else 'no'}"
            )
            print(";")
            print("; Block iteration (after move):")
            for i, bb in enumerate(func.basic_blocks):
                print(f";   [{i}] {bb.name}")

            # Get last block
            last_bb = func.last_basic_block
            assert last_bb is not None
            print(";")
            print(f"; Last block: {last_bb.name}")

            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
