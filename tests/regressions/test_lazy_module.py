"""
Test for memory safety with lazy module loading.
"""

import llvm
from pathlib import Path


def print_functions(*, lazy: bool):
    print(f"[{lazy=}] Loading module...")
    with llvm.global_context().parse_bitcode_from_file(
        Path(__file__).parent / "factorial.bc", lazy=lazy
    ) as mod:
        for func in mod.functions:
            print(f" - {func.name}")


def test_non_lazy_module():
    """Test non-lazy module loading (baseline - should work)."""
    print_functions(lazy=False)


def test_lazy_module():
    """Test lazy module loading that triggers the crash."""
    print_functions(lazy=True)


if __name__ == "__main__":
    test_lazy_module()
    test_non_lazy_module()
