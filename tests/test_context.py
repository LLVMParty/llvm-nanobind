#!/usr/bin/env -S uv run
"""
Test: test_context
Tests LLVM Context creation and properties via Python bindings.

This is the Python equivalent of tests/test_context.cpp.
Output should match the C++ golden master test.

LLVM APIs covered (via Python bindings):
- Context creation/destruction (via context manager)
- global_context()
- discard_value_names property
"""

import sys
import llvm


def main():
    # Create a new context using context manager
    with llvm.create_context() as ctx:
        # Get global context for comparison
        global_ctx = llvm.global_context()

        # Test discard value names property
        discard_before = ctx.discard_value_names
        ctx.discard_value_names = True
        discard_after = ctx.discard_value_names

        # Reset it back for the module we'll create
        ctx.discard_value_names = False

        # Create a minimal module using context manager
        with ctx.create_module("test_context") as mod:
            # Verify module
            if not mod.verify():
                print(
                    f"; Verification failed: {mod.get_verification_error()}",
                    file=sys.stderr,
                )
                return 1

            # Print diagnostic comments
            print("; Test: test_context")
            print("; Context created: yes")
            print(
                f"; Global context same as created: {'yes' if ctx is global_ctx else 'no'}"
            )
            print(
                f"; Discard value names (before): {'true' if discard_before else 'false'}"
            )
            print(
                f"; Discard value names (after set to true): {'true' if discard_after else 'false'}"
            )
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
