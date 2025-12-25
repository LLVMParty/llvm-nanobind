# Feature Matrix Progress

## Current Status

**Phase:** Phase 1-2 Complete - All Headers Enumerated

## Summary Statistics

| Header | Total | ✅ Impl | 🚫 Skip | ❌ TODO | Coverage |
|--------|-------|---------|---------|---------|----------|
| Core.h | 640 | 413 | 44 | 183 | 64.5% |
| DebugInfo.h | 99 | ~50 | 0 | ~49 | ~50% |
| Target.h | 22 | 0 | 0 | 22 | 0.0% |
| TargetMachine.h | 29 | 7 | 0 | 22 | 24.1% |
| Object.h | 31 | 23 | 0 | 8 | 74.2% |
| Misc headers | 79 | 31 | 0 | 48 | 39.2% |
| **Tracked Total** | **~900** | **~524** | **~44** | **~332** | **~58%** |

**Not tracked:** Orc.h (~68), LLJIT.h (~20), ExecutionEngine.h (~38), Remarks.h (~24)

## Task Checklist

### Phase 1: Core.h Inventory ✅
- [x] Extract section structure from Core.h (39 @defgroup sections identified)
- [x] Group functions by category (Context, Module, Type, Value, Builder, etc.)
- [x] Create `core.md` with category structure
- [x] Enumerate all 640 functions with implementation status
- [x] Cross-reference each function with bindings to verify status
- [x] Document deviations and remarks for each function

### Phase 2: Other High-Priority Headers ✅
- [x] Analysis.h - Create section in `misc.md`
- [x] BitReader.h - Create section in `misc.md`
- [x] BitWriter.h - Create section in `misc.md`
- [x] IRReader.h - Create section in `misc.md`
- [x] DebugInfo.h - Create `debuginfo.md`
- [x] Target.h - Create `target.md`
- [x] TargetMachine.h - Add to `target.md`
- [x] PassBuilder.h - Create section in `misc.md`

### Phase 3: Medium-Priority Headers ✅
- [x] Object.h - Create section in `misc.md`
- [x] Disassembler.h - Create section in `misc.md`
- [ ] Orc.h - Create `jit.md` (not yet included in bindings)
- [ ] LLJIT.h - Add to `jit.md` (not yet included in bindings)
- [x] Error.h - Create section in `misc.md`
- [x] Linker.h - Create section in `misc.md`

### Phase 4: Summary ✅
- [x] Update `summary.md` with overall statistics
- [x] Add coverage percentage calculations
- [x] Document priority implementation gaps

## Completed Items

- [x] Created feature-matrix devdocs directory structure
- [x] Created `plan.md` with goals, scope, and phases
- [x] Created `progress.md` for tracking
- [x] Created `summary.md` with overview and design philosophy
- [x] Documented intentionally skipped categories (global context, legacy PM, deprecated)
- [x] Documented API design differences (OOP wrappers, properties, safety checks)

### Phase 1: Core.h - COMPLETE
- [x] Extracted all 640 functions from Core.h
- [x] Grouped by 39 @defgroup categories
- [x] Cross-referenced with bindings (415 implemented, 44 skipped, 181 not implemented)
- [x] Added notes for deprecated, global context, legacy PM functions
- [x] Created `core.md` with full function matrix

### Phase 2: Other Headers - COMPLETE
- [x] DebugInfo.h - 99 functions enumerated → `debuginfo.md`
- [x] Target.h + TargetMachine.h - 51 functions enumerated → `target.md`
- [x] Analysis.h - 4 functions
- [x] BitReader.h - 8 functions
- [x] BitWriter.h - 4 functions
- [x] IRReader.h - 1 function
- [x] Disassembler.h - 6 functions
- [x] Object.h - 31 functions
- [x] PassBuilder.h - 5 functions
- [x] Linker.h - 1 function
- [x] Error.h - 7 functions
- [x] ErrorHandling.h - 3 functions
- [x] Support.h - 4 functions
- [x] Comdat.h - 5 functions
- [x] All misc headers → `misc.md`

## Notes

- The C++ bindings use wrapper classes (LLVMModuleWrapper, LLVMBuilderWrapper, etc.) which reorganize the C API into more Pythonic interfaces
- Some C API functions are exposed as properties instead of methods
- Some C API functions are combined into single Python methods (e.g., type+value getters)
- Safety checks are added before calling C API to prevent crashes
- Global context functions are generally not exposed (prefer explicit contexts)

## Open Questions

1. Should deprecated LLVM-C functions be tracked? (Probably mark as 🚫 skipped)
2. Should we track which Python wrapper class exposes each function?
3. Should we track test coverage alongside implementation status?
