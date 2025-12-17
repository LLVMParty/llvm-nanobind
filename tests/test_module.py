#!/usr/bin/env -S uv run
"""
Test: test_module
Tests LLVM Module creation and properties via Python bindings.

This is the Python equivalent of tests/test_module.cpp.
Output should match the C++ golden master test.

LLVM APIs covered (via Python bindings):
- Module creation (via context manager)
- name property (get/set)
- source_filename property (get/set)
- data_layout property (get/set)
- target_triple property (get/set)
- to_string()
- clone()
- verify()
"""

import sys
import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_module") as mod:
            # Get initial module identifier
            initial_id = mod.name

            # Set new module identifier
            mod.name = "renamed_module"
            new_id = mod.name

            # Get/set source filename
            initial_src = mod.source_filename
            mod.source_filename = "test_source.c"
            new_src = mod.source_filename

            # Get/set data layout
            initial_layout = mod.data_layout
            mod.data_layout = (
                "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
            )
            new_layout = mod.data_layout

            # Get/set target triple
            initial_target = mod.target_triple
            mod.target_triple = "x86_64-unknown-linux-gnu"
            new_target = mod.target_triple

            # Clone module - returns a ModuleManager, must use 'with' or .dispose()
            with mod.clone() as cloned:
                cloned_id = cloned.name

            # Verify module
            if not mod.verify():
                print(
                    f"; Verification failed: {mod.get_verification_error()}",
                    file=sys.stderr,
                )
                return 1

            # Print diagnostic comments
            print("; Test: test_module")
            print(f"; Initial module ID: {initial_id}")
            print(f"; New module ID: {new_id}")
            print(f"; Initial source filename: {initial_src}")
            print(f"; New source filename: {new_src}")
            print(
                f"; Initial data layout: {initial_layout if initial_layout else '(empty)'}"
            )
            print(f"; New data layout: {new_layout}")
            print(
                f"; Initial target: {initial_target if initial_target else '(empty)'}"
            )
            print(f"; New target: {new_target}")
            print(f"; Cloned module ID: {cloned_id}")
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
