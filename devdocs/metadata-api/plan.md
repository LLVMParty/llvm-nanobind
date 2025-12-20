# Metadata API Refactor

Move metadata operations from module scope to appropriate classes (Module, Value, Instruction, Context, Metadata).

**Part of the broader API refactor** - see `devdocs/api-design-philosophy.md` for principles.

---

## Design Principles Applied

1. **Methods Belong to Objects**: `inst.set_metadata(kind, md)` instead of `llvm.set_metadata(inst, kind, md)`
2. **Logical Grouping**: Metadata operations distributed to their natural owners
3. **Consistency**: Each object manages its own metadata

---

## Goals

1. Move instruction metadata to Instruction/Value methods
2. Move module metadata to Module methods
3. Move metadata creation to Context methods
4. Move metadata conversion to Metadata methods

---

## Functions to Move

### Instruction Metadata → Value Methods

| Current | New |
|---------|-----|
| `llvm.set_metadata(inst, kind, md)` | `inst.set_metadata(kind, md)` |
| `llvm.global_set_metadata(val, kind, md)` | `val.set_metadata(kind, md)` |

### Module Metadata → Module Methods

| Current | New |
|---------|-----|
| `llvm.add_named_metadata_operand(mod, name, val)` | `mod.add_named_metadata_operand(name, val)` |

### Metadata Creation → Context Methods

| Current | New |
|---------|-----|
| `llvm.md_string_in_context_2(ctx, str)` | `ctx.md_string(str)` |
| `llvm.md_node_in_context_2(ctx, mds)` | `ctx.md_node(mds)` |

### Metadata Conversion → Metadata/Value Methods

| Current | New |
|---------|-----|
| `llvm.metadata_as_value(ctx, md)` | `md.as_value(ctx)` |
| `llvm.metadata_replace_all_uses_with(temp, md)` | `temp.replace_all_uses_with(md)` |

**Total: 7 functions to move**

---

## Implementation

### Add to LLVMValueWrapper

```cpp
void set_metadata(unsigned kind_id, const LLVMMetadataWrapper& md) {
  check_valid();
  md.check_valid();
  if (LLVMIsAInstruction(m_ref)) {
    LLVMSetMetadata(m_ref, kind_id, LLVMMetadataAsValue(
        LLVMGetModuleContext(LLVMGetGlobalParent(LLVMGetBasicBlockParent(
            LLVMGetInstructionParent(m_ref)))), md.m_ref));
  } else if (LLVMIsAGlobalValue(m_ref)) {
    LLVMGlobalSetMetadata(m_ref, kind_id, md.m_ref);
  } else {
    throw LLVMAssertionError("set_metadata requires an instruction or global value");
  }
}
```

### Add to LLVMModuleWrapper

```cpp
void add_named_metadata_operand(const std::string& name, const LLVMValueWrapper& val) {
  check_valid();
  val.check_valid();
  LLVMAddNamedMetadataOperand(m_ref, name.c_str(), val.m_ref);
}
```

### Add to LLVMContextWrapper

```cpp
LLVMMetadataWrapper md_string(const std::string& str) {
  check_valid();
  return LLVMMetadataWrapper(
      LLVMMDStringInContext2(m_ref, str.c_str(), str.size()),
      m_token);
}

LLVMMetadataWrapper md_node(const std::vector<LLVMMetadataWrapper>& mds) {
  check_valid();
  std::vector<LLVMMetadataRef> refs;
  for (const auto& md : mds) {
    md.check_valid();
    refs.push_back(md.m_ref);
  }
  return LLVMMetadataWrapper(
      LLVMMDNodeInContext2(m_ref, refs.data(), refs.size()),
      m_token);
}
```

### Add to LLVMMetadataWrapper

```cpp
LLVMValueWrapper as_value(const LLVMContextWrapper& ctx) const {
  check_valid();
  ctx.check_valid();
  return LLVMValueWrapper(LLVMMetadataAsValue(ctx.m_ref, m_ref), ctx.m_token);
}

void replace_all_uses_with(const LLVMMetadataWrapper& md) {
  check_valid();
  md.check_valid();
  LLVMMetadataReplaceAllUsesWith(m_ref, md.m_ref);
}
```

---

## Remove from Module Scope

- `set_metadata`
- `global_set_metadata`
- `add_named_metadata_operand`
- `md_string_in_context_2`
- `md_node_in_context_2`
- `metadata_as_value`
- `metadata_replace_all_uses_with`

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/llvm-nanobind.cpp` | Add methods to Value/Module/Context/Metadata, remove globals |
| `llvm_c_test/metadata.py` | Update to use new API |
| `llvm_c_test/echo.py` | Update metadata cloning |

---

## Verification

```bash
cmake --build build
uvx ty check
uv run run_tests.py
uv run run_llvm_c_tests.py --use-python
```
