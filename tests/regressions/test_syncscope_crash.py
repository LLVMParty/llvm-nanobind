#!/usr/bin/env -S uv run
"""
Regression test for syncscope handling in echo.py.

This test verifies that custom syncscopes (like "agent") can be cloned
from a source module to a destination module when both modules are in
the same context.

The key insight is that syncscope IDs are context-specific. When both
modules are in the same context, the syncscope ID from the source module
is valid in the destination module.

Previously, get_module_context() was broken and always returned the global
context instead of the module's actual context. This caused TypeCloner in
echo.py to create types in the wrong context, leading to crashes when
printing modules with custom syncscopes.

The fix was to make get_module_context() return a borrowed (non-owning)
wrapper around the module's actual context.

Affects lit tests:
- echo.ll
"""

import sys
import llvm
from pathlib import Path


def test_syncscope_clone():
    """Test that syncscopes can be cloned between modules in the same context."""
    with llvm.create_context() as ctx:
        # Load source module from file
        bitcode_path = Path(__file__).parent / "syncscope.bc"
        with open(bitcode_path, "rb") as f:
            bitcode = f.read()

        # Both modules must be open at the same time (like echo.py does)
        with ctx.parse_bitcode_from_bytes(bitcode) as src:
            print("Source module loaded", file=sys.stderr)

            # Get the syncscope ID from source
            src_func = src.get_function("test")
            assert src_func is not None, "Function 'test' not found"
            src_bb = src_func.first_basic_block
            assert src_bb is not None, "Function must have at least one basic block"
            src_inst = src_bb.first_instruction
            assert src_inst is not None, (
                "Basic block must have at least one instruction"
            )
            sync_scope_id = src_inst.atomic_sync_scope_id

            print(f"Source sync_scope_id={sync_scope_id}", file=sys.stderr)

            # Print source module (should work)
            print("Source module:", file=sys.stderr)
            print(str(src)[:200], file=sys.stderr)

            # Create destination module in same context while source is still open
            with ctx.create_module(src.name) as dst:
                # Copy module properties
                dst.source_filename = src.source_filename
                dst.target_triple = src.target_triple
                dst.data_layout = src.data_layout

                # Get the context from the destination module
                # This should return the same context as ctx, not the global context
                dst_ctx = dst.context

                # Clone the function type using the destination module's context
                # (This is what TypeCloner does in echo.py)
                ptr_ty = dst_ctx.types.ptr()
                void_ty = dst_ctx.types.void
                func_ty = dst_ctx.types.function(void_ty, [ptr_ty], False)

                # Add the function to the destination module
                dst_func = dst.add_function("test", func_ty)

                # Clone the basic block and instruction
                with ctx.create_builder() as builder:
                    bb = dst_func.append_basic_block("")
                    builder.position_at_end(bb)

                    # Get destination parameter
                    dst_param = dst_func.first_param()
                    assert dst_param is not None, (
                        "Function must have at least one parameter"
                    )

                    # Clone the atomic instruction using the DESTINATION parameter
                    # with the syncscope ID from the SOURCE instruction
                    i8 = dst_ctx.types.i8
                    val = i8.constant(0)

                    print(
                        f"Creating atomic with sync_scope_id={sync_scope_id}",
                        file=sys.stderr,
                    )

                    atomic = builder.atomic_rmw_sync_scope(
                        llvm.AtomicRMWBinOp.Xchg,
                        dst_param,
                        val,
                        llvm.AtomicOrdering.AcquireRelease,
                        sync_scope_id,
                    )
                    atomic.set_volatile(True)
                    atomic.alignment = 8

                    print("Atomic created", file=sys.stderr)

                    builder.ret_void()

                print("Trying to print destination module...", file=sys.stderr)
                output = str(dst)

                print("SUCCESS!", file=sys.stderr)
                print(output)

                # Verify the output contains the correct syncscope
                assert 'syncscope("agent")' in output, (
                    f"Expected syncscope('agent') in output, got:\n{output}"
                )

    print("Test passed!", file=sys.stderr)
    return True


if __name__ == "__main__":
    success = test_syncscope_clone()
    sys.exit(0 if success else 1)
