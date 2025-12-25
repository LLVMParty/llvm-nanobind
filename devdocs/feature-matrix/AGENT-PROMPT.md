# Agent Prompt: Complete LLVM-C API Feature Matrix

## Your Mission

Implement ALL remaining TODO items from the LLVM-C API feature matrix. Continue working until every item is either:
1. ✅ Implemented with tests passing
2. 🚫 Explicitly marked as skipped with justification

**Do not stop until the task is complete.**

---

## Context Files to Read First

1. **Read `devdocs/feature-matrix/implementation-task.md`** - Contains the prioritized TODO list and skip rules
2. **Read `devdocs/api-design-philosophy.md`** - Understand API design principles  
3. **Read `AGENTS.md`** - Build and test commands

---

## Critical Rules

### DO NOT Implement (Mark as 🚫 Skip)

1. **Manual memory management** - `LLVMDispose*`, `LLVMCreate*` requiring manual cleanup
2. **Global context functions** - Anything using `LLVMGetGlobalContext()`
3. **Deprecated functions** - `LLVMX86MMXType`, `LLVMBuildNUWNeg`, legacy pass manager
4. **Debugging-only** - `LLVMDump*`, `LLVMViewFunctionCFG*`
5. **Temporary metadata nodes** - `LLVMTemporaryMDNode`, `LLVMDisposeTemporaryMDNode`
6. **Module flag iteration** - Complex API, can parse IR instead
7. **FP math tags** - Very specialized

### API Design Rules

- Operations on objects → **methods**, not global functions
- No-argument getters → **properties**
- Collections → return **lists**, not iterators
- Always call `check_valid()` before LLVM-C API calls
- Use wrapper classes with validity tokens for return values

---

## Implementation Loop

For each remaining TODO item:

### Step 1: Add C++ Implementation

```cpp
// In src/llvm-nanobind.cpp

// For methods on wrapper classes:
ReturnWrapper ClassName::method_name(args) {
  check_valid();
  // ... validate args ...
  return ReturnWrapper(LLVMSomeFunction(...), m_context_token);
}

// For free functions:
ReturnWrapper function_name(args) {
  arg.check_valid();
  return ReturnWrapper(LLVMSomeFunction(...), arg.m_context_token);
}
```

### Step 2: Add Python Binding

```cpp
// Method binding:
.def("method_name", &ClassName::method_name, "arg"_a,
     R"(Docstring describing the method.)")

// Free function binding:
m.def("function_name", &function_name, "arg"_a,
      R"(Docstring describing the function.)")
```

### Step 3: Build and Test

```bash
# Build
uv run python -c "import llvm; print('Build OK')"

# Run tests
uv run run_tests.py

# Type check
uvx ty check
```

### Step 4: Update Documentation

Update these files to mark items as ✅:
- `devdocs/feature-matrix/progress.md`
- `devdocs/feature-matrix/debuginfo.md` (if debug info related)
- `devdocs/feature-matrix/misc.md` (if misc header related)
- `devdocs/feature-matrix/core.md` (if Core.h related)

---

## Remaining Items Checklist

### Core.h
- [ ] `LLVMGetCastOpcode` → `llvm.get_cast_opcode(src_ty, src_signed, dst_ty, dst_signed)`
- [ ] `LLVMIntrinsicGetType` → `llvm.intrinsic_get_type(ctx, id, param_types)`
- [ ] `LLVMCreateMemoryBufferWithMemoryRange` → `llvm.MemoryBuffer.from_bytes_no_copy(data)`
- [ ] `LLVMReplaceMDNodeOperandWith` → `md_node.replace_operand(index, new_md)`

### DebugInfo.h
- [ ] `LLVMDIBuilderCreateClassType` → `dib.create_class_type(...)`
- [ ] `LLVMDIBuilderCreateStaticMemberType` → `dib.create_static_member_type(...)`
- [ ] `LLVMDIBuilderCreateMemberPointerType` → `dib.create_member_pointer_type(...)`
- [ ] `LLVMDIGlobalVariableExpressionGetVariable` → `gve.variable` property
- [ ] `LLVMDIGlobalVariableExpressionGetExpression` → `gve.expression` property
- [ ] `LLVMDIBuilderInsertDeclareRecordBefore` → `dib.insert_declare_record_before(...)`
- [ ] `LLVMDIBuilderInsertDbgValueRecordBefore` → `dib.insert_dbg_value_record_before(...)`

### Object.h
- [ ] `LLVMBinaryCopyMemoryBuffer` → `binary.copy_to_memory_buffer()`
- [ ] `LLVMGetSectionContainsSymbol` → `section.contains_symbol(symbol)`

### Support.h (JIT)
- [ ] `LLVMLoadLibraryPermanently` → `llvm.load_library(path)`
- [ ] `LLVMSearchForAddressOfSymbol` → `llvm.search_for_symbol(name)`
- [ ] `LLVMAddSymbol` → `llvm.add_symbol(name, addr)`
- [ ] `LLVMParseCommandLineOptions` → `llvm.parse_command_line_options(args)`

### Comdat.h (Windows)
- [ ] `LLVMGetOrInsertComdat` → `mod.get_or_insert_comdat(name)`
- [ ] `LLVMGetComdat` → `gv.comdat` property
- [ ] `LLVMSetComdat` → `gv.comdat = comdat` setter
- [ ] `LLVMGetComdatSelectionKind` → `comdat.selection_kind` property
- [ ] `LLVMSetComdatSelectionKind` → `comdat.selection_kind = kind` setter

### ErrorHandling.h
- [ ] `LLVMInstallFatalErrorHandler` → `llvm.install_fatal_error_handler(callback)`
- [ ] `LLVMResetFatalErrorHandler` → `llvm.reset_fatal_error_handler()`
- [ ] `LLVMEnablePrettyStackTrace` → `llvm.enable_pretty_stack_trace()`

---

## Completion Verification

Run this final check:

```bash
# All tests pass
uv run run_tests.py

# Type check passes
uvx ty check

# Verify new functions are accessible
uv run python << 'EOF'
import llvm

# Test each new function exists (add your tests here)
print("All new functions accessible!")
EOF
```

Then update `progress.md` with final coverage numbers.

---

## When You're Done

1. Update `devdocs/feature-matrix/progress.md` summary table with final counts
2. Update `devdocs/feature-matrix/summary.md` with final coverage
3. Mark `implementation-task.md` as complete
4. Report: "Feature matrix implementation complete. Coverage: X%"

**Keep going until ALL items are handled (implemented or explicitly skipped).**
