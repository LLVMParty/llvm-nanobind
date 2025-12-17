# llvm-nanobind

LLVM-C Python bindings with nanobind. 

⚠️ This project is still in a very early design phase and not remotely usable.

## Local development

Set the CMake prefix path environment variable to point to the LLVM prefix:

```bash
export CMAKE_PREFIX_PATH=$(brew --prefix llvm)
```

Configure the bindings:

```bash
cmake -B build -G Ninja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Build the bindings:

```bash
cmake --build build
```

Run the example:

```bash
uv run playground.py
```

## Testing

Run the golden master tests (C++ and Python bindings):

```bash
uv run python run_tests.py
```

Run the vendored llvm-c-test integration tests:

```bash
uv run python run_llvm_c_tests.py        # Run all tests
uv run python run_llvm_c_tests.py -v     # Verbose output
```

## Type Checking

Check type correctness of Python code:

```bash
uvx ty check                              # Check all Python files
uvx ty check llvm_c_test/                 # Check specific directory
```

## Code Coverage

Generate code coverage reports:

```bash
uv run coverage run test_factorial.py     # Run test with coverage
uv run coverage report                    # Show coverage summary
uv run coverage html                      # Generate HTML report (htmlcov/)
```

To combine coverage from multiple test runs:

```bash
uv run coverage run --data-file=.coverage.test1 test_factorial.py
uv run coverage run --data-file=.coverage.test2 test_module.py
uv run coverage combine                   # Combine all .coverage.* files
uv run coverage report                    # Show combined report
```

For comprehensive coverage including test runners and all subprocess tests:

```bash
COVERAGE_RUN=1 uv run coverage run --data-file=.coverage.run_tests run_tests.py
COVERAGE_RUN=1 uv run coverage run --data-file=.coverage.run_llvm_c_tests run_llvm_c_tests.py
uv run coverage combine                   # Combine all coverage files
uv run coverage report                    # Show comprehensive report
uv run coverage html                      # Generate HTML report
```

The `COVERAGE_RUN=1` environment variable enables coverage for subprocesses spawned by the test runners.
