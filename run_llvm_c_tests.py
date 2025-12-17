#!/usr/bin/env python3
"""
Test runner for llvm-c-test lit tests.

Runs the vendored llvm-c-test integration tests using LLVM's lit test runner.

LLVM tools are located in this order:
  1. --llvm-prefix command line argument
  2. CMAKE_PREFIX_PATH environment variable
  3. brew --prefix llvm (macOS only)

Usage:
    python run_llvm_c_tests.py [options] [lit options...]

Options:
    --llvm-prefix PATH    Path to LLVM installation prefix

Examples:
    python run_llvm_c_tests.py                          # Run all tests
    python run_llvm_c_tests.py -v                       # Verbose output
    python run_llvm_c_tests.py --llvm-prefix /opt/llvm  # Use specific LLVM
    python run_llvm_c_tests.py calc.test                # Run specific test
"""

import os
import subprocess
import sys
from pathlib import Path


def coverage_wrap(name: str, args: list[str]) -> list[str]:
    """Wrap command with coverage if COVERAGE_RUN environment variable is set."""
    if os.environ.get("COVERAGE_RUN"):
        return ["-m", "coverage", "run", f"--data-file=.coverage.{name}"] + args
    return args


def get_llvm_prefix_from_brew() -> Path | None:
    """Get the LLVM installation prefix from Homebrew (macOS only)."""
    if sys.platform != "darwin":
        return None

    result = subprocess.run(
        ["brew", "--prefix", "llvm"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        return None
    return Path(result.stdout.strip())


def get_llvm_prefix(cli_prefix: str | None) -> Path:
    """
    Get the LLVM installation prefix.

    Priority:
      1. Command line argument (--llvm-prefix)
      2. CMAKE_PREFIX_PATH environment variable
      3. brew --prefix llvm (macOS only)
    """
    # 1. Command line argument
    if cli_prefix:
        path = Path(cli_prefix)
        if path.exists():
            return path
        print(f"Error: Specified LLVM prefix does not exist: {path}", file=sys.stderr)
        sys.exit(1)

    # 2. CMAKE_PREFIX_PATH environment variable
    cmake_prefix_path = os.environ.get("CMAKE_PREFIX_PATH")
    if cmake_prefix_path:
        # CMAKE_PREFIX_PATH can be a semicolon-separated list; take the first
        first_path = cmake_prefix_path.split(";")[0].split(":")[0]
        path = Path(first_path)
        if path.exists():
            return path

    # 3. Homebrew (macOS only)
    brew_prefix = get_llvm_prefix_from_brew()
    if brew_prefix and brew_prefix.exists():
        return brew_prefix

    # No LLVM found
    print("Error: Could not find LLVM installation.", file=sys.stderr)
    print("", file=sys.stderr)
    print("Please specify LLVM location using one of:", file=sys.stderr)
    print("  --llvm-prefix /path/to/llvm", file=sys.stderr)
    print("  CMAKE_PREFIX_PATH=/path/to/llvm", file=sys.stderr)
    if sys.platform == "darwin":
        print("  brew install llvm", file=sys.stderr)
    sys.exit(1)


def parse_args(args: list[str]) -> tuple[str | None, list[str]]:
    """Parse our custom arguments, return (llvm_prefix, remaining_args)."""
    llvm_prefix = None
    remaining = []

    i = 0
    while i < len(args):
        if args[i] == "--llvm-prefix":
            if i + 1 >= len(args):
                print("Error: --llvm-prefix requires an argument", file=sys.stderr)
                sys.exit(1)
            llvm_prefix = args[i + 1]
            i += 2
        elif args[i].startswith("--llvm-prefix="):
            llvm_prefix = args[i].split("=", 1)[1]
            i += 1
        else:
            remaining.append(args[i])
            i += 1

    return llvm_prefix, remaining


def main():
    # Parse our custom arguments
    llvm_prefix_arg, lit_args_extra = parse_args(sys.argv[1:])

    project_root = Path(__file__).parent.resolve()
    build_dir = project_root / "build"
    test_dir = project_root / "llvm-c" / "llvm-c-test" / "inputs"

    # Verify build directory exists
    if not build_dir.exists():
        print(f"Error: Build directory not found: {build_dir}", file=sys.stderr)
        print("Run: cmake -B build -G Ninja && cmake --build build", file=sys.stderr)
        sys.exit(1)

    # Find llvm-c-test executable
    llvm_c_test_exe = build_dir / "llvm-c-test"
    if sys.platform == "win32":
        llvm_c_test_exe = llvm_c_test_exe.with_suffix(".exe")

    if not llvm_c_test_exe.exists():
        print(
            f"Error: llvm-c-test executable not found: {llvm_c_test_exe}",
            file=sys.stderr,
        )
        print(
            "Build with: cmake --build build --target llvm-c-test-vendored",
            file=sys.stderr,
        )
        sys.exit(1)

    # Get LLVM tools directory
    llvm_prefix = get_llvm_prefix(llvm_prefix_arg)
    llvm_tools_dir = llvm_prefix / "bin"

    # Verify required LLVM tools exist
    required_tools = ["llvm-as", "llvm-dis", "FileCheck", "not"]
    missing_tools = []
    for tool in required_tools:
        if not (llvm_tools_dir / tool).exists():
            missing_tools.append(tool)

    if missing_tools:
        print(f"Error: Missing LLVM tools in {llvm_tools_dir}:", file=sys.stderr)
        for tool in missing_tools:
            print(f"  - {tool}", file=sys.stderr)
        sys.exit(1)

    # Verify test directory exists
    if not test_dir.exists():
        print(f"Error: Test directory not found: {test_dir}", file=sys.stderr)
        sys.exit(1)

    # Set up environment for lit.cfg.py
    env = os.environ.copy()
    env["LLVM_TOOLS_DIR"] = str(llvm_tools_dir)
    env["LLVM_C_TEST_EXE"] = str(llvm_c_test_exe)

    # Use build directory for test outputs to keep source tree clean
    lit_exec_root = build_dir / "llvm-c-test-output"
    lit_exec_root.mkdir(parents=True, exist_ok=True)
    env["LIT_EXEC_ROOT"] = str(lit_exec_root)

    # Build lit command - use lit from the same venv/environment as this script
    # Try to find lit executable in the same directory as the Python interpreter
    python_dir = Path(sys.executable).parent
    lit_exe = python_dir / "lit"

    # Default options: single-threaded, deterministic order
    # User can override with e.g. -j 10 for parallel execution
    default_opts = ["-j", "1", "--order=lexical"]

    if lit_exe.exists():
        lit_args = [str(lit_exe)] + default_opts + [str(test_dir)]
    else:
        # Fall back to running lit as a module
        lit_args = [sys.executable, "-m", "lit"] + default_opts + [str(test_dir)]

    # Pass through any additional arguments
    lit_args.extend(lit_args_extra)

    # Run lit
    print("Running llvm-c-test lit tests...")
    print(f"  Test directory: {test_dir}")
    print(f"  llvm-c-test:    {llvm_c_test_exe}")
    print(f"  LLVM tools:     {llvm_tools_dir}")
    print()

    result = subprocess.run(lit_args, env=env)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
