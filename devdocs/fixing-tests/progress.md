# Progress: Fix Python llvm-c-test Implementation

**Last Updated:** December 18, 2025
**Current Phase:** Investigation Complete, Phases 4-5 Ready for Implementation

## Quick Summary

| Status | Count | Tests |
|--------|-------|-------|
| ✅ Passing | 21 | ARM/disassemble, X86/disassemble, add_named_metadata_operand, atomics, calc, callsite_attributes, di-type-get-name, empty, float_ops, freeze, function_attributes, functions, get-di-tag, globals, invalid-bitcode, invoke, is_a_value_as_metadata, memops, objectfile, replace_md_operand, set_metadata |
| ❌ Failing | 2 | debug_info_new_format, echo |

**Progress:** 21/23 tests passing (91.30%)

---

## Completed Phases

### Phase 1: ModuleID Fix ✅
**Completed:** December 18, 2025
**Tests Fixed:** 5 (atomics.ll, float_ops.ll, freeze.ll, invoke.ll, memops.ll)

**Changes:**
- `llvm_c_test/echo.py`: Changed module name to `"<stdin>"` after parsing to match C version

### Phase 2: Error Message Format + Diagnostic Handler ✅
**Completed:** December 18, 2025
**Tests Fixed:** 2 (invalid-bitcode.test, empty.ll)

**Changes:**
- `llvm_c_test/module_ops.py`: Added `_extract_error_message()` HACK to format error messages
- `llvm_c_test/diagnostic.py`: Implemented `test_diagnostic_handler()` using context diagnostics

### Phase 3: Lazy Loading Support ✅
**Completed:** December 18, 2025
**Tests Fixed:** 1 (functions.ll)

**Changes:**
- `src/llvm-nanobind.cpp`: Added `lazy` parameter to `parse_bitcode_from_bytes()`
- `llvm_c_test/module_ops.py`: Pass `lazy` parameter and set module name to `"<stdin>"`

---

## Remaining Failures (2 tests) - Root Causes Identified

### echo.ll - `get_module_context` Returns Wrong Context

**Root Cause:** The `get_module_context` binding in `llvm-nanobind.cpp` is fundamentally broken. It retrieves the context via `LLVMGetModuleContext(mod.m_ref)` but then **ignores** it and returns a static wrapper around the global context instead.

**Evidence:**
```cpp
// Current broken code:
static thread_local LLVMContextWrapper global_ctx_wrapper(true);
return &global_ctx_wrapper;  // Always returns global context!
```

**Effect:**
- `echo.py`'s `TypeCloner` class gets the global context instead of the module's context
- Types are created in the wrong context
- Custom syncscopes (like `"agent"`) exist only in the user-created context
- When printing the cloned module, LLVM crashes trying to look up invalid syncscope IDs

**Verified:** Simple test case crashes in Python but works in C:
```bash
# Crashes:
printf 'define void @test(ptr %%ptr) {\n  %%a = atomicrmw volatile xchg ptr %%ptr, i8 0 syncscope("agent") acq_rel, align 8\n  ret void\n}\n' | llvm-as | uv run llvm-c-test --echo

# Works:
printf 'define void @test(ptr %%ptr) {\n  %%a = atomicrmw volatile xchg ptr %%ptr, i8 0 syncscope("agent") acq_rel, align 8\n  ret void\n}\n' | llvm-as | ./build/llvm-c-test --echo
```

**Fix Required:** Update `get_module_context` to return a wrapper for the actual context.

---

### debug_info_new_format.ll - Metadata ID Mismatch (!45 vs !44)

**Root Causes Identified:**

1. **`dibuilder_create_struct_type` binding incomplete:**
   - Missing: `elements`, `runtime_lang`, `unique_identifier` parameters
   - C creates MyStruct with `elements: !{!6, !6, !6}`, `runtimeLang: DW_LANG_C89`, `identifier: "MyStruct"`
   - Python creates MyStruct with `elements: !{}` (empty)

2. **`debuginfo.py` passes `None` instead of `foo_var1` for `associated`:**
   - Line 510: passes `None` to `dibuilder_create_dynamic_array_type`
   - C passes `FooVar1`, which causes it to appear as `!42` (referenced by DynType)
   - This shifts the DISubprogram from `!44` to `!45` in Python

3. **LLVM upstream C test code bugs (we fix, diverging from upstream):**
   - Forward decl passes `"Class1"` with length 5 → produces `"Class"` (we use correct `"Class1"`)
   - Enumerator passes `"Test_B"` with `strlen("Test_C")` → produces `"Test_B"` (we use correct `"Test_C"`)
   - These will be submitted as a PR to LLVM upstream

**Fix Required:**
1. Extend `dibuilder_create_struct_type` binding with missing parameters
2. Update `debuginfo.py` to pass `foo_var1` for `associated` parameter
3. Update lit test expected output to match our corrected implementation

---

## Next Steps

### Phase 4: Fix `get_module_context` binding
**File:** `src/llvm-nanobind.cpp`
**Change:** Return a wrapper that properly references the module's actual context
**Complexity:** Need to handle ownership carefully (context may be user-created or global)

### Phase 5: Fix DIBuilder metadata order
**Files:** 
- `src/llvm-nanobind.cpp` - extend `dibuilder_create_struct_type`
- `llvm_c_test/debuginfo.py` - pass `foo_var1` to dynamic array type
- `llvm-c/llvm-c-test/inputs/debug_info_new_format.ll` - update expected output

---

## Test Commands

```bash
# Run all Python tests
uv run run_llvm_c_tests.py --use-python

# Run with verbose output
uv run run_llvm_c_tests.py --use-python -v

# Quick syncscope test
printf 'define void @test(ptr %%ptr) {\n  %%a = atomicrmw volatile xchg ptr %%ptr, i8 0 syncscope("agent") acq_rel, align 8\n  ret void\n}\n' | /path/to/llvm-as | uv run llvm-c-test --echo

# Run individual regression tests
uv run python tests/regressions/test_dibuilder_metadata.py
```

---

## Key Files Modified (Phases 1-3)

| File | Changes |
|------|---------|
| `llvm_c_test/echo.py` | Module name fix (`<stdin>`) |
| `llvm_c_test/module_ops.py` | Error message extraction, lazy loading, module name fix |
| `llvm_c_test/diagnostic.py` | Implemented diagnostic handler test |
| `src/llvm-nanobind.cpp` | Added `lazy` parameter to `parse_bitcode_from_bytes()` |

## Files to Modify (Phases 4-5)

| File | Planned Changes |
|------|-----------------|
| `src/llvm-nanobind.cpp` | Fix `get_module_context`, extend `dibuilder_create_struct_type` |
| `llvm_c_test/debuginfo.py` | Pass `foo_var1` to `dibuilder_create_dynamic_array_type` |
| `llvm-c/llvm-c-test/inputs/debug_info_new_format.ll` | Update expected output for bug fixes |
| `tests/regressions/test_syncscope_crash.py` | Fix test to keep both modules open simultaneously |
