# Module/Builder/Function API Refactor Progress

Tracking progress on miscellaneous API refactor defined in `plan.md`.

## Quick Status

| Phase | Progress | Notes |
|-------|----------|-------|
| Module methods | ⬜ Not started | 5 functions |
| Builder methods | ⬜ Not started | 2 functions |
| Function properties | ⬜ Not started | 1 function |
| Debug record properties | ⬜ Not started | 2-4 functions |
| Remove old globals | ⬜ Not started | |
| Update tests | ⬜ Not started | |

---

## Phase 1: Module Methods/Properties

- [ ] Add `create_dibuilder()` method to Module
- [ ] Add `context` property to Module (read-only)
- [ ] Add `is_new_dbg_info_format` property to Module (read/write)
- [ ] Add `get_intrinsic_declaration(id, types)` method to Module
- [ ] Bind all new members

## Phase 2: Builder Methods

- [ ] Add `position_before_dbg_records(bb, inst)` to Builder
- [ ] Add `position_before_instr_and_dbg_records(inst)` to Builder
- [ ] Bind methods

## Phase 3: Function Properties

- [ ] Add `subprogram` setter to Function (write-only property)
- [ ] Bind property

## Phase 4: Debug Record Properties (Optional)

- [ ] Add `first_dbg_record` property to Value (for instructions)
- [ ] Add `last_dbg_record` property to Value (for instructions)
- [ ] Decide: Create DbgRecord wrapper or keep globals?

## Phase 5: Remove Old Globals

- [ ] Remove `create_dibuilder`
- [ ] Remove `get_module_context`
- [ ] Remove `is_new_dbg_info_format`
- [ ] Remove `set_is_new_dbg_info_format`
- [ ] Remove `get_intrinsic_declaration`
- [ ] Remove `position_builder_before_dbg_records`
- [ ] Remove `position_builder_before_instr_and_dbg_records`
- [ ] Remove `set_subprogram`

## Phase 6: Update Tests

- [ ] Update any llvm_c_test files using these functions
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
- Created plan.md with ~10 functions to move
- Created progress.md for tracking
