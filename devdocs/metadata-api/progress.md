# Metadata API Refactor Progress

Tracking progress on metadata API refactor defined in `plan.md`.

## Quick Status

| Phase | Progress | Notes |
|-------|----------|-------|
| Value methods | ⬜ Not started | |
| Module methods | ⬜ Not started | |
| Context methods | ⬜ Not started | |
| Metadata methods | ⬜ Not started | |
| Remove old globals | ⬜ Not started | |
| Update tests | ⬜ Not started | |

---

## Phase 1: Value Metadata Methods

- [ ] Add `set_metadata(kind, md)` to LLVMValueWrapper
- [ ] Handle both instruction and global value cases
- [ ] Bind method on Value class

## Phase 2: Module Metadata Methods

- [ ] Add `add_named_metadata_operand(name, val)` to LLVMModuleWrapper
- [ ] Bind method on Module class

## Phase 3: Context Metadata Creation

- [ ] Add `md_string(str)` to LLVMContextWrapper
- [ ] Add `md_node(mds)` to LLVMContextWrapper
- [ ] Bind methods on Context class

## Phase 4: Metadata Conversion Methods

- [ ] Add `as_value(ctx)` to LLVMMetadataWrapper
- [ ] Add `replace_all_uses_with(md)` to LLVMMetadataWrapper
- [ ] Bind methods on Metadata class

## Phase 5: Remove Old Globals

- [ ] Remove `set_metadata`
- [ ] Remove `global_set_metadata`
- [ ] Remove `add_named_metadata_operand`
- [ ] Remove `md_string_in_context_2`
- [ ] Remove `md_node_in_context_2`
- [ ] Remove `metadata_as_value`
- [ ] Remove `metadata_replace_all_uses_with`

## Phase 6: Update Tests

- [ ] Update `llvm_c_test/metadata.py`
- [ ] Update `llvm_c_test/echo.py` metadata handling
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
