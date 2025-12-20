# DIBuilder API Refactor Progress

Tracking progress on DIBuilder API refactor defined in `plan.md`.

## Quick Status

| Phase | Progress | Notes |
|-------|----------|-------|
| File/Scope methods | ⬜ Not started | 5 functions |
| Function methods | ⬜ Not started | 2 functions |
| Type methods | ⬜ Not started | 12 functions |
| Variable methods | ⬜ Not started | 5 functions |
| Label methods | ⬜ Not started | 3 functions |
| Debug record methods | ⬜ Not started | 2 functions |
| Array/Subrange methods | ⬜ Not started | 2 functions |
| Enumerator methods | ⬜ Not started | 2 functions |
| ObjC/Inheritance methods | ⬜ Not started | 3 functions |
| Import/Macro methods | ⬜ Not started | 4 functions |
| Remove old globals | ⬜ Not started | |
| Update tests | ⬜ Not started | |

**Total: 40+ functions**

---

## Phase 1: File & Scope Creation

- [ ] `create_file(filename, directory)`
- [ ] `create_compile_unit(...)`
- [ ] `create_module(...)`
- [ ] `create_namespace(...)`
- [ ] `create_lexical_block(...)`

## Phase 2: Function & Subroutine

- [ ] `create_function(...)`
- [ ] `create_subroutine_type(...)`

## Phase 3: Type Creation

- [ ] `create_basic_type(...)`
- [ ] `create_pointer_type(...)`
- [ ] `create_vector_type(...)`
- [ ] `create_typedef(...)`
- [ ] `create_struct_type(...)`
- [ ] `create_enumeration_type(...)`
- [ ] `create_forward_decl(...)`
- [ ] `create_replaceable_composite_type(...)`
- [ ] `create_subrange_type(...)`
- [ ] `create_set_type(...)`
- [ ] `create_dynamic_array_type(...)`
- [ ] `create_member_type(...)` (if exists)

## Phase 4: Variable & Expression

- [ ] `create_parameter_variable(...)`
- [ ] `create_auto_variable(...)`
- [ ] `create_global_variable_expression(...)`
- [ ] `create_expression(...)`
- [ ] `create_constant_value_expression(...)`

## Phase 5: Label Creation

- [ ] `create_label(...)`
- [ ] `insert_label_at_end(...)`
- [ ] `insert_label_before(...)`

## Phase 6: Debug Record Insertion

- [ ] `insert_declare_record_at_end(...)`
- [ ] `insert_dbg_value_record_at_end(...)`

## Phase 7: Array & Subrange

- [ ] `get_or_create_subrange(...)`
- [ ] `get_or_create_array(...)`

## Phase 8: Enumerator

- [ ] `create_enumerator(...)`
- [ ] `create_enumerator_of_arbitrary_precision(...)`

## Phase 9: ObjC & Inheritance

- [ ] `create_objc_property(...)`
- [ ] `create_objc_ivar(...)`
- [ ] `create_inheritance(...)`

## Phase 10: Import & Macro

- [ ] `create_imported_module_from_module(...)`
- [ ] `create_imported_module_from_alias(...)`
- [ ] `create_temp_macro_file(...)`
- [ ] `create_macro(...)`

## Phase 11: Utility

- [ ] `replace_arrays(...)`
- [ ] Move `di_type_get_name` to Metadata property
- [ ] Move `get_di_node_tag` to Metadata property

## Phase 12: Remove Old Globals

- [ ] Remove all `dibuilder_*` functions from module scope
- [ ] Remove `replace_arrays`
- [ ] Remove `di_type_get_name`
- [ ] Remove `get_di_node_tag`

## Phase 13: Update Tests

- [ ] Update `llvm_c_test/debuginfo.py`
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
- Created plan.md with 40+ functions to move
- Created progress.md for tracking
