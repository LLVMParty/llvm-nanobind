# Attribute API Refactor Progress

Tracking progress on attribute API refactor defined in `plan.md`.

## Quick Status

| Phase | Progress | Notes |
|-------|----------|-------|
| Function methods | ⬜ Not started | |
| CallInst methods | ⬜ Not started | |
| Context method | ⬜ Not started | |
| Remove old globals | ⬜ Not started | |
| Update tests | ⬜ Not started | |

---

## Phase 1: Function Attribute Methods

- [ ] Add `add_attribute(idx, attr)` to LLVMFunctionWrapper
- [ ] Add `get_enum_attribute(idx, kind)` to LLVMFunctionWrapper
- [ ] Add `get_attribute_count(idx)` to LLVMFunctionWrapper
- [ ] Bind methods on Function class

## Phase 2: Call Instruction Attribute Methods

- [ ] Add `add_attribute(idx, attr)` to LLVMValueWrapper (with call assertion)
- [ ] Add `get_enum_attribute(idx, kind)` to LLVMValueWrapper (with call assertion)
- [ ] Add `get_attribute_count(idx)` to LLVMValueWrapper (with call assertion)
- [ ] Bind methods on Value class

## Phase 3: Context Attribute Creation

- [ ] Add `create_enum_attribute(kind, val)` to LLVMContextWrapper
- [ ] Bind method on Context class

## Phase 4: Remove Old Globals

- [ ] Remove `add_attribute_at_index`
- [ ] Remove `get_enum_attribute_at_index`
- [ ] Remove `get_attribute_count_at_index`
- [ ] Remove `add_callsite_attribute`
- [ ] Remove `get_callsite_enum_attribute`
- [ ] Remove `get_callsite_attribute_count`
- [ ] Remove `create_enum_attribute`

## Phase 5: Update Tests

- [ ] Update `llvm_c_test/attributes.py`
- [ ] Update `llvm_c_test/echo.py` attribute cloning
- [ ] Run lit tests

---

## Verification

- [ ] Build succeeds
- [ ] Type stubs generate correctly
- [ ] `uvx ty check` passes
- [ ] `uv run run_llvm_c_tests.py --use-python` passes

---

## Session Log

### (Date) - Task Created
- Created plan.md with 7 functions to move
- Created progress.md for tracking
