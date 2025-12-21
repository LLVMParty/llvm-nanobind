# Metadata API Refactor

Moved metadata operations from module scope to object methods.

## Summary

Refactored 7 global functions into methods on Value, Module, Context, and Metadata classes following the API design philosophy of "methods belong to objects."

## API Changes

### Instruction/Global Metadata → Value Methods

| Before | After |
|--------|-------|
| `llvm.set_metadata(inst, kind, val)` | `inst.set_metadata(kind, md, ctx)` |
| `llvm.global_set_metadata(val, kind, md)` | `val.set_metadata(kind, md, ctx)` |

### Module Metadata → Module Methods

| Before | After |
|--------|-------|
| `llvm.add_named_metadata_operand(mod, name, val)` | `mod.add_named_metadata_operand(name, md)` |

### Metadata Creation → Context Methods

| Before | After |
|--------|-------|
| `llvm.md_string_in_context_2(ctx, str)` | `ctx.md_string(str)` |
| `llvm.md_node_in_context_2(ctx, mds)` | `ctx.md_node(mds)` |

### Metadata Conversion → Metadata Methods

| Before | After |
|--------|-------|
| `llvm.metadata_as_value(ctx, md)` | `md.as_value(ctx)` |
| `llvm.metadata_replace_all_uses_with(temp, md)` | `temp.replace_all_uses_with(md)` |

## Key Implementation Details

1. **Unified `set_metadata`**: Single method works for both instructions and global values - internally checks `LLVMIsAGlobalValue` to dispatch correctly

2. **Context parameter required**: `Value.set_metadata(kind, md, ctx)` requires a context because instructions may not be attached to a basic block yet (needed to convert Metadata to Value for the C API)

3. **Metadata objects only**: New API uses `LLVMMetadataWrapper` objects directly, not `LLVMValueWrapper` for metadata. The `md.as_value(ctx)` method handles conversion when needed.

4. **Forward declarations**: Context methods `md_string` and `md_node` are forward-declared in the struct and implemented after `LLVMMetadataWrapper` is defined (due to C++ declaration order)

5. **No backward compatibility** - old global functions were completely removed

## Files Modified

- `src/llvm-nanobind.cpp` - Added methods to wrappers, removed globals
- `llvm_c_test/metadata.py` - Updated to new API
- `llvm_c_test/echo.py` - Updated metadata handling
- `llvm_c_test/debuginfo.py` - Updated debug info metadata creation

## Completed

December 2025
