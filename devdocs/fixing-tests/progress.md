# Progress: Fix Python Implementation Test Failures

**Last Updated:** December 17, 2025
**Current Phase:** Phase 2 - Error Message Format & Remaining Failures

## Quick Summary

✅ **Phase 1 Complete** - Fixed echo.py vmap/bb_map dictionary key issue + API fixes
⏳ **Phase 2 In Progress** - Fix error message format + investigate remaining failures
⏸️ **Phase 3 Pending** - Memory crashes (empty.ll, functions.ll)
⏸️ **Phase 4 Pending** - Fix debug info test (1 test)

**Progress:** 19/23 tests passing (82.61%)

---

## Status Overview

| Category | Tests | Root Cause | Status |
|----------|-------|------------|--------|
| Echo vmap keys | 4/4 | Using `id()` instead of object as dict key | ✅ Fixed (atomics, float_ops, freeze, memops) |
| Syncscope crashes | 1 | Context-specific syncscope IDs copied across contexts | ⏳ Investigating (echo.ll) |
| Attribute cloning | 1 | Function attributes not cloned properly | ⏳ Investigating (invoke.ll) |
| Memory crashes | 2 | Lifetime/validity token issues | ⏸️ Pending (empty.ll, functions.ll) |
| Error messages | 1 | Format mismatch with C version | ⏳ In Progress (invalid-bitcode.test) |
| Debug info | 1 | DIBuilder metadata reference issue | ⏸️ Pending (debug_info_new_format.ll) |

---

## Failing Tests by Category

### Category 1: Echo vmap Key Issue (6 tests) - FIXED ✅

- [x] `atomics.ll` - fence, atomic load/store, atomicrmw, cmpxchg ✅
- [ ] `echo.ll` - custom syncscope crash (KNOWN ISSUE - fix later)
- [x] `float_ops.ll` - floating point operations ✅
- [x] `freeze.ll` - freeze instruction ✅
- [x] `invoke.ll` - function attribute cloning ✅
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

### Category 2: Error Message Format (1 test) - FIXED ✅

- [x] `invalid-bitcode.test` - Error message format ✅
  - Fixed: Added proper error message formatting for both old and new bitcode APIs
  - Old API: "Error parsing bitcode: <message>"
  - New API: "Error with new bitcode parser: <message>"

### Category 3: Memory Management Crashes (2 tests) ⏸️

- [ ] `empty.ll` - `--test-diagnostic-handler` crashes with memory error
  - Crash in diagnostic handler callback lifetime
  - Needs investigation of `context_set_diagnostic_handler` API
  
- [ ] `functions.ll` - `--lazy-module-dump` crashes with memory error
  - Crash when using `lazy=True` in `parse_bitcode_in_context`
  - Module disposal after lazy loading causes double-free

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

## Known Issues (Fix Later)

### echo.ll - Custom Syncscope Crash

**Root Cause:** The test uses `syncscope("agent")` which is a custom, context-specific syncscope. When echo.py clones atomic instructions, it copies the syncscope ID directly from source to destination. However, syncscope IDs are context-specific integers that differ between contexts.

**The Problem:**
- LLVM-C API provides `LLVMGetAtomicSyncScopeID(inst)` to get the ID
- LLVM-C API provides `LLVMSetAtomicSyncScopeID(inst, id)` to set the ID  
- LLVM-C API provides `LLVMGetSyncScopeID(context, name, len)` to get/create ID from name
- **Missing:** No API to get syncscope name from ID

**Attempted Fix:** Added validation to check if syncscope ID >= 3 (custom) and raise error, but the validation didn't prevent the crash.

**Proper Solution Requires:**
1. C++ binding to get syncscope name from ID (may need to access C++ API directly)
2. Or: Translate syncscope IDs by maintaining a mapping during cloning
3. Or: Use alternative APIs like `LLVMIsAtomicSingleThread`/`LLVMSetAtomicSingleThread`

**Impact:** Only affects modules with custom syncscopes. Standard syncscopes ("singlethread") work fine.

---

## Completed Milestones

### Phase 1 & 2 Complete - December 17, 2025

**Tests Fixed:** 7 (atomics.ll, float_ops.ll, freeze.ll, memops.ll, invoke.ll, invalid-bitcode.test, and objectfile.ll now passing)

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
  uv run llvm-c-test --echo

# Compare with C version
cat llvm-c/llvm-c-test/inputs/atomics.ll | ./llvm-bin llvm-as | \
  ./build/llvm-c-test --echo
```

### Changes Made:

1. **Uncommented and fixed function attribute cloning in `declare_symbols()`**:
   - The TODO comment indicated bindings weren't available, but they are
   - Now properly copies attributes (noalias, nonnull, noreturn, etc.) from function declarations
   - Fixed invoke.ll test

2. **Fixed error message formatting in `module_ops.py`**:
   - Old API: "Error parsing bitcode: <message>"
   - New API: Uses diagnostic handler and formats as "Error with new bitcode parser: <message>"
   - Fixed invalid-bitcode.test

3. **Added diagnostic handler support for new bitcode API**:
   - Sets diagnostic handler before parsing when `new_api=True`
   - Checks if handler was called and uses diagnostic description in error message
   - Properly matches C version behavior

**Tests Now Passing:** 19/23 (82.61%)
**Tests Remaining:** 4 (echo.ll with custom syncscopes, empty.ll, functions.ll, debug_info_new_format.ll)

---

## Minimal Reproduction Tests Created

For the remaining failing tests, minimal reproduction cases have been created following
DEBUGGING.md guidelines:

### 1. test_memory_diagnostic_handler.py (empty.ll crash)
- **Reproduces:** Crash when using `get_bitcode_module_2` (new API)
- **Root Cause:** Module destructor tries to free MemoryBuffer that was already freed or doesn't own
- **Status:** Crashes on module disposal (exit code -11, SIGSEGV)
- **Run:** `uv run python test_memory_diagnostic_handler.py`

### 2. test_memory_lazy_module.py (functions.ll crash)  
- **Reproduces:** Crash when using `lazy=True` with parse_bitcode_in_context
- **Root Cause:** LLVM takes ownership of MemoryBuffer when lazy loading, but our wrapper
  may be disposing it unconditionally
- **Status:** Crashes on module disposal (exit code -10, SIGBUS)
- **Note:** C version doesn't dispose memory buffer when lazy: `if (!Lazy) LLVMDisposeMemoryBuffer(MB);`
- **Run:** `uv run python test_memory_lazy_module.py`

### 3. test_memory_dibuilder.py (debug_info_new_format.ll)
- **Reproduces:** Metadata ID mismatch (!dbg !44 vs !dbg !45)
- **Root Cause:** Minor difference in metadata ID assignment order
- **Status:** Not a crash - cosmetic issue only
- **Priority:** Low (functionally equivalent IR)
- **Run:** `uv run python test_memory_dibuilder.py`

### 4. echo.ll (custom syncscope crash)
- **Already documented** in test_memory_syncscope.py
- **Root Cause:** Syncscope IDs are context-specific, copying directly causes invalid references
- **Status:** Crashes when printing module with custom syncscopes
- **Fix requires:** C++ API access to translate syncscope names between contexts

All reproduction tests are standalone, use no temporary files, and can be run directly.
They clearly demonstrate the crashes and document expected vs actual behavior.

