# Types API Refactor Progress

Tracking progress on the types API refactor defined in `plan.md`.

## Quick Status

| Phase | Progress | Notes |
|-------|----------|-------|
| Phase 1: TypeFactory | ⬜ Not started | |
| Phase 2: Type Constants | ⬜ Not started | |
| Phase 3: Assertions | ⬜ Not started | |
| Phase 4: Remove Old API | ⬜ Not started | |
| Phase 5: Test Updates | ⬜ Not started | |

---

## Phase 1: TypeFactory Namespace

### C++ Implementation

- [ ] Add `LLVMTypeFactoryWrapper` struct to `src/llvm-nanobind.cpp`
- [ ] Add `check_valid()` method
- [ ] Add integer type properties: `i1`, `i8`, `i16`, `i32`, `i64`, `i128`
- [ ] Add float type properties: `f16`, `bf16`, `f32`, `f64`
- [ ] Add other type properties: `void_`, `label`, `metadata`, `token`
- [ ] Add parameterized methods: `ptr()`, `int_n()`, `function()`, `struct_()`
- [ ] Add `get_types()` method to `LLVMContextWrapper`

### Bindings

- [ ] Bind `TypeFactory` class with `nb::class_<LLVMTypeFactoryWrapper>`
- [ ] Bind all properties with `def_prop_ro`
- [ ] Bind parameterized methods with `def`
- [ ] Add `types` property to Context with `def_prop_ro`

### Verification

- [ ] Build succeeds
- [ ] Type stubs generate correctly
- [ ] `uvx ty check` passes with new API

---

## Phase 2: Type-Based Constant Creation

### Constant Methods

- [ ] Add `constant(val, sign_extend=false)` to `LLVMTypeWrapper`
- [ ] Add `real_constant(val)` to `LLVMTypeWrapper`
- [ ] Add `null()` to `LLVMTypeWrapper`
- [ ] Add `all_ones()` to `LLVMTypeWrapper`
- [ ] Add `undef()` to `LLVMTypeWrapper`
- [ ] Add `poison()` to `LLVMTypeWrapper`

### Composite Type Methods

- [ ] Add `array(count)` to `LLVMTypeWrapper`
- [ ] Add `vector(count)` to `LLVMTypeWrapper`
- [ ] Add `pointer(address_space=0)` to `LLVMTypeWrapper`

### Bindings

- [ ] Bind all new Type methods

### Verification

- [ ] Build succeeds
- [ ] Type stubs generate correctly

---

## Phase 3: Runtime Assertions

- [ ] Add PHI node type check to `phi_add_incoming()`
- [ ] Add PHI incoming value type mismatch check
- [ ] Add switch instruction opcode check to `switch_add_case()`
- [ ] Add switch case constant check

### Verification

- [ ] Create test case that triggers PHI assertion
- [ ] Create test case that triggers switch assertion
- [ ] Existing tests still pass

---

## Phase 4: Remove Old API

### From Context

- [ ] Remove `int1_type()`, `int8_type()`, `int16_type()`, `int32_type()`, `int64_type()`, `int128_type()`
- [ ] Remove `half_type()`, `bfloat_type()`, `float_type()`, `double_type()`
- [ ] Remove `void_type()`, `label_type()`, `metadata_type()`, `token_type()`
- [ ] Remove `pointer_type()`, `int_type()`
- [ ] Remove `array_type()`, `vector_type()`, `struct_type()`, `function_type()`

### From Module Scope

- [ ] Remove `const_int()`, `const_real()`
- [ ] Remove `const_null()`, `const_all_ones()`
- [ ] Remove `undef()`, `poison()`
- [ ] Remove `const_pointer_null()`
- [ ] Remove `const_array()`, `const_array2()`

---

## Phase 5: Test Updates

### Golden Master Tests

- [ ] `tests/test_types.py`
- [ ] `tests/test_constants.py`
- [ ] `tests/test_context.py`
- [ ] `tests/test_module.py`
- [ ] `tests/test_function.py`
- [ ] `tests/test_basic_block.py`
- [ ] `tests/test_builder_arithmetic.py`
- [ ] `tests/test_builder_memory.py`
- [ ] `tests/test_builder_control_flow.py`
- [ ] `tests/test_builder_casts.py`
- [ ] `tests/test_builder_cmp.py`
- [ ] `tests/test_globals.py`
- [ ] `tests/test_phi.py`
- [ ] `tests/test_struct.py`
- [ ] `tests/test_factorial.py`

### llvm_c_test

- [ ] `llvm_c_test/echo.py` - TypeCloner class
- [ ] `llvm_c_test/echo.py` - Constant cloning functions
- [ ] `llvm_c_test/calc.py` (if uses const_int)

### Final Verification

- [ ] `uv run run_tests.py` - All golden master tests pass
- [ ] `uv run run_llvm_c_tests.py --use-python` - All lit tests pass
- [ ] `uvx ty check` - Type checking passes

---

## Blockers & Issues

None currently.

---

## Session Log

### (Date) - Task Created
- Created plan.md with full API design
- Created progress.md for tracking
- Design decisions finalized:
  - Flat hierarchy with runtime assertions
  - `ctx.types.void` (no underscore)
  - `ctx.types.struct(...)` (not reserved)
  - `ctx.types.int_n(bits)` for arbitrary width
  - `ty.real_constant(val)` for float constants
  - `ty.null()` unified for all types
  - Remove old API immediately
