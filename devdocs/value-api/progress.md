# Value API Refactor Progress

Tracking progress on value API refactor defined in `plan.md`.

## Quick Status

| Phase | Progress | Notes |
|-------|----------|-------|
| Add Value properties | ⬜ Not started | |
| Add Value methods | ⬜ Not started | |
| Remove old globals | ⬜ Not started | |
| Update tests | ⬜ Not started | |

---

## Phase 1: Add Value Properties

- [ ] Add `is_null` property to LLVMValueWrapper
- [ ] Add `zext_value` property to LLVMValueWrapper
- [ ] Add `sext_value` property to LLVMValueWrapper
- [ ] Add `is_value_as_metadata` property to LLVMValueWrapper
- [ ] Bind properties with `def_prop_ro`

## Phase 2: Add Value Methods

- [ ] Add `const_bitcast(type)` method to LLVMValueWrapper
- [ ] Add `as_metadata()` method to LLVMValueWrapper
- [ ] Add `delete()` method to LLVMValueWrapper
- [ ] Bind methods with `def`

## Phase 3: Remove Old Globals

- [ ] Remove `value_is_null` from module scope
- [ ] Remove `const_int_get_zext_value` from module scope
- [ ] Remove `const_int_get_sext_value` from module scope
- [ ] Remove `const_bitcast` from module scope
- [ ] Remove `delete_instruction` from module scope
- [ ] Remove `is_a_value_as_metadata` from module scope
- [ ] Remove `value_as_metadata` from module scope

## Phase 4: Update Tests

- [ ] Update `llvm_c_test/echo.py` constant cloning
- [ ] Run golden master tests
- [ ] Run lit tests

---

## Verification

- [ ] Build succeeds
- [ ] Type stubs generate correctly
- [ ] `uvx ty check` passes
- [ ] `uv run run_tests.py` passes
- [ ] `uv run run_llvm_c_tests.py --use-python` passes

---

## Session Log

### (Date) - Task Created
- Created plan.md with 7 functions to move
- Created progress.md for tracking
