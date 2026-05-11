# Agent Guidelines for llvm-nanobind

## Build & Test Commands
```bash
# CMake builds (for direct C++ development)
cmake -B build -G Ninja                      # Configure (first time only)
cmake --build build                          # Build

# uv builds (recommended for Python development)
uv run <command>                             # Auto-rebuilds the extension if needed

# Testing
uv run run_tests.py                          # Main golden-master suite: C++ tests + paired Python comparison
uv run run_tests.py --regressions            # Python regression scripts in tests/regressions/
uv run run_llvm_c_tests.py                   # Vendored llvm-c-test lit tests (auto-rebuilds C binary)
uv run run_llvm_c_tests.py --use-python      # Vendored llvm-c-test lit tests with Python implementation
uv run run_llvm_c_tests.py -v                # Run lit tests with verbose output
./build/test_factorial                       # Run single C++ test
uv run test_factorial.py                     # Run single Python test
uv run pytest tests/regressions/...          # Target a specific regression file or subset
uv run tests/test_module.py                  # Python tests are intended to be directly runnable scripts
uv run tests/regressions/test_const_bytes.py # Regression scripts should also run directly via __main__
uv run python -m llvm_c_test --targets-list  # Run Python llvm-c-test port directly (dev-only tool)
./build/llvm-c-test --echo < input.bc        # Run vendored C llvm-c-test binary directly
uvx ty check                                 # Type check Python code (not a test runner)
uvx ty check llvm_c_test/                    # Type check specific directory

# Closest thing to “run everything”
uv run run_tests.py
uv run run_tests.py --regressions
uv run run_llvm_c_tests.py
uv run run_llvm_c_tests.py --use-python

# Code Coverage
uv run coverage run test_factorial.py        # Run single test with coverage
uv run coverage run --data-file=.coverage.test1 test_factorial.py  # Separate coverage file
uv run coverage combine                      # Combine multiple .coverage.* files
uv run coverage report                       # Show coverage report
uv run coverage html                         # Generate HTML coverage report (htmlcov/)

# Comprehensive Coverage (test runners + all tests)
uv run coverage run --data-file=.coverage.run_tests run_tests.py
uv run coverage run run_llvm_c_tests.py --use-python  # Each lit test creates .coverage.llvm_c_test.* files
uv run coverage combine                      # Combine all coverage files
uv run coverage report                       # Show comprehensive report
uv run coverage report --include="llvm_c_test/*"  # Show llvm_c_test coverage only
```

## Type Checking
- Type stubs are auto-generated in `.venv/lib/python3.12/site-packages/llvm/__init__.pyi`
- The stubs are regenerated on every rebuild
- Run `uvx ty check` to verify all Python code type-checks correctly
- Python LSPs (Pyright, etc.) automatically pick up the generated stubs

## Code Style
- **C++**: Use `LLVMXxxWrapper` for wrapper structs, `m_` prefix for members, `snake_case` for methods
- **Python**: Follow PEP 8, use `snake_case`, type hints optional but encouraged
- **Formatting**: 2-space indent in C++, 4-space in Python; no trailing whitespace
- **Imports**: C++ standard headers first, then LLVM-C headers, then nanobind
- **Error handling**: Throw `LLVMException` subclasses in C++; call `check_valid()` before LLVM-C API calls

## Testing Pattern (Golden Master)
C++ tests output LLVM IR to stdout → saved as `tests/output/*.ll` → Python tests must produce identical output.
Tests must be deterministic (no timestamps, PIDs, addresses). See `devdocs/archive/bindings.md` for details.

## Debugging Hard Crashes

**Important**: Our goal is to protect users from footguns by raising Python exceptions instead of hard crashes whenever possible. We have lifetime information for references and ownership tracking, so we can do additional checks before calling the C API.

When encountering segfaults or crashes:

1. **Isolate the crash** into a `test_memory_*.py` file with pure Python reproduction (no subprocesses)
2. **Document the root cause** in the test file docstring
3. **Add validation checks** in the C++ bindings to raise exceptions instead of crashing
4. **Reference `devdocs/DEBUGGING.md`** for complete best practices

See `devdocs/DEBUGGING.md` for detailed debugging guidelines and patterns.

## Common Build Issues
- **CMake can't find LLVM**: Ask the user to configure the build manually once, this will create `.llvm-prefix` used for the rest of the build.
- **Type stubs not updating**: Rebuild with `uv sync`
- **Import errors in Python**: The llvm module is dynamically generated; type checkers need the stubs

## Development Documentation (devdocs/)

The `devdocs/` directory tracks multi-phase tasks and preserves key learnings.

**For in-progress tasks:** Create `devdocs/<task>/plan.md` and `progress.md` to track work across sessions.

**For completed tasks:** Extract key learnings into `devdocs/archive/<task>.md` and delete the plan/progress files.

**Reference docs:** `DEBUGGING.md`, `memory-model.md`, `lit-tests.md` contain active technical documentation.

See `devdocs/README.md` for the full methodology.

**Important:** Task creation and archival are user-initiated. The agent should:
- Update `progress.md` as work progresses
- Suggest when a task might be ready for archival
- Only create/archive tasks when explicitly asked by the user
