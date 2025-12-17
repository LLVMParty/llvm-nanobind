"""
Implementation of --targets-list command.

Lists all registered LLVM targets.
"""

import sys
import llvm


def targets_list():
    """List all registered LLVM targets."""
    # Initialize all target info and targets
    llvm.initialize_all_target_infos()
    llvm.initialize_all_targets()

    # Iterate through targets
    target = llvm.get_first_target()
    while target is not None:
        # Print target name
        print(target.name, end="")

        # Add (+jit) suffix if target has JIT support
        if target.has_jit:
            print(" (+jit)", end="")

        print()

        # Print target description with " - " prefix
        print(f" - {target.description}")

        # Move to next target
        target = target.next

    return 0
