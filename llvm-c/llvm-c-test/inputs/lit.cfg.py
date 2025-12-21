# type: ignore  # config object is injected by lit at runtime
# -*- Python -*-
"""
lit configuration for llvm-c-test integration tests.
"""

import os
import sys
import lit.formats
import lit.util

config.name = "llvm-c-test"
# On Windows, we need to use external shell execution despite path issues
# because lit's internal shell has problems loading Python extension modules.
# We work around the path issues by ensuring all paths use forward slashes.
config.test_format = lit.formats.ShTest(execute_external=True)

# Suffixes for test files
config.suffixes = [".ll", ".test"]

# Source root is where the tests live
config.test_source_root = os.path.dirname(__file__)

# Exec root is where Output directories are created - use build directory
# to keep source tree clean. Falls back to source root if not set.
config.test_exec_root = os.environ.get("LIT_EXEC_ROOT", os.path.dirname(__file__))

# Get paths from environment (set by run_llvm_c_tests.py)
llvm_tools_dir = os.environ.get("LLVM_TOOLS_DIR")
llvm_c_test_cmd = os.environ.get("LLVM_C_TEST_CMD")

if not llvm_tools_dir:
    lit_config.fatal("LLVM_TOOLS_DIR environment variable not set")
if not llvm_c_test_cmd:
    lit_config.fatal("LLVM_C_TEST_CMD environment variable not set")

# Tool substitutions
# On Windows, wrap individual tool paths with quotes to handle backslashes in bash
# The llvm_c_test_cmd already includes quotes around the python.exe path if needed
def make_shell_safe(path):
    """Make a path safe for use in shell commands on Windows."""
    if sys.platform == "win32":
        # Use double quotes around paths to preserve backslashes
        return f'"{path}"'
    return path

config.substitutions.append(("%llvm-c-test", llvm_c_test_cmd))
config.substitutions.append(("llvm-c-test", llvm_c_test_cmd))
config.substitutions.append(("llvm-as", make_shell_safe(os.path.join(llvm_tools_dir, "llvm-as"))))
config.substitutions.append(("llvm-dis", make_shell_safe(os.path.join(llvm_tools_dir, "llvm-dis"))))
config.substitutions.append(("FileCheck", make_shell_safe(os.path.join(llvm_tools_dir, "FileCheck"))))
config.substitutions.append(("not", make_shell_safe(os.path.join(llvm_tools_dir, "not"))))

# Get available targets from llvm-config
import subprocess

def get_llvm_targets():
    """Get list of targets built into LLVM."""
    try:
        llvm_config = os.path.join(llvm_tools_dir, "llvm-config")
        if sys.platform == "win32":
            llvm_config += ".exe"
        
        result = subprocess.run(
            [llvm_config, "--targets-built"],
            capture_output=True,
            text=True,
            check=True,
        )
        # Output is space-separated list like "X86 NVPTX AMDGPU"
        return result.stdout.strip().split()
    except (subprocess.CalledProcessError, FileNotFoundError):
        # If llvm-config doesn't exist or fails, assume all targets
        lit_config.warning(f"Could not determine available targets from llvm-config, assuming all targets available")
        return [
            "AArch64", "AMDGPU", "ARM", "AVR", "BPF", "Hexagon", "Lanai",
            "LoongArch", "Mips", "MSP430", "NVPTX", "PowerPC", "RISCV",
            "Sparc", "SystemZ", "VE", "WebAssembly", "X86", "XCore",
        ]

config.targets = get_llvm_targets()

# Make targets available to lit.local.cfg files
config.root.targets = config.targets
