# llvm-nanobind

Python bindings for the LLVM-C API using [nanobind](https://github.com/wjakob/nanobind).

This project provides a Pythonic interface to LLVM's compiler infrastructure, enabling you to build compilers, analyzers, and code transformation tools in Python.

**Current status**: Experimental but usable. The bindings target LLVM 21.1.6 and are published as `llvm-nanobind` on PyPI (import name: `llvm`). Wheels bundle LLVM and are built/tested in CI for Linux x86_64/aarch64, macOS arm64, and Windows x86_64. The API is still evolving; expect breaking changes until a stable release.

Version numbers track LLVM: `21.1.6.1` means LLVM `21.1.6` plus binding/package revision `1`.

_Note_: This project is 90%+ vibe coded. It is mostly an experiment to see what LLMs can do when you set things up properly.

## Installation

Released wheels bundle LLVM for supported platforms, so normal installation is:

```bash
pip install llvm-nanobind
python -c "import llvm; print(llvm)"
```

For source/development builds, see [Development](#development).

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

        assert mod.verify(), mod.verification_error
        print(mod)
```

This exact snippet is available as [`examples/quick_start.py`](examples/quick_start.py).

## Examples

Runnable examples live in [`examples/`](examples/) and are covered by `tests/test_examples.py`.

```bash
uv run python examples/quick_start.py
uv run python examples/transform_replace_add.py
```

More examples worth browsing:

- [`examples/intrinsic_memcpy.py`](examples/intrinsic_memcpy.py) - call an LLVM intrinsic by name with `Builder.intrinsic(...)`
- [`examples/optimize_module.py`](examples/optimize_module.py) - optimize a module with a PassBuilder pipeline string
- [`examples/emit_object_assembly.py`](examples/emit_object_assembly.py) - emit host object code and assembly from a module
- [`examples/jit_add.py`](examples/jit_add.py) - JIT-compile IR, call it through ctypes, and register a Python callback
- [`examples/transform_replace_add.py`](examples/transform_replace_add.py) - simple IR transformation using operands, RAUW, and instruction deletion
- [`examples/bc-stats.py`](examples/bc-stats.py) - print per-function instruction histograms from LLVM IR
- [`examples/bc-graphviz.py`](examples/bc-graphviz.py) - emit a GraphViz-style control-flow graph from LLVM IR
- [`examples/bc-profile.py`](examples/bc-profile.py) - instrument LLVM IR with profiling hooks

## Current capabilities

- Pythonic wrappers for contexts, modules, types, values, functions, basic blocks, builders, metadata, debug info, object files, targets, target machines, and pass builder options
- IR construction, traversal, and transformation helpers: constants, globals, PHI/control flow/memory/cast/cmp instructions, operands, predecessors, RAUW, split blocks, move/clone/erase instructions
- IR and bitcode parsing/writing, lazy bitcode modules, module cloning/linking, module flags, diagnostics, attributes, COMDATs, calling conventions, linkage/visibility/storage controls
- Target initialization/lookup, data layouts, `Module.emit_object()`, `Module.emit_assembly()`, target-machine emission, and PassBuilder pipeline execution via `Module.optimize()` / `Module.run_passes()`
- Generic intrinsic calls with `Builder.intrinsic(...)` and in-process JIT execution through the LLVM-C ORC LLJIT API
- Lifetime/validity guards that turn many use-after-free, disposed-object, null-reference, and wrong-kind mistakes into Python exceptions instead of hard crashes
- Auto-generated typed `.pyi` stubs for IDEs/type checkers
- Golden-master tests, Python regression scripts, examples, and vendored `llvm-c-test` lit tests, including CI coverage against installed wheels

## Documentation

Type stubs are auto-generated and provide IDE intellisense. After building, find them at:
```
.venv/lib/python3.*/site-packages/llvm/__init__.pyi
```

For development documentation, see `devdocs/README.md`.

## Known limitations

- The API is not stable yet; method names and ownership rules may still change.
- Scope follows the LLVM-C API, not the full LLVM C++ API. JIT support is intentionally limited to what is exposed cleanly through LLVM-C ORC LLJIT.
- Docs are currently the README, examples, `devdocs/`, and the generated `.pyi` stub; there is no hosted API reference yet.
- Prebuilt wheels currently target CPython 3.12+ stable ABI on Linux x86_64/aarch64, macOS arm64, and Windows x86_64. Other platforms require a source build.

## Development

### Setup

Source/development builds need LLVM 21.1.6 available to CMake. The LLVM archives used for packaging are published at [LLVMParty/llvm-builds v21.1.6](https://github.com/LLVMParty/llvm-builds/releases/tag/v21.1.6).

From a checkout, download the matching LLVM archive and then run `uv sync`:

```bash
# Choose the archive matching your platform:
#   llvm-21.1.6-linux-x86_64.zip
#   llvm-21.1.6-linux-aarch64.zip
#   llvm-21.1.6-macos-arm64.zip
#   llvm-21.1.6-windows-x86_64.zip
python tools/ci/install_llvm.py \
  --version 21.1.6 \
  --archive llvm-21.1.6-linux-x86_64.zip \
  --dest .llvm \
  --prefix-file .llvm-prefix

uv sync --verbose
```

If you are using your own LLVM install instead, set `LLVM_ROOT` to its install prefix:

```bash
export LLVM_ROOT=/path/to/llvm
uv sync --verbose
```

If `.llvm-prefix` already points at another LLVM install, delete it first; CMake may reuse it from a previous configure.

For offline builds:

```bash
uv sync --offline --no-build-isolation --verbose
```

For Windows C++/LSP setup, see the Windows development section below.

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

### Windows source builds and C++ IntelliSense

The wheel is the easiest path on Windows. If you need a source/development build, install Visual Studio or Visual Studio Build Tools with the C++ workload, then fetch LLVM and run `uv sync`:

```powershell
py -3.12 tools\ci\install_llvm.py `
  --version 21.1.6 `
  --archive llvm-21.1.6-windows-x86_64.zip `
  --dest .llvm `
  --prefix-file .llvm-prefix

uv sync --verbose
```

That command downloads from [LLVMParty/llvm-builds v21.1.6](https://github.com/LLVMParty/llvm-builds/releases/tag/v21.1.6), installs into `.llvm`, and writes `.llvm-prefix` for CMake.

If you are using your own LLVM install instead:

```powershell
$env:LLVM_ROOT = "C:\path\to\llvm"
uv sync --verbose
```

For C++ local development with an LSP, create a local `CMakeUserPresets.json` at the repository root. CMake gets LLVM from `.llvm-prefix` created above, or from `LLVM_ROOT` on first configure. The compiler paths below assume the usual LLVM installer location; adjust them if your `clang-cl.exe` is elsewhere:

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
                "CMAKE_BUILD_TYPE": "RelWithDebInfo"
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

Then configure/build with:

```powershell
cmake --preset clang-cl
cmake --build --preset clang-cl
```

If you move or replace the LLVM install, delete or regenerate `.llvm-prefix` from any previous configure.

## License

This project is licensed under the MIT License. See [LICENSE](LICENSE) for details.

LLVM is licensed under the Apache License v2.0 with LLVM Exceptions.

