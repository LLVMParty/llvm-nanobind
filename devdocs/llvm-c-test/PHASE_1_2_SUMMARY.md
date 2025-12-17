# llvm-c-test Python Port - Phases 1 & 2 Complete ✅

**Project:** Python port of LLVM's llvm-c-test using llvm-nanobind  
**Date:** December 16, 2025  
**Status:** 14/22 commands complete (64%), 39/~235 bindings (17%)

## Completed Phases

### ✅ Phase 1: Foundation (8 commands)

**Commands:**
1. `--targets-list` - List LLVM targets with JIT support indicators
2. `--calc` - RPN calculator generating LLVM IR
3. `--module-dump` - Parse bitcode and print IR
4. `--lazy-module-dump` - Lazy-load bitcode and print IR
5. `--new-module-dump` - Parse bitcode using new diagnostic API
6. `--lazy-new-module-dump` - Lazy-load using new API
7. `--module-list-functions` - List functions with stats and call graph
8. `--module-list-globals` - List global variables with types

**Bindings Added (30):**
- Target API (13): Full target initialization and iteration
- Memory Buffer (4): stdin reading, buffer access
- BitReader (4): Legacy and new bitcode parsing
- Instruction Iteration (7): Navigate instructions in basic blocks
- Core additions (2): LLVMBuildBinOp, LLVMOpcode enum

**Test Results:**
```bash
# All tests passing ✅
uv run llvm-c-test --targets-list
uv run llvm-c-test --calc < calc.test
llvm-as < globals.ll | uv run llvm-c-test --module-list-globals
llvm-as < functions.ll | uv run llvm-c-test --module-list-functions
llvm-as < empty.ll | uv run llvm-c-test --module-dump
```

### ✅ Phase 2: Metadata & Attributes (6 commands)

**Commands:**
1. `--test-function-attributes` - Enumerate function attributes
2. `--test-callsite-attributes` - Enumerate call site attributes
3. `--add-named-metadata-operand` - Test named metadata API
4. `--set-metadata` - Test instruction metadata API
5. `--replace-md-operand` - Test metadata operand replacement
6. `--is-a-value-as-metadata` - Test ValueAsMetadata checking

**Bindings Added (9):**
- Attribute constants (2): AttributeReturnIndex, AttributeFunctionIndex
- Attribute functions (2): get_attribute_count_at_index, get_callsite_attribute_count
- Metadata functions (5): md_node, add_named_metadata_operand, set_metadata, get_md_kind_id, is_a_value_as_metadata
- Helper functions (2): delete_instruction, get_module_context

**Test Results:**
```bash
# All tests passing ✅ (silent tests - no output expected)
llvm-as < function_attributes.ll | uv run llvm-c-test --test-function-attributes
llvm-as < callsite_attributes.ll | uv run llvm-c-test --test-callsite-attributes
uv run llvm-c-test --add-named-metadata-operand
uv run llvm-c-test --set-metadata
uv run llvm-c-test --is-a-value-as-metadata
```

## Project Structure

```
llvm-nanobind/
├── src/
│   └── llvm-nanobind.cpp          # C++ bindings (2800+ lines, +~200 new)
├── llvm_c_test/                    # Python package (NEW)
│   ├── __init__.py
│   ├── __main__.py                 # CLI entry point
│   ├── main.py                     # Command dispatcher
│   ├── helpers.py                  # Utilities (tokenize_stdin)
│   ├── targets.py                  # Phase 1: --targets-list
│   ├── calc.py                     # Phase 1: --calc
│   ├── module_ops.py               # Phase 1: module commands
│   ├── attributes.py               # Phase 2: attribute tests
│   └── metadata.py                 # Phase 2: metadata tests
└── devdocs/llvm-c-test/
    ├── plan.md                     # Implementation plan
    └── progress.md                 # Progress tracking (UPDATED)
```

## Technical Highlights

### Nanobind Type Compatibility Fix
**Problem:** Lambda parameters with `unsigned` type don't accept Python `int` values  
**Solution:** Use `int` in signature, cast to `unsigned` when calling C API
```cpp
// Before (doesn't work):
[](const LLVMFunctionWrapper &func, unsigned idx) { ... }

// After (works):
[](const LLVMFunctionWrapper &func, int idx) {
    return LLVMGetAttributeCountAtIndex(func.m_ref, static_cast<unsigned>(idx));
}
```

### Class Hierarchy
- Function properly inherits from Value in Python
- `isinstance(func, llvm.Value)` works correctly
- Nanobind handles C++ inheritance seamlessly

### Build Process
```bash
# Build with uv (automatic rebuild)
uv sync

# Run tests
uv run llvm-c-test <command>
```

## Remaining Work

### Phase 3: Complex (Echo/Debug) - 5 commands
- `--echo` - Full IR traversal and printing
- `--diagnostic` - Diagnostic handler testing
- `--get-di-tag` - Debug info tag extraction
- Other debug info commands

**Estimated Bindings:** ~150 (requires extensive debug info API)

### Phase 4: Platform-Specific - 3 commands
- `--disassemble` - Disassembly testing
- `--objectfile` - Object file parsing
- Other platform commands

**Estimated Bindings:** ~20

## Statistics

- **Lines of C++ Added:** ~200 (bindings)
- **Lines of Python Added:** ~600 (commands + tests)
- **Commands Working:** 14/22 (64%)
- **API Bindings:** 39/~235 (17%)
- **Test Success Rate:** 100% (14/14 commands tested and passing)

## Next Steps

1. Implement Phase 3 (echo command is most complex)
2. Add debug info bindings as needed
3. Implement Phase 4 platform-specific commands
4. Set up lit test integration for automated testing

## References

- Original C code: `llvm-c/llvm-c-test/`
- Lit tests: `llvm-c/llvm-c-test/inputs/`
- Plan: `devdocs/llvm-c-test/plan.md`
- Progress: `devdocs/llvm-c-test/progress.md`
