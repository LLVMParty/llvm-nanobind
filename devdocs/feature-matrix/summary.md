# Feature Matrix Summary

## Overview

This document tracks the implementation status of LLVM-C APIs in the llvm-nanobind Python bindings.

**Last Updated:** 2024-12-25

## Coverage Summary

| Header | Total | ✅ Impl | 🚫 Skip | ❌ TODO | Coverage |
|--------|-------|---------|---------|---------|----------|
| **Core.h** | 640 | 413 | 44 | 183 | 64.5% |
| **DebugInfo.h** | 99 | ~50 | 0 | ~49 | ~50% |
| **Target.h** | 22 | 0 | 0 | 22 | 0.0% |
| **TargetMachine.h** | 29 | 7 | 0 | 22 | 24.1% |
| **Object.h** | 31 | 23 | 0 | 8 | 74.2% |
| **Analysis.h** | 4 | 1 | 0 | 3 | 25.0% |
| **BitReader.h** | 8 | 3 | 0 | 5 | 37.5% |
| **BitWriter.h** | 4 | 0 | 0 | 4 | 0.0% |
| **IRReader.h** | 1 | 1 | 0 | 0 | 100.0% |
| **PassBuilder.h** | 5 | 0 | 0 | 5 | 0.0% |
| **Disassembler.h** | 6 | 3 | 0 | 3 | 50.0% |
| **Linker.h** | 1 | 0 | 0 | 1 | 0.0% |
| **Error.h** | 7 | 0 | 0 | 7 | 0.0% |
| **ErrorHandling.h** | 3 | 0 | 0 | 3 | 0.0% |
| **Support.h** | 4 | 0 | 0 | 4 | 0.0% |
| **Comdat.h** | 5 | 0 | 0 | 5 | 0.0% |
| **Total** | **~869** | **~501** | **~44** | **~324** | **~58%** |

*Note: DebugInfo.h count is approximate*

## Not Yet Tracked

These headers are not included in the bindings and not fully tracked:

| Header | Functions | Priority | Notes |
|--------|-----------|----------|-------|
| ExecutionEngine.h | ~38 | Low | Prefer ORC JIT |
| Orc.h | ~68 | Medium | Modern JIT API |
| LLJIT.h | ~20 | Medium | High-level ORC |
| OrcEE.h | ~3 | Low | ORC EE bridge |
| LLJITUtils.h | ~1 | Low | LLJIT utils |
| Remarks.h | ~24 | Low | Optimization remarks |
| blake3.h | ~9 | Skip | Not LLVM IR related |
| lto.h | ~many | Skip | LTO - separate use case |

## Intentionally Skipped Categories (🚫)

### Global Context APIs
Functions using `LLVMGetGlobalContext()` are **not exposed**. Python bindings require explicit context management.

Examples: `LLVMModuleCreateWithName`, `LLVMGetMDKindID`, `LLVMAppendBasicBlock`, `LLVMInt32Type`, etc.

### Legacy Pass Manager
All legacy PM functions are skipped. Use the new PassBuilder API:
- `LLVMCreatePassManager`, `LLVMRunPassManager`, etc.

### Deprecated Functions
- `LLVMGetDataLayout` → Use `LLVMGetDataLayoutStr`
- `LLVMBuildLoad` → Use `LLVMBuildLoad2`
- `LLVMBuildGEP` → Use `LLVMBuildGEP2`
- `LLVMBuildCall` → Use `LLVMBuildCall2`
- `LLVMBuildInvoke` → Use `LLVMBuildInvoke2`
- etc.

### Unsafe for Embedding
- `LLVMShutdown` - Would corrupt Python process

### Internal Memory Management
- `LLVMCreateMessage` / `LLVMDisposeMessage` - Used internally

## API Design Philosophy

### 1. Object-Oriented Wrappers
```python
# C API                                    # Python API
LLVMModuleCreateWithNameInContext(n, c)    ctx.create_module(n)
LLVMGetModuleIdentifier(mod)               mod.name
LLVMSetModuleIdentifier(mod, n)            mod.name = n
LLVMDisposeModule(mod)                     # automatic
```

### 2. Properties Instead of Get/Set
- `LLVMGetModuleIdentifier`/`LLVMSetModuleIdentifier` → `Module.name`
- `LLVMGetDataLayoutStr`/`LLVMSetDataLayout` → `Module.data_layout`
- `LLVMGetAlignment`/`LLVMSetAlignment` → `GlobalValue.alignment`

### 3. Safety Checks
Python bindings add validity checks that raise exceptions instead of crashing:
- Context lifetime tracking
- Module ownership tracking  
- Builder position validation
- Use-after-dispose detection

### 4. Pythonic Iterations
```python
# C API
fn = LLVMGetFirstFunction(mod)
while fn:
    # use fn
    fn = LLVMGetNextFunction(fn)

# Python API
for fn in mod.functions:
    # use fn
```

## Detailed Matrix Files

| File | Contents |
|------|----------|
| [core.md](core.md) | Core.h - 640 functions by category |
| [debuginfo.md](debuginfo.md) | DebugInfo.h - 99 functions |
| [target.md](target.md) | Target.h + TargetMachine.h - 51 functions |
| [misc.md](misc.md) | All other headers - 79 functions |

## Priority Implementation Gaps

### High Priority (blocking common use cases)

1. **BitWriter.h** (0%) - Can't write bitcode files
   - `LLVMWriteBitcodeToFile`
   - `LLVMWriteBitcodeToMemoryBuffer`

2. **PassBuilder.h** (0%) - Can't run optimization passes
   - `LLVMCreatePassBuilderOptions`
   - `LLVMRunPasses`

3. **Target.h** (0%) - Can't query data layout info
   - `LLVMSizeOfTypeInBits`
   - `LLVMABISizeOfType`

### Medium Priority

4. **TargetMachine.h** (24%) - Limited code generation
   - `LLVMCreateTargetMachine`
   - `LLVMTargetMachineEmitToFile`

5. **Linker.h** (0%) - Can't link modules
   - `LLVMLinkModules2`

6. **Analysis.h** (25%) - Limited verification
   - `LLVMVerifyFunction`
