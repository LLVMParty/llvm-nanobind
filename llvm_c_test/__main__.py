"""
CLI entry point for llvm-c-test Python port.

Usage:
    uv run llvm-c-test --targets-list
    uv run llvm-c-test --calc < calc.test
    uv run llvm-c-test --module-dump < input.bc
"""

import sys
from .main import main

if __name__ == "__main__":
    sys.exit(main())
