# Module/Builder/Function API Refactor

Move miscellaneous global functions to their appropriate classes (Module, Builder, Function, Instruction).

**Part of the broader API refactor** - see `devdocs/api-design-philosophy.md` for principles.

---

## Design Principles Applied

1. **Methods Belong to Objects**: `mod.create_dibuilder()` instead of `llvm.create_dibuilder(mod)`
2. **Properties for Simple Accessors**: `mod.context` instead of `llvm.get_module_context(mod)`
3. **Discoverability**: Related operations grouped on their owning objects

---

## Goals

1. Move module operations to Module class
2. Move builder positioning to Builder class
3. Move function operations to Function class
4. Move instruction debug record access to Value class

---

## Functions to Move

### Module Operations → Module Methods/Properties

| Current | New | Type |
|---------|-----|------|
| `llvm.create_dibuilder(mod)` | `mod.create_dibuilder()` | method |
| `llvm.get_module_context(mod)` | `mod.context` | property |
| `llvm.is_new_dbg_info_format(mod)` | `mod.is_new_dbg_info_format` | property |
| `llvm.set_is_new_dbg_info_format(mod, val)` | `mod.is_new_dbg_info_format = val` | setter |
| `llvm.get_intrinsic_declaration(mod, id, types)` | `mod.get_intrinsic_declaration(id, types)` | method |

### Builder Operations → Builder Methods

| Current | New |
|---------|-----|
| `llvm.position_builder_before_dbg_records(b, bb, inst)` | `b.position_before_dbg_records(bb, inst)` |
| `llvm.position_builder_before_instr_and_dbg_records(b, inst)` | `b.position_before_instr_and_dbg_records(inst)` |

### Function Operations → Function Properties

| Current | New | Type |
|---------|-----|------|
| `llvm.set_subprogram(fn, sp)` | `fn.subprogram = sp` | setter |

### Instruction Debug Records → Value Properties

| Current | New | Type |
|---------|-----|------|
| `llvm.get_first_dbg_record(inst)` | `inst.first_dbg_record` | property |
| `llvm.get_last_dbg_record(inst)` | `inst.last_dbg_record` | property |

### Debug Record Navigation (if we wrap DbgRecord)

| Current | New | Type |
|---------|-----|------|
| `llvm.get_next_dbg_record(rec)` | `rec.next` | property |
| `llvm.get_previous_dbg_record(rec)` | `rec.prev` | property |

**Total: ~10 functions to move**

---

## Implementation

### Module Methods/Properties

```cpp
// In LLVMModuleWrapper
LLVMDIBuilderWrapper create_dibuilder() {
  check_valid();
  return LLVMDIBuilderWrapper(LLVMCreateDIBuilder(m_ref), m_context_token);
}

LLVMContextWrapper get_context() const {
  check_valid();
  return LLVMContextWrapper(LLVMGetModuleContext(m_ref), m_context_token);
}

bool is_new_dbg_info_format() const {
  check_valid();
  return LLVMIsNewDbgInfoFormat(m_ref);
}

void set_is_new_dbg_info_format(bool val) {
  check_valid();
  LLVMSetIsNewDbgInfoFormat(m_ref, val);
}

LLVMValueWrapper get_intrinsic_declaration(unsigned id,
                                           const std::vector<LLVMTypeWrapper>& types) {
  check_valid();
  std::vector<LLVMTypeRef> refs;
  for (const auto& t : types) {
    t.check_valid();
    refs.push_back(t.m_ref);
  }
  return LLVMValueWrapper(
      LLVMGetIntrinsicDeclaration(m_ref, id, refs.data(), refs.size()),
      m_context_token);
}
```

### Builder Methods

```cpp
// In LLVMBuilderWrapper
void position_before_dbg_records(const LLVMBasicBlockWrapper& bb,
                                  const LLVMValueWrapper& inst) {
  check_valid();
  bb.check_valid();
  inst.check_valid();
  LLVMPositionBuilderBeforeDbgRecords(m_ref, bb.m_ref, inst.m_ref);
}

void position_before_instr_and_dbg_records(const LLVMValueWrapper& inst) {
  check_valid();
  inst.check_valid();
  LLVMPositionBuilderBeforeInstrAndDbgRecords(m_ref, inst.m_ref);
}
```

### Function Properties

```cpp
// In LLVMFunctionWrapper (or LLVMValueWrapper with assertion)
void set_subprogram(const LLVMMetadataWrapper& sp) {
  check_valid();
  sp.check_valid();
  LLVMSetSubprogram(m_ref, sp.m_ref);
}
```

### Instruction Debug Record Properties

```cpp
// In LLVMValueWrapper
// Note: These return opaque pointers - may need a DbgRecord wrapper
void* get_first_dbg_record() const {
  check_valid();
  return LLVMGetFirstDbgRecord(m_ref);
}

void* get_last_dbg_record() const {
  check_valid();
  return LLVMGetLastDbgRecord(m_ref);
}
```

---

## Design Note: Debug Records

The debug record functions (`get_first_dbg_record`, `get_next_dbg_record`, etc.) return opaque `DbgRecord*` pointers. Two options:

1. **Keep as globals**: These work with opaque pointers, not wrapped types
2. **Create DbgRecord wrapper**: More Pythonic but requires new wrapper class

Recommendation: Keep these as globals for now (low priority), or create a minimal wrapper later.

---

## Bindings

```cpp
// Module
.def("create_dibuilder", &LLVMModuleWrapper::create_dibuilder)
.def_prop_ro("context", &LLVMModuleWrapper::get_context)
.def_prop_rw("is_new_dbg_info_format",
             &LLVMModuleWrapper::is_new_dbg_info_format,
             &LLVMModuleWrapper::set_is_new_dbg_info_format)
.def("get_intrinsic_declaration", &LLVMModuleWrapper::get_intrinsic_declaration,
     "id"_a, "types"_a)

// Builder
.def("position_before_dbg_records",
     &LLVMBuilderWrapper::position_before_dbg_records,
     "basic_block"_a, "instruction"_a)
.def("position_before_instr_and_dbg_records",
     &LLVMBuilderWrapper::position_before_instr_and_dbg_records,
     "instruction"_a)

// Function (or Value with assertion)
.def_prop_wo("subprogram", &LLVMFunctionWrapper::set_subprogram)
```

---

## Remove from Module Scope

- `create_dibuilder`
- `get_module_context`
- `is_new_dbg_info_format`
- `set_is_new_dbg_info_format`
- `get_intrinsic_declaration`
- `position_builder_before_dbg_records`
- `position_builder_before_instr_and_dbg_records`
- `set_subprogram`
- Optionally: `get_first_dbg_record`, `get_last_dbg_record`, `get_next_dbg_record`, `get_previous_dbg_record`

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/llvm-nanobind.cpp` | Add methods to Module/Builder/Function, remove globals |
| `llvm_c_test/*.py` | Update any files using these functions |

---

## Verification

```bash
cmake --build build
uvx ty check
uv run run_tests.py
uv run run_llvm_c_tests.py --use-python
```
