# API Design Philosophy

This document explains the principles behind the llvm-nanobind API refactor.

---

## Why This Refactor Matters

The current API mirrors the LLVM C API directly. While this made initial implementation easier, it creates a **non-Pythonic** experience:

```python
# Current: C-style global functions
i32 = ctx.int32_type()
const = llvm.const_int(i32, 42)
llvm.add_attribute_at_index(func, 0, attr)
llvm.set_metadata(inst, kind, md)
```

```python
# Target: Pythonic object-oriented API
i32 = ctx.types.i32
const = i32.constant(42)
func.add_attribute(0, attr)
inst.set_metadata(kind, md)
```

---

## Core Principles

### 1. Methods Belong to Objects

**Principle**: Operations on an object should be methods of that object, not global functions that take the object as an argument.

**Why**: Python is object-oriented. When you write `func.add_attribute()`, it's immediately clear you're modifying a function. With `llvm.add_attribute_at_index(func, ...)`, you must read the arguments to understand what's being modified.

**C API constraint**: The C language has no classes, so LLVM uses `LLVMAddAttributeAtIndex(LLVMValueRef Fn, ...)`. Python has no such limitation.

---

### 2. Discoverability via Autocomplete

**Principle**: Users should be able to discover available operations through IDE autocomplete.

**Why**: With 93 global functions, typing `llvm.` is overwhelming. When operations are methods:
- `type.` shows only type operations
- `func.` shows only function operations
- `builder.` shows only builder operations

This reduces cognitive load and helps users explore the API.

---

### 3. Logical Grouping (Cohesion)

**Principle**: Related functionality should live together.

**Why**: The C API scatters related operations:
- `LLVMConstInt`, `LLVMConstReal`, `LLVMConstNull` are all global
- `LLVMAddAttributeAtIndex`, `LLVMGetAttributeCountAtIndex` are both global

In Python, these naturally group:
- All constant creation → methods on Type
- All attribute operations → methods on Function/CallInst

---

### 4. Property-Based Access for Namespaces

**Principle**: When there's a fixed set of named items, use properties instead of factory methods.

**Why**: Reduces boilerplate and reveals structure:

```python
# Verbose: each type requires a method call
i8 = ctx.int8_type()
i16 = ctx.int16_type()
i32 = ctx.int32_type()

# Concise: types are organized under a namespace
i8 = ctx.types.i8
i16 = ctx.types.i16
i32 = ctx.types.i32
```

The `ctx.types` namespace makes it clear these are all type-related, and IDE autocomplete shows all available types.

---

### 5. Consistent with Python Ecosystem

**Principle**: Follow patterns established by popular Python libraries.

**Examples**:
- numpy: `array.reshape()`, not `np.reshape(array, shape)`
- pandas: `df.groupby()`, not `pd.groupby(df, column)`
- pathlib: `path.exists()`, not `os.path.exists(str(path))`

Python developers expect `object.operation()`, not `module.operation(object)`.

---

### 6. Global Functions Should Be Exceptional

**Principle**: Module-level functions should only exist when there's no natural object to attach them to.

**Acceptable globals**:
- Factory functions: `create_context()`, `create_binary_from_file()`
- Initialization: `initialize_all_targets()`
- Registry lookups: `get_md_kind_id()` (no object owns the metadata registry)

**Not acceptable**: Any function that takes an LLVM object as its first argument.

---

### 7. Flat Hierarchy with Runtime Assertions

**Principle**: Keep the type system simple, but catch errors at runtime.

**Why not full hierarchy**: Creating dozens of Python classes (IntegerType, PointerType, PHINode, CallInst, BranchInst...) would:
- Massively increase binding complexity
- Make the API harder to learn
- Create confusion about which class to use

**Instead**: Keep Value and Type as the main classes, but add runtime assertions:

```python
# Instead of: isinstance(phi, llvm.PHINode) and phi.add_incoming(val, bb)
# We have:    value.add_incoming(val, bb)  # asserts value is actually a PHI
```

This catches errors with clear messages while keeping the API simple.

---

### 8. Method Chaining Potential

**Principle**: Methods returning values enable fluent APIs.

**Future potential**:
```python
result = i32.constant(42).const_bitcast(ptr_ty)
```

With global functions, chaining is impossible.

---

## The Problem with the Current API

### Visual Comparison

**Current (C-style)**:
```python
import llvm

with llvm.create_context() as ctx:
    i32 = ctx.int32_type()
    i64 = ctx.int64_type()
    fn_ty = ctx.function_type(i32, [i32, i32])

    const_42 = llvm.const_int(i32, 42)
    const_0 = llvm.const_null(i32)

    with ctx.create_module("test") as mod:
        fn = mod.add_function("add", fn_ty)
        llvm.add_attribute_at_index(fn, 0, attr)

        with ctx.create_builder() as builder:
            phi = builder.phi(i32)
            llvm.phi_add_incoming(phi, val, bb)  # Easy to misuse
```

**Target (Pythonic)**:
```python
import llvm

with llvm.create_context() as ctx:
    i32 = ctx.types.i32
    i64 = ctx.types.i64
    fn_ty = ctx.types.function(i32, [i32, i32])

    const_42 = i32.constant(42)
    const_0 = i32.null()

    with ctx.create_module("test") as mod:
        fn = mod.add_function("add", fn_ty)
        fn.add_attribute(0, attr)

        with ctx.create_builder() as builder:
            phi = builder.phi(i32)
            phi.add_incoming(val, bb)  # Clear: operating on the phi
```

---

## Benefits Summary

1. **Fewer imports**: No need to remember which operations are global
2. **IDE support**: Autocomplete shows relevant operations
3. **Self-documenting**: `phi.add_incoming()` vs `llvm.phi_add_incoming(phi, ...)`
4. **Error prevention**: Methods can validate their receiver (e.g., assert it's a PHI)
5. **Consistency**: All operations follow the same pattern

---

## Related Task Directories

- `devdocs/types-api/` - Type and constant creation refactor
- `devdocs/value-api/` - Value inspection methods
- `devdocs/attribute-api/` - Function/CallInst attributes
- `devdocs/metadata-api/` - Metadata operations
- `devdocs/dibuilder-api/` - Debug info builder methods
- `devdocs/module-api/` - Module-level operations
