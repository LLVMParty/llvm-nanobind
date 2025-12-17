# type: ignore  # config object is injected by lit at runtime
# -*- Python -*-
"""
lit configuration for llvm-c-test integration tests.
"""

import os
import lit.formats
import lit.util

config.name = "llvm-c-test"
config.test_format = lit.formats.ShTest(execute_external=True)

# Suffixes for test files
config.suffixes = [".ll", ".test"]

# Source root is where the tests live
config.test_source_root = os.path.dirname(__file__)

# Exec root is where Output directories are created - use build directory
# to keep source tree clean. Falls back to source root if not set.
config.test_exec_root = os.environ.get("LIT_EXEC_ROOT", os.path.dirname(__file__))

# Get paths from environment (set by run_llvm_c_tests.py)
llvm_tools_dir = os.environ.get("LLVM_TOOLS_DIR", "")
llvm_c_test_exe = os.environ.get("LLVM_C_TEST_EXE", "")

# Tool substitutions
config.substitutions.append(("%llvm-c-test", llvm_c_test_exe))
config.substitutions.append(("llvm-c-test", llvm_c_test_exe))
config.substitutions.append(("llvm-as", os.path.join(llvm_tools_dir, "llvm-as")))
config.substitutions.append(("llvm-dis", os.path.join(llvm_tools_dir, "llvm-dis")))
config.substitutions.append(("FileCheck", os.path.join(llvm_tools_dir, "FileCheck")))
config.substitutions.append(("not", os.path.join(llvm_tools_dir, "not")))

# Available targets (assume all targets available)
config.targets = [
    "AArch64",
    "AMDGPU",
    "ARM",
    "AVR",
    "BPF",
    "Hexagon",
    "Lanai",
    "LoongArch",
    "Mips",
    "MSP430",
    "NVPTX",
    "PowerPC",
    "RISCV",
    "Sparc",
    "SystemZ",
    "VE",
    "WebAssembly",
    "X86",
    "XCore",
]

# Make targets available to lit.local.cfg files
config.root.targets = config.targets
