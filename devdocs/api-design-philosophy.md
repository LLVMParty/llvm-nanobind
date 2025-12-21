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

## Properties vs Methods

**Principle**: Use properties for attribute-like access; use methods for operations that require arguments or have side effects.

### When to Use Properties

| Pattern | Use Property | Example |
|---------|-------------|---------|
| No-argument getter | ✅ | `inst.opcode`, `value.type`, `block.terminator` |
| Boolean check | ✅ | `value.is_constant`, `func.is_declaration` |
| Count/length | ✅ | `inst.num_operands`, `phi.num_incoming` |
| Collection access | ✅ | `block.instructions`, `term.successors` |
| Read-write attribute | ✅ | `global.linkage`, `global.alignment` |

### When to Use Methods

| Pattern | Use Method | Example |
|---------|-----------|---------|
| Takes arguments | ✅ | `inst.get_operand(index)`, `phi.get_incoming_value(i)` |
| Has side effects | ✅ | `phi.add_incoming(val, bb)`, `block.move_before(other)` |
| Factory/creation | ✅ | `type.constant(42)`, `ctx.create_module(name)` |

### Property Naming

```python
# Good: property access reads naturally
opcode = inst.opcode
count = phi.num_incoming
ty = value.type

# Avoid: get_ prefix is redundant for properties
opcode = inst.get_instruction_opcode()  # Too verbose
```

---

## Naming Conventions

### Type Checking Properties: `is_*` not `is_a_*`

**Principle**: Use `is_*` for type checking properties, not `is_a_*`.

```python
# Good: follows Python's isinstance() naming pattern
if value.is_function:
    ...
if value.is_constant_int:
    ...

# Avoid: the "a" is redundant
if value.is_a_function:  # Reads awkwardly
    ...
```

**Rationale**: Python uses `isinstance()`, not `is_an_instance()`. The `is_*` pattern is consistent with Python conventions and more concise.

### Collection Properties: Plural Names

**Principle**: Use plural names for properties that return collections.

```python
# Good: plural indicates iteration
for inst in block.instructions:
    ...
for succ in terminator.successors:
    ...
for handler in catchswitch.handlers:
    ...

# Avoid: singular names for collections
for inst in block.instruction_list:  # Verbose
    ...
```

### Drop Redundant Prefixes

| C API Pattern | Python Property |
|---------------|-----------------|
| `LLVMGetInstructionOpcode()` | `opcode` |
| `LLVMGetNumOperands()` | `num_operands` |
| `LLVMGetAlignment()` | `alignment` |
| `LLVMGetLinkage()` | `linkage` |

The object context makes the prefix unnecessary: `inst.opcode` is clearly the instruction's opcode.

---

## Iterators and Collections

**Principle**: Provide collection properties for common iteration patterns. Prefer returning lists over exposing manual linked-list traversal APIs.

### Before (Manual Iteration)

```python
# Tedious: manual linked-list traversal
inst = block.first_instruction
while inst is not None:
    process(inst)
    inst = inst.next_instruction
```

### After (Collection Property)

```python
# Pythonic: standard iteration
for inst in block.instructions:
    process(inst)
```

### Guidelines

1. **Provide collection properties** when iteration is common:
   - `block.instructions` - all instructions in a basic block
   - `func.basic_blocks` - all basic blocks in a function
   - `term.successors` - all successor blocks of a terminator
   - `value.uses` - all uses of a value
   - `value.users` - all users of a value

2. **Return lists, not iterators**: Collection properties return `list[T]`, not lazy iterators. This is safer and avoids common pitfalls.

3. **Avoid exposing manual iteration APIs**: Do NOT expose `first_*` / `next_*` style APIs for linked-list traversal. These are footguns:

   ```python
   # DANGEROUS: Iterator invalidation during mutation
   use = value.first_use
   while use is not None:
       user = use.user
       user.replace_with(new_value)  # Modifies use-def chain!
       use = use.next_use  # CRASH: use may be invalid
   ```

   Instead, the list-based API creates a snapshot that's safe to iterate while mutating:

   ```python
   # SAFE: List is a snapshot, iteration is stable
   for user in value.users:
       user.replace_with(new_value)  # Safe: not iterating the live chain
   ```

4. **Exception: Keep linked-list navigation only when necessary** for insertion/deletion at specific positions:
   - `inst.next_instruction` / `inst.prev_instruction` - needed for inserting before/after
   - `block.next_block` / `block.prev_block` - needed for block reordering

   These are for targeted navigation, not iteration. The pattern `while x is not None: x = x.next_*` should never be needed.

---

## Nullable vs Exception-Throwing

**Principle**: Prefer throwing exceptions with a `has_*` check over returning None when None makes code awkward.

### When to Return None

Return `None` when the absence of a value is a normal, expected case that the caller typically handles inline:

```python
# Good: None is natural here
next_block = block.next_block  # None if last block
parent = inst.prev_instruction  # None if first instruction
```

### When to Throw + Provide has_* Check

Throw an exception when:
1. None would require defensive checks in most use cases
2. The caller should explicitly opt-in to handling the missing case

```python
# Before: None requires defensive checks everywhere
terminator = block.terminator  # Returns None if no terminator
if terminator is not None:  # Required before every use
    match terminator.opcode:
        ...

# After: throws by default, has_* for explicit check
if block.has_terminator:
    terminator = block.terminator  # Safe: guaranteed non-None
    match terminator.opcode:
        ...
```

**Benefits**:
- Cleaner code in common cases (most blocks have terminators)
- Explicit opt-in for edge cases
- Catches bugs: accessing terminator on incomplete block is likely an error

### Pattern Summary

| Situation | Approach |
|-----------|----------|
| Missing value is common/expected | Return `None` |
| Missing value is edge case | Throw + `has_*` check |
| Iterator end | Return `None` |
| Invalid state access | Throw exception |

---

## Benefits Summary

1. **Fewer imports**: No need to remember which operations are global
2. **IDE support**: Autocomplete shows relevant operations
3. **Self-documenting**: `phi.add_incoming()` vs `llvm.phi_add_incoming(phi, ...)`
4. **Error prevention**: Methods can validate their receiver (e.g., assert it's a PHI)
5. **Consistency**: All operations follow the same pattern
6. **Ergonomic iteration**: Collection properties like `block.instructions` reduce boilerplate
7. **Cleaner null handling**: `has_*` + throwing pattern reduces defensive checks

---

## Related Task Directories

- `devdocs/types-api/` - Type and constant creation refactor
- `devdocs/value-api/` - Value inspection methods
- `devdocs/attribute-api/` - Function/CallInst attributes
- `devdocs/metadata-api/` - Metadata operations
- `devdocs/dibuilder-api/` - Debug info builder methods
- `devdocs/module-api/` - Module-level operations
