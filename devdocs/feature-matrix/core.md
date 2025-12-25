# Core.h Feature Matrix

LLVM-C Core API implementation status with Python API mappings.

**Total Functions:** ~640 (excluding deprecated)  
**Header:** `llvm-c/Core.h`

## Legend

| Status | Meaning |
|--------|---------|
| ✅ | Implemented |
| ❌ | Not implemented |
| 🚫 | Intentionally skipped (global context, unsafe, internal) |

---

## Quick Start Example

```python
import llvm

# Create context and module
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        # Create function type: i32 @add(i32, i32)
        i32 = ctx.types.i32
        fn_ty = ctx.types.function(i32, [i32, i32])
        
        # Add function and basic block
        fn = mod.add_function("add", fn_ty)
        bb = fn.append_basic_block("entry")
        
        # Build instructions
        with ctx.create_builder(bb) as b:
            result = b.add(fn.get_param(0), fn.get_param(1), "result")
            b.ret(result)
        
        print(mod)  # Print LLVM IR
```

---

## Context APIs

### Context Management

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMContextCreate` | `llvm.create_context()` | `with llvm.create_context() as ctx:` |
| `LLVMContextDispose` | Context manager `__exit__` | Automatic on `with` block exit |
| `LLVMGetGlobalContext` | 🚫 Not exposed | Use explicit contexts |
| `LLVMContextShouldDiscardValueNames` | `ctx.discard_value_names` | `if ctx.discard_value_names:` |
| `LLVMContextSetDiscardValueNames` | `ctx.discard_value_names = bool` | `ctx.discard_value_names = True` |

### Diagnostics

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMContextSetDiagnosticHandler` | Internal (auto-configured) | - |
| `LLVMGetDiagInfoDescription` | `Diagnostic.message` | `diag.message` |
| `LLVMGetDiagInfoSeverity` | `Diagnostic.severity` | `diag.severity` |

```python
try:
    mod = ctx.parse_ir("invalid syntax")
except llvm.LLVMParseError:
    for diag in ctx.get_diagnostics():
        print(f"{diag.severity}: {diag.message}")
```

### Metadata Kind IDs

| C API | Python API | Status |
|-------|------------|--------|
| `LLVMGetMDKindIDInContext` | ❌ | Not exposed directly |
| `LLVMGetMDKindID` | 🚫 | Uses global context |
| `LLVMGetSyncScopeID` | `ctx.get_sync_scope_id(name)` | Maps scope name to ID |

### Attributes

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetEnumAttributeKindForName` | `llvm.get_enum_attribute_kind_for_name(name)` | `kind = llvm.get_enum_attribute_kind_for_name("noinline")` |
| `LLVMGetLastEnumAttributeKind` | ❌ | Not implemented |
| `LLVMCreateEnumAttribute` | `ctx.create_enum_attribute(kind, val)` | `attr = ctx.create_enum_attribute(kind, 0)` |
| `LLVMGetEnumAttributeKind` | `attr.kind` | `kind_id = attr.kind` |
| `LLVMGetEnumAttributeValue` | `attr.value` | `val = attr.value` |
| `LLVMCreateTypeAttribute` | ❌ | TODO |
| `LLVMGetTypeAttributeValue` | ❌ | TODO |
| `LLVMCreateConstantRangeAttribute` | ❌ | TODO |
| `LLVMCreateStringAttribute` | ✅ | `ctx.create_string_attribute(key, val)` |
| `LLVMGetStringAttributeKind` | ✅ | `attr.string_kind` |
| `LLVMGetStringAttributeValue` | ✅ | `attr.string_value` |
| `LLVMIsEnumAttribute` | ✅ | `attr.is_enum_attribute` |
| `LLVMIsStringAttribute` | ✅ | `attr.is_string_attribute` |
| `LLVMIsTypeAttribute` | ✅ | `attr.is_type_attribute` |

```python
# Adding noinline attribute to a function
kind = llvm.get_enum_attribute_kind_for_name("noinline")
attr = ctx.create_enum_attribute(kind, 0)
fn.add_attribute(llvm.AttributeIndex.Function, attr)
```

### Type Lookup

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetTypeByName2` | `ctx.get_type_by_name(name)` | `struct_ty = ctx.get_type_by_name("MyStruct")` |

---

## Module APIs

### Module Creation & Lifecycle

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMModuleCreateWithNameInContext` | `ctx.create_module(name)` | `with ctx.create_module("mymod") as mod:` |
| `LLVMModuleCreateWithName` | 🚫 | Uses global context |
| `LLVMCloneModule` | `mod.clone()` | `mod_copy = mod.clone()` |
| `LLVMDisposeModule` | Context manager `__exit__` | Automatic |

### Module Properties

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetModuleIdentifier` | `mod.name` | `print(mod.name)` |
| `LLVMSetModuleIdentifier` | `mod.name = str` | `mod.name = "renamed"` |
| `LLVMGetSourceFileName` | `mod.source_filename` | `print(mod.source_filename)` |
| `LLVMSetSourceFileName` | `mod.source_filename = str` | `mod.source_filename = "main.c"` |
| `LLVMGetDataLayoutStr` | `mod.data_layout` | `print(mod.data_layout)` |
| `LLVMSetDataLayout` | `mod.data_layout = str` | `mod.data_layout = "e-m:e-i64:64-..."` |
| `LLVMGetTarget` | `mod.target_triple` | `print(mod.target_triple)` |
| `LLVMSetTarget` | `mod.target_triple = str` | `mod.target_triple = "x86_64-linux-gnu"` |

```python
with ctx.create_module("example") as mod:
    mod.name = "my_module"
    mod.source_filename = "source.c"
    mod.target_triple = "x86_64-unknown-linux-gnu"
    mod.data_layout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"
```

### Module Flags

| C API | Python API | Status |
|-------|------------|--------|
| `LLVMCopyModuleFlagsMetadata` | ❌ | TODO |
| `LLVMDisposeModuleFlagsMetadata` | ❌ | TODO |
| `LLVMModuleFlagEntriesGetFlagBehavior` | ❌ | TODO |
| `LLVMModuleFlagEntriesGetKey` | ❌ | TODO |
| `LLVMModuleFlagEntriesGetMetadata` | ❌ | TODO |
| `LLVMGetModuleFlag` | ✅ | `mod.get_module_flag(key)` |
| `LLVMAddModuleFlag` | ✅ | `mod.add_module_flag(behavior, key, val)` |

### Module Output

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMPrintModuleToString` | `str(mod)` or `mod.to_string()` | `ir = str(mod)` |
| `LLVMDumpModule` | ❌ | Use `print(mod)` instead |
| `LLVMPrintModuleToFile` | ✅ | `mod.print_to_file(filename)` |

```python
# Print module IR
print(mod)

# Save to file
with open("output.ll", "w") as f:
    f.write(str(mod))
```

### Module Inline Assembly

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetModuleInlineAsm` | `mod.inline_asm` | `asm = mod.inline_asm` |
| `LLVMSetModuleInlineAsm2` | `mod.inline_asm = str` | `mod.inline_asm = ".globl foo"` |
| `LLVMAppendModuleInlineAsm` | ❌ | TODO - use `mod.inline_asm += "..."` |

### Inline Assembly Values

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetInlineAsm` | `ctx.get_inline_asm(...)` | See example below |
| `LLVMGetInlineAsmAsmString` | `val.inline_asm_string` | `s = val.inline_asm_string` |
| `LLVMGetInlineAsmConstraintString` | `val.inline_asm_constraint_string` | `c = val.inline_asm_constraint_string` |
| `LLVMGetInlineAsmDialect` | `val.inline_asm_dialect` | `d = val.inline_asm_dialect` |
| `LLVMGetInlineAsmFunctionType` | `val.inline_asm_function_type` | `ty = val.inline_asm_function_type` |
| `LLVMGetInlineAsmHasSideEffects` | `val.inline_asm_has_side_effects` | `b = val.inline_asm_has_side_effects` |
| `LLVMGetInlineAsmCanUnwind` | `val.inline_asm_can_unwind` | - |

```python
# Create inline assembly
asm_ty = ctx.types.function(ctx.types.i32, [ctx.types.i32])
inline_asm = ctx.get_inline_asm(
    asm_ty, "mov $0, $1", "=r,r",
    has_side_effects=True, is_align_stack=False,
    dialect=llvm.InlineAsmDialect.ATT
)
result = builder.call(asm_ty, inline_asm, [value])
```

### Module Context

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetModuleContext` | `mod.context` | `ctx = mod.context` |

### Named Metadata

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetFirstNamedMetadata` | `mod.first_named_metadata` | `nmd = mod.first_named_metadata` |
| `LLVMGetLastNamedMetadata` | `mod.last_named_metadata` | `nmd = mod.last_named_metadata` |
| `LLVMGetNextNamedMetadata` | `nmd.next` | `nmd = nmd.next` |
| `LLVMGetPreviousNamedMetadata` | `nmd.prev` | `nmd = nmd.prev` |
| `LLVMGetNamedMetadata` | `mod.get_named_metadata(name)` | `nmd = mod.get_named_metadata("llvm.dbg.cu")` |
| `LLVMGetOrInsertNamedMetadata` | `mod.get_or_insert_named_metadata(name)` | `nmd = mod.get_or_insert_named_metadata("my.md")` |
| `LLVMGetNamedMetadataName` | `nmd.name` | `name = nmd.name` |
| `LLVMGetNamedMetadataNumOperands` | `mod.get_named_metadata_num_operands(name)` | `n = mod.get_named_metadata_num_operands("llvm.ident")` |
| `LLVMGetNamedMetadataOperands` | `mod.get_named_metadata_operands(name)` | `ops = mod.get_named_metadata_operands("llvm.ident")` |
| `LLVMAddNamedMetadataOperand` | `nmd.add_operand(val)` | `nmd.add_operand(md_node)` |

```python
# Add custom metadata
nmd = mod.get_or_insert_named_metadata("my.custom.metadata")
md = ctx.md_string("my metadata value")
nmd.add_operand(ctx.md_node([md]).as_value(ctx))
```

### Functions in Module

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMAddFunction` | `mod.add_function(name, ty)` | `fn = mod.add_function("foo", fn_ty)` |
| `LLVMGetNamedFunction` | `mod.get_function(name)` | `fn = mod.get_function("main")` |
| `LLVMGetNamedFunctionWithLength` | ❌ | Use `mod.get_function(name)` |
| `LLVMGetFirstFunction` | `mod.first_function` | `fn = mod.first_function` |
| `LLVMGetLastFunction` | `mod.last_function` | `fn = mod.last_function` |
| `LLVMGetNextFunction` | `fn.next_function` | `fn = fn.next_function` |
| `LLVMGetPreviousFunction` | `fn.prev_function` | `fn = fn.prev_function` |

```python
# Iterate over all functions
for fn in mod.functions:
    print(f"Function: {fn.name}")

# Or manually:
fn = mod.first_function
while fn:
    print(fn.name)
    fn = fn.next_function
```

### Debug Info Format

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMIsNewDbgInfoFormat` | `mod.is_new_dbg_info_format` | `if mod.is_new_dbg_info_format:` |
| `LLVMSetIsNewDbgInfoFormat` | `mod.set_new_dbg_info_format(bool)` | `mod.set_new_dbg_info_format(True)` |

---

## Type APIs

### Type Queries

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetTypeKind` | `ty.kind` | `if ty.kind == llvm.TypeKind.Integer:` |
| `LLVMTypeIsSized` | `ty.is_sized` | `if ty.is_sized:` |
| `LLVMGetTypeContext` | `ty.context` | `ctx = ty.context` |
| `LLVMPrintTypeToString` | `str(ty)` | `print(ty)` |
| `LLVMDumpType` | ❌ | Use `print(ty)` |

### Integer Types

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMInt1TypeInContext` | `ctx.types.i1` | `bool_ty = ctx.types.i1` |
| `LLVMInt8TypeInContext` | `ctx.types.i8` | `byte_ty = ctx.types.i8` |
| `LLVMInt16TypeInContext` | `ctx.types.i16` | `short_ty = ctx.types.i16` |
| `LLVMInt32TypeInContext` | `ctx.types.i32` | `int_ty = ctx.types.i32` |
| `LLVMInt64TypeInContext` | `ctx.types.i64` | `long_ty = ctx.types.i64` |
| `LLVMInt128TypeInContext` | `ctx.types.i128` | `i128_ty = ctx.types.i128` |
| `LLVMIntTypeInContext` | `ctx.types.int(bits)` | `i256_ty = ctx.types.int(256)` |
| `LLVMGetIntTypeWidth` | `ty.int_width` | `bits = ty.int_width` |
| `LLVMInt1Type` ... `LLVMIntType` | 🚫 | Use context versions |

```python
# Common integer types
i1 = ctx.types.i1    # bool
i8 = ctx.types.i8    # char/byte
i32 = ctx.types.i32  # int
i64 = ctx.types.i64  # long

# Custom width
i256 = ctx.types.int(256)
print(i256.int_width)  # 256
```

### Floating Point Types

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMHalfTypeInContext` | `ctx.types.f16` | `half_ty = ctx.types.f16` |
| `LLVMBFloatTypeInContext` | `ctx.types.bf16` | `bf16_ty = ctx.types.bf16` |
| `LLVMFloatTypeInContext` | `ctx.types.f32` | `float_ty = ctx.types.f32` |
| `LLVMDoubleTypeInContext` | `ctx.types.f64` | `double_ty = ctx.types.f64` |
| `LLVMX86FP80TypeInContext` | `ctx.types.x86_fp80` | `x86fp80 = ctx.types.x86_fp80` |
| `LLVMFP128TypeInContext` | `ctx.types.fp128` | `fp128 = ctx.types.fp128` |
| `LLVMPPCFP128TypeInContext` | `ctx.types.ppc_fp128` | `ppcfp128 = ctx.types.ppc_fp128` |
| Global versions | 🚫 | Use context versions |

```python
# Floating point types
f32 = ctx.types.f32   # float
f64 = ctx.types.f64   # double
f16 = ctx.types.f16   # half precision
```

### Function Types

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMFunctionType` | `ctx.types.function(ret, params, vararg=False)` | See below |
| `LLVMIsFunctionVarArg` | `ty.is_vararg` | `if fn_ty.is_vararg:` |
| `LLVMGetReturnType` | `ty.return_type` | `ret_ty = fn_ty.return_type` |
| `LLVMCountParamTypes` | `ty.param_count` | `n = fn_ty.param_count` |
| `LLVMGetParamTypes` | `ty.param_types` | `params = fn_ty.param_types` |

```python
# Create function type: i32 @fn(i32, i32)
fn_ty = ctx.types.function(ctx.types.i32, [ctx.types.i32, ctx.types.i32])

# Vararg function: i32 @printf(i8*, ...)
printf_ty = ctx.types.function(
    ctx.types.i32, 
    [ctx.types.i8.pointer()],
    vararg=True
)

# Inspect function type
print(fn_ty.return_type)    # i32
print(fn_ty.param_count)    # 2
print(fn_ty.param_types)    # [i32, i32]
print(fn_ty.is_vararg)      # False
```

### Structure Types

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMStructTypeInContext` | `ctx.types.struct(elements, packed=False)` | See below |
| `LLVMStructCreateNamed` | `ctx.types.named_struct(name)` | `ty = ctx.types.named_struct("Point")` |
| `LLVMGetStructName` | `ty.struct_name` | `name = ty.struct_name` |
| `LLVMStructSetBody` | `ty.set_body(elements, packed=False)` | `ty.set_body([i32, i32])` |
| `LLVMCountStructElementTypes` | `ty.struct_element_count` | `n = ty.struct_element_count` |
| `LLVMGetStructElementTypes` | `ty.struct_element_types` | `elems = ty.struct_element_types` |
| `LLVMStructGetTypeAtIndex` | `ty.get_struct_element_type(idx)` | `elem = ty.get_struct_element_type(0)` |
| `LLVMIsPackedStruct` | `ty.is_packed_struct` | `if ty.is_packed_struct:` |
| `LLVMIsOpaqueStruct` | `ty.is_opaque_struct` | `if ty.is_opaque_struct:` |
| `LLVMIsLiteralStruct` | ✅ | `ty.is_literal_struct` |
| `LLVMStructType` | 🚫 | Uses global context |

```python
# Anonymous struct: { i32, f64 }
anon_struct = ctx.types.struct([ctx.types.i32, ctx.types.f64])

# Named struct (forward declaration)
point = ctx.types.named_struct("Point")
point.set_body([ctx.types.f64, ctx.types.f64])  # { f64, f64 }

# Packed struct
packed = ctx.types.struct([ctx.types.i8, ctx.types.i32], packed=True)

# Inspect struct
print(point.struct_name)           # "Point"
print(point.struct_element_count)  # 2
print(point.get_struct_element_type(0))  # f64
```

### Sequential Types (Array, Vector, Pointer)

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMArrayType2` | `ty.array(count)` | `arr_ty = i32.array(10)` |
| `LLVMArrayType` | ❌ | Use `LLVMArrayType2` |
| `LLVMGetArrayLength2` | `ty.array_length` | `n = arr_ty.array_length` |
| `LLVMPointerType` | `ty.pointer(addrspace=0)` | `ptr_ty = i32.pointer()` |
| `LLVMPointerTypeInContext` | `ctx.types.ptr(addrspace=0)` | `ptr = ctx.types.ptr()` |
| `LLVMPointerTypeIsOpaque` | `ty.is_opaque_pointer` | `if ty.is_opaque_pointer:` |
| `LLVMGetPointerAddressSpace` | `ty.pointer_address_space` | `as = ty.pointer_address_space` |
| `LLVMVectorType` | `ty.vector(count)` | `vec_ty = i32.vector(4)` |
| `LLVMScalableVectorType` | `ctx.types.scalable_vector(elem, min_count)` | `ty = ctx.types.scalable_vector(i32, 4)` |
| `LLVMGetVectorSize` | `ty.vector_size` | `n = vec_ty.vector_size` |
| `LLVMGetElementType` | `ty.element_type` | `elem = arr_ty.element_type` |

```python
# Array type: [10 x i32]
arr_ty = ctx.types.i32.array(10)
print(arr_ty.array_length)   # 10
print(arr_ty.element_type)   # i32

# Pointer types
ptr = ctx.types.ptr()        # Opaque pointer (default)
ptr_as1 = ctx.types.ptr(1)   # Address space 1

# Vector type: <4 x float>
vec_ty = ctx.types.f32.vector(4)
print(vec_ty.vector_size)    # 4

# Scalable vector: <vscale x 4 x i32>
svec = ctx.types.scalable_vector(ctx.types.i32, 4)
```

### Other Types

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMVoidTypeInContext` | `ctx.types.void` | `void_ty = ctx.types.void` |
| `LLVMLabelTypeInContext` | `ctx.types.label` | `label_ty = ctx.types.label` |
| `LLVMTokenTypeInContext` | `ctx.types.token` | `token_ty = ctx.types.token` |
| `LLVMMetadataTypeInContext` | `ctx.types.metadata` | `md_ty = ctx.types.metadata` |
| `LLVMX86MMXTypeInContext` | ❌ | TODO |
| `LLVMX86AMXTypeInContext` | ❌ | TODO |
| Global versions | 🚫 | Use context versions |

```python
# Void function: void @foo()
void_fn_ty = ctx.types.function(ctx.types.void, [])
```

---

## Value APIs

### Value Properties

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMTypeOf` | `val.type` | `ty = val.type` |
| `LLVMGetValueKind` | `val.kind` | `if val.kind == llvm.ValueKind.Function:` |
| `LLVMGetValueName2` | `val.name` | `name = val.name` |
| `LLVMSetValueName2` | `val.name = str` | `val.name = "result"` |
| `LLVMPrintValueToString` | `str(val)` | `print(val)` |
| `LLVMDumpValue` | ❌ | Use `print(val)` |
| `LLVMReplaceAllUsesWith` | `val.replace_all_uses_with(new_val)` | `old.replace_all_uses_with(new)` |
| `LLVMIsConstant` | `val.is_constant` | `if val.is_constant:` |
| `LLVMIsUndef` | `val.is_undef` | `if val.is_undef:` |
| `LLVMIsPoison` | `val.is_poison` | `if val.is_poison:` |

```python
# Working with values
print(f"Value: {val}")
print(f"Type: {val.type}")
print(f"Name: {val.name}")

val.name = "my_result"
```

### Use Iteration

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetFirstUse` | `val.first_use` | `use = val.first_use` |
| `LLVMGetNextUse` | `use.next_use` | `use = use.next_use` |
| `LLVMGetUser` | `use.user` | `user = use.user` |
| `LLVMGetUsedValue` | `use.used_value` | `used = use.used_value` |

```python
# Iterate through all uses of a value
use = val.first_use
while use:
    print(f"Used by: {use.user}")
    use = use.next_use
```

### User/Operand Access

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetNumOperands` | `val.num_operands` | `n = val.num_operands` |
| `LLVMGetOperand` | `val.get_operand(idx)` | `op = val.get_operand(0)` |
| `LLVMSetOperand` | ✅ | `val.set_operand(index, val)` |
| `LLVMGetOperandUse` | ❌ | TODO |

---

## Constant APIs

### Scalar Constants

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMConstInt` | `ty.constant(val, sign_extend=False)` | `five = i32.constant(5)` |
| `LLVMConstIntOfArbitraryPrecision` | ❌ | TODO |
| `LLVMConstIntOfString` | ✅ | `ty.constant_from_string(text, radix)` |
| `LLVMConstIntOfStringAndSize` | ✅ | `ty.constant_from_string(text, radix)` |
| `LLVMConstReal` | `ty.real_constant(val)` | `pi = f64.real_constant(3.14159)` |
| `LLVMConstRealOfString` | ✅ | `ty.real_constant_from_string(text)` |
| `LLVMConstRealOfStringAndSize` | ✅ | `ty.real_constant_from_string(text)` |
| `LLVMConstIntGetZExtValue` | `val.zext_value` | `n = val.zext_value` |
| `LLVMConstIntGetSExtValue` | `val.sext_value` | `n = val.sext_value` |

```python
# Integer constants
i32 = ctx.types.i32
zero = i32.constant(0)
one = i32.constant(1)
neg_one = i32.constant(-1, sign_extend=True)

# Float constants
f64 = ctx.types.f64
pi = f64.real_constant(3.14159265359)

# Get value back
print(one.zext_value)  # 1
```

### Special Constants

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMConstNull` | `ty.null()` | `null_ptr = ptr_ty.null()` |
| `LLVMConstAllOnes` | `ty.all_ones()` | `all_ones = i32.all_ones()` |
| `LLVMGetUndef` | `ty.undef()` | `undef = i32.undef()` |
| `LLVMGetPoison` | `ty.poison()` | `poison = i32.poison()` |
| `LLVMIsNull` | `val.is_null` | `if val.is_null:` |

```python
# Special constants
null = ctx.types.ptr().null()
undef = ctx.types.i32.undef()
poison = ctx.types.i32.poison()
all_ones = ctx.types.i32.all_ones()  # -1 for signed
```

### Composite Constants

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMConstStringInContext` | `ctx.const_string(str, null_terminate=True)` | `s = ctx.const_string("hello")` |
| `LLVMConstStringInContext2` | Same as above | - |
| `LLVMConstStructInContext` | `ctx.const_struct(vals, packed=False)` | See below |
| `LLVMConstArray2` | `ctx.const_array(elem_ty, vals)` | See below |
| `LLVMConstNamedStruct` | `ty.const_named_struct(vals)` | `val = struct_ty.const_named_struct([x, y])` |
| `LLVMGetAggregateElement` | `val.get_aggregate_element(idx)` | `elem = val.get_aggregate_element(0)` |
| `LLVMConstVector` | ❌ | TODO |
| `LLVMConstArray` | ❌ | Use `LLVMConstArray2` |

```python
# String constant
hello = ctx.const_string("Hello, World!")

# Struct constant
point_val = ctx.const_struct([
    ctx.types.f64.real_constant(1.0),
    ctx.types.f64.real_constant(2.0)
])

# Array constant
arr = ctx.const_array(ctx.types.i32, [
    ctx.types.i32.constant(1),
    ctx.types.i32.constant(2),
    ctx.types.i32.constant(3)
])
```

### Constant Expressions

Most constant expression APIs are not exposed. Use Builder APIs instead for most operations.

| C API | Status | Alternative |
|-------|--------|-------------|
| `LLVMConstNeg`, `LLVMConstNot`, etc. | ❌ | Build in function, then use value |
| `LLVMConstAdd`, `LLVMConstSub`, etc. | ❌ | Build in function |
| `LLVMConstGEP2`, `LLVMConstInBoundsGEP2` | ❌ | Use builder.gep() |
| `LLVMConstTrunc`, `LLVMConstZExt`, etc. | ❌ | Use builder casts |
| `LLVMConstBitCast`, `LLVMConstAddrSpaceCast` | ❌ | Use builder casts |
| `LLVMConstICmp`, `LLVMConstFCmp` | ❌ | Use builder.icmp/fcmp |
| `LLVMConstSelect` | ❌ | Use builder.select |
| `LLVMBlockAddress` | ❌ | TODO |

---

## Global Value APIs

### Common Properties

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetLinkage` | `gv.linkage` | `link = gv.linkage` |
| `LLVMSetLinkage` | `gv.linkage = Linkage` | `gv.linkage = llvm.External` |
| `LLVMGetVisibility` | `gv.visibility` | `vis = gv.visibility` |
| `LLVMSetVisibility` | `gv.visibility = Visibility` | `gv.visibility = llvm.Hidden` |
| `LLVMGetUnnamedAddress` | `gv.unnamed_addr` | `ua = gv.unnamed_addr` |
| `LLVMSetUnnamedAddress` | `gv.unnamed_addr = UnnamedAddr` | `gv.unnamed_addr = llvm.Global` |
| `LLVMGetSection` | `gv.section` | `sec = gv.section` |
| `LLVMSetSection` | `gv.section = str` | `gv.section = ".text"` |
| `LLVMGetDLLStorageClass` | `gv.dll_storage_class` | `dsc = gv.dll_storage_class` |
| `LLVMSetDLLStorageClass` | `gv.dll_storage_class = ...` | `gv.dll_storage_class = llvm.DLLStorageClass.DLLExport` |
| `LLVMGetAlignment` | `gv.alignment` | `align = gv.alignment` |
| `LLVMSetAlignment` | `gv.alignment = int` | `gv.alignment = 16` |

```python
fn = mod.add_function("foo", fn_ty)
fn.linkage = llvm.Internal
fn.visibility = llvm.Hidden
fn.section = ".custom"
fn.alignment = 16
```

### Global Variables

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMAddGlobal` | `mod.add_global(ty, name)` | `g = mod.add_global(i32, "counter")` |
| `LLVMAddGlobalInAddressSpace` | `mod.add_global_in_address_space(ty, name, as)` | `g = mod.add_global_in_address_space(i32, "g", 1)` |
| `LLVMGetNamedGlobal` | `mod.get_global(name)` | `g = mod.get_global("counter")` |
| `LLVMGetFirstGlobal` | `mod.first_global` | `g = mod.first_global` |
| `LLVMGetLastGlobal` | `mod.last_global` | `g = mod.last_global` |
| `LLVMGetNextGlobal` | `gv.next_global` | `g = g.next_global` |
| `LLVMGetPreviousGlobal` | `gv.prev_global` | `g = g.prev_global` |
| `LLVMGetInitializer` | `gv.initializer` | `init = gv.initializer` |
| `LLVMSetInitializer` | `gv.initializer = val` | `gv.initializer = i32.constant(0)` |
| `LLVMIsGlobalConstant` | `gv.is_global_constant` | `if gv.is_global_constant:` |
| `LLVMSetGlobalConstant` | `gv.is_global_constant = bool` | `gv.is_global_constant = True` |
| `LLVMIsThreadLocal` | `gv.is_thread_local` | `if gv.is_thread_local:` |
| `LLVMSetThreadLocal` | `gv.is_thread_local = bool` | `gv.is_thread_local = True` |
| `LLVMIsExternallyInitialized` | `gv.externally_initialized` | `if gv.externally_initialized:` |
| `LLVMSetExternallyInitialized` | `gv.externally_initialized = bool` | `gv.externally_initialized = True` |

```python
# Create global variable
counter = mod.add_global(ctx.types.i32, "counter")
counter.initializer = ctx.types.i32.constant(0)
counter.is_global_constant = False
counter.linkage = llvm.Internal

# Iterate globals
for g in mod.globals:
    print(f"Global: {g.name}")
```

### Global Aliases

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMAddAlias2` | `mod.add_alias(val_ty, addr_space, aliasee, name)` | See below |
| `LLVMGetNamedGlobalAlias` | `mod.get_named_global_alias(name)` | `alias = mod.get_named_global_alias("foo_alias")` |
| `LLVMGetFirstGlobalAlias` | `mod.first_global_alias` | `alias = mod.first_global_alias` |
| `LLVMGetLastGlobalAlias` | `mod.last_global_alias` | `alias = mod.last_global_alias` |
| `LLVMGetNextGlobalAlias` | `alias.next_global_alias` | `alias = alias.next_global_alias` |
| `LLVMGetPreviousGlobalAlias` | `alias.prev_global_alias` | - |
| `LLVMAliasGetAliasee` | `alias.aliasee` | `target = alias.aliasee` |
| `LLVMAliasSetAliasee` | `alias.aliasee = val` | `alias.aliasee = fn` |

```python
# Create alias to function
fn = mod.add_function("original", fn_ty)
alias = mod.add_alias(fn_ty.pointer(), 0, fn, "alias_name")
```

### IFuncs (Indirect Functions)

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMAddGlobalIFunc` | `mod.add_global_ifunc(name, ty, addr_space, resolver)` | See below |
| `LLVMGetNamedGlobalIFunc` | `mod.get_named_global_ifunc(name)` | `ifunc = mod.get_named_global_ifunc("foo")` |
| `LLVMGetFirstGlobalIFunc` | `mod.first_global_ifunc` | `ifunc = mod.first_global_ifunc` |
| `LLVMGetLastGlobalIFunc` | `mod.last_global_ifunc` | - |
| `LLVMGetNextGlobalIFunc` | `ifunc.next_global_ifunc` | - |
| `LLVMGetPreviousGlobalIFunc` | `ifunc.prev_global_ifunc` | - |
| `LLVMGetGlobalIFuncResolver` | `ifunc.resolver` | `resolver = ifunc.resolver` |
| `LLVMSetGlobalIFuncResolver` | `ifunc.resolver = fn` | `ifunc.resolver = resolver_fn` |
| `LLVMEraseGlobalIFunc` | `ifunc.erase_from_parent_ifunc()` | Deletes IFunc from module |
| `LLVMRemoveGlobalIFunc` | `ifunc.remove_from_parent_ifunc()` | Removes but keeps alive (advanced) |

---

## Function APIs

### Function Properties

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetFunctionCallConv` | `fn.calling_conv` | `cc = fn.calling_conv` |
| `LLVMSetFunctionCallConv` | `fn.calling_conv = int` | `fn.calling_conv = llvm.CallConv.Fast` |
| `LLVMGetGC` | ✅ | `fn.get_gc()` |
| `LLVMSetGC` | ✅ | `fn.set_gc(name)` |
| `LLVMDeleteFunction` | `fn.delete()` | `fn.delete()` |
| `LLVMHasPersonalityFn` | `fn.has_personality_fn` | `if fn.has_personality_fn:` |
| `LLVMGetPersonalityFn` | `fn.personality_fn` | `pfn = fn.personality_fn` |
| `LLVMSetPersonalityFn` | `fn.personality_fn = fn` | `fn.personality_fn = gxx_personality` |
| `LLVMIntrinsicGetType` | ❌ | TODO |
| `LLVMLookupIntrinsicID` | ✅ | `llvm.lookup_intrinsic_id(name)` |
| `LLVMGetIntrinsicID` | `fn.intrinsic_id` | `id = fn.intrinsic_id` |
| `LLVMGetIntrinsicDeclaration` | `mod.get_intrinsic_declaration(id, param_types)` | See below |

```python
fn = mod.add_function("fast_fn", fn_ty)
fn.calling_conv = llvm.CallConv.Fast

# Get intrinsic
memcpy_id = llvm.lookup_intrinsic_id("llvm.memcpy")
memcpy = mod.get_intrinsic_declaration(memcpy_id, [...])
```

### Function Parameters

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMCountParams` | `fn.param_count` | `n = fn.param_count` |
| `LLVMGetParams` | `fn.params` | `params = fn.params` |
| `LLVMGetParam` | `fn.get_param(idx)` | `arg0 = fn.get_param(0)` |
| `LLVMGetParamParent` | `param.parent_function` | `fn = param.parent_function` |
| `LLVMGetFirstParam` | `fn.first_param()` | `p = fn.first_param()` |
| `LLVMGetLastParam` | `fn.last_param()` | `p = fn.last_param()` |
| `LLVMGetNextParam` | `param.next_param` | `p = p.next_param` |
| `LLVMGetPreviousParam` | `param.prev_param` | `p = p.prev_param` |

```python
fn = mod.add_function("add", ctx.types.function(i32, [i32, i32]))

# Access parameters
print(fn.param_count)  # 2
for i, param in enumerate(fn.params):
    param.name = f"arg{i}"

# Or by index
a = fn.get_param(0)
b = fn.get_param(1)
```

### Function Attributes

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetAttributeCountAtIndex` | `fn.get_attribute_count(idx)` | `n = fn.get_attribute_count(llvm.AttributeIndex.Function)` |
| `LLVMGetAttributesAtIndex` | ❌ | TODO |
| `LLVMGetEnumAttributeAtIndex` | `fn.get_enum_attribute(idx, kind)` | `attr = fn.get_enum_attribute(0, kind)` |
| `LLVMGetStringAttributeAtIndex` | ❌ | TODO |
| `LLVMAddAttributeAtIndex` | `fn.add_attribute(idx, attr)` | `fn.add_attribute(0, attr)` |
| `LLVMRemoveEnumAttributeAtIndex` | ❌ | TODO |
| `LLVMRemoveStringAttributeAtIndex` | ❌ | TODO |
| `LLVMAddTargetDependentFunctionAttr` | ❌ | TODO |

---

## BasicBlock APIs

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBasicBlockAsValue` | `bb.as_value()` | `val = bb.as_value()` |
| `LLVMValueIsBasicBlock` | `val.is_basic_block` | `if val.is_basic_block:` |
| `LLVMValueAsBasicBlock` | `val.as_basic_block()` | `bb = val.as_basic_block()` |
| `LLVMGetBasicBlockName` | `bb.name` | `name = bb.name` |
| `LLVMGetBasicBlockParent` | `bb.function` | `fn = bb.function` |
| `LLVMGetBasicBlockTerminator` | `bb.terminator` | `term = bb.terminator` |
| `LLVMCountBasicBlocks` | `fn.basic_block_count` | `n = fn.basic_block_count` |
| `LLVMGetBasicBlocks` | `fn.basic_blocks` | `bbs = fn.basic_blocks` |
| `LLVMGetFirstBasicBlock` | `fn.first_basic_block` | `bb = fn.first_basic_block` |
| `LLVMGetLastBasicBlock` | `fn.last_basic_block` | `bb = fn.last_basic_block` |
| `LLVMGetNextBasicBlock` | `bb.next_block` | `bb = bb.next_block` |
| `LLVMGetPreviousBasicBlock` | `bb.prev_block` | `bb = bb.prev_block` |
| `LLVMGetEntryBasicBlock` | `fn.entry_block` | `entry = fn.entry_block` |
| `LLVMAppendBasicBlockInContext` | `fn.append_basic_block(name)` | `bb = fn.append_basic_block("entry")` |
| `LLVMAppendBasicBlock` | 🚫 | Uses global context |
| `LLVMInsertBasicBlockInContext` | `ctx.create_basic_block(name)` | `bb = ctx.create_basic_block("temp")` |
| `LLVMInsertBasicBlock` | 🚫 | Uses global context |
| `LLVMDeleteBasicBlock` | `bb.delete()` | `bb.delete()` |
| `LLVMRemoveBasicBlockFromParent` | `bb.remove_from_parent()` | `bb.remove_from_parent()` |
| `LLVMMoveBasicBlockBefore` | `bb.move_before(other)` | `bb.move_before(other_bb)` |
| `LLVMMoveBasicBlockAfter` | `bb.move_after(other)` | `bb.move_after(other_bb)` |
| `LLVMGetFirstInstruction` | `bb.first_instruction` | `inst = bb.first_instruction` |
| `LLVMGetLastInstruction` | `bb.last_instruction` | `inst = bb.last_instruction` |

```python
# Create basic blocks
entry = fn.append_basic_block("entry")
then_bb = fn.append_basic_block("then")
else_bb = fn.append_basic_block("else")
merge_bb = fn.append_basic_block("merge")

# Iterate basic blocks
for bb in fn.basic_blocks:
    print(f"Block: {bb.name}")
    for inst in bb.instructions:
        print(f"  {inst}")

# Check terminator
if bb.has_terminator:
    print(f"Terminator: {bb.terminator}")
```

---

## Instruction APIs

### Instruction Properties

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetInstructionParent` | `inst.parent` | `bb = inst.parent` |
| `LLVMGetNextInstruction` | `inst.next_instruction` | `next = inst.next_instruction` |
| `LLVMGetPreviousInstruction` | `inst.prev_instruction` | `prev = inst.prev_instruction` |
| `LLVMInstructionRemoveFromParent` | `inst.remove_from_parent()` | `inst.remove_from_parent()` |
| `LLVMInstructionEraseFromParent` | `inst.erase_from_parent()` | `inst.erase_from_parent()` |
| `LLVMInstructionClone` | `inst.clone()` | `copy = inst.clone()` |
| `LLVMGetInstructionOpcode` | `inst.opcode` | `op = inst.opcode` |
| `LLVMGetICmpPredicate` | `inst.icmp_predicate` | `pred = inst.icmp_predicate` |
| `LLVMGetFCmpPredicate` | `inst.fcmp_predicate` | `pred = inst.fcmp_predicate` |
| `LLVMIsTerminatorInst` | Via `bb.terminator` | `if inst == bb.terminator:` |

### Instruction Metadata

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMHasMetadata` | `inst.has_metadata` | `if inst.has_metadata:` |
| `LLVMGetMetadata` | `inst.get_metadata(kind)` | `md = inst.get_metadata(kind_id)` |
| `LLVMSetMetadata` | `inst.set_metadata(kind, md)` | `inst.set_metadata(kind_id, md)` |
| `LLVMInstructionGetAllMetadataOtherThanDebugLoc` | `inst.get_all_metadata()` | `entries = inst.get_all_metadata()` |

---

## Call Site APIs

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMGetNumArgOperands` | `call.num_arg_operands` | `n = call.num_arg_operands` |
| `LLVMSetInstructionCallConv` | `call.call_conv = int` | `call.call_conv = llvm.CallConv.Fast` |
| `LLVMGetInstructionCallConv` | `call.call_conv` | `cc = call.call_conv` |
| `LLVMIsTailCall` | `call.is_tail_call` | `if call.is_tail_call:` |
| `LLVMSetTailCall` | `call.is_tail_call = bool` | `call.is_tail_call = True` |
| `LLVMGetTailCallKind` | `call.tail_call_kind` | `kind = call.tail_call_kind` |
| `LLVMSetTailCallKind` | `call.tail_call_kind = kind` | `call.tail_call_kind = llvm.TailCallKind.Tail` |
| `LLVMGetCalledFunctionType` | `call.called_function_type` | `fn_ty = call.called_function_type` |
| `LLVMGetCalledValue` | `call.called_value` | `fn = call.called_value` |
| `LLVMGetCallSiteAttributeCount` | ❌ | TODO |
| `LLVMAddCallSiteAttribute` | `call.add_call_site_attribute(idx, attr)` | `call.add_call_site_attribute(0, attr)` |
| `LLVMGetCallSiteEnumAttribute` | ❌ | TODO |
| `LLVMGetOperandBundleAtIndex` | `call.get_operand_bundle_at_index(idx)` | `bundle = call.get_operand_bundle_at_index(0)` |
| `LLVMGetNumOperandBundles` | `call.num_operand_bundles` | `n = call.num_operand_bundles` |

```python
# Make a tail call
call = builder.call(fn_ty, fn, args, "result")
call.is_tail_call = True
call.tail_call_kind = llvm.TailCallKind.MustTail
```

---

## Builder APIs

### Builder Management

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMCreateBuilderInContext` | `ctx.create_builder(bb)` | `with ctx.create_builder(bb) as b:` |
| `LLVMCreateBuilder` | 🚫 | Uses global context |
| `LLVMPositionBuilder` | ❌ | TODO |
| `LLVMPositionBuilderBefore` | `b.position_before(inst)` | `b.position_before(term)` |
| `LLVMPositionBuilderAtEnd` | `b.position_at_end(bb)` | `b.position_at_end(bb)` |
| `LLVMGetInsertBlock` | `b.insert_block` | `bb = b.insert_block` |
| `LLVMClearInsertionPosition` | ❌ | TODO |
| `LLVMInsertIntoBuilder` | ❌ | Use specific build methods |
| `LLVMInsertIntoBuilderWithName` | `b.insert_into_builder_with_name(inst, name)` | - |
| `LLVMDisposeBuilder` | Context manager `__exit__` | Automatic |
| `LLVMGetCurrentDebugLocation2` | `b.current_debug_location` | `loc = b.current_debug_location` |
| `LLVMSetCurrentDebugLocation2` | `b.current_debug_location = md` | `b.current_debug_location = loc_md` |
| `LLVMBuilderGetDefaultFPMathTag` | ❌ | TODO |
| `LLVMBuilderSetDefaultFPMathTag` | ❌ | TODO |

```python
with ctx.create_builder(entry_bb) as b:
    # Build instructions...
    result = b.add(a, b, "result")
    
    # Reposition builder
    b.position_at_end(other_bb)
    b.position_before(some_inst)
```

### Arithmetic Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildAdd` | `b.add(lhs, rhs, name)` | `sum = b.add(a, b, "sum")` |
| `LLVMBuildNSWAdd` | `b.nsw_add(lhs, rhs, name)` | `sum = b.nsw_add(a, b)` |
| `LLVMBuildNUWAdd` | `b.nuw_add(lhs, rhs, name)` | `sum = b.nuw_add(a, b)` |
| `LLVMBuildSub` | `b.sub(lhs, rhs, name)` | `diff = b.sub(a, b)` |
| `LLVMBuildNSWSub` | `b.nsw_sub(lhs, rhs, name)` | - |
| `LLVMBuildNUWSub` | `b.nuw_sub(lhs, rhs, name)` | - |
| `LLVMBuildMul` | `b.mul(lhs, rhs, name)` | `prod = b.mul(a, b)` |
| `LLVMBuildNSWMul` | `b.nsw_mul(lhs, rhs, name)` | - |
| `LLVMBuildNUWMul` | `b.nuw_mul(lhs, rhs, name)` | - |
| `LLVMBuildUDiv` | `b.udiv(lhs, rhs, name)` | `quot = b.udiv(a, b)` |
| `LLVMBuildSDiv` | `b.sdiv(lhs, rhs, name)` | `quot = b.sdiv(a, b)` |
| `LLVMBuildExactSDiv` | `b.exact_sdiv(lhs, rhs, name)` | - |
| `LLVMBuildExactUDiv` | ❌ | TODO |
| `LLVMBuildURem` | `b.urem(lhs, rhs, name)` | `rem = b.urem(a, b)` |
| `LLVMBuildSRem` | `b.srem(lhs, rhs, name)` | - |
| `LLVMBuildFAdd` | `b.fadd(lhs, rhs, name)` | `sum = b.fadd(x, y)` |
| `LLVMBuildFSub` | `b.fsub(lhs, rhs, name)` | - |
| `LLVMBuildFMul` | `b.fmul(lhs, rhs, name)` | - |
| `LLVMBuildFDiv` | `b.fdiv(lhs, rhs, name)` | - |
| `LLVMBuildFRem` | `b.frem(lhs, rhs, name)` | - |
| `LLVMBuildNeg` | `b.neg(val, name)` | `neg = b.neg(x)` |
| `LLVMBuildNSWNeg` | `b.nsw_neg(val, name)` | - |
| `LLVMBuildNUWNeg` | ❌ | TODO |
| `LLVMBuildFNeg` | `b.fneg(val, name)` | `neg = b.fneg(x)` |
| `LLVMBuildNot` | `b.not_(val, name)` | `inv = b.not_(x)` |

```python
# Arithmetic example
with ctx.create_builder(bb) as b:
    a = fn.get_param(0)
    b_param = fn.get_param(1)
    
    sum_val = b.add(a, b_param, "sum")
    diff = b.sub(a, b_param, "diff")
    prod = b.mul(a, b_param, "prod")
    quot = b.sdiv(a, b_param, "quot")
```

### Bitwise Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildShl` | `b.shl(lhs, rhs, name)` | `shifted = b.shl(x, n)` |
| `LLVMBuildLShr` | `b.lshr(lhs, rhs, name)` | `shifted = b.lshr(x, n)` |
| `LLVMBuildAShr` | `b.ashr(lhs, rhs, name)` | `shifted = b.ashr(x, n)` |
| `LLVMBuildAnd` | `b.and_(lhs, rhs, name)` | `masked = b.and_(x, mask)` |
| `LLVMBuildOr` | `b.or_(lhs, rhs, name)` | `combined = b.or_(a, b)` |
| `LLVMBuildXor` | `b.xor(lhs, rhs, name)` | `toggled = b.xor(x, mask)` |
| `LLVMBuildBinOp` | `b.binop(opcode, lhs, rhs, name)` | - |

### Memory Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildAlloca` | `b.alloca(ty, name)` | `ptr = b.alloca(i32, "x")` |
| `LLVMBuildArrayAlloca` | `b.array_alloca(ty, size, name)` | `arr = b.array_alloca(i32, n, "arr")` |
| `LLVMBuildLoad2` | `b.load(ty, ptr, name)` | `val = b.load(i32, ptr, "val")` |
| `LLVMBuildStore` | `b.store(val, ptr)` | `b.store(val, ptr)` |
| `LLVMBuildGEP2` | `b.gep(ty, ptr, indices, name)` | See below |
| `LLVMBuildInBoundsGEP2` | `b.inbounds_gep(ty, ptr, indices, name)` | See below |
| `LLVMBuildStructGEP2` | `b.struct_gep(ty, ptr, idx, name)` | See below |
| `LLVMBuildGEPWithNoWrapFlags` | `b.gep_with_no_wrap_flags(...)` | - |
| `LLVMGetVolatile` | `inst.is_volatile` | `if load.is_volatile:` |
| `LLVMSetVolatile` | `inst.is_volatile = bool` | `store.is_volatile = True` |
| `LLVMGetOrdering` | `inst.ordering` | `ord = inst.ordering` |
| `LLVMSetOrdering` | `inst.ordering = AtomicOrdering` | `inst.ordering = llvm.SequentiallyConsistent` |

```python
# Stack allocation
ptr = b.alloca(ctx.types.i32, "local_var")

# Load and store
val = b.load(ctx.types.i32, ptr, "loaded")
b.store(ctx.types.i32.constant(42), ptr)

# GEP for array access: arr[i]
elem_ptr = b.gep(arr_ty, arr_ptr, [i32.constant(0), index], "elem_ptr")

# Struct member access: point->x
x_ptr = b.struct_gep(point_ty, point_ptr, 0, "x_ptr")
```

### Comparison Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildICmp` | `b.icmp(pred, lhs, rhs, name)` | `cmp = b.icmp(llvm.IntPredicate.EQ, a, b)` |
| `LLVMBuildFCmp` | `b.fcmp(pred, lhs, rhs, name)` | `cmp = b.fcmp(llvm.RealPredicate.OLT, x, y)` |

```python
# Integer comparison
is_equal = b.icmp(llvm.IntPredicate.EQ, a, b, "eq")
is_less = b.icmp(llvm.IntPredicate.SLT, a, b, "lt")
is_unsigned_greater = b.icmp(llvm.IntPredicate.UGT, a, b)

# Float comparison
is_less_float = b.fcmp(llvm.RealPredicate.OLT, x, y, "lt")
```

### Cast Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildTrunc` | `b.trunc(val, ty, name)` | `i8_val = b.trunc(i32_val, i8)` |
| `LLVMBuildZExt` | `b.zext(val, ty, name)` | `i64_val = b.zext(i32_val, i64)` |
| `LLVMBuildSExt` | `b.sext(val, ty, name)` | `i64_val = b.sext(i32_val, i64)` |
| `LLVMBuildFPToUI` | `b.fptoui(val, ty, name)` | `int_val = b.fptoui(float_val, i32)` |
| `LLVMBuildFPToSI` | `b.fptosi(val, ty, name)` | `int_val = b.fptosi(float_val, i32)` |
| `LLVMBuildUIToFP` | `b.uitofp(val, ty, name)` | `float_val = b.uitofp(uint_val, f64)` |
| `LLVMBuildSIToFP` | `b.sitofp(val, ty, name)` | `float_val = b.sitofp(int_val, f64)` |
| `LLVMBuildFPTrunc` | `b.fptrunc(val, ty, name)` | `f32_val = b.fptrunc(f64_val, f32)` |
| `LLVMBuildFPExt` | `b.fpext(val, ty, name)` | `f64_val = b.fpext(f32_val, f64)` |
| `LLVMBuildPtrToInt` | `b.ptrtoint(val, ty, name)` | `int_val = b.ptrtoint(ptr, i64)` |
| `LLVMBuildIntToPtr` | `b.inttoptr(val, ty, name)` | `ptr = b.inttoptr(int_val, ptr_ty)` |
| `LLVMBuildBitCast` | `b.bitcast(val, ty, name)` | `cast = b.bitcast(val, other_ty)` |
| `LLVMBuildIntCast2` | `b.int_cast2(val, ty, is_signed, name)` | - |
| `LLVMBuildAddrSpaceCast` | ✅ | `b.addr_space_cast(val, ty, name)` |
| `LLVMBuildZExtOrBitCast` | ❌ | TODO |
| `LLVMBuildSExtOrBitCast` | ❌ | TODO |
| `LLVMBuildTruncOrBitCast` | ❌ | TODO |
| `LLVMBuildCast` | ❌ | TODO |
| `LLVMBuildPointerCast` | ❌ | TODO |
| `LLVMBuildFPCast` | ❌ | TODO |
| `LLVMGetCastOpcode` | ❌ | TODO |

```python
# Extend i32 to i64
i64_val = b.sext(i32_val, ctx.types.i64, "extended")

# Convert float to int
int_val = b.fptosi(float_val, ctx.types.i32, "as_int")

# Truncate
i8_val = b.trunc(i32_val, ctx.types.i8, "truncated")
```

### Terminator Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildRet` | `b.ret(val)` | `b.ret(result)` |
| `LLVMBuildRetVoid` | `b.ret_void()` | `b.ret_void()` |
| `LLVMBuildBr` | `b.br(dest)` | `b.br(next_bb)` |
| `LLVMBuildCondBr` | `b.cond_br(cond, then_bb, else_bb)` | `b.cond_br(cond, then_bb, else_bb)` |
| `LLVMBuildSwitch` | `b.switch_(val, default_bb, n)` | See below |
| `LLVMBuildUnreachable` | `b.unreachable()` | `b.unreachable()` |
| `LLVMBuildResume` | `b.resume(exn)` | - |
| `LLVMBuildIndirectBr` | ❌ | TODO |
| `LLVMAddCase` | `switch.add_case(val, bb)` | `sw.add_case(i32.constant(1), case1_bb)` |
| `LLVMAddDestination` | ❌ | TODO |
| `LLVMGetNumSuccessors` | `term.num_successors` | `n = term.num_successors` |
| `LLVMGetSuccessor` | `term.get_successor(idx)` | `bb = term.get_successor(0)` |
| `LLVMSetSuccessor` | `term.set_successor(idx, bb)` | - |

```python
# Unconditional branch
b.br(next_bb)

# Conditional branch
cond = b.icmp(llvm.IntPredicate.EQ, x, zero)
b.cond_br(cond, then_bb, else_bb)

# Switch
sw = b.switch_(val, default_bb, 3)
sw.add_case(i32.constant(0), case0_bb)
sw.add_case(i32.constant(1), case1_bb)
sw.add_case(i32.constant(2), case2_bb)

# Return
b.ret(result)
b.ret_void()
```

### Call Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildCall2` | `b.call(fn_ty, fn, args, name)` | `result = b.call(fn_ty, fn, [a, b], "res")` |
| `LLVMBuildCallWithOperandBundles` | `b.call_with_operand_bundles(...)` | - |
| `LLVMBuildInvoke2` | `b.invoke(fn_ty, fn, args, normal, unwind, name)` | See below |
| `LLVMBuildInvokeWithOperandBundles` | `b.invoke_with_operand_bundles(...)` | - |

```python
# Simple call
result = b.call(add_fn_ty, add_fn, [x, y], "sum")

# Invoke with exception handling
result = b.invoke(fn_ty, fn, args, normal_bb, unwind_bb, "result")
```

### PHI Nodes

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildPhi` | `b.phi(ty, name)` | `phi = b.phi(i32, "result")` |
| `LLVMAddIncoming` | `phi.add_incoming(vals, bbs)` | `phi.add_incoming([v1, v2], [bb1, bb2])` |
| `LLVMCountIncoming` | `phi.count_incoming` | `n = phi.count_incoming` |
| `LLVMGetIncomingValue` | `phi.get_incoming_value(idx)` | `val = phi.get_incoming_value(0)` |
| `LLVMGetIncomingBlock` | `phi.get_incoming_block(idx)` | `bb = phi.get_incoming_block(0)` |

```python
# PHI node for loop variable
with ctx.create_builder(loop_bb) as b:
    phi = b.phi(ctx.types.i32, "i")
    # Add incoming values after creating the blocks
    phi.add_incoming([init_val, inc_val], [entry_bb, loop_bb])
```

### Vector Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildExtractElement` | `b.extract_element(vec, idx, name)` | `elem = b.extract_element(vec, i)` |
| `LLVMBuildInsertElement` | `b.insert_element(vec, val, idx, name)` | `new_vec = b.insert_element(vec, val, i)` |
| `LLVMBuildShuffleVector` | `b.shuffle_vector(v1, v2, mask, name)` | - |
| `LLVMGetNumMaskElements` | `inst.num_mask_elements` | - |
| `LLVMGetMaskValue` | `inst.get_mask_value(idx)` | - |
| `LLVMGetUndefMaskElem` | `llvm.get_undef_mask_elem()` | - |

### Aggregate Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildExtractValue` | `b.extract_value(agg, idx, name)` | `elem = b.extract_value(struct_val, 0)` |
| `LLVMBuildInsertValue` | `b.insert_value(agg, val, idx, name)` | `new = b.insert_value(struct_val, val, 0)` |

### Other Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildSelect` | `b.select(cond, then_val, else_val, name)` | `val = b.select(cond, a, b)` |
| `LLVMBuildFreeze` | `b.freeze(val, name)` | `frozen = b.freeze(val)` |
| `LLVMBuildLandingPad2` | `b.landing_pad(ty, num_clauses, name)` | - |
| `LLVMAddClause` | `lp.add_clause(clause)` | - |
| `LLVMSetCleanup` | `lp.set_cleanup(is_cleanup)` | - |
| `LLVMBuildCatchPad` | `b.catch_pad(parent, args, name)` | - |
| `LLVMBuildCleanupPad` | `b.cleanup_pad(parent, args, name)` | - |
| `LLVMBuildCatchSwitch` | `b.catch_switch(parent, unwind_bb, n, name)` | - |
| `LLVMAddHandler` | `cs.add_handler(bb)` | - |
| `LLVMBuildCatchRet` | `b.catch_ret(pad, bb)` | - |
| `LLVMBuildCleanupRet` | `b.cleanup_ret(pad, bb)` | - |

### Atomic Instructions

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMBuildFence` | ✅ | `b.fence(ordering, single_thread, name)` |
| `LLVMBuildFenceSyncScope` | `b.fence_sync_scope(ord, scope, name)` | - |
| `LLVMBuildAtomicRMW` | ❌ | TODO (use sync_scope version) |
| `LLVMBuildAtomicRMWSyncScope` | `b.atomic_rmw_sync_scope(op, ptr, val, ord, scope)` | See below |
| `LLVMBuildAtomicCmpXchg` | ❌ | TODO (use sync_scope version) |
| `LLVMBuildAtomicCmpXchgSyncScope` | `b.atomic_cmpxchg_sync_scope(...)` | - |
| `LLVMIsAtomic` | `inst.is_atomic` | `if inst.is_atomic:` |
| `LLVMGetAtomicSyncScopeID` | `inst.atomic_sync_scope_id` | - |
| `LLVMSetAtomicSyncScopeID` | `inst.atomic_sync_scope_id = id` | - |

```python
# Atomic increment
result = b.atomic_rmw_sync_scope(
    llvm.AtomicRMWBinOp.Add,
    ptr, 
    i32.constant(1),
    llvm.AtomicOrdering.SequentiallyConsistent,
    sync_scope_id
)
```

---

## Memory Buffer APIs

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMCreateMemoryBufferWithContentsOfFile` | `llvm.MemoryBuffer.from_file(path)` | `buf = llvm.MemoryBuffer.from_file("mod.bc")` |
| `LLVMCreateMemoryBufferWithSTDIN` | ❌ | TODO |
| `LLVMCreateMemoryBufferWithMemoryRange` | ❌ | TODO |
| `LLVMCreateMemoryBufferWithMemoryRangeCopy` | `llvm.MemoryBuffer(data)` | `buf = llvm.MemoryBuffer(bytes_data)` |
| `LLVMGetBufferStart` | `buf.data` | `data = buf.data` |
| `LLVMGetBufferSize` | `buf.size` or `len(buf)` | `size = len(buf)` |
| `LLVMDisposeMemoryBuffer` | Automatic (destructor) | - |

---

## Metadata APIs

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMMDStringInContext2` | `ctx.md_string(str)` | `md = ctx.md_string("value")` |
| `LLVMMDNodeInContext2` | `ctx.md_node(mds)` | `node = ctx.md_node([md1, md2])` |
| `LLVMMetadataAsValue` | `md.as_value(ctx)` | `val = md.as_value(ctx)` |
| `LLVMValueAsMetadata` | `val.as_metadata()` | `md = val.as_metadata()` |
| `LLVMGetMDString` | `val.md_string` | `s = val.md_string` |
| `LLVMMDStringInContext` | 🚫 | Uses deprecated API |
| `LLVMMDNodeInContext` | 🚫 | Uses deprecated API |
| `LLVMMDString` | 🚫 | Uses global context |
| `LLVMMDNode` | 🚫 | Uses global context |
| `LLVMReplaceMDNodeOperandWith` | ❌ | TODO |

```python
# Create metadata
md_str = ctx.md_string("my metadata")
md_node = ctx.md_node([md_str])

# Convert to value for use with instructions
md_val = md_node.as_value(ctx)

# Attach to instruction
inst.set_metadata(kind_id, md_node)
```

---

## Operand Bundle APIs

| C API | Python API | Example |
|-------|------------|---------|
| `LLVMCreateOperandBundle` | `llvm.create_operand_bundle(tag, args)` | `bundle = llvm.create_operand_bundle("deopt", [val])` |
| `LLVMDisposeOperandBundle` | Automatic (destructor) | - |
| `LLVMGetOperandBundleTag` | `bundle.tag` | `tag = bundle.tag` |
| `LLVMGetNumOperandBundleArgs` | `bundle.num_args` | `n = bundle.num_args` |
| `LLVMGetOperandBundleArgAtIndex` | `bundle.get_arg_at_index(idx)` | `arg = bundle.get_arg_at_index(0)` |

---

## Skipped APIs

The following categories are intentionally not exposed:

### Global Context APIs (🚫)
All functions using `LLVMGetGlobalContext()` are not exposed for safety:
- `LLVMModuleCreateWithName`, `LLVMAppendBasicBlock`, `LLVMCreateBuilder`
- All type functions without `InContext` suffix

### Legacy Pass Manager (🚫)
Use the new PassBuilder API instead:
- `LLVMCreatePassManager`, `LLVMRunPassManager`, etc.

### Module Providers (🚫)
Deprecated:
- `LLVMCreateModuleProviderForExistingModule`, `LLVMDisposeModuleProvider`

### Threading (🚫)
Deprecated:
- `LLVMStartMultithreaded`, `LLVMStopMultithreaded`

### Unsafe (🚫)
- `LLVMShutdown` - Would corrupt Python process

### Internal (🚫)
- `LLVMCreateMessage`, `LLVMDisposeMessage` - Internal memory management
