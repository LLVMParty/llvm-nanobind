# llvm-c-test Python Port - Completion Summary

**Project:** Python port of LLVM's `llvm-c-test` using llvm-nanobind  
**Completed:** December 17, 2025  
**Result:** 22/22 commands, 23/23 lit tests passing

## Goal & Motivation

Port the LLVM-C test suite (`llvm-c-test`) from C/C++ to Python as a **drop-in replacement** that produces byte-identical output. This serves four purposes:

1. **Validate binding completeness** - Exercises nearly all LLVM-C APIs
2. **Test binding correctness** - Golden-master comparison ensures identical behavior
3. **Living documentation** - Python port serves as comprehensive API usage examples
4. **Continuous regression testing** - Any binding regression is caught by lit tests

---

## Architecture

### Package Structure

```
llvm_c_test/
├── __init__.py
├── __main__.py          # CLI entry point (uv run python -m llvm_c_test)
├── main.py              # Command dispatcher
├── helpers.py           # tokenize_stdin(), shared utilities
├── targets.py           # --targets-list
├── calc.py              # --calc (RPN calculator)
├── module_ops.py        # --module-dump, --module-list-{functions,globals}, lazy variants
├── attributes.py        # --test-{function,callsite}-attributes
├── metadata.py          # --{add-named-metadata-operand,set-metadata,...}
├── debuginfo.py         # --test-dibuilder, --get-di-tag, --di-type-get-name
├── diagnostic.py        # --test-diagnostic-handler
├── disassemble.py       # --disassemble
├── object_file.py       # --object-list-{sections,symbols}
└── echo.py              # --echo (module cloning, ~1400 lines)
```

### Command Reference

| Command | Module | Description |
|---------|--------|-------------|
| `--targets-list` | `targets.py` | List LLVM targets with JIT/ASM indicators |
| `--calc` | `calc.py` | RPN calculator generating LLVM IR |
| `--module-dump` | `module_ops.py` | Parse bitcode, print IR |
| `--lazy-module-dump` | `module_ops.py` | Lazy-load bitcode, print IR |
| `--new-module-dump` | `module_ops.py` | Parse with new diagnostic API |
| `--lazy-new-module-dump` | `module_ops.py` | Lazy-load with new API |
| `--module-list-functions` | `module_ops.py` | List functions with stats |
| `--module-list-globals` | `module_ops.py` | List global variables |
| `--test-function-attributes` | `attributes.py` | Enumerate function attributes |
| `--test-callsite-attributes` | `attributes.py` | Enumerate call site attributes |
| `--add-named-metadata-operand` | `metadata.py` | Test named metadata API |
| `--set-metadata` | `metadata.py` | Test instruction metadata |
| `--replace-md-operand` | `metadata.py` | Replace metadata operands |
| `--is-a-value-as-metadata` | `metadata.py` | Test ValueAsMetadata |
| `--test-dibuilder` | `debuginfo.py` | DIBuilder comprehensive test |
| `--get-di-tag` | `debuginfo.py` | Get DWARF tag from DI node |
| `--di-type-get-name` | `debuginfo.py` | Get type name from DI |
| `--test-diagnostic-handler` | `diagnostic.py` | Test diagnostic callbacks |
| `--disassemble` | `disassemble.py` | Disassemble hex bytes |
| `--object-list-sections` | `object_file.py` | List object file sections |
| `--object-list-symbols` | `object_file.py` | List object file symbols |
| `--echo` | `echo.py` | Clone module via C API |

---

## Technical Insights

### Nanobind Type Compatibility

Lambda parameters with `unsigned` type don't accept Python `int` values. Use `int` in signature and cast internally:

```cpp
// WRONG - doesn't accept Python int:
[](const LLVMFunctionWrapper &func, unsigned idx) { ... }

// CORRECT - cast internally:
[](const LLVMFunctionWrapper &func, int idx) {
    return LLVMGetAttributeCountAtIndex(func.m_ref, static_cast<unsigned>(idx));
}
```

### Value Equality and Hashing

For the echo command's value mapping to work, `Value`, `Type`, `BasicBlock`, and `NamedMDNode` wrappers needed equality operators:

```cpp
.def("__eq__", [](const LLVMValueWrapper &self, const LLVMValueWrapper &other) {
    return self.m_ref == other.m_ref;
})
.def("__ne__", [](const LLVMValueWrapper &self, const LLVMValueWrapper &other) {
    return self.m_ref != other.m_ref;
})
.def("__hash__", [](const LLVMValueWrapper &self) {
    return std::hash<LLVMValueRef>{}(self.m_ref);
})
```

This enables using LLVM objects as dictionary keys for source-to-clone mappings.

### Memory Safety: Module/Context Lifetime

**Problem:** If a Module outlives its Context (due to Python's GC order), calling `LLVMDisposeModule` crashes.

**Solution:** Module destructor checks context validity token before disposal:

```cpp
~LLVMModuleWrapper() {
    if (m_ref) {
        if (!m_context_token || !m_context_token->is_valid()) {
            // Context already destroyed - can't safely dispose
            // Print warning, leak the module (better than crash)
            return;
        }
        LLVMDisposeModule(m_ref);
    }
}
```

See `devdocs/memory-model.md` for the full lifetime management strategy.

### Echo Command Architecture

The `--echo` command (~1400 lines Python) clones an entire module using only C API calls:

1. **TypeCloner** - Maps source types to destination types (handles all 20+ type kinds)
2. **clone_constant()** - Recursively clones constants (15+ kinds including ptrauth)
3. **FunCloner** - Clones function bodies instruction by instruction:
   - Handles all 60+ instruction opcodes
   - Preserves instruction flags (nsw, nuw, exact, fast-math, etc.)
   - Clones operand bundles for calls/invokes
   - Handles exception handling (landingpad, catchswitch, catchpad, etc.)
4. **declare_symbols() / clone_symbols()** - Clones globals, aliases, IFuncs

**Critical Pattern:** Proper cleanup order prevents use-after-free:
```python
# Clone must be disposed BEFORE original to avoid referencing deleted types
clone.dispose()
original.dispose()
```

---

## Testing

### Running Lit Tests

```bash
# Run all 23 lit tests with C binary
uv run run_llvm_c_tests.py

# Run with Python implementation
uv run run_llvm_c_tests.py --use-python

# Verbose output
uv run run_llvm_c_tests.py -v

# With coverage
uv run coverage run run_llvm_c_tests.py --use-python
uv run coverage combine
uv run coverage report
```

### Test Categories

| Category | Tests | Commands |
|----------|-------|----------|
| Echo (module cloning) | `echo.ll`, `atomics.ll`, `float_ops.ll`, `freeze.ll`, `invoke.ll`, `memops.ll` | `--echo` |
| Module operations | `functions.ll`, `globals.ll`, `empty.ll` | `--module-*` |
| Attributes | `function_attributes.ll`, `callsite_attributes.ll` | `--test-*-attributes` |
| Metadata | `add_named_metadata_operand.ll`, `set_metadata.ll`, `replace_md_operand.ll`, `is_a_value_as_metadata.ll` | `--*-metadata*` |
| Debug info | `debug_info_new_format.ll`, `get-di-tag.ll`, `di-type-get-name.ll` | `--test-dibuilder`, `--get-di-tag`, `--di-type-get-name` |
| Calculator | `calc.test` | `--calc` |
| Disassembly | `X86/disassemble.test`, `ARM/disassemble.test` | `--disassemble` |
| Object files | `objectfile.ll` | `--object-list-*` |
| Error handling | `invalid-bitcode.test` | Error paths |

### Lit Configuration

Tests are in `llvm-c/llvm-c-test/inputs/`. The `lit.cfg.py` substitutes `llvm-c-test` with either the C binary or Python module based on `LLVM_C_TEST_CMD` environment variable.

---

## Known Limitations

1. **No attribute string support** - Only enum attributes are copied, string attributes are skipped (not needed for lit tests)

2. **Architecture-specific tests** - Disassembly tests require X86/ARM targets to be built

3. **Memory model is flat** - Only context-level token invalidation; module/function/block hierarchy not tracked individually (see `devdocs/memory-model.md` for future design)

---

## Bindings Added

Approximately 300+ new LLVM-C API bindings were added across all phases:

- **Target APIs** - Initialization, iteration, capability queries
- **BitReader APIs** - Legacy and new bitcode parsing, lazy loading
- **Memory Buffer APIs** - stdin reading, file loading
- **Attribute APIs** - Function and callsite attribute enumeration
- **Metadata APIs** - Named metadata, instruction metadata, ValueAsMetadata
- **Debug Info APIs** - DIBuilder, DWARF tags, type names
- **Disassembler APIs** - Multi-architecture disassembly
- **Object File APIs** - Section/symbol iteration
- **Type APIs** - All type kinds including target extensions
- **Constant APIs** - All constant kinds, constant expressions, ptrauth
- **Instruction APIs** - All opcodes, flags, operand bundles
- **Global APIs** - Aliases, IFuncs, unnamed address, personality

---

## Files Reference

| File | Purpose |
|------|---------|
| `llvm_c_test/` | Python implementation package |
| `src/llvm-nanobind.cpp` | C++ bindings (~3000+ lines total) |
| `run_llvm_c_tests.py` | Lit test runner script |
| `llvm-c/llvm-c-test/inputs/` | Lit test files |
| `devdocs/memory-model.md` | Memory/lifetime management docs |
