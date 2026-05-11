# llvm-nanobind

Python bindings for the LLVM-C API using [nanobind](https://github.com/wjakob/nanobind).

This project provides a Pythonic interface to LLVM's compiler infrastructure, enabling you to build compilers, analyzers, and code transformation tools in Python.

**Status**: Under active development. Core APIs are bound and tested against LLVM's `llvm-c-test` suite, but the API is not yet stable. Expect breaking changes.

_Note_: This project is 90%+ vibe coded. It is mostly an experiment to see what LLMs can do when you set things up properly.

## Features

- Comprehensive LLVM-C API coverage (~7300 lines of bindings)
- Memory-safe: validity tokens prevent use-after-free crashes
- Type-safe: auto-generated `.pyi` stubs for IDE support
- Tested: golden-master tests, regression scripts, pytest, and vendored `llvm-c-test` lit tests

## Installation

Released wheels bundle LLVM for supported platforms, so normal installation is:

```bash
pip install llvm-nanobind
```

Source builds need LLVM 21.1.6 available to CMake. Set `LLVM_ROOT` if LLVM is not in a standard location:

```bash
export LLVM_ROOT=/path/to/llvm
pip install .
```

See [llvm-nanobind-example](https://github.com/LLVMParty/llvm-nanobind-example) for a simple example project.

## Quick Start

```python
import llvm

# Create a simple function that returns 42.
with llvm.create_context() as ctx:
    i32 = ctx.types.i32
    fn_type = ctx.types.function(i32, [])

    with ctx.create_module("example") as mod:
        fn = mod.add_function("get_answer", fn_type)
        entry = fn.append_basic_block("entry")

        with entry.create_builder() as builder:
            builder.ret(i32.constant(42))

        assert mod.verify(), mod.get_verification_error()
        print(mod)
```

The same code is kept as a runnable smoke-tested script in `examples/quick_start.py`:

```bash
uv run python examples/quick_start.py
```

## Development

### Setup

```bash
# Configure (first time)
cmake -B build -G Ninja

# Build
cmake --build build

# Or use uv (recommended) - auto-rebuilds as needed
uv sync

# Offline build
uv sync --offline --no-build-isolation --verbose
```

### Testing

```bash
# Main golden-master suite:
# - runs C++ test executables from build/
# - runs paired Python scripts
# - compares Python output against stored C++ behavior
uv run run_tests.py

# Python-only regression scripts in tests/regressions/
uv run run_tests.py --regressions

# Vendored llvm-c-test lit suite against the C test binary
# Rebuilds the vendored C binary before running lit.
uv run run_llvm_c_tests.py
uv run run_llvm_c_tests.py -v

# Vendored llvm-c-test lit suite against the Python implementation
uv run run_llvm_c_tests.py --use-python

# Run the Python llvm-c-test port directly during development
uv run python -m llvm_c_test --targets-list

# Type checking (not a test suite, but commonly run in CI/dev)
uvx ty check
```

If you want the closest thing to “run everything in this repo”, use:

```bash
uv run run_tests.py
uv run run_tests.py --regressions
uv run run_llvm_c_tests.py
uv run run_llvm_c_tests.py --use-python
```

Python tests here are intended to be executable as standalone scripts
(e.g. `uv run tests/test_module.py` or `uv run tests/regressions/test_const_bytes.py`).
They are generally pytest-compatible too, but direct script execution is the
historical/default style used by `run_tests.py` and for one-off debugging.

`pytest` is still useful for targeting specific regression files or subsets, but
it is not our complete top-level test entrypoint by itself.

### Coverage

```bash
# Run with coverage
uv run coverage run run_llvm_c_tests.py --use-python
uv run coverage combine
uv run coverage report --include="llvm_c_test/*"
```

### Examples

Runnable examples live in `examples/` and are covered by `tests/test_examples.py`:

```bash
uv run python examples/quick_start.py
uv run python examples/transform_replace_add.py
```

## Documentation

Type stubs are auto-generated and provide IDE intellisense. After building, find them at:
```
.venv/lib/python3.*/site-packages/llvm/__init__.pyi
```

For development documentation, see `devdocs/README.md`.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

LLVM is licensed under the Apache License v2.0 with LLVM Exceptions.

## Windows

Download LLVM+Clang:

- https://github.com/vovkos/llvm-package-windows/releases/download/llvm-21.1.1/llvm-21.1.1-windows-amd64-msvc17-msvcrt.7z
- https://github.com/vovkos/llvm-package-windows/releases/download/clang-20.1.8/clang-20.1.8-windows-amd64-msvc17-msvcrt.7z

Merge them together in `C:\llvm-21.1.1`.

Create `CMakeUserPresets.json`:

```json
{
    "version": 3,
    "configurePresets": [
        {
            "name": "clang-cl",
            "displayName": "Ninja with clang-cl",
            "generator": "Ninja",
            "binaryDir": "${sourceDir}/build",
            "cacheVariables": {
                "CMAKE_C_COMPILER": "C:/Program Files/LLVM/bin/clang-cl.exe",
                "CMAKE_CXX_COMPILER": "C:/Program Files/LLVM/bin/clang-cl.exe",
                "CMAKE_EXPORT_COMPILE_COMMANDS": "ON",
                "CMAKE_BUILD_TYPE": "RelWithDebInfo",
                "CMAKE_PREFIX_PATH": "d:/llvm-21.1.1"
            }
        }
    ],
    "buildPresets": [
        {
            "name": "clang-cl",
            "configurePreset": "clang-cl"
        }
    ]
}
```

Create a virtual environment:

```bash
uv venv
```

Activate the virtual environment:

```bash
.venv/Scripts/activate
```

Configure the CMake project (this should find the Python from your venv):

```bash
cmake --preset clang-cl
```

_Note_: This saves the LLVM prefix to a file called `.llvm-prefix`, make sure to delete that if you change the LLVM prefix path.

Build the bindings:

```bash
cmake --build build
```

After that works you can build the Python package with `uv`:

```bash
uv sync --verbose
```
