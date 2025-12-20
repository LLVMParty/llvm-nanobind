# Value API Refactor

Move value inspection functions from module scope to Value class methods and properties.

**Part of the broader API refactor** - see `devdocs/api-design-philosophy.md` for principles.

---

## Design Principles Applied

1. **Methods Belong to Objects**: `val.is_null` instead of `llvm.value_is_null(val)`
2. **Discoverability**: Value properties visible via autocomplete on value objects
3. **Consistency**: Follows Python convention (`obj.property` for inspection)

---

## Goals

1. Move value inspection globals to Value properties
2. Move constant operations to Value methods
3. Add instruction-specific methods (e.g., `inst.delete()`)

---

## Functions to Move

| Current | New | Type |
|---------|-----|------|
| `llvm.value_is_null(val)` | `val.is_null` | property |
| `llvm.const_int_get_zext_value(val)` | `val.zext_value` | property |
| `llvm.const_int_get_sext_value(val)` | `val.sext_value` | property |
| `llvm.const_bitcast(val, ty)` | `val.const_bitcast(ty)` | method |
| `llvm.delete_instruction(inst)` | `inst.delete()` | method |
| `llvm.is_a_value_as_metadata(val)` | `val.is_value_as_metadata` | property |
| `llvm.value_as_metadata(val)` | `val.as_metadata()` | method |

**Total: 7 functions**

---

## Implementation

### Add Properties to LLVMValueWrapper

```cpp
// Properties
bool is_null() const {
  check_valid();
  return LLVMIsNull(m_ref);
}

long long zext_value() const {
  check_valid();
  if (!LLVMIsAConstantInt(m_ref))
    throw LLVMAssertionError("zext_value requires a constant integer");
  return LLVMConstIntGetZExtValue(m_ref);
}

long long sext_value() const {
  check_valid();
  if (!LLVMIsAConstantInt(m_ref))
    throw LLVMAssertionError("sext_value requires a constant integer");
  return LLVMConstIntGetSExtValue(m_ref);
}

bool is_value_as_metadata() const {
  check_valid();
  return LLVMIsAValueAsMetadata(m_ref) != nullptr;
}
```

### Add Methods to LLVMValueWrapper

```cpp
LLVMValueWrapper const_bitcast(const LLVMTypeWrapper& ty) const {
  check_valid();
  ty.check_valid();
  return LLVMValueWrapper(LLVMConstBitCast(m_ref, ty.m_ref), m_context_token);
}

LLVMMetadataWrapper as_metadata() const {
  check_valid();
  return LLVMMetadataWrapper(LLVMValueAsMetadata(m_ref), m_context_token);
}

void delete_() const {  // exposed as 'delete' in Python
  check_valid();
  LLVMInstructionEraseFromParent(m_ref);
}
```

### Bindings

```cpp
// Properties
.def_prop_ro("is_null", &LLVMValueWrapper::is_null)
.def_prop_ro("zext_value", &LLVMValueWrapper::zext_value)
.def_prop_ro("sext_value", &LLVMValueWrapper::sext_value)
.def_prop_ro("is_value_as_metadata", &LLVMValueWrapper::is_value_as_metadata)

// Methods
.def("const_bitcast", &LLVMValueWrapper::const_bitcast, "type"_a)
.def("as_metadata", &LLVMValueWrapper::as_metadata)
.def("delete", &LLVMValueWrapper::delete_)
```

---

## Remove from Module Scope

After adding to Value class, remove:
- `value_is_null`
- `const_int_get_zext_value`
- `const_int_get_sext_value`
- `const_bitcast`
- `delete_instruction`
- `is_a_value_as_metadata`
- `value_as_metadata`

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/llvm-nanobind.cpp` | Add Value properties/methods, remove globals |
| `llvm_c_test/echo.py` | Update constant cloning to use new API |

---

## Verification

```bash
cmake --build build
uv sync
uvx ty check
uv run run_tests.py
uv run run_llvm_c_tests.py --use-python
```
