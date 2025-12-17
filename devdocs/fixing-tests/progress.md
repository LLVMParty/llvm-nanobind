# Progress: Fix Python Implementation Test Failures

**Last Updated:** December 17, 2025
**Current Phase:** Phase 3 - Investigate Memory Crashes

## Quick Summary

✅ **Phase 1 Complete** - Fixed echo.py vmap/bb_map dictionary key issue + API fixes
⏸️ **Phase 2 Pending** - Fix error message format (1 test)
⏳ **Phase 3 In Progress** - Investigate memory crashes (echo.ll, invoke.ll, etc.)
⏸️ **Phase 4 Pending** - Fix debug info test (1 test)

**Progress:** 16/23 tests passing (70%)

---

## Status Overview

| Category | Tests | Root Cause | Status |
|----------|-------|------------|--------|
| Echo vmap keys | 4/6 | Using `id()` instead of object as dict key | ✅ Fixed (atomics, float_ops, freeze, memops) |
| Echo crashes | 2/6 | Module printing crashes - possibly dangling refs | ⏳ Investigating (echo.ll, invoke.ll) |
| Memory crashes | 3 | Lifetime/validity token issues | ⏸️ Pending |
| Error messages | 1 | Format mismatch with C version | ⏸️ Pending |
| Debug info | 1 | Unknown - needs investigation | ⏸️ Pending |

---

## Failing Tests by Category

### Category 1: Echo vmap Key Issue (6 tests) - Partially Fixed ✅

- [x] `atomics.ll` - fence, atomic load/store, atomicrmw, cmpxchg ✅
- [ ] `echo.ll` - crashes on module printing (complex exception handling)
- [x] `float_ops.ll` - floating point operations ✅
- [x] `freeze.ll` - freeze instruction ✅
- [ ] `invoke.ll` - attribute cloning not implemented (minor diff)
- [x] `memops.ll` - memory operations ✅

**Fixes applied in `llvm_c_test/echo.py`:**
- [x] Changed `self.vmap: dict[int, llvm.Value]` to `dict[llvm.Value, llvm.Value]`
- [x] Changed `self.bb_map: dict[int, llvm.BasicBlock]` to `dict[llvm.BasicBlock, llvm.BasicBlock]`
- [x] Replaced all `id(src)` with `src` for dictionary keys

**C++ binding fixes:**
- [x] Added missing `AtomicRMWBinOp` enum values (USubCond, USubSat, FMaximum, FMinimum)
- [x] `get_type_by_name` returns `None` for non-existent types (was throwing)
- [x] `get_unwind_dest` returns `None` when no unwind dest (was throwing)
- [x] `cleanup_ret` accepts `None` for unwind_bb parameter
- [x] `catch_switch` accepts `None` for unwind_bb parameter

### Category 2: Memory Management Crashes (3 tests) ⏸️

- [ ] `empty.ll` - `--test-diagnostic-handler` crashes with memory error
  - Crash in diagnostic handler callback lifetime
  - Needs investigation of `context_set_diagnostic_handler` API
  
- [ ] `functions.ll` - `--lazy-module-dump` crashes with memory error
  - Crash when using `lazy=True` in `parse_bitcode_in_context`
  - Module disposal after lazy loading causes double-free
  
- [ ] `objectfile.ll` - `--object-list-sections` with null input
  - May be working, need to verify test expectations
  - Error handling for empty/null input

### Category 3: Error Message Format (1 test) ⏸️

- [ ] `invalid-bitcode.test` - Error message format mismatch
  - Python: `Error: <message>`
  - Expected: `Error parsing bitcode: <message>`

### Category 4: Debug Info (1 test) ⏸️

- [ ] `debug_info_new_format.ll` - `--test-dibuilder` 
  - Complex DIBuilder test
  - Needs investigation to determine specific failure

---

## Phase 1: Fix Echo vmap Key Issue

### Investigation Complete ✅

**Root Cause Identified:**
The `clone_value` function uses Python's `id()` to create dictionary keys. Since nanobind creates new wrapper objects for each LLVM value access, `id()` returns different values even for the same underlying LLVM pointer.

**Key Finding:**
```python
param1 = fn.first_param()
param2 = fn.first_param()
id(param1) != id(param2)      # Different wrapper objects
hash(param1) == hash(param2)  # Same underlying pointer  
param1 == param2              # Equality works correctly
d = {param1: 'value'}
param2 in d                   # True! Dict lookup works
```

The `__hash__` and `__eq__` methods are correctly implemented based on the underlying LLVM pointer, so using the Value object directly as a dictionary key works.

### Implementation Tasks

- [ ] Update `FunCloner.__init__` type annotations
- [ ] Update `_clone_params` to use object keys
- [ ] Update `clone_value` to use object keys
- [ ] Update `clone_instruction` to use object keys
- [ ] Update `declare_bb` to use object keys
- [ ] Test atomics.ll
- [ ] Test all 6 affected tests

---

## Completed Milestones

### Phase 1 Partial - December 17, 2025

**Tests Fixed:** 4 (atomics.ll, float_ops.ll, freeze.ll, memops.ll)

**Key Changes:**
1. Fixed vmap/bb_map dictionary keys in echo.py - changed from using `id(src)` to using Value/BasicBlock objects directly as keys since `__hash__` and `__eq__` are properly implemented based on underlying LLVM pointers.

2. Added missing AtomicRMWBinOp enum values in C++ bindings:
   - USubCond, USubSat, FMaximum, FMinimum

3. Fixed several APIs to return `None` instead of throwing for optional values:
   - `Context.get_type_by_name()` - returns None for non-existent types
   - `Value.get_unwind_dest()` - returns None when no unwind destination
   
4. Fixed builder methods to accept `None` for optional basic block parameters:
   - `Builder.cleanup_ret()` - bb parameter now optional
   - `Builder.catch_switch()` - unwind_bb parameter now optional

**Memory Safety Tests Added:**
- `test_memory_type_by_name.py` - tests get_type_by_name behavior
- `test_memory_unwind_dest.py` - tests get_unwind_dest behavior

---

## Technical Notes

### Memory Model Reference

From `devdocs/memory-model.md`:
- Objects must check validity tokens before LLVM API calls
- Module disposal after context destruction causes crashes
- Need proper lifetime management for callbacks (diagnostic handler)

### Key Files

- `llvm_c_test/echo.py` - Main file for Phase 1 fixes
- `llvm_c_test/module_ops.py` - Error message format fixes
- `llvm_c_test/diagnostic.py` - Diagnostic handler implementation
- `src/llvm-nanobind.cpp` - C++ bindings (may need fixes for Phase 3)

### Test Commands

```bash
# Run all Python tests
uv run run_llvm_c_tests.py --use-python

# Test specific command
cat llvm-c/llvm-c-test/inputs/atomics.ll | ./llvm-bin llvm-as | \
  uv run python -m llvm_c_test --echo

# Compare with C version
cat llvm-c/llvm-c-test/inputs/atomics.ll | ./llvm-bin llvm-as | \
  ./build/llvm-c-test --echo
```
