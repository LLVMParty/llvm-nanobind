"""
Regression tests for end-iterator guards in binary iterators.

Accessing properties on end iterators used to hard-abort in LLVM APIs.
Bindings should raise LLVMAssertionError instead.
"""

import gc
from pathlib import Path

import llvm


OBJECT_PATH = Path("llvm-c/llvm-c-test/inputs/simple.o")


def _assert_end_guard(fn, label: str) -> None:
    try:
        fn()
        raise AssertionError(f"Expected LLVMAssertionError for {label}")
    except llvm.LLVMAssertionError as e:
        assert "not at end" in str(e)


def test_section_symbol_and_relocation_end_guards() -> None:
    if not OBJECT_PATH.exists():
        print(f"SKIP: object file not found: {OBJECT_PATH.resolve()}")
        return

    with llvm.BinaryManager.from_file(OBJECT_PATH) as binary:
        sections = binary.sections
        while not sections.is_at_end():
            sections.move_next()

        _assert_end_guard(lambda: sections.name, "SectionIterator.name")
        _assert_end_guard(lambda: sections.address, "SectionIterator.address")
        _assert_end_guard(lambda: sections.size, "SectionIterator.size")
        _assert_end_guard(lambda: sections.contents, "SectionIterator.contents")

        symbols = binary.symbols
        while not symbols.is_at_end():
            symbols.move_next()

        _assert_end_guard(lambda: symbols.name, "SymbolIterator.name")
        _assert_end_guard(lambda: symbols.address, "SymbolIterator.address")
        _assert_end_guard(lambda: symbols.size, "SymbolIterator.size")

        section_for_reloc = binary.sections
        if not section_for_reloc.is_at_end():
            reloc = section_for_reloc.relocations
            while not reloc.is_at_end():
                reloc.move_next()
            _assert_end_guard(lambda: reloc.offset, "RelocationIterator.offset")
            _assert_end_guard(lambda: reloc.type, "RelocationIterator.type")
            _assert_end_guard(
                lambda: reloc.type_name,
                "RelocationIterator.type_name",
            )
            _assert_end_guard(
                lambda: reloc.value_string,
                "RelocationIterator.value_string",
            )


def test_relocation_iterator_keeps_temporary_section_alive() -> None:
    if not OBJECT_PATH.exists():
        print(f"SKIP: object file not found: {OBJECT_PATH.resolve()}")
        return

    with llvm.BinaryManager.from_file(OBJECT_PATH) as binary:
        probe = binary.sections
        if probe.is_at_end():
            print("SKIP: object file has no sections")
            return

        reloc = binary.sections.relocations
        gc.collect()

        assert isinstance(reloc.is_at_end(), bool)


def test_contains_symbol_and_move_to_containing_section_require_non_end_symbol() -> None:
    if not OBJECT_PATH.exists():
        print(f"SKIP: object file not found: {OBJECT_PATH.resolve()}")
        return

    with llvm.BinaryManager.from_file(OBJECT_PATH) as binary:
        sections = binary.sections
        symbols = binary.symbols

        while not symbols.is_at_end():
            symbols.move_next()

        _assert_end_guard(
            lambda: sections.contains_symbol(symbols),
            "SectionIterator.contains_symbol",
        )
        _assert_end_guard(
            lambda: sections.move_to_containing_section(symbols),
            "SectionIterator.move_to_containing_section",
        )


if __name__ == "__main__":
    test_section_symbol_and_relocation_end_guards()
    test_relocation_iterator_keeps_temporary_section_alive()
    test_contains_symbol_and_move_to_containing_section_require_non_end_symbol()
    print("test_binary_iterator_end_guards: PASSED")
