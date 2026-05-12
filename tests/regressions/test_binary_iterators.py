"""
Regression tests for binary iterator Python protocol semantics.

These iterators should yield the current element and advance between __next__
calls. Advancing before exposing the current element can surface invalid
end-iterator state to Python property access.
"""

from pathlib import Path

import llvm


def _manual_section_names(binary: llvm.Binary) -> list[str]:
    it = binary.sections
    names: list[str] = []
    while not it.is_at_end():
        names.append(it.name)
        it.move_next()
    return names


def _manual_symbol_names(binary: llvm.Binary) -> list[str]:
    it = binary.symbols
    names: list[str] = []
    while not it.is_at_end():
        names.append(it.name)
        it.move_next()
    return names


def test_binary_iterators_match_manual_iteration():
    obj = Path("llvm-c/llvm-c-test/inputs/simple.o")
    if not obj.exists():
        print(f"SKIP: object file not found: {obj.resolve()}")
        return

    with llvm.BinaryManager.from_file(obj) as binary:
        # Sections
        manual_sections = _manual_section_names(binary)
        for_sections = [sect.name for sect in binary.sections]
        assert for_sections == manual_sections, (
            f"for-loop sections mismatch: {for_sections} != {manual_sections}"
        )
        assert len(for_sections) > 0

        # Symbols
        manual_symbols = _manual_symbol_names(binary)
        for_symbols = [sym.name for sym in binary.symbols]
        assert for_symbols == manual_symbols, (
            f"for-loop symbols mismatch: {for_symbols} != {manual_symbols}"
        )


if __name__ == "__main__":
    test_binary_iterators_match_manual_iteration()
    print("test_binary_iterators_match_manual_iteration: PASSED")
