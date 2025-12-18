# Fixing Python llvm-c-test Implementation

## Goal and Scope

Fixed all 23 lit tests to pass with the Python `llvm_c_test` implementation, achieving 100% parity with the C `llvm-c-test` binary. The work involved fixing binding bugs, extending APIs, and correcting test code issues across 5 phases.

## Key Architectural Decisions

### Borrowed Context Wrappers

Added support for non-owning ("borrowed") context wrappers to fix `get_module_context()`:

```cpp
// Non-owning wrapper shares validity token with owning context
LLVMContextWrapper(LLVMContextRef ref, std::shared_ptr<ValidityToken> token)
    : m_ref(ref), m_token(std::move(token)), m_global(false), m_borrowed(true) {}
```

Key properties:
- `m_borrowed = true` prevents disposal in destructor
- Shares validity token with owning wrapper
- Enables correct syncscope handling when cloning modules

### Global Diagnostic Registry

Replaced per-wrapper diagnostic storage with a thread-safe global registry:

```cpp
struct DiagnosticRegistry {
  std::mutex mutex;
  std::unordered_map<LLVMContextRef, std::vector<Diagnostic>> diagnostics;
  // ...
};
```

Benefits:
- Both owning and borrowed wrappers access the same diagnostics
- Thread-safe via mutex
- Automatic cleanup when owning context is destroyed

### Extended DIBuilder Bindings

Extended `dibuilder_create_struct_type` to expose all C API parameters:

```python
llvm.dibuilder_create_struct_type(
    dib, scope, name, file, line, size, align, flags,
    derived_from=None,      # NEW
    elements=[],            # NEW
    runtime_lang=0,         # NEW
    vtable_holder=None,     # NEW
    unique_id=""            # NEW
)
```

## Technical Insights and Gotchas

### Syncscope IDs are Context-Specific

Custom syncscopes like `"agent"` are registered per-context. When cloning modules:
- Both source and destination must be in the same context
- `get_module_context()` must return the actual context, not global
- Syncscope IDs from source are valid in destination only if contexts match

**Symptom:** Crash in `writeSyncScope` when printing module with custom syncscopes.

### Module ID Differs by Parse Method

- `LLVMCreateMemoryBufferWithSTDIN` → module ID is `<stdin>`
- `LLVMCreateMemoryBufferWithMemoryRangeCopy` → module ID is `<bytes>`

**Fix:** Set module name to `"<stdin>"` after parsing for compatibility.

### Lazy vs Eager Bitcode Loading

- Eager: `LLVMParseBitcodeInContext2` - fully materializes module
- Lazy: `LLVMGetBitcodeModuleInContext2` - defers function body loading

The `functions.ll` test requires lazy loading to test `LLVMMaterializeAll`.

### LLVM Upstream Test Bugs

Found and fixed 2 bugs in vendored C test code (marked for upstream PR):

1. **Enumerator name bug:** Passes `"Test_B"` with `strlen("Test_C")` → produces wrong name
2. **Forward decl length bug:** Passes `"Class1"` with length 5 → truncates to `"Class"`

## API Summary

### New/Modified Bindings

| Function | Change |
|----------|--------|
| `get_module_context(mod)` | Returns borrowed wrapper for module's actual context |
| `dibuilder_create_struct_type(...)` | Added `derived_from`, `elements`, `runtime_lang`, `vtable_holder`, `unique_id` |
| `parse_bitcode_from_bytes(data, lazy)` | Added `lazy` parameter |

### New Internal Components

| Component | Purpose |
|-----------|---------|
| `DiagnosticRegistry` | Global thread-safe diagnostic storage keyed by context ref |
| `m_borrowed` flag | Marks non-owning context wrappers |

## Test Commands

```bash
# Run Python implementation
uv run run_llvm_c_tests.py --use-python

# Run C implementation
uv run run_llvm_c_tests.py

# Run syncscope regression test
uv run python tests/regressions/test_syncscope_crash.py
```

## References

- `memory-model.md` - Documents context borrowing and diagnostic registry
- `lit-tests.md` - Lit test infrastructure documentation
- `tests/regressions/test_syncscope_crash.py` - Regression test for syncscope fix
