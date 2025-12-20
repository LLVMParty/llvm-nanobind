# DIBuilder API Refactor

Move all 40+ `llvm.dibuilder_*` global functions to DIBuilder class methods.

**Part of the broader API refactor** - see `devdocs/api-design-philosophy.md` for principles.

---

## Design Principles Applied

1. **Methods Belong to Objects**: `dib.create_file(...)` instead of `llvm.dibuilder_create_file(dib, ...)`
2. **Discoverability**: All debug info creation visible via `dib.` autocomplete
3. **Logical Grouping**: All DIBuilder operations in one place

---

## Goals

1. Move all `llvm.dibuilder_*` functions to DIBuilder methods
2. Remove the `dibuilder_` prefix (redundant when on class)
3. Provide clean debug info creation API

---

## Functions to Move (40+)

### File & Scope Creation

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_file(dib, filename, directory)` | `dib.create_file(filename, directory)` |
| `llvm.dibuilder_create_compile_unit(dib, ...)` | `dib.create_compile_unit(...)` |
| `llvm.dibuilder_create_module(dib, ...)` | `dib.create_module(...)` |
| `llvm.dibuilder_create_namespace(dib, ...)` | `dib.create_namespace(...)` |
| `llvm.dibuilder_create_lexical_block(dib, ...)` | `dib.create_lexical_block(...)` |

### Function & Subroutine Creation

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_function(dib, ...)` | `dib.create_function(...)` |
| `llvm.dibuilder_create_subroutine_type(dib, ...)` | `dib.create_subroutine_type(...)` |

### Type Creation

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_basic_type(dib, ...)` | `dib.create_basic_type(...)` |
| `llvm.dibuilder_create_pointer_type(dib, ...)` | `dib.create_pointer_type(...)` |
| `llvm.dibuilder_create_vector_type(dib, ...)` | `dib.create_vector_type(...)` |
| `llvm.dibuilder_create_typedef(dib, ...)` | `dib.create_typedef(...)` |
| `llvm.dibuilder_create_struct_type(dib, ...)` | `dib.create_struct_type(...)` |
| `llvm.dibuilder_create_enumeration_type(dib, ...)` | `dib.create_enumeration_type(...)` |
| `llvm.dibuilder_create_forward_decl(dib, ...)` | `dib.create_forward_decl(...)` |
| `llvm.dibuilder_create_replaceable_composite_type(dib, ...)` | `dib.create_replaceable_composite_type(...)` |
| `llvm.dibuilder_create_subrange_type(dib, ...)` | `dib.create_subrange_type(...)` |
| `llvm.dibuilder_create_set_type(dib, ...)` | `dib.create_set_type(...)` |
| `llvm.dibuilder_create_dynamic_array_type(dib, ...)` | `dib.create_dynamic_array_type(...)` |

### Variable & Expression Creation

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_parameter_variable(dib, ...)` | `dib.create_parameter_variable(...)` |
| `llvm.dibuilder_create_auto_variable(dib, ...)` | `dib.create_auto_variable(...)` |
| `llvm.dibuilder_create_global_variable_expression(dib, ...)` | `dib.create_global_variable_expression(...)` |
| `llvm.dibuilder_create_expression(dib, ...)` | `dib.create_expression(...)` |
| `llvm.dibuilder_create_constant_value_expression(dib, ...)` | `dib.create_constant_value_expression(...)` |

### Debug Location

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_debug_location(ctx, ...)` | `ctx.create_debug_location(...)` or `dib.create_debug_location(...)` |

### Label Creation

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_label(dib, ...)` | `dib.create_label(...)` |
| `llvm.dibuilder_insert_label_at_end(dib, ...)` | `dib.insert_label_at_end(...)` |
| `llvm.dibuilder_insert_label_before(dib, ...)` | `dib.insert_label_before(...)` |

### Debug Record Insertion

| Current | New |
|---------|-----|
| `llvm.dibuilder_insert_declare_record_at_end(dib, ...)` | `dib.insert_declare_record_at_end(...)` |
| `llvm.dibuilder_insert_dbg_value_record_at_end(dib, ...)` | `dib.insert_dbg_value_record_at_end(...)` |

### Array & Subrange

| Current | New |
|---------|-----|
| `llvm.dibuilder_get_or_create_subrange(dib, ...)` | `dib.get_or_create_subrange(...)` |
| `llvm.dibuilder_get_or_create_array(dib, ...)` | `dib.get_or_create_array(...)` |

### Enumerator Creation

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_enumerator(dib, ...)` | `dib.create_enumerator(...)` |
| `llvm.dibuilder_create_enumerator_of_arbitrary_precision(dib, ...)` | `dib.create_enumerator_of_arbitrary_precision(...)` |

### ObjC & Inheritance

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_objc_property(dib, ...)` | `dib.create_objc_property(...)` |
| `llvm.dibuilder_create_objc_ivar(dib, ...)` | `dib.create_objc_ivar(...)` |
| `llvm.dibuilder_create_inheritance(dib, ...)` | `dib.create_inheritance(...)` |

### Imports & Macros

| Current | New |
|---------|-----|
| `llvm.dibuilder_create_imported_module_from_module(dib, ...)` | `dib.create_imported_module_from_module(...)` |
| `llvm.dibuilder_create_imported_module_from_alias(dib, ...)` | `dib.create_imported_module_from_alias(...)` |
| `llvm.dibuilder_create_temp_macro_file(dib, ...)` | `dib.create_temp_macro_file(...)` |
| `llvm.dibuilder_create_macro(dib, ...)` | `dib.create_macro(...)` |

### Utility

| Current | New |
|---------|-----|
| `llvm.replace_arrays(dib, ...)` | `dib.replace_arrays(...)` |
| `llvm.di_type_get_name(di_type)` | `di_type.name` (property on Metadata) |
| `llvm.get_di_node_tag(md)` | `md.di_node_tag` (property on Metadata) |

### Related Module Functions

| Current | New |
|---------|-----|
| `llvm.create_dibuilder(mod)` | `mod.create_dibuilder()` |
| `llvm.di_subprogram_replace_type(sp, type)` | `sp.replace_type(type)` (on Metadata) |

---

## Implementation Pattern

All methods follow the same pattern - remove the first `dib` argument since it's now `this`:

```cpp
// Current global function
LLVMMetadataRef dibuilder_create_file(LLVMDIBuilderWrapper& dib,
                                       const std::string& filename,
                                       const std::string& directory) {
  dib.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateFile(dib.m_ref, filename.c_str(), filename.size(),
                              directory.c_str(), directory.size()),
      dib.m_context_token);
}

// New method on LLVMDIBuilderWrapper
LLVMMetadataWrapper create_file(const std::string& filename,
                                const std::string& directory) {
  check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateFile(m_ref, filename.c_str(), filename.size(),
                              directory.c_str(), directory.size()),
      m_context_token);
}
```

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/llvm-nanobind.cpp` | Move 40+ functions to DIBuilder class methods |
| `llvm_c_test/debuginfo.py` | Update to use new API |

---

## Verification

```bash
cmake --build build
uvx ty check
uv run run_tests.py
uv run run_llvm_c_tests.py --use-python
```
