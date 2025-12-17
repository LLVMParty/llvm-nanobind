# LLVM Module Parsing API Design

## Overview

This document describes the API for parsing LLVM IR (both bitcode and textual formats) with integration into the existing validity token lifetime management system.

## Core Parsing Methods

### `Context.parse_bitcode_from_file(filename: str, lazy: bool = False) -> ModuleManager`

Parse LLVM bitcode from a file with optional lazy loading.

**Parameters:**
- `filename` (str): Path to bitcode file
- `lazy` (bool): If `True`, only parse module header; load function bodies on-demand

**Returns:**
- `ModuleManager`: Context manager for module lifecycle

**Memory behavior:**
- Files >16KB automatically use mmap (OS-managed paging)
- Files <16KB read fully into memory
- File descriptors closed immediately after loading
- With `lazy=True`, `MemoryBuffer` kept alive by `Module` wrapper (via `m_memory_buffer` member)
- With `lazy=False`, `MemoryBuffer` disposed immediately after parsing

**Validity tokens:**
```cpp
struct LLVMModule {
    LLVMModuleRef m_ref;
    std::shared_ptr<ValidityToken> m_context_token;  // Parent context
    std::shared_ptr<ValidityToken> m_token;           // This module's token
    LLVMMemoryBufferRef m_memory_buffer = nullptr;    // Only if lazy
    
    ~LLVMModule() {
        m_token->invalidate();  // Invalidate all functions/globals
        if (m_memory_buffer)
            LLVMDisposeMemoryBuffer(m_memory_buffer);
        LLVMDisposeModule(m_ref);
    }
};
```

**Example:**
```python
with ctx.create_context() as ctx:
    # Small eager module
    with ctx.parse_bitcode_from_file("small.bc") as mod:
        func = mod.get_function("main")
    
    # Large lazy module
    with ctx.parse_bitcode_from_file("huge.bc", lazy=True) as mod:
        # Only 'main' parsed, rest on-demand
        main = mod.get_function("main")
```

---

### `Context.parse_bitcode_from_bytes(data: bytes | bytearray) -> ModuleManager`

Parse LLVM bitcode from bytes with eager parsing (no lazy option).

**Parameters:**
- `data` (bytes | bytearray): Bitcode data

**Returns:**
- `ModuleManager`: Context manager for module lifecycle

**Memory behavior:**
- **Always eager parsing** for safety (Python GC makes lazy unsafe)
- Creates temporary `MemoryBuffer` from bytes pointer
- Buffer disposed immediately after parsing
- No reference to `data` retained

**Implementation note:**
```cpp
// C++ side (nanobind)
nb::class_<LLVMContext>(m, "Context")
    .def("parse_bitcode_from_bytes", [](LLVMContext& self, nb::bytes data) {
        auto buf = LLVMCreateMemoryBufferWithMemoryRange(
            data.c_str(), data.size(), "<bytes>", false
        );
        
        LLVMModuleRef mod_ref;
        auto ret = LLVMParseBitcodeInContext2(
            self.m_ref, buf, &mod_ref
        );
        LLVMDisposeMemoryBuffer(buf);  // Dispose immediately

        // NOTE: for the other parse functions we also need to dispose the buffer on failure
        
        if (ret)
            throw LLVMParseError(self.get_diagnostics());
        
        return std::make_unique<LLVMModuleManager>(
            std::make_shared<LLVMModule>(mod_ref, self.m_context_token)
        );
    });
```

**Example:**
```python
with ctx.create_context() as ctx:
    # From stdin
    import sys
    bitcode = sys.stdin.buffer.read()
    with ctx.parse_bitcode_from_bytes(bitcode) as mod:
        process(mod)
    
    # From network
    import requests
    bitcode = requests.get("https://example.com/module.bc").content
    with ctx.parse_bitcode_from_bytes(bitcode) as mod:
        analyze(mod)
```

---

### `Context.parse_ir(source: str) -> ModuleManager`

Parse textual LLVM IR from string.

**Parameters:**
- `source` (str): LLVM IR in textual format

**Returns:**
- `ModuleManager`: Context manager for module lifecycle

**Memory behavior:**
- Creates `MemoryBuffer` from string data
- **Always eager** (textual IR doesn't support lazy parsing)
- Buffer disposed immediately after parsing

**Implementation:**
```cpp
nb::class_<LLVMContext>(m, "Context")
    .def("parse_ir", [](LLVMContext& self, const std::string& source) {
        auto buf = LLVMCreateMemoryBufferWithMemoryRange(
            source.c_str(), source.size(), "<source>", false
        );
        
        LLVMModuleRef mod_ref;
        auto ret = LLVMParseIRInContext(self.m_ref, buf, &mod_ref, nullptr);
        LLVMDisposeMemoryBuffer(buf);
        
        if (ret)
            throw LLVMParseError(self.get_diagnostics());
        
        return std::make_unique<LLVMModuleManager>(
            std::make_shared<LLVMModule>(mod_ref, self.m_context_token)
        );
    });
```

**Example:**
```python
with ctx.create_context() as ctx:
    ir_source = """
    define i32 @main() {
        ret i32 0
    }
    """
    with ctx.parse_ir(ir_source) as mod:
        main = mod.get_function("main")
        print(main.name)  # "main"
```

---

## Design Decisions

### No `parse_bitcode_from_stdin()`

We remove this API completely.

**Rationale:**
- Redundant with `parse_bitcode_from_bytes(sys.stdin.buffer.read())`
- Less Pythonic - hides memory implications
- User has better control over stdin reading (progress, errors, etc.)
- Matches llvmlite's design

**Migration:**
```python
# ❌ Not provided
module = ctx.parse_bitcode_from_stdin()

# ✅ Explicit and clear
import sys
bitcode = sys.stdin.buffer.read()
module = ctx.parse_bitcode_from_bytes(bitcode)
```

### No MemoryBuffer Exposure

We remove this API completely.

**Rationale:**
- `MemoryBuffer` is an implementation detail
- Users don't need to manage buffer lifetimes explicitly
- Preventing error-prone manual management
- C++ wrapper handles all buffer lifecycle internally

**Internal management:**
```cpp
// User never sees LLVMMemoryBufferRef
struct LLVMModule {
    LLVMMemoryBufferRef m_memory_buffer = nullptr;  // Private
    
    // Automatic cleanup
    ~LLVMModule() {
        if (m_memory_buffer)
            LLVMDisposeMemoryBuffer(m_memory_buffer);
        // ...
    }
};
```

---

## Error Handling

### Exception Hierarchy

```python
class LLVMError(Exception):
    """Base exception for all LLVM binding errors"""
    pass

class LLVMParseError(LLVMError):
    """Raised when parsing bitcode or IR fails"""
    def __init__(self, diagnostics: list[Diagnostic]):
        self.diagnostics = diagnostics
        messages = '\n'.join(d.message for d in diagnostics)
        super().__init__(f"Failed to parse LLVM IR:\n{messages}")

class Diagnostic:
    """Diagnostic information from LLVM parser"""
    severity: LLVMDiagnosticSeverity
    message: str
```

### Context-Level Diagnostics

Diagnostics are per-context, exposed as methods:

```cpp
struct LLVMContext {
    std::vector<Diagnostic> m_diagnostics;
    
    void diagnostic_handler(LLVMDiagnosticInfoRef diag_info) {
        // Extract diagnostic information
        auto severity = LLVMGetDiagInfoSeverity(diag_info);
        char* message = LLVMGetDiagInfoDescription(diag_info);
        
        m_diagnostics.push_back({
            severity_to_string(severity),
            std::string(message),
            std::nullopt,  // line (if available)
            std::nullopt   // column (if available)
        });
        
        LLVMDisposeMessage(message);
    }
    
    std::vector<Diagnostic> get_diagnostics() const {
        return m_diagnostics;
    }
    
    void clear_diagnostics() {
        m_diagnostics.clear();
    }
};

// Set handler on context creation
LLVMContextSetDiagnosticHandler(
    ctx->m_ref,
    [](LLVMDiagnosticInfoRef info, void* ctx_ptr) {
        static_cast<LLVMContext*>(ctx_ptr)->diagnostic_handler(info);
    },
    ctx.get()
);
```

### Python API

```python
with llvm.create_context() as ctx:
    # Diagnostics accumulate per operation in the context
    # NOTE: They should be cleared before the next operation that can raise an error
    try:
        mod_mgr = ctx.parse_ir("invalid ir { syntax")
    except LLVMParseError as e:
        print(f"Parse failed: {e}")
        for diag in e.diagnostics:
            print(f"  {diag.severity}: {diag.message}")
```

---

## Lifetime Integration

All parsed modules integrate with the validity token system:

```python
with llvm.create_context() as ctx:
    with ctx.parse_bitcode_from_file("app.bc") as mod:
        func = mod.get_function("main")
        bb = func.entry_block
        inst = bb.first_instruction
        
        # Save references
        saved_func = func
        saved_inst = inst
    
    # Module disposed - tokens invalidated
    saved_func.name  # LLVMError: Function's module has been disposed
    saved_inst.name  # LLVMError: Instruction's module has been disposed
```

Module disposal invalidates all descendants:
```
Context Token
    └── Module Token (invalidated on module.dispose())
            ├── Function Token
            │       └── BasicBlock Token
            │               └── Instruction (checks module token)
            └── GlobalVariable (checks module token)
```

---

## Complete Example

```python
import llvm
import sys

with llvm.create_context() as ctx:
    # Parse from file (lazy for large modules)
    with ctx.parse_bitcode_from_file("large_app.bc", lazy=True) as mod:
        main = mod.get_function("main")
        print(f"Entry: {main.name}")
    
    # Parse from stdin
    bitcode = sys.stdin.buffer.read()
    with ctx.parse_bitcode_from_bytes(bitcode) as mod:
        for func in mod.functions():
            print(f"Function: {func.name}")
    
    # Parse textual IR
    ir = """
    define i32 @add(i32 %a, i32 %b) {
        %sum = add i32 %a, %b
        ret i32 %sum
    }
    """
    try:
        with ctx.parse_ir(ir) as mod:
            add_fn = mod.get_function("add")
    except llvm.LLVMParseError as e:
        print(f"Parse error: {e}")
        for diag in e.diagnostics:
            print(f"  {diag.severity}: {diag.message}")

    # Print any non-fatal diagnostics (NOTE: this API is very unimportant)
    for diag in ctx.diagnostics:
        print(f"  {diag.severity}: {diag.message}")
```

---

## Summary

| Method | Input | Lazy? | Returns | MemoryBuffer |
|--------|-------|-------|---------|--------------|
| `parse_bitcode_from_file()` | File path | Optional | ModuleManager | Internal (kept if lazy) |
| `parse_bitcode_from_bytes()` | bytes/bytearray | No | ModuleManager | Internal (disposed after parse) |
| `parse_ir()` | str | No | ModuleManager | Internal (disposed after parse) |

**Key principles:**
- ✅ No `MemoryBuffer` exposure - fully internal
- ✅ No `parse_bitcode_from_stdin()` - use `sys.stdin.buffer.read()` + `parse_bitcode_from_bytes()`
- ✅ All modules integrate with validity token system
- ✅ Specialized `LLVMParseError` with context diagnostics
- ✅ Consistent `ModuleManager` return type for RAII cleanup