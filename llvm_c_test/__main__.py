"""
CLI entry point for llvm-c-test Python port.

Usage:
    python -m llvm_c_test --targets-list
    python -m llvm_c_test --calc < calc.test
    python -m llvm_c_test --module-dump < input.bc
"""

import sys
from .main import main

if __name__ == "__main__":
    sys.exit(main())
