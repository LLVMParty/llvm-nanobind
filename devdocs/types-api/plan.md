# Types API Refactor

Refactor the llvm-nanobind API for improved ergonomics by moving type creation to a property-based namespace and constant creation to type methods.

**Part of the broader API refactor** - see `devdocs/api-design-philosophy.md` for principles.

---

## Design Principles Applied

This refactor applies the following principles from the API design philosophy:

1. **Property-Based Namespaces**: `ctx.types.i32` reveals structure and enables autocomplete
2. **Methods Belong to Objects**: `i32.constant(42)` instead of `llvm.const_int(i32, 42)`
3. **Discoverability**: Type operations grouped under `ctx.types`, constant ops on Type
4. **Flat Hierarchy with Assertions**: Runtime checks instead of dozens of type subclasses

---

## Goals

1. **Property-based types**: `ctx.types.i32` instead of `ctx.int32_type()`
2. **Type-scoped constants**: `i32.constant(42)` instead of `llvm.const_int(i32, 42)`
3. **Type-scoped composites**: `i32.array(10)` instead of `ctx.array_type(i32, 10)`
4. **Runtime assertions**: Type checks for operations like PHI node incoming values

## Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Hierarchy | Flat + assertions | Keep single Value/Type classes, add runtime type checks |
| Void naming | `ctx.types.void` | Not reserved in Python, accept future risk |
| Struct naming | `ctx.types.struct(...)` | Not reserved in Python |
| Int arbitrary | `ctx.types.int_n(bits)` | Avoids redundant "type" suffix |
| Float constants | `ty.real_constant(val)` | Clearer than `constant_real` |
| Null constants | `ty.null()` | Unified for all types (opaque pointers) |
| Deprecation | Remove immediately | No backward compatibility required |

---

## Phase 1: TypeFactory Namespace

Add `LLVMTypeFactoryWrapper` struct providing property-based access to types.

### New Struct

```cpp
struct LLVMTypeFactoryWrapper {
  LLVMContextRef m_ctx_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  void check_valid() const;

  // Fixed-width integer types (properties)
  LLVMTypeWrapper i1() const;
  LLVMTypeWrapper i8() const;
  LLVMTypeWrapper i16() const;
  LLVMTypeWrapper i32() const;
  LLVMTypeWrapper i64() const;
  LLVMTypeWrapper i128() const;

  // Floating-point types (properties)
  LLVMTypeWrapper f16() const;   // half
  LLVMTypeWrapper bf16() const;  // bfloat
  LLVMTypeWrapper f32() const;   // float
  LLVMTypeWrapper f64() const;   // double

  // Other types (properties)
  LLVMTypeWrapper void_() const;  // exposed as 'void' in Python
  LLVMTypeWrapper label() const;
  LLVMTypeWrapper metadata() const;
  LLVMTypeWrapper token() const;

  // Parameterized types (methods)
  LLVMTypeWrapper ptr(unsigned address_space = 0) const;
  LLVMTypeWrapper int_n(unsigned bits) const;
  LLVMTypeWrapper function(...) const;
  LLVMTypeWrapper struct_(...) const;  // exposed as 'struct' in Python
};
```

### Context Property

```cpp
// In LLVMContextWrapper
LLVMTypeFactoryWrapper get_types() {
  check_valid();
  return LLVMTypeFactoryWrapper(m_ref, m_token);
}
```

### Bindings

```cpp
nb::class_<LLVMTypeFactoryWrapper>(m, "TypeFactory")
    .def_prop_ro("i1", &LLVMTypeFactoryWrapper::i1)
    .def_prop_ro("i8", &LLVMTypeFactoryWrapper::i8)
    .def_prop_ro("i16", &LLVMTypeFactoryWrapper::i16)
    .def_prop_ro("i32", &LLVMTypeFactoryWrapper::i32)
    .def_prop_ro("i64", &LLVMTypeFactoryWrapper::i64)
    .def_prop_ro("i128", &LLVMTypeFactoryWrapper::i128)
    .def_prop_ro("f16", &LLVMTypeFactoryWrapper::f16)
    .def_prop_ro("bf16", &LLVMTypeFactoryWrapper::bf16)
    .def_prop_ro("f32", &LLVMTypeFactoryWrapper::f32)
    .def_prop_ro("f64", &LLVMTypeFactoryWrapper::f64)
    .def_prop_ro("void", &LLVMTypeFactoryWrapper::void_)
    .def_prop_ro("label", &LLVMTypeFactoryWrapper::label)
    .def_prop_ro("metadata", &LLVMTypeFactoryWrapper::metadata)
    .def_prop_ro("token", &LLVMTypeFactoryWrapper::token)
    .def("ptr", &LLVMTypeFactoryWrapper::ptr, "address_space"_a = 0)
    .def("int_n", &LLVMTypeFactoryWrapper::int_n, "bits"_a)
    .def("function", &LLVMTypeFactoryWrapper::function, ...)
    .def("struct", &LLVMTypeFactoryWrapper::struct_, ...);
```

---

## Phase 2: Type-Based Constant Creation

Add constant creation methods to `LLVMTypeWrapper`.

### New Methods

```cpp
// Integer constants
LLVMValueWrapper constant(long long val, bool sign_extend = false) const {
  check_valid();
  if (!is_integer())
    throw LLVMAssertionError("constant() requires integer type");
  return LLVMValueWrapper(LLVMConstInt(m_ref, val, sign_extend), m_context_token);
}

// Float constants
LLVMValueWrapper real_constant(double val) const {
  check_valid();
  if (!is_float())
    throw LLVMAssertionError("real_constant() requires floating-point type");
  return LLVMValueWrapper(LLVMConstReal(m_ref, val), m_context_token);
}

// Special values
LLVMValueWrapper null() const;      // Works for all types (opaque pointers)
LLVMValueWrapper all_ones() const;
LLVMValueWrapper undef() const;
LLVMValueWrapper poison() const;
```

### Composite Type Factories

```cpp
// i32.array(10) -> [10 x i32]
LLVMTypeWrapper array(uint64_t count) const {
  check_valid();
  return LLVMTypeWrapper(LLVMArrayType2(m_ref, count), m_context_token);
}

// i32.vector(4) -> <4 x i32>
LLVMTypeWrapper vector(unsigned count) const {
  check_valid();
  return LLVMTypeWrapper(LLVMVectorType(m_ref, count), m_context_token);
}

// i32.pointer(address_space=0) -> ptr
LLVMTypeWrapper pointer(unsigned address_space = 0) const {
  check_valid();
  return LLVMTypeWrapper(LLVMPointerTypeInContext(
      LLVMGetTypeContext(m_ref), address_space), m_context_token);
}
```

---

## Phase 3: Runtime Assertions

Add type validation to operations that are easy to misuse.

### PHI Node Validation

```cpp
void phi_add_incoming(LLVMValueWrapper &phi, const LLVMValueWrapper &val,
                      const LLVMBasicBlockWrapper &bb) {
  phi.check_valid();
  val.check_valid();
  bb.check_valid();

  // Check this is a PHI node
  if (!LLVMIsAPHINode(phi.m_ref))
    throw LLVMAssertionError("add_incoming() requires a PHI node");

  // Check type compatibility
  LLVMTypeRef phi_type = LLVMTypeOf(phi.m_ref);
  LLVMTypeRef val_type = LLVMTypeOf(val.m_ref);
  if (phi_type != val_type)
    throw LLVMAssertionError("PHI incoming value type mismatch");

  LLVMValueRef vals[] = {val.m_ref};
  LLVMBasicBlockRef bbs[] = {bb.m_ref};
  LLVMAddIncoming(phi.m_ref, vals, bbs, 1);
}
```

### Switch Case Validation

```cpp
if (LLVMGetInstructionOpcode(switch_inst.m_ref) != LLVMSwitch)
  throw LLVMAssertionError("add_case() requires a switch instruction");
if (!LLVMIsConstant(val.m_ref))
  throw LLVMAssertionError("Switch case value must be constant");
```

---

## Phase 4: Remove Old API

### From Context

- `int1_type()`, `int8_type()`, `int16_type()`, `int32_type()`, `int64_type()`, `int128_type()`
- `half_type()`, `bfloat_type()`, `float_type()`, `double_type()`
- `void_type()`, `label_type()`, `metadata_type()`, `token_type()`
- `pointer_type()`, `int_type()`
- `array_type()`, `vector_type()`, `struct_type()`, `function_type()`

### From Module Scope

- `const_int()`, `const_real()`, `const_null()`, `const_all_ones()`
- `undef()`, `poison()`, `const_pointer_null()`
- `const_array()`, `const_array2()`

---

## Phase 5: Test Updates

### Search-and-Replace Patterns

| Old | New |
|-----|-----|
| `ctx.int32_type()` | `ctx.types.i32` |
| `ctx.int64_type()` | `ctx.types.i64` |
| `ctx.int_type(n)` | `ctx.types.int_n(n)` |
| `ctx.float_type()` | `ctx.types.f32` |
| `ctx.double_type()` | `ctx.types.f64` |
| `ctx.void_type()` | `ctx.types.void` |
| `ctx.pointer_type()` | `ctx.types.ptr()` |
| `ctx.struct_type(...)` | `ctx.types.struct(...)` |
| `ctx.array_type(ty, n)` | `ty.array(n)` |
| `ctx.vector_type(ty, n)` | `ty.vector(n)` |
| `llvm.const_int(ty, v)` | `ty.constant(v)` |
| `llvm.const_real(ty, v)` | `ty.real_constant(v)` |
| `llvm.undef(ty)` | `ty.undef()` |
| `llvm.poison(ty)` | `ty.poison()` |
| `llvm.const_null(ty)` | `ty.null()` |
| `llvm.const_pointer_null(ty)` | `ty.null()` |

### Files to Update

1. `tests/test_types.py`
2. `tests/test_constants.py`
3. `tests/test_context.py`
4. `tests/test_module.py`
5. `tests/test_function.py`
6. `tests/test_basic_block.py`
7. `tests/test_builder_arithmetic.py`
8. `tests/test_builder_memory.py`
9. `tests/test_builder_control_flow.py`
10. `tests/test_builder_casts.py`
11. `tests/test_builder_cmp.py`
12. `tests/test_globals.py`
13. `tests/test_phi.py`
14. `tests/test_struct.py`
15. `tests/test_factorial.py`
16. `llvm_c_test/echo.py`

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/llvm-nanobind.cpp` | Add TypeFactory, Type methods, assertions, remove old API |
| `llvm_c_test/echo.py` | Update TypeCloner, constant cloning |
| `tests/test_*.py` | Update 15 golden master tests |

---

## Verification

After each phase:

```bash
cmake --build build        # Build bindings
uv sync                    # Verify stubs generate
uvx ty check               # Type checking
uv run run_tests.py        # Golden master tests
uv run run_llvm_c_tests.py --use-python  # Lit tests
```

---

## API Summary (After Refactor)

```python
import llvm

with llvm.create_context() as ctx:
    # Types via property namespace
    i32 = ctx.types.i32
    i64 = ctx.types.i64
    f64 = ctx.types.f64
    void = ctx.types.void
    ptr = ctx.types.ptr()       # default address space
    ptr1 = ctx.types.ptr(1)     # address space 1
    i256 = ctx.types.int_n(256) # arbitrary width

    # Composite types from element type
    arr = i32.array(10)         # [10 x i32]
    vec = f64.vector(4)         # <4 x f64>

    # Function types via TypeFactory
    fn_ty = ctx.types.function(i32, [i32, i32])

    # Struct types via TypeFactory
    struct_ty = ctx.types.struct([i32, f64], packed=False)

    # Constants from type
    const_42 = i32.constant(42)
    const_pi = f64.real_constant(3.14159)
    null_ptr = ptr.null()
    undef_val = i32.undef()

    with ctx.create_module("example") as mod:
        fn = mod.add_function("add", fn_ty)
        bb = fn.append_basic_block("entry")

        with ctx.create_builder() as builder:
            builder.position_at_end(bb)
            a, b = fn.get_param(0), fn.get_param(1)
            result = builder.add(a, b, "sum")
            builder.ret(result)
```
