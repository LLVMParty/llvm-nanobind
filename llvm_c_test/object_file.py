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
    # Read object file from stdin
    try:
        membuf = llvm.create_memory_buffer_with_stdin()
    except llvm.LLVMError as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        sys.exit(1)

    # Create binary
    try:
        binary = llvm.create_binary(membuf)
    except llvm.LLVMError as e:
        print(f"Error reading object: {e}", file=sys.stderr)
        sys.exit(1)

    # Iterate sections
    sect = llvm.copy_section_iterator(binary)
    while sect and sect.is_valid and not sect.is_at_end():
        name = sect.name
        address = sect.address
        size = sect.size
        print(f"'{name}': @0x{address:08x} +{size}")
        sect.move_next()

    return 0


def object_list_symbols() -> int:
    """
    List symbols in an object file read from stdin.

    Format: name @0xADDRESS +SIZE (section_name)

    Returns:
        Exit code (0 for success, 1 for error)
    """
    # Read object file from stdin
    try:
        membuf = llvm.create_memory_buffer_with_stdin()
    except llvm.LLVMError as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        sys.exit(1)

    # Create binary
    try:
        binary = llvm.create_binary(membuf)
    except llvm.LLVMError as e:
        print(f"Error reading object: {e}", file=sys.stderr)
        sys.exit(1)

    # Get iterators
    sect = llvm.copy_section_iterator(binary)
    sym = llvm.copy_symbol_iterator(binary)

    # Iterate symbols
    while sect and sym and sect.is_valid and sym.is_valid and not sym.is_at_end():
        # Move section iterator to containing section of this symbol
        llvm.move_to_containing_section(sect, sym)

        sym_name = sym.name
        sym_address = sym.address
        sym_size = sym.size
        sect_name = sect.name

        print(f"{sym_name} @0x{sym_address:08x} +{sym_size} ({sect_name})")

        sym.move_next()

    return 0
