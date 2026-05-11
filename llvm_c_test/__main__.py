"""
CLI entry point for llvm-c-test Python port.

Usage:
    uv run python -m llvm_c_test --targets-list
    uv run python -m llvm_c_test --calc < calc.test
    uv run python -m llvm_c_test --module-dump < input.bc
"""

import sys
from .main import main

if __name__ == "__main__":
    sys.exit(main())
