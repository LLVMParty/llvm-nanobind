# Module/Builder/Function API Refactor

**Completed**: 2024-12-21

Moved miscellaneous global functions to their appropriate classes following the API design philosophy of "methods belong to objects."

## Summary

Refactored 8 global functions into methods on their owning classes:

| Old API | New API |
|---------|---------|
| `llvm.create_dibuilder(mod)` | `mod.create_dibuilder()` |
| `llvm.get_module_context(mod)` | `mod.context` |
| `llvm.is_new_dbg_info_format(mod)` | `mod.is_new_dbg_info_format` |
| `llvm.set_is_new_dbg_info_format(mod, val)` | `mod.is_new_dbg_info_format = val` |
| `llvm.get_intrinsic_declaration(mod, id, types)` | `mod.get_intrinsic_declaration(id, types)` |
| `llvm.position_builder_before_dbg_records(b, bb, i)` | `b.position_before_dbg_records(bb, i)` |
| `llvm.position_builder_before_instr_and_dbg_records(b, i)` | `b.position_before_instr_and_dbg_records(i)` |
| `llvm.set_subprogram(fn, sp)` | `fn.set_subprogram(sp)` |

## Key Learnings

### Forward Declarations for Deferred Implementations

When adding methods that return types defined later in the file:
1. Add forward declaration at the top (`struct LLVMDIBuilderManager;`)
2. Declare method in class with comment about deferred implementation
3. Implement method after the dependency is defined using `inline`

```cpp
// In class declaration:
LLVMDIBuilderManager *create_dibuilder();  // Declared here, implemented after LLVMDIBuilderManager

// After LLVMDIBuilderManager is defined:
inline LLVMDIBuilderManager *LLVMModuleWrapper::create_dibuilder() {
  check_valid();
  return new LLVMDIBuilderManager(const_cast<LLVMModuleWrapper *>(this));
}
```

### nanobind Limitations

- **No write-only properties**: nanobind doesn't support `def_prop_wo`. Use a regular method instead:
  ```cpp
  // Instead of: .def_prop_wo("subprogram", &set_subprogram)
  .def("set_subprogram", &LLVMFunctionWrapper::set_subprogram, "sp"_a)
  ```

### Opaque Pointers Stay Global

Debug record functions (`get_first_dbg_record`, etc.) return opaque `void*` pointers. These were kept as global functions since:
1. They don't operate on wrapped types
2. Creating a DbgRecord wrapper would add complexity for minimal benefit
3. They're rarely used directly by end users

### Removing Old Globals

Replace removed global function bindings with NOTE comments to document the migration:
```cpp
// NOTE: create_dibuilder has been moved to Module.create_dibuilder() method
```

This helps developers find the new location if they encounter old code.

## Files Modified

- `src/llvm-nanobind.cpp` - Added methods, removed globals
- `llvm_c_test/debuginfo.py` - Updated to use new API
- `llvm_c_test/echo.py` - Updated to use new API

## Verification

All tests passed after refactor:
- 34/34 lit tests (Python implementation)
- 15/15 regular tests
- Type checking passes
