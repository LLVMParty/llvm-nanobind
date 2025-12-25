# Feature Matrix Plan

## Goal

Create and maintain a comprehensive feature matrix that tracks:
1. All LLVM-C API functions across all headers
2. Implementation status (bound/not bound/partial)
3. Python documentation status
4. Remarks and notes (limitations, deviations from C API, safety additions)

## Scope

### LLVM-C Headers to Track

| Header | Functions | Priority | Notes |
|--------|-----------|----------|-------|
| Core.h | ~670 | High | Core IR building - main focus |
| DebugInfo.h | ~99 | High | Debug info metadata |
| TargetMachine.h | ~37 | High | Code generation |
| Object.h | ~37 | Medium | Object file parsing |
| Target.h | ~32 | Medium | Target initialization |
| ExecutionEngine.h | ~38 | Low | JIT execution (prefer ORC) |
| Orc.h | ~68 | Medium | Modern JIT API |
| LLJIT.h | ~20 | Medium | High-level ORC wrapper |
| Remarks.h | ~24 | Low | Remarks/optimization notes |
| Analysis.h | ~4 | High | Module verification |
| BitReader.h | ~8 | High | Bitcode reading |
| BitWriter.h | ~4 | High | Bitcode writing |
| IRReader.h | ~1 | High | IR parsing |
| PassBuilder.h | ~17 | High | Optimization passes |
| Disassembler.h | ~6 | Medium | Instruction disassembly |
| Linker.h | ~1 | Medium | Module linking |
| Comdat.h | ~5 | Low | COMDAT handling |
| Error.h | ~7 | Medium | Error handling |
| ErrorHandling.h | ~3 | Low | Fatal error handlers |
| Support.h | ~4 | Low | Misc support functions |
| OrcEE.h | ~3 | Low | ORC execution engine bridge |
| LLJITUtils.h | ~1 | Low | LLJIT utilities |
| blake3.h | ~9 | Skip | Hashing - not LLVM IR related |
| lto.h | ~many | Skip | LTO - separate use case |

**Total: ~1,088 LLVM-C API functions**

### Currently Included in Bindings

Based on `#include` directives in `src/llvm-nanobind.cpp`:
- ✅ Core.h
- ✅ DebugInfo.h
- ✅ TargetMachine.h
- ✅ Object.h
- ✅ Target.h
- ✅ Analysis.h
- ✅ BitReader.h
- ✅ Disassembler.h
- ✅ IRReader.h
- ❌ BitWriter.h
- ❌ ExecutionEngine.h
- ❌ Orc.h / LLJIT.h
- ❌ PassBuilder.h
- ❌ Others...

## Matrix Structure

The matrix will be organized by header file, with each function tracked as:

```markdown
| Function | Status | Docs | Remarks |
|----------|--------|------|---------|
| LLVMFunctionName | ✅/⚠️/❌ | ✅/❌ | Notes about deviations |
```

Status legend:
- ✅ Fully implemented
- ⚠️ Partial (some parameters not exposed, or wrapped differently)
- ❌ Not implemented
- 🚫 Intentionally skipped (deprecated, unsafe, etc.)

## Phases

### Phase 1: Core.h Inventory
Extract all 670 functions from Core.h and track implementation status.
Group by functionality (Context, Module, Types, Values, Instructions, Builder, etc.)

### Phase 2: Other High-Priority Headers
- Analysis.h
- BitReader.h / BitWriter.h
- IRReader.h
- DebugInfo.h
- Target.h / TargetMachine.h
- PassBuilder.h

### Phase 3: Medium-Priority Headers
- Object.h
- Disassembler.h
- Orc.h / LLJIT.h
- Error.h
- Linker.h

### Phase 4: Low-Priority Headers
- Remaining headers

## Output Files

- `devdocs/feature-matrix/core.md` - Core.h functions
- `devdocs/feature-matrix/debuginfo.md` - DebugInfo.h functions
- `devdocs/feature-matrix/target.md` - Target.h + TargetMachine.h functions
- `devdocs/feature-matrix/jit.md` - ExecutionEngine, Orc, LLJIT functions
- `devdocs/feature-matrix/misc.md` - All other headers
- `devdocs/feature-matrix/summary.md` - Overall coverage statistics

## Automation Considerations

To keep the matrix maintainable:
1. Consider scripts to extract function names from headers
2. Consider scripts to check which functions are called in bindings
3. Update matrix when new bindings are added

## Testing Strategy

The matrix itself doesn't need tests, but it should inform:
- Which APIs need test coverage
- Which APIs are safe to use in production
- Which APIs have known limitations
