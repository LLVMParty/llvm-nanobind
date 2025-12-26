# Feature Matrix Implementation

**Completed:** December 2024

## Goal

Implement remaining LLVM-C API bindings to reach ~85% coverage of the C API, with explicit documentation of what's skipped and why.

## Final Coverage

| Header | Coverage | Notes |
|--------|----------|-------|
| Core.h | 82% | Main API, extensive |
| DebugInfo.h | ~83% | Debug info creation |
| Target.h | 100% | Target queries |
| TargetMachine.h | 79% | Code generation |
| Object.h | 77% | Object file parsing |
| PassBuilder.h | 100% | Optimization passes |
| Analysis.h | 100% | Verification |
| BitWriter.h | 100% | Bitcode output |
| Linker.h | 100% | Module linking |
| Comdat.h | 100% | Windows COMDAT |
| **Total** | **~85%** | |

## Key Implementations

### Session 6 - Final Items

**Core.h:**
- `llvm.get_cast_opcode(src, src_signed, dest_ty, dest_signed)` - Determine cast instruction
- `llvm.intrinsic_get_type(ctx, id, param_types)` - Get intrinsic return type
- `llvm.replace_md_node_operand_with(val, index, replacement)` - Metadata modification

**DebugInfo.h:**
- `dib.create_class_type(...)` - C++ class debug info
- `dib.create_static_member_type(...)` - Static members
- `dib.create_member_pointer_type(...)` - Pointer-to-member
- `dib.insert_declare_record_before(...)` - Debug intrinsic records
- `dib.insert_dbg_value_record_before(...)` - Debug value records
- `llvm.di_global_variable_expression_get_{variable,expression}()` - GVE accessors

**Object.h:**
- `binary.copy_to_memory_buffer()` - Returns Python `bytes`
- `section.contains_symbol(symbol)` - Symbol membership test

**Comdat.h (all 5 functions):**
- `mod.get_or_insert_comdat(name)` - Get/create COMDAT
- `gv.comdat` / `gv.set_comdat(comdat)` - Global COMDAT access
- `comdat.selection_kind` - Selection kind property

## Explicitly Skipped

| Item | Reason |
|------|--------|
| `LLVMCreateMemoryBufferWithMemoryRange` | Zero-copy unsafe with Python GC |
| Support.h JIT functions (4) | JIT symbol resolution not in scope |
| ErrorHandling.h (3) | C callback-based fatal error handling problematic in Python |

## Design Decisions

1. **MemoryBuffer is internal** - Users work with `bytes` or file paths directly (per parsing-refactor.md)

2. **DIBuilder via context manager** - `with mod.create_dibuilder() as dib:` ensures finalization

3. **`@api LLVMXyz` documentation** - Added to docstrings to reference underlying C API (40% coverage, ongoing)

4. **Global functions for metadata accessors** - `llvm.di_location_get_line(loc)` style for standalone helpers

## Test Suite

Created `tests/test_feature_matrix.py` with comprehensive tests for all Session 6 items:
- Cast opcode determination
- Intrinsic type lookup
- Metadata replacement
- Debug info type creation
- Object file operations
- COMDAT operations

## References

- Feature matrix files: `devdocs/feature-matrix/*.md` (now archived)
- API design: `devdocs/archive/parsing-refactor.md`
- Memory model: `devdocs/memory-model.md`
