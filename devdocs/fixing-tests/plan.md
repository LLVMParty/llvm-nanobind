# Plan: Fix Python Implementation Test Failures

## Overview

When running lit tests with `--use-python`, 11 out of 23 tests fail. This plan documents the root causes and fixes for each category of failure.

## Current Test Status

```
Passed: 12 (52%)
Failed: 11 (48%)
```

## Root Cause Analysis

### Category 1: Echo vmap Key Issue (6 tests)

**Tests Affected:**
- `atomics.ll`
- `echo.ll`
- `float_ops.ll`
- `freeze.ll`
- `invoke.ll`
- `memops.ll`

**Root Cause:**
The `clone_value` function in `echo.py` uses `id(src)` as a key to the `vmap` dictionary. Since nanobind returns new Python wrapper objects each time (even for the same underlying LLVM value), `id()` returns different values. However, the Value class correctly implements `__hash__` and `__eq__` based on the underlying LLVM pointer.

**Evidence:**
```python
# Testing shows id() differs but hash() and == work correctly:
param1 = fn.first_param()
param2 = fn.first_param()
id(param1) != id(param2)      # True - different wrapper objects
hash(param1) == hash(param2)  # True - same underlying pointer
param1 == param2              # True - equality works
d = {param1: 'value'}
d[param2]                     # 'value' - dict lookup works!
```

**Fix:**
Change dictionary keys from `id(src)` to `src` directly.

### Category 2: Memory Management Crashes (3 tests)

**Tests Affected:**
- `empty.ll` - diagnostic handler crash
- `functions.ll` - lazy-module-dump crash
- `objectfile.ll` - potential crash

**Root Cause:**
Memory corruption (double-free/use-after-free) in the bindings. The crash signatures show `free_medium_botch` errors. Per the memory model documentation, objects need to check validity tokens before accessing LLVM APIs.

**Specific Issues:**

1. **Diagnostic Handler (`empty.ll`):**
   - `context_set_diagnostic_handler` stores a callback that may be invoked after the associated objects are freed
   - Need to investigate callback lifetime management

2. **Lazy Module Dump (`functions.ll`):**
   - `parse_bitcode_in_context(ctx, membuf, lazy=True)` crashes on module disposal
   - The lazy-loaded module may have different lifetime requirements

3. **Object File (`objectfile.ll`):**
   - Test with null/empty input may trigger edge cases
   - May need better error handling

**Fix Strategy:**
- Investigate each crash individually
- Ensure validity tokens are checked before LLVM API calls
- May require C++ binding fixes for proper lifetime management

### Category 3: Error Message Format (1 test)

**Tests Affected:**
- `invalid-bitcode.test`

**Root Cause:**
Error message format differs between Python and C implementations.

**Expected (C version):**
```
Error parsing bitcode: Unknown attribute kind (255)
```

**Actual (Python version):**
```
Error: Unknown attribute kind (255)
```

**Fix:**
Update error message format in `module_ops.py` to match C version.

### Category 4: Debug Info (1 test)

**Tests Affected:**
- `debug_info_new_format.ll`

**Root Cause:**
Complex DIBuilder test - needs investigation to determine specific failure.

**Fix Strategy:**
Run test in isolation and analyze the specific failure point.

---

## Implementation Phases

### Phase 1: Fix Echo vmap Key Issue

**Priority:** High (fixes 6 tests, straightforward fix)
**Effort:** Low
**Risk:** Low

**File:** `llvm_c_test/echo.py`

**Changes:**
1. Update type annotations for `vmap` and `bb_map` dictionaries
2. Replace all `id(src)` dictionary keys with `src` directly
3. Same for `bb_map` with BasicBlock keys

**Lines to modify:**
- Line 322: `self.vmap: dict[int, llvm.Value]` → `self.vmap: dict[llvm.Value, llvm.Value]`
- Line 323: `self.bb_map: dict[int, llvm.BasicBlock]` → `self.bb_map: dict[llvm.BasicBlock, llvm.BasicBlock]`
- Line 354: `self.vmap[id(src_cur)]` → `self.vmap[src_cur]`
- Line 397-398: `if id(src) in self.vmap` → `if src in self.vmap`
- Line 439, 441: Same pattern
- Line 691: `self.vmap[id(src)]` → `self.vmap[src]`
- Line 935: `self.vmap[id(src)]` → `self.vmap[src]`
- Lines 950-951, 960: `bb_map` changes

### Phase 2: Fix Error Message Format

**Priority:** Medium (fixes 1 test, easy fix)
**Effort:** Low
**Risk:** Low

**File:** `llvm_c_test/module_ops.py`

**Changes:**
Update error message format to match C version:
- "Error parsing bitcode: ..." for bitcode errors
- "Error with new bitcode parser: ..." for new API errors

### Phase 3: Investigate Memory Crashes

**Priority:** High (3 tests, requires investigation)
**Effort:** Medium-High
**Risk:** Medium

**Strategy:**
1. Run each crashing test in isolation with debugging
2. Identify the specific API causing the crash
3. Check if validity tokens are being used correctly
4. May need C++ binding modifications

**Sub-tasks:**
- [ ] Investigate diagnostic handler crash
- [ ] Investigate lazy-module-dump crash
- [ ] Investigate objectfile test requirements

### Phase 4: Fix Debug Info Test

**Priority:** Low (1 test, complex)
**Effort:** Unknown
**Risk:** Unknown

**Strategy:**
1. Run `--test-dibuilder` in isolation
2. Capture full error output
3. Compare with C version output
4. Identify specific DIBuilder API issues

---

## Testing Strategy

```bash
# Run all tests with Python implementation
uv run run_llvm_c_tests.py --use-python

# Run specific test
cat llvm-c/llvm-c-test/inputs/atomics.ll | ./llvm-bin llvm-as | \
  uv run python -m llvm_c_test --echo

# Compare with C version
cat llvm-c/llvm-c-test/inputs/atomics.ll | ./llvm-bin llvm-as | \
  ./build/llvm-c-test --echo
```

---

## Success Criteria

All 23 lit tests pass with `--use-python`:
```bash
uv run run_llvm_c_tests.py --use-python
# Expected: 23 tests passed, 0 failed
```

---

## Summary Statistics

| Phase | Tests Fixed | Effort | Status |
|-------|-------------|--------|--------|
| Phase 1: vmap keys | 6 | Low | Pending |
| Phase 2: Error messages | 1 | Low | Pending |
| Phase 3: Memory crashes | 3 | Medium | Pending |
| Phase 4: Debug info | 1 | Unknown | Pending |
| **Total** | **11** | | |
