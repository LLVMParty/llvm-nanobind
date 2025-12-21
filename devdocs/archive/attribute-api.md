# Attribute API Refactor

Moved attribute management functions from module scope to object methods.

## Summary

Refactored 7 global functions into methods on Function, Value, and Context classes following the API design philosophy of "methods belong to objects."

## API Changes

### Function Attributes → Function Methods

| Before | After |
|--------|-------|
| `llvm.add_attribute_at_index(fn, idx, attr)` | `fn.add_attribute(idx, attr)` |
| `llvm.get_enum_attribute_at_index(fn, idx, kind)` | `fn.get_enum_attribute(idx, kind)` |
| `llvm.get_attribute_count_at_index(fn, idx)` | `fn.get_attribute_count(idx)` |

### Call Site Attributes → Value Methods

| Before | After |
|--------|-------|
| `llvm.add_callsite_attribute(call, idx, attr)` | `call.add_callsite_attribute(idx, attr)` |
| `llvm.get_callsite_enum_attribute(call, idx, kind)` | `call.get_callsite_enum_attribute(idx, kind)` |
| `llvm.get_callsite_attribute_count(call, idx)` | `call.get_callsite_attribute_count(idx)` |

### Attribute Creation → Context Method

| Before | After |
|--------|-------|
| `llvm.create_enum_attribute(ctx, kind, val)` | `ctx.create_enum_attribute(kind, val)` |

### Kept Global

- `llvm.get_last_enum_attribute_kind()` - static registry lookup, no object context needed

## Key Implementation Details

1. **Function methods** added to `LLVMFunctionWrapper` struct
2. **Call site methods** added to `LLVMValueWrapper` with internal assertions for call/invoke instructions
3. **Method naming**: Used `add_callsite_attribute` instead of `add_attribute` on Value to avoid confusion with function attributes
4. **No backward compatibility** - old global functions were completely removed

## Files Modified

- `src/llvm-nanobind.cpp` - Added methods, removed globals
- `llvm_c_test/attributes.py` - Updated to new API
- `llvm_c_test/echo.py` - Updated attribute cloning in `clone_attrs()`

## Completed

December 2025
