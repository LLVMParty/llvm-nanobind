# Attribute API Refactor

Move attribute management functions from module scope to Function and CallInst methods.

**Part of the broader API refactor** - see `devdocs/api-design-philosophy.md` for principles.

---

## Design Principles Applied

1. **Methods Belong to Objects**: `fn.add_attribute(idx, attr)` instead of `llvm.add_attribute_at_index(fn, idx, attr)`
2. **Logical Grouping**: All attribute operations on Function, call site ops on CallInst
3. **Discoverability**: Attribute methods visible on function/call objects

---

## Goals

1. Move function attribute operations to Function class
2. Move call site attribute operations to Value class (for call instructions)
3. Move attribute creation to Context class

---

## Functions to Move

### Function Attributes → Function Methods

| Current | New |
|---------|-----|
| `llvm.add_attribute_at_index(fn, idx, attr)` | `fn.add_attribute(idx, attr)` |
| `llvm.get_enum_attribute_at_index(fn, idx, kind)` | `fn.get_enum_attribute(idx, kind)` |
| `llvm.get_attribute_count_at_index(fn, idx)` | `fn.get_attribute_count(idx)` |

### Call Site Attributes → Value Methods (for call instructions)

| Current | New |
|---------|-----|
| `llvm.add_callsite_attribute(call, idx, attr)` | `call.add_attribute(idx, attr)` |
| `llvm.get_callsite_enum_attribute(call, idx, kind)` | `call.get_enum_attribute(idx, kind)` |
| `llvm.get_callsite_attribute_count(call, idx)` | `call.get_attribute_count(idx)` |

### Attribute Creation → Context Method

| Current | New |
|---------|-----|
| `llvm.create_enum_attribute(ctx, kind, val)` | `ctx.create_enum_attribute(kind, val)` |

### Keep Global

| Function | Reason |
|----------|--------|
| `llvm.get_last_enum_attribute_kind()` | Static registry lookup, no object context |

**Total: 7 functions to move, 1 stays global**

---

## Implementation

### Add to LLVMFunctionWrapper (or use Value with assertion)

```cpp
void add_attribute(unsigned idx, const LLVMAttributeWrapper& attr) {
  check_valid();
  attr.check_valid();
  LLVMAddAttributeAtIndex(m_ref, idx, attr.m_ref);
}

LLVMAttributeWrapper get_enum_attribute(unsigned idx, unsigned kind_id) {
  check_valid();
  LLVMAttributeRef attr = LLVMGetEnumAttributeAtIndex(m_ref, idx, kind_id);
  return LLVMAttributeWrapper(attr, m_context_token);
}

unsigned get_attribute_count(unsigned idx) {
  check_valid();
  return LLVMGetAttributeCountAtIndex(m_ref, idx);
}
```

### Add Call Site Methods to LLVMValueWrapper

```cpp
// These work on call instructions
void add_callsite_attribute(unsigned idx, const LLVMAttributeWrapper& attr) {
  check_valid();
  if (!LLVMIsACallInst(m_ref) && !LLVMIsAInvokeInst(m_ref))
    throw LLVMAssertionError("add_attribute() requires a call or invoke instruction");
  attr.check_valid();
  LLVMAddCallSiteAttribute(m_ref, idx, attr.m_ref);
}

// Similar for get_callsite_enum_attribute, get_callsite_attribute_count
```

### Add to LLVMContextWrapper

```cpp
LLVMAttributeWrapper create_enum_attribute(unsigned kind_id, uint64_t val) {
  check_valid();
  LLVMAttributeRef attr = LLVMCreateEnumAttribute(m_ref, kind_id, val);
  return LLVMAttributeWrapper(attr, m_token);
}
```

---

## Design Note: Unified Method Names

For call instructions, we could use the same method names as functions:
- `call.add_attribute(idx, attr)` - works because we assert it's a call instruction
- `call.get_enum_attribute(idx, kind)` - same

This provides a consistent API where both functions and call sites use the same method names.

---

## Remove from Module Scope

- `add_attribute_at_index`
- `get_enum_attribute_at_index`
- `get_attribute_count_at_index`
- `add_callsite_attribute`
- `get_callsite_enum_attribute`
- `get_callsite_attribute_count`
- `create_enum_attribute`

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/llvm-nanobind.cpp` | Add Function/Value/Context methods, remove globals |
| `llvm_c_test/attributes.py` | Update to use new API |
| `llvm_c_test/echo.py` | Update attribute cloning |

---

## Verification

```bash
cmake --build build
uvx ty check
uv run run_tests.py
uv run run_llvm_c_tests.py --use-python
```
