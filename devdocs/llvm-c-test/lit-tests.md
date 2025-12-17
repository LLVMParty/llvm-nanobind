# LLVM-C-Test Lit Test Suite Documentation

This document describes the lit test suite that validates the Python port of llvm-c-test.

## Overview

The lit test suite verifies that the Python implementation (`llvm_c_test`) produces **byte-identical** output to the original C implementation (`llvm-c-test`). This ensures API compatibility and correctness.

## Running the Tests

```bash
# Run all lit tests with C binary (default)
uv run run_llvm_c_tests.py

# Run all lit tests with Python implementation
uv run run_llvm_c_tests.py --use-python

# Run with verbose output
uv run run_llvm_c_tests.py -v

# Run with Python implementation and coverage collection
uv run coverage run run_llvm_c_tests.py --use-python

# Enable command logging for debugging
LLVM_C_TEST_LOG=commands.log uv run run_llvm_c_tests.py --use-python
```

## Test Directory Structure

```
llvm-c/llvm-c-test/inputs/
├── lit.cfg.py              # Lit configuration
├── ARM/                    # ARM-specific tests
│   ├── lit.local.cfg       # Local config (requires ARM target)
│   └── disassemble.test    # ARM disassembly test
├── X86/                    # X86-specific tests
│   ├── lit.local.cfg       # Local config (requires X86 target)
│   └── disassemble.test    # X86 disassembly test
├── Inputs/                 # Test input files
│   └── invalid.ll.bc       # Invalid bitcode for error tests
└── *.ll / *.test          # Test files
```

## Test Files by Category

### Echo Tests (Module Cloning)

These tests verify the `--echo` command which clones IR using the C API:

| Test File | Description |
|-----------|-------------|
| `echo.ll` | Comprehensive IR with all instruction types |
| `atomics.ll` | Atomic operations (cmpxchg, atomicrmw, fence) |
| `float_ops.ll` | Floating-point operations and fast-math flags |
| `freeze.ll` | Freeze instruction handling |
| `invoke.ll` | Invoke/landingpad exception handling |
| `memops.ll` | Memory operations (load, store, GEP) |

### Module Operation Tests

| Test File | Description |
|-----------|-------------|
| `functions.ll` | `--module-list-functions` - Function listing |
| `globals.ll` | `--module-list-globals` - Global variable listing |
| `empty.ll` | `--module-dump` with minimal module |

### Attribute Tests

| Test File | Description |
|-----------|-------------|
| `function_attributes.ll` | `--test-function-attributes` - Enumerate function attributes |
| `callsite_attributes.ll` | `--test-callsite-attributes` - Enumerate call site attributes |

### Metadata Tests

| Test File | Description |
|-----------|-------------|
| `add_named_metadata_operand.ll` | `--add-named-metadata-operand` - Named metadata API |
| `set_metadata.ll` | `--set-metadata` - Instruction metadata |
| `replace_md_operand.ll` | `--replace-md-operand` - Metadata operand replacement |
| `is_a_value_as_metadata.ll` | `--is-a-value-as-metadata` - ValueAsMetadata checking |

### Debug Info Tests

| Test File | Description |
|-----------|-------------|
| `debug_info_new_format.ll` | `--test-dibuilder` - DIBuilder comprehensive test |
| `get-di-tag.ll` | `--get-di-tag` - DWARF tag extraction |
| `di-type-get-name.ll` | `--di-type-get-name` - Type name from debug info |

### Calculator Test

| Test File | Description |
|-----------|-------------|
| `calc.test` | `--calc` - RPN calculator generating IR |

### Error Handling Tests

| Test File | Description |
|-----------|-------------|
| `invalid-bitcode.test` | Test invalid bitcode error handling |

### Disassembly Tests

| Test File | Description |
|-----------|-------------|
| `ARM/disassemble.test` | `--disassemble` for ARM architecture |
| `X86/disassemble.test` | `--disassemble` for X86 architecture |

### Object File Tests

| Test File | Description |
|-----------|-------------|
| `objectfile.ll` | `--object-list-sections/symbols` - Object file parsing |

## Test Format

Tests use the lit ShTest format with FileCheck verification:

```llvm
; RUN: llvm-as < %s | llvm-c-test --echo | FileCheck %s

; CHECK: define i32 @main()
define i32 @main() {
  ret i32 0
}
```

### Common RUN Line Patterns

1. **Echo test** (clones IR):
   ```
   ; RUN: llvm-as < %s | llvm-c-test --echo | FileCheck %s
   ```

2. **Module dump**:
   ```
   ; RUN: llvm-as < %s | llvm-c-test --module-dump
   ```

3. **Silent test** (no output expected):
   ```
   ; RUN: llvm-as < %s | llvm-c-test --test-function-attributes
   ```

4. **Error test**:
   ```
   ; RUN: not llvm-c-test --module-dump < %S/Inputs/invalid.ll.bc 2>&1 | FileCheck %s
   ```

## Configuration

### lit.cfg.py

The configuration file (`llvm-c/llvm-c-test/inputs/lit.cfg.py`) sets up:

- Test format: ShTest
- Suffixes: `.ll`, `.test`
- Tool substitutions: `llvm-c-test`, `llvm-as`, `llvm-dis`, `FileCheck`, `not`
- Available targets for architecture-specific tests

### Environment Variables

Set by `run_llvm_c_tests.py`:

| Variable | Description |
|----------|-------------|
| `LLVM_TOOLS_DIR` | Path to LLVM tools (llvm-as, FileCheck, etc.) |
| `LLVM_C_TEST_CMD` | Full command to run llvm-c-test (C binary, Python module, or coverage-wrapped) |
| `LIT_EXEC_ROOT` | Output directory for test artifacts |
| `PYTHONPATH` | Set to project root when using Python mode |

For command logging:

| Variable | Description |
|----------|-------------|
| `LLVM_C_TEST_LOG` | Path to log file for recording executed commands |

## Architecture

### Test Runner Flow

```
run_llvm_c_tests.py
    │
    ├── Default mode (C binary):
    │   └── LLVM_C_TEST_CMD = /path/to/build/llvm-c-test
    │
    └── Python mode (--use-python):
        │
        ├── With coverage (COVERAGE_RUN set by `uv run coverage run`):
        │   └── LLVM_C_TEST_CMD = python -m coverage run --parallel-mode -m llvm_c_test
        │
        ├── With logging (LLVM_C_TEST_LOG set):
        │   └── LLVM_C_TEST_CMD = python llvm-c-test-wrapper.py
        │
        └── Default:
            └── LLVM_C_TEST_CMD = python -m llvm_c_test
```

### Coverage Collection

When running with coverage:
```bash
uv run coverage run --data-file=.coverage.run_llvm_c_tests run_llvm_c_tests.py --use-python
```

Each lit test invocation creates a unique coverage data file (due to `--parallel-mode`).
After running, combine all coverage data:
```bash
uv run coverage combine
uv run coverage html
```

### llvm-c-test-wrapper.py

The wrapper script is used only for command logging. It:

1. **Logs commands**: When `LLVM_C_TEST_LOG` is set, logs all executed commands with timestamps
2. **Sets up path**: Ensures `llvm_c_test` module is importable from the project root

## Adding New Tests

1. Create a `.ll` or `.test` file in `llvm-c/llvm-c-test/inputs/`
2. Add RUN line with the command to test
3. Add CHECK lines for expected output (if applicable)
4. Run `uv run run_llvm_c_tests.py` to verify

Example test file:
```llvm
; RUN: llvm-as < %s | llvm-c-test --echo | FileCheck %s

; Test new instruction type
; CHECK: define void @test()
define void @test() {
entry:
  ; CHECK: %result = add i32 1, 2
  %result = add i32 1, 2
  ret void
}
```

## Test Results

All 23 tests should pass:

```
PASS: llvm-c-test :: ARM/disassemble.test
PASS: llvm-c-test :: X86/disassemble.test
PASS: llvm-c-test :: add_named_metadata_operand.ll
PASS: llvm-c-test :: atomics.ll
PASS: llvm-c-test :: calc.test
PASS: llvm-c-test :: callsite_attributes.ll
PASS: llvm-c-test :: debug_info_new_format.ll
PASS: llvm-c-test :: di-type-get-name.ll
PASS: llvm-c-test :: echo.ll
PASS: llvm-c-test :: empty.ll
PASS: llvm-c-test :: float_ops.ll
PASS: llvm-c-test :: freeze.ll
PASS: llvm-c-test :: function_attributes.ll
PASS: llvm-c-test :: functions.ll
PASS: llvm-c-test :: get-di-tag.ll
PASS: llvm-c-test :: globals.ll
PASS: llvm-c-test :: invalid-bitcode.test
PASS: llvm-c-test :: invoke.ll
PASS: llvm-c-test :: is_a_value_as_metadata.ll
PASS: llvm-c-test :: memops.ll
PASS: llvm-c-test :: objectfile.ll
PASS: llvm-c-test :: replace_md_operand.ll
PASS: llvm-c-test :: set_metadata.ll

Total Discovered Tests: 23
  Passed: 23 (100.00%)
```

## Troubleshooting

### Test Failures

1. **FileCheck mismatch**: Output differs from expected
   - Run with `-v` to see actual vs expected output
   - Check for whitespace or formatting differences

2. **Tool not found**: Missing LLVM tools
   - Ask the user to configure the CMake project once, this will yield .llvm-prefix

3. **Python import error**: Module not built
   - Run `uv sync`

### Debugging

```bash
# Run single test with verbose output
$(cat .llvm-prefix)/bin/lit llvm-c/llvm-c-test/inputs/echo.ll -v -a

# Test Python module directly
echo 'define i32 @main() { ret i32 0 }' | ./llvm-bin llvm-as | \
  uv run python -m llvm_c_test --echo
```
