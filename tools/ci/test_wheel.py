#!/usr/bin/env python3
"""Run the repository test suites against an installed wheel.

cibuildwheel installs the freshly built wheel into an isolated test
environment before running this script. The script then builds the C++ test
executables from source, but keeps Python imports pointed at the installed
wheel rather than build/llvm.*.
"""

from __future__ import annotations

import importlib.metadata
import os
import shutil
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[2]
BUILD_DIR = PROJECT_ROOT / "build"


def run(args: list[str], *, env: dict[str, str] | None = None) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=PROJECT_ROOT, env=env, check=True)


def find_llvm_root() -> Path:
    candidates: list[Path] = []

    if cmake_prefix_path := os.environ.get("CMAKE_PREFIX_PATH"):
        candidates.extend(Path(p) for p in cmake_prefix_path.split(os.pathsep) if p)

    if llvm_root := os.environ.get("LLVM_ROOT"):
        candidates.append(Path(llvm_root))

    prefix_file = PROJECT_ROOT / ".llvm-prefix"
    if prefix_file.exists():
        candidates.append(Path(prefix_file.read_text(encoding="utf-8").strip()))

    candidates.append(PROJECT_ROOT / ".llvm")

    for candidate in candidates:
        llvm_config = candidate / "bin" / ("llvm-config.exe" if sys.platform == "win32" else "llvm-config")
        if llvm_config.exists():
            return candidate.resolve()

    raise RuntimeError(
        "Could not find LLVM root. Set CMAKE_PREFIX_PATH or LLVM_ROOT to the prebuilt LLVM prefix."
    )


def make_test_env(llvm_root: Path) -> dict[str, str]:
    env = os.environ.copy()
    env["LLVM_NANOBIND_TEST_INSTALLED"] = "1"
    env["CMAKE_PREFIX_PATH"] = str(llvm_root)
    env["LLVM_ROOT"] = str(llvm_root)

    path_entries = [str(llvm_root / "bin")]
    if existing_path := env.get("PATH"):
        path_entries.append(existing_path)
    env["PATH"] = os.pathsep.join(path_entries)

    if sys.platform == "darwin":
        key = "DYLD_LIBRARY_PATH"
        env[key] = os.pathsep.join([str(llvm_root / "lib"), env.get(key, "")]).rstrip(os.pathsep)
    elif sys.platform != "win32":
        key = "LD_LIBRARY_PATH"
        env[key] = os.pathsep.join([str(llvm_root / "lib"), env.get(key, "")]).rstrip(os.pathsep)

    return env


def configure_and_build_cpp_tests(env: dict[str, str]) -> None:
    if BUILD_DIR.exists():
        shutil.rmtree(BUILD_DIR)

    cmake_args = [
        "cmake",
        "-B",
        str(BUILD_DIR),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
    ]
    if sys.platform == "win32":
        cmake_args.extend([
            "-DCMAKE_C_COMPILER=clang-cl",
            "-DCMAKE_CXX_COMPILER=clang-cl",
        ])

    run(cmake_args, env=env)
    run(["cmake", "--build", str(BUILD_DIR)], env=env)


def run_extra_standalone_tests(env: dict[str, str]) -> None:
    scripts = [
        "tests/test_binary.py",
        "tests/test_bitcode_linker.py",
        "tests/test_feature_matrix.py",
        "tests/test_function_extended.py",
        "tests/test_passbuilder.py",
        "tests/test_target_codegen.py",
    ]
    for script in scripts:
        run([sys.executable, script], env=env)


def run_extra_cpp_tests(env: dict[str, str]) -> None:
    exe_suffix = ".exe" if sys.platform == "win32" else ""
    tests = [
        "test_target_codegen",
        "test_bitcode_linker",
        "test_passbuilder",
        "test_function_extended",
        "test_symbol_size_crash",
    ]
    for test in tests:
        run([str(BUILD_DIR / f"{test}{exe_suffix}")], env=env)


def main() -> int:
    llvm_root = find_llvm_root()
    (PROJECT_ROOT / ".llvm-prefix").write_text(str(llvm_root) + "\n", encoding="utf-8")

    env = make_test_env(llvm_root)

    import llvm

    print(f"Testing installed llvm-nanobind {importlib.metadata.version('llvm-nanobind')}")
    print(f"llvm module: {llvm.__file__}")
    print(f"LLVM root: {llvm_root}")

    configure_and_build_cpp_tests(env)

    run([sys.executable, "run_tests.py"], env=env)
    run([sys.executable, "run_tests.py", "--regressions"], env=env)
    run([sys.executable, "run_llvm_c_tests.py", "-v"], env=env)
    run([sys.executable, "run_llvm_c_tests.py", "--use-python", "-v"], env=env)
    run_extra_standalone_tests(env)
    run_extra_cpp_tests(env)
    # The script-based runners above cover the main, regression, standalone,
    # and C++ suites. Keep pytest for tests that are intentionally pytest-only.
    run([sys.executable, "-m", "pytest", "tests/test_examples.py"], env=env)
    run([shutil.which("ty") or "ty", "check"], env=env)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
