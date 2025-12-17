"""
Helper utilities for llvm-c-test Python port.
"""

import sys


def tokenize_stdin(callback):
    """
    Read lines from stdin, tokenize them, and call callback for each line.

    Skips lines starting with ';' (comments) or empty lines.
    Tokenizes on whitespace and calls callback(tokens) for each line.

    Args:
        callback: Function to call with list of tokens for each line
    """
    for line in sys.stdin:
        # Skip comments and empty lines
        line = line.strip()
        if not line or line.startswith(";"):
            continue

        # Tokenize on whitespace
        tokens = line.split()
        if tokens:
            callback(tokens)
