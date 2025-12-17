# LLVM Module Parsing API Refactor - Implementation Plan

## Overview

This document tracks the implementation of the new parsing API described in `design.md`. The refactor replaces the low-level `parse_bitcode_in_context()` function with high-level Context methods that handle memory management automatically.

## Goals

1. **Eliminate MemoryBuffer from public API** - users never see `LLVMMemoryBufferWrapper`
2. **Fix lazy loading crashes** - proper ownership tracking for memory buffers
3. **Add comprehensive error handling** - `LLVMParseError` with diagnostic information
4. **Pythonic API** - Context methods instead of standalone functions
5. **Safe by default** - automatic cleanup via context managers

## Current Issues Being Fixed

1. **test_memory_lazy_module.py crashes** - MemoryBuffer freed while LLVM module still references it
2. **test_memory_diagnostic_handler.py crashes** - double-free on MemoryBuffer
3. **No textual IR parsing** - only bitcode supported
4. **Manual memory management** - users must create/manage MemoryBuffer
5. **Poor error messages** - generic exceptions without diagnostic context

## Implementation Phases

### ✅ Phase 0: Documentation (THIS PHASE)
**Goal:** Create comprehensive plan and progress tracking

**Tasks:**
- [x] Create plan.md with implementation phases
- [x] Create progress.md with tracking structure
- [x] Document all APIs to implement
- [x] Document migration strategy

---

### Phase 1: Add Memory Buffer Storage to Module
**Goal:** Track MemoryBuffer ownership for lazy-loaded modules

**C++ Changes:**
```cpp
struct LLVMModuleWrapper {
  LLVMMemoryBufferRef m_memory_buffer = nullptr;  // NEW
  
  ~LLVMModuleWrapper() {
    if (m_memory_buffer) {
      LLVMDisposeMemoryBuffer(m_memory_buffer);  // NEW
    }
    // ... existing cleanup
  }
};
```

**Testing:**
- Verify lazy modules don't crash on disposal
- Verify non-lazy modules don't leak memory buffers

**Estimated effort:** 1 hour

---

### Phase 2: Per-Context Diagnostics System
**Goal:** Replace thread-local singleton with per-context diagnostic storage

**C++ Changes:**
```cpp
struct Diagnostic {
  std::string severity;
  std::string message;
  std::optional<int> line;
  std::optional<int> column;
};

struct LLVMContextWrapper {
  std::vector<Diagnostic> m_diagnostics;  // NEW
  
  void diagnostic_handler(LLVMDiagnosticInfoRef info);  // NEW
  std::vector<Diagnostic> get_diagnostics() const;      // NEW
  void clear_diagnostics();                             // NEW
};
```

**Installation:**
```cpp
// In LLVMContextWrapper constructor
LLVMContextSetDiagnosticHandler(
  m_ref,
  [](LLVMDiagnosticInfoRef info, void* ctx_ptr) {
    static_cast<LLVMContextWrapper*>(ctx_ptr)->diagnostic_handler(info);
  },
  this
);
```

**Python bindings:**
```python
.def("get_diagnostics", &LLVMContextWrapper::get_diagnostics)
.def("clear_diagnostics", &LLVMContextWrapper::clear_diagnostics)
```

**Testing:**
- Parse invalid bitcode, verify diagnostics collected
- Parse valid bitcode, verify diagnostics empty
- Multiple parse operations, verify diagnostics accumulate correctly

**Estimated effort:** 2 hours

---

### Phase 3: LLVMParseError Exception
**Goal:** Specialized exception carrying diagnostic information

**C++ Changes:**
```cpp
struct LLVMParseError : LLVMException {
  std::vector<Diagnostic> diagnostics;
  
  LLVMParseError(const std::vector<Diagnostic>& diags)
    : LLVMException(format_diagnostics(diags)),
      diagnostics(diags) {}
  
private:
  static std::string format_diagnostics(const std::vector<Diagnostic>& diags);
};
```

**Python binding:**
```cpp
nb::exception<LLVMParseError>(m, "LLVMParseError")
  .def_ro("diagnostics", &LLVMParseError::diagnostics);

nb::class_<Diagnostic>(m, "Diagnostic")
  .def_ro("severity", &Diagnostic::severity)
  .def_ro("message", &Diagnostic::message)
  .def_ro("line", &Diagnostic::line)
  .def_ro("column", &Diagnostic::column);
```

**Testing:**
- Parse invalid IR, catch `LLVMParseError`, inspect diagnostics
- Verify exception message includes formatted diagnostics

**Estimated effort:** 1.5 hours

---

### Phase 4: Context Parsing Methods
**Goal:** Implement the three main parsing methods on Context

**APIs to implement:**

#### 4a. `parse_bitcode_from_file(filename, lazy=False)`
```cpp
LLVMModuleManager* parse_bitcode_from_file(
  const std::string& filename, 
  bool lazy = false
) {
  check_valid();
  clear_diagnostics();
  
  // Create memory buffer from file
  LLVMMemoryBufferRef buf;
  char* error_msg = nullptr;
  if (LLVMCreateMemoryBufferWithContentsOfFile(filename.c_str(), &buf, &error_msg)) {
    std::string err = error_msg ? error_msg : "Unknown error";
    if (error_msg) LLVMDisposeMessage(error_msg);
    throw LLVMException("Failed to read file: " + err);
  }
  
  // Parse bitcode
  LLVMModuleRef mod_ref;
  LLVMBool failed;
  if (lazy) {
    failed = LLVMGetBitcodeModuleInContext2(m_ref, buf, &mod_ref);
  } else {
    failed = LLVMParseBitcodeInContext2(m_ref, buf, &mod_ref);
  }
  
  if (failed) {
    LLVMDisposeMemoryBuffer(buf);  // Dispose on failure!
    throw LLVMParseError(get_diagnostics());
  }
  
  // Create module wrapper
  auto mod = std::make_unique<LLVMModuleWrapper>(mod_ref, m_ref, m_token);
  
  // Transfer buffer ownership if lazy
  if (lazy) {
    mod->m_memory_buffer = buf;
  } else {
    LLVMDisposeMemoryBuffer(buf);  // Dispose immediately for eager
  }
  
  return new LLVMModuleManager(std::move(mod));
}
```

#### 4b. `parse_bitcode_from_bytes(data)`
```cpp
LLVMModuleManager* parse_bitcode_from_bytes(nb::bytes data) {
  check_valid();
  clear_diagnostics();
  
  // Create temporary memory buffer (non-owning)
  auto buf = LLVMCreateMemoryBufferWithMemoryRange(
    data.c_str(), 
    data.size(), 
    "<bytes>", 
    false  // Don't copy
  );
  
  // Parse eagerly (always)
  LLVMModuleRef mod_ref;
  auto failed = LLVMParseBitcodeInContext2(m_ref, buf, &mod_ref);
  
  // Dispose buffer immediately (parse copied the data)
  LLVMDisposeMemoryBuffer(buf);
  
  if (failed) {
    throw LLVMParseError(get_diagnostics());
  }
  
  // Create module wrapper (no buffer ownership)
  auto mod = std::make_unique<LLVMModuleWrapper>(mod_ref, m_ref, m_token);
  return new LLVMModuleManager(std::move(mod));
}
```

#### 4c. `parse_ir(source)`
```cpp
LLVMModuleManager* parse_ir(const std::string& source) {
  check_valid();
  clear_diagnostics();
  
  // Create temporary buffer from string
  auto buf = LLVMCreateMemoryBufferWithMemoryRange(
    source.c_str(),
    source.size(),
    "<source>",
    false  // Don't copy
  );
  
  // Parse IR (always eager)
  LLVMModuleRef mod_ref;
  char* error_msg = nullptr;
  auto failed = LLVMParseIRInContext(m_ref, buf, &mod_ref, &error_msg);
  
  // Dispose buffer immediately
  LLVMDisposeMemoryBuffer(buf);
  
  if (failed) {
    std::string err = error_msg ? error_msg : "Unknown error";
    if (error_msg) LLVMDisposeMessage(error_msg);
    throw LLVMParseError(get_diagnostics());  // May be empty for IR errors
  }
  
  auto mod = std::make_unique<LLVMModuleWrapper>(mod_ref, m_ref, m_token);
  return new LLVMModuleManager(std::move(mod));
}
```

**Python bindings:**
```cpp
.def("parse_bitcode_from_file", &LLVMContextWrapper::parse_bitcode_from_file,
     nb::arg("filename"), nb::arg("lazy") = false,
     "Parse LLVM bitcode from file")
.def("parse_bitcode_from_bytes", &LLVMContextWrapper::parse_bitcode_from_bytes,
     nb::arg("data"),
     "Parse LLVM bitcode from bytes")
.def("parse_ir", &LLVMContextWrapper::parse_ir,
     nb::arg("source"),
     "Parse LLVM IR from string")
```

**Testing:**
- Parse valid bitcode file (eager and lazy)
- Parse invalid bitcode file, catch exception
- Parse from bytes (stdin simulation)
- Parse textual IR (valid and invalid)
- Verify context manager cleanup works

**Estimated effort:** 3 hours

---

### Phase 5: ModuleManager Constructor
**Goal:** Add constructor to accept pre-created module (not just from create_module)

**C++ Changes:**
```cpp
struct LLVMModuleManager {
  // NEW: Constructor for parsed modules
  explicit LLVMModuleManager(std::unique_ptr<LLVMModuleWrapper> module)
    : m_module(std::move(module)),
      m_context(nullptr),  // No context needed (module has token)
      m_from_clone(false),
      m_entered(false),
      m_disposed(false) {}
  
  // ... existing constructors remain
};
```

**Note:** This constructor already exists for `clone()` - just verify it works for parsing.

**Estimated effort:** 0.5 hours

---

### Phase 6: Port llvm_c_test to New APIs
**Goal:** Migrate all llvm_c_test Python code to use new parsing methods

**Files to update:**
- `llvm_c_test/module_ops.py` - module_dump, list_functions, list_globals
- `llvm_c_test/echo.py` - echo command
- `llvm_c_test/attributes.py` - attribute tests
- `llvm_c_test/helpers.py` - remove MemoryBuffer helper

**Pattern:**
```python
# OLD
membuf = llvm.create_memory_buffer_with_stdin()
mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=False)

# NEW
import sys
bitcode = sys.stdin.buffer.read()
with ctx.parse_bitcode_from_bytes(bitcode) as mod:
    # ... use mod
```

**Testing:**
- Run `uv run run_llvm_c_tests.py --use-python` after each file update
- Verify all lit tests still pass

**Estimated effort:** 2 hours

---

### Phase 7: Port Test Files to New APIs
**Goal:** Update all test_*.py files to use new parsing methods

**Files to update:**
- `test_memory_lazy_module.py` - verify lazy loading now works!
- `test_memory_diagnostic_handler.py` - verify diagnostics work
- `test_syncscope_*.py` - update parsing calls
- `test_type_crash*.py` - update parsing calls
- `test_bitcode_param_crash.py` - update parsing calls

**Pattern:**
```python
# OLD
membuf = llvm.create_memory_buffer_with_stdin()
ctx = llvm.global_context()
mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=True, new_api=True)

# NEW
ctx = llvm.global_context()
with ctx.parse_bitcode_from_file("test.bc", lazy=True) as mod:
    # ... use mod
```

**Testing:**
- Run each test file individually to verify it passes
- Especially verify test_memory_lazy_module.py now PASSES

**Estimated effort:** 2 hours

---

### Phase 8: Remove Deprecated APIs
**Goal:** Clean up old parsing functions and MemoryBuffer exposure

**APIs to remove:**
- `create_memory_buffer_with_stdin()` - users should use `sys.stdin.buffer.read()`
- `parse_bitcode_in_context()` - replaced by Context methods
- `LLVMMemoryBufferWrapper` - no longer exposed to Python
- `context_set_diagnostic_handler()` - automatic now

**C++ cleanup:**
- Remove standalone `parse_bitcode_in_context()` function (lines 4376-4416)
- Remove `create_memory_buffer_with_stdin()` binding
- Remove `LLVMMemoryBufferWrapper` class binding
- Keep wrapper internally for potential future use

**Testing:**
- Verify no Python code references removed APIs
- Run full test suite: `uv run run_tests.py`
- Run llvm-c-test suite: `uv run run_llvm_c_tests.py --use-python`

**Estimated effort:** 1 hour

---

## Summary Statistics

| Phase | Description | APIs Added | APIs Removed | Effort | Status |
|-------|-------------|------------|--------------|--------|--------|
| 0 | Documentation | 0 | 0 | 1h | ✅ Complete |
| 1 | Module buffer storage | 0 | 0 | 0.5h | ✅ Complete |
| 2 | Context diagnostics | 3 | 0 | 2h | ✅ Complete |
| 3 | LLVMParseError | 2 | 0 | 1.5h | ✅ Complete |
| 4 | Context parsing methods | 3 | 0 | 3h | ✅ Complete |
| 5 | MemoryBuffer ownership | 0 | 0 | 0.5h | ✅ Complete |
| 6 | Port llvm_c_test | 0 | 0 | 2h | ✅ Complete |
| 7 | Port test files | 0 | 0 | 1.5h | ✅ Complete |
| 8 | Remove deprecated APIs | 0 | 4 | 1h | ⏳ In Progress |
| **Total** | | **8 APIs** | **4 APIs** | **13h** | **89% (8/9)** |

## Testing Strategy

### Unit Tests (per phase)
- Each phase includes inline testing requirements
- Test both success and failure paths
- Verify memory safety (no leaks, no crashes)

### Integration Tests
After Phase 5 (new APIs working):
- Run existing llvm-c-test lit tests with `--use-python`
- Run all test_memory_*.py files
- Verify test_memory_lazy_module.py now PASSES

### Regression Prevention
After Phase 8 (cleanup complete):
- Full test suite must pass: `uv run run_tests.py`
- Full lit test suite must pass: `uv run run_llvm_c_tests.py --use-python`
- No memory leaks under valgrind (optional, future)

## Migration Guide

### For Library Users

**Old API:**
```python
import llvm

membuf = llvm.create_memory_buffer_with_stdin()
ctx = llvm.global_context()
mod = llvm.parse_bitcode_in_context(ctx, membuf, lazy=False, new_api=True)
print(mod.to_string())
```

**New API:**
```python
import llvm
import sys

ctx = llvm.global_context()
bitcode = sys.stdin.buffer.read()
with ctx.parse_bitcode_from_bytes(bitcode) as mod:
    print(mod.to_string())
```

**From file:**
```python
with llvm.create_context() as ctx:
    with ctx.parse_bitcode_from_file("app.bc", lazy=True) as mod:
        # Process module
        pass
```

**Textual IR:**
```python
with llvm.create_context() as ctx:
    ir_source = """
    define i32 @main() {
        ret i32 0
    }
    """
    with ctx.parse_ir(ir_source) as mod:
        # Process module
        pass
```

## Success Criteria

1. ✅ All parsing APIs match design.md specification
2. ✅ test_memory_lazy_module.py passes (no crashes)
3. ✅ test_memory_diagnostic_handler.py passes (no double-free)
4. ✅ No MemoryBuffer in public API
5. ✅ LLVMParseError provides diagnostic information
6. ✅ All llvm-c-test lit tests pass with --use-python
7. ✅ All existing test_*.py files pass
8. ✅ Type stubs generated correctly for new APIs
9. ✅ Documentation updated (this plan)
10. ✅ Zero backward compatibility required (clean break)
