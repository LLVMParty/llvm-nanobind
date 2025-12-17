"""
Disassemble command implementation for llvm-c-test Python port.

Implements --disassemble command which reads lines from stdin containing:
  triple  features  hex_bytes...

And prints disassembled instructions.
"""

import sys
import llvm
from .helpers import tokenize_stdin


def pprint(pos: int, buf: bytes, length: int, disasm: str) -> None:
    """
    Pretty print a disassembled instruction.

    Format: XXXX:  HH HH HH HH HH HH HH HH    disasm
    """
    # Print position
    print(f"{pos:04x}:  ", end="")

    # Print up to 8 hex bytes
    for i in range(8):
        if i < length:
            print(f"{buf[i]:02x} ", end="")
        else:
            print("   ", end="")

    # Print disassembly
    print(f"   {disasm}")


def do_disassemble(triple: str, features: str, buf: bytes) -> None:
    """
    Disassemble a byte buffer and print results.

    Args:
        triple: Target triple string
        features: Feature string (empty string if "NULL" was specified)
        buf: Bytes to disassemble
    """
    # Create disassembler
    disasm = llvm.create_disasm_cpu_features(triple, "", features)

    if not disasm.is_valid:
        print(f"ERROR: Couldn't create disassembler for triple {triple}")
        return

    # Convert bytes to list for the API
    byte_list = list(buf)

    pos = 0
    while pos < len(buf):
        consumed, outline = disasm.disasm_instruction(byte_list, pos, 0)

        if consumed == 0:
            # Failed to disassemble - print as unknown byte
            pprint(pos, buf[pos : pos + 1], 1, "\t???")
            pos += 1
        else:
            pprint(pos, buf[pos : pos + consumed], consumed, outline)
            pos += consumed


def handle_line(tokens: list[str]) -> None:
    """
    Handle a single input line.

    Args:
        tokens: List of tokens from the line
                [0] = triple
                [1] = features (or "NULL")
                [2:] = hex bytes
    """
    if len(tokens) < 2:
        return

    triple = tokens[0]
    features = tokens[1]

    print(f"triple: {triple}, features: {features}")

    # Convert "NULL" to empty string
    if features == "NULL":
        features = ""

    # Parse hex bytes
    disbuf = bytearray()
    for i in range(2, len(tokens)):
        if len(disbuf) >= 128:
            print("Warning: Too long line, truncating", file=sys.stderr)
            break
        disbuf.append(int(tokens[i], 16))

    do_disassemble(triple, features, bytes(disbuf))


def disassemble() -> int:
    """
    Main entry point for --disassemble command.

    Reads lines from stdin, parses them as triple/features/hex bytes,
    and prints disassembly of the machine code.

    Returns:
        Exit code (0 for success)
    """
    # Initialize all targets
    llvm.initialize_all_target_infos()
    llvm.initialize_all_target_mcs()
    llvm.initialize_all_disassemblers()

    # Process stdin
    tokenize_stdin(handle_line)

    return 0
