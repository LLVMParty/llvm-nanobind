# Types API Refactor

Refactored the llvm-nanobind API for improved ergonomics by moving type creation to a property-based namespace and constant creation to type methods.

## Summary

Replaced C-style global functions with a Pythonic property-based API:

| Old API | New API |
|---------|---------|
| `ctx.int32_type()` | `ctx.types.i32` |
| `ctx.int64_type()` | `ctx.types.i64` |
| `ctx.void_type()` | `ctx.types.void` |
| `ctx.pointer_type()` | `ctx.types.ptr()` |
| `ctx.function_type(ret, params)` | `ctx.types.function(ret, params)` |
| `ctx.struct_type([...])` | `ctx.types.struct([...])` |
| `ctx.named_struct_type("X")` | `ctx.types.opaque_struct("X")` |
| `ctx.get_type_by_name("X")` | `ctx.types.get("X")` |
| `ctx.array_type(ty, n)` | `ty.array(n)` |
| `ctx.vector_type(ty, n)` | `ty.vector(n)` |
| `llvm.const_int(ty, v)` | `ty.constant(v)` |
| `llvm.const_real(ty, v)` | `ty.real_constant(v)` |
| `llvm.undef(ty)` | `ty.undef()` |
| `llvm.poison(ty)` | `ty.poison()` |
| `llvm.const_null(ty)` | `ty.null()` |

## Implementation

### Phase 1: TypeFactory Namespace

Added `LLVMTypeFactoryWrapper` struct with:
- Integer type properties: `i1`, `i8`, `i16`, `i32`, `i64`, `i128`
- Float type properties: `f16`, `bf16`, `f32`, `f64`, `x86_fp80`, `fp128`, `ppc_fp128`
- Other type properties: `void`, `label`, `metadata`, `token`, `x86_amx`
- Parameterized methods: `ptr()`, `int_n()`, `function()`, `struct()`, `opaque_struct()`, `array()`, `vector()`, `scalable_vector()`, `target_ext()`, `get()`

Accessed via `ctx.types` property on Context.

### Phase 2: Type-Based Constant Creation

Added methods on `LLVMTypeWrapper`:
- `constant(val, sign_extend=False)` - integer constants
- `real_constant(val)` - floating-point constants
- `null()` - null/zero value
- `all_ones()` - all-ones value
- `undef()` - undefined value
- `poison()` - poison value
- `array(count)` - array type from element type
- `vector(count)` - vector type from element type
- `pointer(address_space=0)` - pointer type

### Phase 3: Runtime Assertions

Added validation to prevent common errors:
- PHI node type check in `phi_add_incoming()`
- PHI incoming value type mismatch check
- Switch instruction opcode check in `switch_add_case()`
- Switch case constant check

### Phase 4: Remove Old API

Removed from Context:
- All `*_type()` methods (int1_type, int8_type, float_type, void_type, etc.)
- `pointer_type()`, `int_type()`, `array_type()`, `vector_type()`, `struct_type()`, `function_type()`

Removed from module scope:
- `const_int()`, `const_real()`, `const_null()`, `const_all_ones()`
- `undef()`, `poison()`, `const_pointer_null()`

Kept for echo.py compatibility:
- `const_array`, `const_struct`, `const_vector`, `const_string`, `const_named_struct`
- `const_int_of_arbitrary_precision`, `const_data_array`, `const_bitcast`
- `const_gep_with_no_wrap_flags`, `const_ptr_auth`

### Phase 5: Test Updates

Updated all files to use new API:
- 15 golden master tests in `tests/`
- Regression tests in `tests/regressions/`
- `llvm_c_test/calc.py`, `llvm_c_test/echo.py`, `llvm_c_test/debuginfo.py`

## Key Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Hierarchy | Flat + assertions | Keep single Value/Type classes, add runtime type checks |
| Void naming | `ctx.types.void` | Not reserved in Python |
| Struct naming | `ctx.types.struct(...)` | Not reserved in Python |
| Int arbitrary | `ctx.types.int_n(bits)` | Avoids redundant "type" suffix |
| Float constants | `ty.real_constant(val)` | Clearer than `constant_real` |
| Null constants | `ty.null()` | Unified for all types (opaque pointers) |
| Deprecation | Remove immediately | No backward compatibility required |

## Example Usage

```python
import llvm

with llvm.create_context() as ctx:
    # Types via property namespace
    i32 = ctx.types.i32
    i64 = ctx.types.i64
    f64 = ctx.types.f64
    void = ctx.types.void
    ptr = ctx.types.ptr()       # default address space
    i256 = ctx.types.int_n(256) # arbitrary width

    # Composite types from element type
    arr = i32.array(10)         # [10 x i32]
    vec = f64.vector(4)         # <4 x f64>

    # Function and struct types
    fn_ty = ctx.types.function(i32, [i32, i32])
    struct_ty = ctx.types.struct([i32, f64], packed=False)

    # Constants from type
    const_42 = i32.constant(42)
    const_pi = f64.real_constant(3.14159)
    null_ptr = ptr.null()
    undef_val = i32.undef()
```

## Verification

All tests pass:
- `uv run run_tests.py` - 15/15 golden master tests
- `uv run run_llvm_c_tests.py --use-python` - 34/34 lit tests
- `uvx ty check` - All type checks pass
