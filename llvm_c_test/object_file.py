"""
Object file command implementations for llvm-c-test Python port.

Implements:
  --object-list-sections: List sections in object file from stdin
  --object-list-symbols: List symbols in object file from stdin
"""

import sys
import llvm


def object_list_sections() -> int:
    """
    List sections in an object file read from stdin.

    Format: 'name': @0xADDRESS +SIZE

    Returns:
        Exit code (0 for success, 1 for error)
    """
    # Read object file from stdin as bytes
    data = sys.stdin.buffer.read()

    try:
        with llvm.BinaryManager.from_bytes(data) as binary:
            # Iterate sections using Pythonic for loop
            for sect in binary.sections:
                name = sect.name
                address = sect.address
                size = sect.size
                print(f"'{name}': @0x{address:08x} +{size}")
    except llvm.LLVMError as e:
        print(f"Error reading object: {e}", file=sys.stderr)
        sys.exit(1)

    return 0


def object_list_symbols() -> int:
    """
    List symbols in an object file read from stdin.

    Format: name @0xADDRESS +SIZE (section_name)

    Returns:
        Exit code (0 for success, 1 for error)
    """
    # Read object file from stdin as bytes
    data = sys.stdin.buffer.read()

    try:
        with llvm.BinaryManager.from_bytes(data) as binary:
            # Get a section iterator to track containing sections
            sect = binary.sections

            # Iterate symbols manually (not with for loop since we need
            # to use the iterator reference with move_to_containing_section)
            sym = binary.symbols
            while not sym.is_at_end():
                # Move section iterator to the section containing this symbol
                sect.move_to_containing_section(sym)

                sym_name = sym.name
                sym_address = sym.address
                # Some symbols have no containing section; match C tool output.
                if sect.is_at_end():
                    sect_name = "(null)"
                    # Avoid LLVMGetSymbolSize on symbols without a containing
                    # section (e.g. file symbols), which can assert in debug builds.
                    sym_size = 0
                else:
                    sect_name = sect.name
                    sym_size = sym.size

                print(f"{sym_name} @0x{sym_address:08x} +{sym_size} ({sect_name})")

                sym.move_next()
    except llvm.LLVMError as e:
        print(f"Error reading object: {e}", file=sys.stderr)
        sys.exit(1)

    return 0
