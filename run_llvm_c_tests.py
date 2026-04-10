#!/usr/bin/env -S uv run
"""
Test runner for llvm-c-test lit tests.

Runs the vendored llvm-c-test integration tests using LLVM's lit test runner.

LLVM tools are located in this order:
  1. --llvm-prefix command line argument
  2. .llvm-prefix file in project root

Usage:
    python run_llvm_c_tests.py [options] [lit options...]

Options:
    --llvm-prefix PATH    Path to LLVM installation prefix
    --use-python          Use Python implementation instead of C binary

Examples:
    python run_llvm_c_tests.py                          # Run all tests (C binary)
    python run_llvm_c_tests.py --use-python             # Run with Python implementation
    python run_llvm_c_tests.py -v                       # Verbose output
    python run_llvm_c_tests.py --llvm-prefix /opt/llvm  # Use specific LLVM
    python run_llvm_c_tests.py calc.test                # Run specific test

    # Run with coverage (Python implementation)
    uv run coverage run run_llvm_c_tests.py --use-python
"""

import os
import subprocess
import sys
from pathlib import Path


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
      2. .llvm-prefix file in project root
    """
    # 1. Command line argument
    if cli_prefix:
        path = Path(cli_prefix)
        if path.exists():
            return path
        print(f"Error: Specified LLVM prefix does not exist: {path}", file=sys.stderr)
        sys.exit(1)

    # 2. .llvm-prefix file
    project_root = Path(__file__).parent.resolve()
    llvm_prefix_file = project_root / ".llvm-prefix"
    if llvm_prefix_file.exists():
        with llvm_prefix_file.open("r") as f:
            prefix_path = f.read().strip()
            path = Path(prefix_path).expanduser()
            if path.exists():
                return path
            print(
                f"Error: LLVM prefix in .llvm-prefix does not exist: {path}",
                file=sys.stderr,
            )
            sys.exit(1)

    # No LLVM found
    print("Error: Could not find LLVM installation.", file=sys.stderr)
    print("", file=sys.stderr)
    print("Please specify LLVM location using one of:", file=sys.stderr)
    print("  --llvm-prefix /path/to/llvm", file=sys.stderr)
    print("  .llvm-prefix file containing the path", file=sys.stderr)
    sys.exit(1)


def parse_args(args: list[str]) -> tuple[str | None, bool, list[str]]:
    """Parse our custom arguments, return (llvm_prefix, use_python, remaining_args)."""
    llvm_prefix = None
    use_python = False
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
        elif args[i] == "--use-python":
            use_python = True
            i += 1
        else:
            remaining.append(args[i])
            i += 1

    return llvm_prefix, use_python, remaining


def build_llvm_c_test_cmd(
    use_python: bool, project_root: Path
) -> tuple[str, dict[str, str]]:
    """Build the llvm-c-test command string and extra environment variables.

    Args:
        use_python: If True, use Python implementation; otherwise use C binary.
        project_root: Path to project root for locating files.

    Returns:
        Tuple of (command string, extra environment variables dict).
    """
    extra_env: dict[str, str] = {}

    if use_python:
        # Check if we should enable coverage or logging
        coverage_run = os.environ.get("COVERAGE_RUN")

        # On Windows, quote the Python executable path to handle backslashes in bash
        python_exe = (
            f'"{sys.executable}"' if sys.platform == "win32" else sys.executable
        )

        if coverage_run:
            # Use coverage run with --parallel-mode so each invocation creates
            # a unique data file that can be combined later with `coverage combine`
            # Specify --data-file to write coverage to project root regardless of cwd
            coverage_file = project_root / ".coverage.llvm_c_test"
            coverage_file_arg = coverage_file.as_posix()
            cmd = f"{python_exe} -m coverage run --parallel-mode --data-file={coverage_file_arg} -m llvm_c_test"
        else:
            # Direct invocation using the installed script
            cmd = f"{python_exe} -m llvm_c_test"

        return cmd, extra_env
    else:
        # Use C binary
        llvm_c_test_exe = project_root / "build" / "llvm-c-test"
        if sys.platform == "win32":
            llvm_c_test_exe = llvm_c_test_exe.with_suffix(".exe")

        # On Windows, quote the path to handle backslashes in bash
        exe_path = str(llvm_c_test_exe)
        if sys.platform == "win32":
            exe_path = f'"{exe_path}"'

        return exe_path, extra_env


def prefer_windows_git_bash(env: dict[str, str]) -> str | None:
    """Prefer Git Bash over WSL bash stubs on Windows for lit shell tests."""
    if sys.platform != "win32":
        return None

    candidates = [
        Path(r"C:\Program Files\Git\bin\bash.exe"),
        Path(r"C:\Program Files\Git\usr\bin\bash.exe"),
    ]
    for bash_exe in candidates:
        if not bash_exe.exists():
            continue

        bash_dir = str(bash_exe.parent)
        path_entries = env.get("PATH", "").split(os.pathsep)
        # Prepend and deduplicate (case-insensitive on Windows).
        filtered = [p for p in path_entries if p.lower() != bash_dir.lower()]
        env["PATH"] = os.pathsep.join([bash_dir] + filtered)
        return str(bash_exe)

    return None


def main():
    # Parse our custom arguments
    llvm_prefix_arg, use_python, lit_args_extra = parse_args(sys.argv[1:])

    project_root = Path(__file__).parent.resolve()
    build_dir = project_root / "build"
    test_dir = project_root / "llvm-c" / "llvm-c-test" / "inputs"

    # Verify build directory exists (needed for test outputs even with --use-python)
    if not build_dir.exists():
        print(f"Error: Build directory not found: {build_dir}", file=sys.stderr)
        print("Run: cmake -B build -G Ninja && cmake --build build", file=sys.stderr)
        sys.exit(1)

    # Build the llvm-c-test command
    llvm_c_test_cmd, extra_env = build_llvm_c_test_cmd(use_python, project_root)

    # Verify C binary exists if not using Python
    if not use_python:
        llvm_c_test_exe = project_root / "build" / "llvm-c-test"
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
            print(
                "Or use --use-python to run with Python implementation", file=sys.stderr
            )
            sys.exit(1)

    # Get LLVM tools directory
    llvm_prefix = get_llvm_prefix(llvm_prefix_arg)
    llvm_tools_dir = llvm_prefix / "bin"

    # Verify required LLVM tools exist
    required_tools = ["llvm-as", "llvm-dis", "FileCheck", "not"]
    if sys.platform == "win32":
        required_tools = [tool + ".exe" for tool in required_tools]
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
    env["LLVM_C_TEST_CMD"] = llvm_c_test_cmd
    env.update(extra_env)
    preferred_bash = prefer_windows_git_bash(env)

    # Use build directory for test outputs to keep source tree clean
    lit_exec_root = build_dir / "llvm-c-test-output"
    lit_exec_root.mkdir(parents=True, exist_ok=True)
    env["LIT_EXEC_ROOT"] = str(lit_exec_root)

    # Build lit command - use lit from the same venv/environment as this script
    # Try to find lit executable in the same directory as the Python interpreter
    python_dir = Path(sys.executable).parent
    lit_exe = python_dir / (sys.platform == "win32" and "lit.exe" or "lit")

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
    print(f"  llvm-c-test:    {llvm_c_test_cmd}")
    print(f"  LLVM tools:     {llvm_tools_dir}")
    if preferred_bash:
        print(f"  Preferred bash: {preferred_bash}")
    if use_python:
        print("  Mode:           Python implementation")
    else:
        print("  Mode:           C binary")
    print()

    result = subprocess.run(lit_args, env=env)
    sys.exit(result.returncode)


if __name__ == "__main__":
    main()
