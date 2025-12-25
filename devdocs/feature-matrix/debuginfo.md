# DebugInfo.h Feature Matrix

LLVM-C Debug Info API implementation status with Python API mappings.

**Header:** `llvm-c/DebugInfo.h`

## Legend

| Status | Meaning |
|--------|---------|
| ✅ | Implemented |
| ❌ | Not implemented |

---

## Quick Start Example

```python
import llvm

with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        # Create DIBuilder
        with mod.create_dibuilder() as dib:
            # Create file and compile unit
            file = dib.create_file("main.c", "/path/to/src")
            cu = dib.create_compile_unit(
                lang=0x0001,  # DW_LANG_C
                file=file,
                producer="my-compiler",
                is_optimized=False,
                flags="",
                runtime_ver=0,
                split_name="",
                kind=1,  # Full debug info
                dwo_id=0,
                split_debug_inlining=True,
                debug_info_for_profiling=False,
                sys_root="",
                sdk=""
            )
            
            # Create function debug info
            fn_type = dib.create_subroutine_type(file, [], 0)
            fn_di = dib.create_function(
                scope=file,
                name="main",
                linkage_name="main",
                file=file,
                line_no=10,
                subroutine_type=fn_type,
                is_local_to_unit=False,
                is_definition=True,
                scope_line=10,
                flags=0,
                is_optimized=False
            )
            
            # Create debug location
            loc = llvm.dibuilder_create_debug_location(ctx, 10, 0, fn_di, None)
            
            # Finalize debug info
            dib.finalize()
```

---

## DIBuilder Management

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMCreateDIBuilder` | `mod.create_dibuilder()` | `with mod.create_dibuilder() as dib:` |
| `LLVMDisposeDIBuilder` | Context manager `__exit__` | Automatic |
| `LLVMDIBuilderFinalize` | `dib.finalize()` | `dib.finalize()` |
| `LLVMDIBuilderFinalizeSubprogram` | ✅ | `dib.finalize_subprogram(subprogram)` |

```python
with mod.create_dibuilder() as dib:
    # Create debug info...
    dib.finalize()  # Must call before exiting
```

---

## Compile Unit & File

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateFile` | `dib.create_file(filename, directory)` | `file = dib.create_file("main.c", ".")` |
| `LLVMDIBuilderCreateCompileUnit` | `dib.create_compile_unit(...)` | See example above |
| `LLVMDIBuilderCreateModule` | `dib.create_module(parent, name, ...)` | `mod_di = dib.create_module(cu, "mymod", "", "", "")` |
| `LLVMDIBuilderCreateNameSpace` | `dib.create_namespace(scope, name, export)` | `ns = dib.create_namespace(file, "myns", True)` |

---

## Functions & Scopes

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateFunction` | `dib.create_function(...)` | See example above |
| `LLVMDIBuilderCreateLexicalBlock` | `dib.create_lexical_block(scope, file, line, col)` | `block = dib.create_lexical_block(fn_di, file, 10, 0)` |
| `LLVMDIBuilderCreateLexicalBlockFile` | ✅ | `dib.create_lexical_block_file(scope, file, discriminator)` |
| `LLVMDIBuilderCreateSubroutineType` | `dib.create_subroutine_type(file, types, flags)` | `fn_ty = dib.create_subroutine_type(file, [ret, arg1], 0)` |

```python
# Create function type: void(int, float)
void_ty = None  # Use None for void return
int_ty = dib.create_basic_type("int", 32, 5, 0)  # DW_ATE_signed
float_ty = dib.create_basic_type("float", 32, 4, 0)  # DW_ATE_float
fn_type = dib.create_subroutine_type(file, [void_ty, int_ty, float_ty], 0)

# Create function debug info
fn_di = dib.create_function(
    scope=file,
    name="my_func",
    linkage_name="my_func",
    file=file,
    line_no=20,
    subroutine_type=fn_type,
    is_local_to_unit=False,
    is_definition=True,
    scope_line=20,
    flags=0,
    is_optimized=False
)

# Attach to LLVM function
fn.set_subprogram(fn_di)
```

---

## Debug Locations

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateDebugLocation` | `llvm.dibuilder_create_debug_location(ctx, line, col, scope, inlined_at)` | See below |
| `LLVMDILocationGetLine` | ✅ | `llvm.di_location_get_line(loc)` |
| `LLVMDILocationGetColumn` | ✅ | `llvm.di_location_get_column(loc)` |
| `LLVMDILocationGetScope` | ✅ | `llvm.di_location_get_scope(loc)` |
| `LLVMDILocationGetInlinedAt` | ✅ | `llvm.di_location_get_inlined_at(loc)` |

```python
# Create debug location
loc = llvm.dibuilder_create_debug_location(ctx, 25, 4, fn_di, None)

# Attach to builder for subsequent instructions
builder.current_debug_location = loc

# Now all instructions will have this location
val = builder.add(a, b, "sum")
```

---

## Basic Types

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateBasicType` | `dib.create_basic_type(name, bits, encoding, flags)` | See below |
| `LLVMDIBuilderCreatePointerType` | `dib.create_pointer_type(pointee, bits, align, as, name)` | See below |
| `LLVMDIBuilderCreateVectorType` | `dib.create_vector_type(bits, align, elem, subscripts)` | - |

```python
# Basic types (DWARF encoding values)
# DW_ATE_boolean = 2, DW_ATE_signed = 5, DW_ATE_unsigned = 7, DW_ATE_float = 4
int_ty = dib.create_basic_type("int", 32, 5, 0)      # signed int
uint_ty = dib.create_basic_type("unsigned int", 32, 7, 0)
float_ty = dib.create_basic_type("float", 32, 4, 0)
double_ty = dib.create_basic_type("double", 64, 4, 0)
bool_ty = dib.create_basic_type("bool", 8, 2, 0)

# Pointer type
ptr_ty = dib.create_pointer_type(int_ty, 64, 64, 0, "")
```

---

## Composite Types

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateStructType` | `dib.create_struct_type(...)` | See below |
| `LLVMDIBuilderCreateEnumerationType` | `dib.create_enumeration_type(...)` | See below |
| `LLVMDIBuilderCreateUnionType` | ✅ | `dib.create_union_type(scope, name, file, line, size, align, flags, elements, runtime_lang, unique_id)` |
| `LLVMDIBuilderCreateArrayType` | ✅ | `dib.create_array_type(size, align, elem_type, subscripts)` |
| `LLVMDIBuilderCreateMemberType` | ✅ | `dib.create_member_type(scope, name, file, line, size, align, offset, flags, type)` |
| `LLVMDIBuilderCreateForwardDecl` | `dib.create_forward_decl(...)` | - |
| `LLVMDIBuilderCreateReplaceableCompositeType` | `dib.create_replaceable_composite_type(...)` | - |
| `LLVMDIBuilderCreateTypedef` | `dib.create_typedef(type, name, file, line, scope, align)` | `td = dib.create_typedef(int_ty, "myint", file, 1, file, 0)` |

```python
# Struct type
struct_ty = dib.create_struct_type(
    scope=file,
    name="Point",
    file=file,
    line_number=5,
    size_in_bits=128,
    align_in_bits=64,
    flags=0,
    derived_from=None,
    elements=[],  # Add members later
    runtime_lang=0,
    vtable_holder=None,
    unique_id="Point"
)

# Enumeration type
enum_ty = dib.create_enumeration_type(
    scope=file,
    name="Color",
    file=file,
    line_number=10,
    size_in_bits=32,
    align_in_bits=32,
    elements=[
        dib.create_enumerator("Red", 0, False),
        dib.create_enumerator("Green", 1, False),
        dib.create_enumerator("Blue", 2, False),
    ],
    underlying_type=int_ty
)
```

---

## Variables

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateParameterVariable` | `dib.create_parameter_variable(...)` | See below |
| `LLVMDIBuilderCreateAutoVariable` | `dib.create_auto_variable(...)` | See below |
| `LLVMDIBuilderCreateGlobalVariableExpression` | `dib.create_global_variable_expression(...)` | See below |
| `LLVMDIBuilderInsertDeclareAtEnd` | ❌ | Use record versions |
| `LLVMDIBuilderInsertDeclareRecordAtEnd` | `dib.insert_declare_record_at_end(...)` | - |
| `LLVMDIBuilderInsertDbgValueRecordAtEnd` | `dib.insert_dbg_value_record_at_end(...)` | - |
| `LLVMDIBuilderInsertDeclareRecordBefore` | ❌ | TODO |
| `LLVMDIBuilderInsertDbgValueRecordBefore` | ❌ | TODO |

```python
# Parameter variable
param_var = dib.create_parameter_variable(
    scope=fn_di,
    name="x",
    arg_no=1,  # 1-indexed
    file=file,
    line_no=20,
    type=int_ty,
    always_preserve=True,
    flags=0
)

# Auto (local) variable
local_var = dib.create_auto_variable(
    scope=fn_di,
    name="result",
    file=file,
    line_no=22,
    type=int_ty,
    always_preserve=True,
    flags=0,
    align_in_bits=32
)

# Global variable
global_expr = dib.create_global_variable_expression(
    scope=file,
    name="global_counter",
    linkage="global_counter",
    file=file,
    line_no=5,
    type=int_ty,
    is_local_to_unit=False,
    expr=dib.create_expression([]),
    decl=None,
    align_in_bits=32
)
```

---

## Expressions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateExpression` | `dib.create_expression(addr)` | `expr = dib.create_expression([])` |
| `LLVMDIBuilderCreateConstantValueExpression` | `dib.create_constant_value_expression(val)` | `expr = dib.create_constant_value_expression(42)` |

---

## Subranges & Arrays

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderGetOrCreateSubrange` | `dib.get_or_create_subrange(lo, count)` | `sr = dib.get_or_create_subrange(0, 10)` |
| `LLVMDIBuilderGetOrCreateArray` | `dib.get_or_create_array(elements)` | `arr = dib.get_or_create_array([md1, md2])` |
| `LLVMDIBuilderGetOrCreateTypeArray` | ✅ | `dib.get_or_create_type_array(types)` |

---

## Enumerators

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateEnumerator` | `dib.create_enumerator(name, val, unsigned)` | `e = dib.create_enumerator("Red", 0, False)` |
| `LLVMDIBuilderCreateEnumeratorOfArbitraryPrecision` | `dib.create_enumerator_of_arbitrary_precision(...)` | - |

---

## Labels

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateLabel` | `dib.create_label(scope, name, file, line, preserve)` | `lbl = dib.create_label(fn_di, "loop", file, 30, True)` |
| `LLVMDIBuilderInsertLabelAtEnd` | `dib.insert_label_at_end(label, loc, bb)` | - |
| `LLVMDIBuilderInsertLabelBefore` | `dib.insert_label_before(label, loc, inst)` | - |

---

## Imports

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateImportedModuleFromNamespace` | ✅ | `dib.create_imported_module_from_namespace(scope, ns, file, line)` |
| `LLVMDIBuilderCreateImportedModuleFromAlias` | `dib.create_imported_module_from_alias(...)` | - |
| `LLVMDIBuilderCreateImportedModuleFromModule` | `dib.create_imported_module_from_module(...)` | - |
| `LLVMDIBuilderCreateImportedDeclaration` | ✅ | `dib.create_imported_declaration(scope, decl, file, line, name, elements)` |

---

## Macros

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateMacro` | `dib.create_macro(parent, line, type, name, val)` | - |
| `LLVMDIBuilderCreateTempMacroFile` | `dib.create_temp_macro_file(parent, line, file)` | - |

---

## ObjC Support

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateObjCProperty` | `dib.create_objc_property(...)` | - |
| `LLVMDIBuilderCreateObjCIVar` | `dib.create_objc_ivar(...)` | - |

---

## Inheritance

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMDIBuilderCreateInheritance` | `dib.create_inheritance(derived, base, offset, vbptr_offset, flags)` | - |

---

## Utility Functions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetDINodeTag` | `llvm.get_di_node_tag(md)` | `tag = llvm.get_di_node_tag(type_md)` |
| `LLVMDITypeGetName` | `llvm.di_type_get_name(di_type)` | `name = llvm.di_type_get_name(struct_ty)` |
| `LLVMDISubprogramReplaceType` | `llvm.di_subprogram_replace_type(sp, ty)` | - |
| `LLVMDebugMetadataVersion` | ✅ | `llvm.debug_metadata_version()` |
| `LLVMGetModuleDebugMetadataVersion` | ✅ | `llvm.get_module_debug_metadata_version(mod)` |
| `LLVMStripModuleDebugInfo` | ✅ | `llvm.strip_module_debug_info(mod)` |

---

## File/Scope Queries

| C API | Python API | Status |
|-------|------------|--------|
| `LLVMDIScopeGetFile` | ✅ | `llvm.di_scope_get_file(scope)` |
| `LLVMDIFileGetDirectory` | ✅ | `llvm.di_file_get_directory(file)` |
| `LLVMDIFileGetFilename` | ✅ | `llvm.di_file_get_filename(file)` |
| `LLVMDIFileGetSource` | ✅ | `llvm.di_file_get_source(file)` |
| `LLVMDISubprogramGetLine` | ✅ | `llvm.di_subprogram_get_line(subprogram)` |
| `LLVMDIVariableGetFile` | ✅ | `llvm.di_variable_get_file(variable)` |
| `LLVMDIVariableGetScope` | ✅ | `llvm.di_variable_get_scope(variable)` |
| `LLVMDIVariableGetLine` | ✅ | `llvm.di_variable_get_line(variable)` |

---

## Not Implemented

| C API | Status | Notes |
|-------|--------|-------|
| `LLVMDIBuilderCreateStaticMemberType` | ❌ | TODO |
| `LLVMDIBuilderCreateClassType` | ❌ | TODO |
| `LLVMDIBuilderCreateMemberPointerType` | ❌ | TODO |
| `LLVMDIBuilderCreateTempGlobalVariableFwdDecl` | ❌ | TODO |
| `LLVMDIGlobalVariableExpressionGetVariable` | ❌ | TODO |
| `LLVMDIGlobalVariableExpressionGetExpression` | ❌ | TODO |
| `LLVMTemporaryMDNode` | ❌ | TODO |
| `LLVMDisposeTemporaryMDNode` | ❌ | TODO |
| `LLVMMetadataReplaceAllUsesWith` | ✅ | `md.replace_all_uses_with(new)` |

## Recently Implemented (Session 5)

| C API | Python API |
|-------|------------|
| `LLVMDIBuilderCreateUnionType` | `dib.create_union_type(...)` |
| `LLVMDIBuilderCreateArrayType` | `dib.create_array_type(...)` |
| `LLVMDIBuilderCreateMemberType` | `dib.create_member_type(...)` |
| `LLVMDIBuilderCreateBitFieldMemberType` | `dib.create_bit_field_member_type(...)` |
| `LLVMDIBuilderCreateArtificialType` | `dib.create_artificial_type(type)` |
| `LLVMDIBuilderCreateQualifiedType` | `dib.create_qualified_type(tag, type)` |
| `LLVMDIBuilderCreateReferenceType` | `dib.create_reference_type(tag, type)` |
| `LLVMDIBuilderCreateNullPtrType` | `dib.create_null_ptr_type()` |
| `LLVMDIBuilderGetOrCreateTypeArray` | `dib.get_or_create_type_array(types)` |
| `LLVMDIBuilderFinalizeSubprogram` | `dib.finalize_subprogram(subprogram)` |
| `LLVMDIBuilderCreateLexicalBlockFile` | `dib.create_lexical_block_file(...)` |
| `LLVMDIBuilderCreateImportedDeclaration` | `dib.create_imported_declaration(...)` |
| `LLVMDIBuilderCreateImportedModuleFromNamespace` | `dib.create_imported_module_from_namespace(...)` |
| `LLVMDILocationGetLine` | `llvm.di_location_get_line(loc)` |
| `LLVMDILocationGetColumn` | `llvm.di_location_get_column(loc)` |
| `LLVMDILocationGetScope` | `llvm.di_location_get_scope(loc)` |
| `LLVMDILocationGetInlinedAt` | `llvm.di_location_get_inlined_at(loc)` |
| `LLVMDebugMetadataVersion` | `llvm.debug_metadata_version()` |
| `LLVMGetModuleDebugMetadataVersion` | `llvm.get_module_debug_metadata_version(mod)` |
| `LLVMStripModuleDebugInfo` | `llvm.strip_module_debug_info(mod)` |
| `LLVMDIScopeGetFile` | `llvm.di_scope_get_file(scope)` |
| `LLVMDIFileGetDirectory` | `llvm.di_file_get_directory(file)` |
| `LLVMDIFileGetFilename` | `llvm.di_file_get_filename(file)` |
| `LLVMDIFileGetSource` | `llvm.di_file_get_source(file)` |
| `LLVMDISubprogramGetLine` | `llvm.di_subprogram_get_line(sp)` |
| `LLVMDIVariableGetFile` | `llvm.di_variable_get_file(var)` |
| `LLVMDIVariableGetScope` | `llvm.di_variable_get_scope(var)` |
| `LLVMDIVariableGetLine` | `llvm.di_variable_get_line(var)` |
