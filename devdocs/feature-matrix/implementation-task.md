# Feature Matrix Implementation Task

## Objective

Implement all remaining TODO items from the LLVM-C API feature matrix until coverage reaches ~95% or all reasonable items are implemented.

## Ground Rules

### DO NOT Implement (Skip These)

1. **Manual Memory Management Functions** - Python uses context managers for cleanup
   - `LLVMDispose*` functions (already handled by `__exit__`)
   - `LLVMCreate*` that return resources requiring manual disposal (use context managers instead)

2. **Global Context Functions** - Safety risk, always use explicit contexts
   - Any function using `LLVMGetGlobalContext()`
   - Functions without `InContext` suffix when a context version exists

3. **Deprecated Functions** - Use modern alternatives
   - `LLVMX86MMXTypeInContext` (removed in LLVM 21)
   - `LLVMBuildNUWNeg` (deprecated in LLVM 21)
   - Legacy pass manager functions
   - Old-style `LLVMBuild*` without type parameters (use `*2` versions)

4. **Debugging-Only Functions** - Not useful in production
   - `LLVMViewFunctionCFG`, `LLVMViewFunctionCFGOnly` (requires graphviz)
   - `LLVMDump*` functions (use `print()` instead)

5. **Internal/Low-Level Functions**
   - `LLVMCreateMessage`, `LLVMDisposeMessage` (internal memory management)
   - `LLVMWriteBitcodeToFD`, `LLVMWriteBitcodeToFileHandle` (use file path or memory buffer)

### API Design Rules (from api-design-philosophy.md)

1. **Methods belong to objects** - Don't create global functions for operations on objects
2. **Use properties** for no-argument getters and boolean checks
3. **Use methods** for operations with arguments or side effects
4. **Return lists** for collections, not manual iterators
5. **Throw exceptions + `has_*`** checks instead of returning None for rare edge cases

---

## Remaining TODO Items

### Priority 1: Core.h Gaps (Medium Value)

| Function | Target API | Notes |
|----------|------------|-------|
| `LLVMGetCastOpcode` | `llvm.get_cast_opcode(src_ty, src_signed, dst_ty, dst_signed)` | Utility for determining cast type |
| `LLVMIntrinsicGetType` | `llvm.intrinsic_get_type(ctx, id, param_types)` | Get intrinsic function type |
| `LLVMCreateMemoryBufferWithMemoryRange` | `llvm.MemoryBuffer.from_bytes_no_copy(data)` | Zero-copy memory buffer |
| `LLVMReplaceMDNodeOperandWith` | `md_node.replace_operand(index, new_md)` | Replace metadata operand |

### Priority 2: DebugInfo.h Gaps (Medium Value)

| Function | Target API | Notes |
|----------|------------|-------|
| `LLVMDIBuilderCreateClassType` | `dib.create_class_type(...)` | C++ class debug info |
| `LLVMDIBuilderCreateStaticMemberType` | `dib.create_static_member_type(...)` | Static member debug info |
| `LLVMDIBuilderCreateMemberPointerType` | `dib.create_member_pointer_type(...)` | Member pointer debug info |
| `LLVMDIGlobalVariableExpressionGetVariable` | `gve.variable` (property) | Get variable from GVE |
| `LLVMDIGlobalVariableExpressionGetExpression` | `gve.expression` (property) | Get expression from GVE |
| `LLVMDIBuilderInsertDeclareRecordBefore` | `dib.insert_declare_record_before(...)` | Insert before instruction |
| `LLVMDIBuilderInsertDbgValueRecordBefore` | `dib.insert_dbg_value_record_before(...)` | Insert before instruction |

### Priority 3: Object.h Gaps (Low Value)

| Function | Target API | Notes |
|----------|------------|-------|
| `LLVMBinaryCopyMemoryBuffer` | `binary.copy_to_memory_buffer()` | Copy binary content |
| `LLVMGetSectionContainsSymbol` | `section.contains_symbol(symbol)` | Check if section has symbol |

### Priority 4: Support.h (JIT Support - Low Value)

| Function | Target API | Notes |
|----------|------------|-------|
| `LLVMLoadLibraryPermanently` | `llvm.load_library(path)` | For JIT symbol resolution |
| `LLVMSearchForAddressOfSymbol` | `llvm.search_for_symbol(name)` | For JIT symbol resolution |
| `LLVMAddSymbol` | `llvm.add_symbol(name, addr)` | For JIT symbol injection |
| `LLVMParseCommandLineOptions` | `llvm.parse_command_line_options(args)` | Set LLVM internal flags |

### Priority 5: Comdat.h (Windows-specific - Low Value)

| Function | Target API | Notes |
|----------|------------|-------|
| `LLVMGetOrInsertComdat` | `mod.get_or_insert_comdat(name)` | Get/create COMDAT section |
| `LLVMGetComdat` | `gv.comdat` (property) | Get global's COMDAT |
| `LLVMSetComdat` | `gv.comdat = comdat` (setter) | Set global's COMDAT |
| `LLVMGetComdatSelectionKind` | `comdat.selection_kind` (property) | Get selection kind |
| `LLVMSetComdatSelectionKind` | `comdat.selection_kind = kind` (setter) | Set selection kind |

### Priority 6: ErrorHandling.h (Advanced - Low Value)

| Function | Target API | Notes |
|----------|------------|-------|
| `LLVMInstallFatalErrorHandler` | `llvm.install_fatal_error_handler(callback)` | Custom crash handling |
| `LLVMResetFatalErrorHandler` | `llvm.reset_fatal_error_handler()` | Reset to default |
| `LLVMEnablePrettyStackTrace` | `llvm.enable_pretty_stack_trace()` | Better crash output |

### Skip These (Mark as 🚫)

| Function | Reason |
|----------|--------|
| `LLVMCreateConstantRangeAttribute` | Very rare use case, complex API |
| `LLVMCreateMemoryBufferWithSTDIN` | Python has better stdin handling |
| `LLVMTemporaryMDNode` / `LLVMDisposeTemporaryMDNode` | Manual memory management |
| `LLVMDIBuilderCreateTempGlobalVariableFwdDecl` | Advanced, rarely needed |
| Module Flag Iteration (5 functions) | Can parse IR instead, complex API |
| FP math tag methods | Very specialized, rarely needed |

---

## Implementation Checklist

For each function:

1. [ ] Add C++ wrapper method/function to `src/llvm-nanobind.cpp`
2. [ ] Add Python bindings with proper docstring
3. [ ] Ensure proper validity checking (`check_valid()`)
4. [ ] Use appropriate return type (wrapper class with token)
5. [ ] Update feature matrix docs to mark as ✅
6. [ ] Test the new binding works

## Verification

After implementation:

```bash
# Build and import
uv run python -c "import llvm; print('OK')"

# Run tests
uv run run_tests.py

# Type check
uvx ty check
```

---

## Completion Criteria

Task is complete when:
- All Priority 1-3 items are implemented
- Priority 4-6 items are implemented OR explicitly marked as 🚫 with justification
- All tests pass
- Type checker passes
- Feature matrix docs are updated with accurate coverage numbers
