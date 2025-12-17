# Agent Guidelines for llvm-nanobind

## Build & Test Commands
```bash
# CMake builds (for direct C++ development)
cmake -B build -G Ninja                      # Configure (first time only)
cmake --build build                          # Build

# uv builds (recommended for Python development)
uv run <command>                             # Auto-rebuilds the extension if needed

# Testing
uv run python run_tests.py                   # Run all C++ tests + Python comparison
uv run python run_llvm_c_tests.py            # Run vendored llvm-c-test lit tests
uv run python run_llvm_c_tests.py -v         # Run lit tests with verbose output
./build/test_factorial                       # Run single C++ test
uv run python test_factorial.py              # Run single Python test
uvx ty check                                  # Type check Python code
uvx ty check llvm_c_test/                     # Type check specific directory

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
Tests must be deterministic (no timestamps, PIDs, addresses). See `devdocs/plan.md` for templates.

## Common Build Issues
- **CMake can't find LLVM**: Ensure `CMAKE_PREFIX_PATH=$(brew --prefix llvm)` is set
- **Type stubs not updating**: Rebuild with `CMAKE_PREFIX_PATH=$(brew --prefix llvm) uv sync`
- **Import errors in Python**: The llvm module is dynamically generated; type checkers need the stubs
