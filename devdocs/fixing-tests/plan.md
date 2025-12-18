# Plan: Fix Python llvm-c-test Implementation

## Overview

When running lit tests with `--use-python`, 2 out of 23 tests fail. This plan documents the root causes and fixes for each remaining failure.

## Current Test Status (December 18, 2025)

```
Passed: 21 (91.30%)
Failed:  2 (8.70%)
```

## Root Cause Analysis

### Issue 1: echo.ll - Syncscope Crash

**Root Cause:** The `get_module_context` binding is broken. It ignores the actual context returned by `LLVMGetModuleContext` and always returns a wrapper around the global context.

```cpp
// Current broken implementation in llvm-nanobind.cpp:
m.def("get_module_context",
      [](const LLVMModuleWrapper &mod) -> LLVMContextWrapper * {
        mod.check_valid();
        LLVMContextRef ctx = LLVMGetModuleContext(mod.m_ref);  // <-- Retrieved but IGNORED!
        static thread_local LLVMContextWrapper global_ctx_wrapper(true);
        return &global_ctx_wrapper;  // <-- Always returns global context!
      }, ...);
```

**Impact:**
1. When user creates a context with `llvm.create_context()` and parses a module containing custom syncscopes like `"agent"`, those syncscopes are registered in that context
2. When `TypeCloner` in `echo.py` calls `llvm.get_module_context(module)`, it gets the global context instead
3. Types created through the wrong context end up in the global context
4. When building atomic operations with syncscope IDs from the source module, those IDs don't exist in the global context, causing a crash when printing

**Fix:**
Fix `get_module_context` to properly return the module's actual context. Options:
1. Return a new `LLVMContextWrapper` that wraps the actual context ref (non-owning)
2. Track context wrappers and return the existing one if it matches

**Regression Test:** `tests/regressions/test_syncscope_crash.py` (note: current test has a bug - it closes source module before creating dest, which is not the actual echo.py pattern)

---

### Issue 2: debug_info_new_format.ll - Metadata ID Mismatch

**Root Cause:** Multiple API and usage differences between `debuginfo.py` and `debuginfo.c`:

#### 2a. `dibuilder_create_struct_type` binding is incomplete

The Python binding is missing parameters that the C API supports:
- `derived_from` (LLVMMetadataRef)
- `elements` (array of LLVMMetadataRef)
- `num_elements` (unsigned)
- `runtime_lang` (unsigned)
- `vtable_holder` (LLVMMetadataRef)  
- `unique_identifier` (string)

**Result:** MyStruct in C has:
```
!34 = !DICompositeType(..., elements: !35, runtimeLang: DW_LANG_C89, identifier: "MyStruct")
!35 = !{!6, !6, !6}
```
But Python produces:
```
!34 = !DICompositeType(..., elements: !20)
!20 = !{}
```

#### 2b. `debuginfo.py` passes wrong value to `dibuilder_create_dynamic_array_type`

Python passes `None` for `associated` parameter but C passes `FooVar1`:

```python
# Python (wrong):
dynamic_array_md_ty = llvm.dibuilder_create_dynamic_array_type(
    ...,
    None,  # associated - should be foo_var1
    ...
)
```

```c
// C (correct):
LLVMDIBuilderCreateDynamicArrayType(..., FooVar1, ...);
```

This causes `FooVar1` to be referenced earlier in C (as `!42` within DynType), shifting subsequent metadata IDs.

#### 2c. C code bugs that we fix (diverging from LLVM upstream)

The LLVM upstream C test has bugs. We fix them in our Python implementation but document the divergence:

1. **Forward decl name truncation:** C passes `"Class1"` with length `5`, producing `"Class"`. We use correct `"Class1"`.
2. **Enumerator name mismatch:** C passes `"Test_B"` with `strlen("Test_C")`, producing `"Test_B"` instead of `"Test_C"`. We use correct `"Test_C"`.

These fixes will be submitted as a PR to LLVM upstream.

**Fix Strategy:**
1. Extend `dibuilder_create_struct_type` binding to accept all parameters (medium effort)
2. Update `debuginfo.py` to pass `foo_var1` to `dibuilder_create_dynamic_array_type`
3. Keep our correct implementations and update the lit test expected output

---

## Implementation Phases

### Phase 1: ModuleID Fix ✅ COMPLETE
**Result:** Fixed 5 tests (atomics, float_ops, freeze, invoke, memops)

### Phase 2: Error Message Format Fix ✅ COMPLETE
**Result:** Fixed 2 tests (invalid-bitcode, empty)

### Phase 3: Lazy Module Support ✅ COMPLETE
**Result:** Fixed 1 test (functions)

### Phase 4: Fix `get_module_context` binding
**Priority:** High (fixes echo.ll)
**Effort:** Low-Medium
**Files:** `src/llvm-nanobind.cpp`
**Status:** Root cause identified, fix pending

### Phase 5: Fix DIBuilder metadata order
**Priority:** Medium (fixes debug_info_new_format.ll)  
**Effort:** Medium
**Files:** `src/llvm-nanobind.cpp`, `llvm_c_test/debuginfo.py`
**Status:** Root cause identified, fix pending

---

## Summary Table

| Root Cause | Tests | Fix Effort | Phase | Status |
|------------|-------|------------|-------|--------|
| ModuleID `<bytes>` vs `<stdin>` | 5 | Low | 1 | ✅ Done |
| Error message format | 2 | Low | 2 | ✅ Done |
| Lazy module support | 1 | Medium | 3 | ✅ Done |
| `get_module_context` returns wrong context | 1 | Low-Medium | 4 | **Pending** |
| DIBuilder struct_type binding incomplete | 1 | Medium | 5 | **Pending** |
| DIBuilder debuginfo.py `associated` param | 1 | Low | 5 | **Pending** |
| **Total Fixed** | **8** | | | |
| **Remaining** | **2** | | | |

---

## Success Criteria

All 23 lit tests pass with `--use-python`:
```bash
uv run run_llvm_c_tests.py --use-python
# Expected: 23 tests passed, 0 failed
```
