# Transformation API Improvements

## Status

Complete. This file is kept as the original improvement plan, updated with the API names that shipped. See `progress.md` for completion status and `devdocs/porting-guide.md` for current usage examples.

## Overview

This task tracked improvements to the Python bindings to better support IR transformation use cases (as opposed to just code generation). These issues were discovered while porting LLVM obfuscator passes to Python.

## Priority 1: Critical Blockers

These issues completely block certain use cases.

### 1.1 Raw Bytes Support for Constants

**Problem**: raw binary payloads must not go through text/UTF-8 encoding.

**Shipped solution**: `const_string` and `const_data_array` accept `bytes` and pass raw bytes to LLVM without encoding.

```python
raw = bytes([0xFF, 0x80, 0x42])
const = llvm.const_string(ctx, raw, dont_null_terminate=True)
assert const.type.array_length == len(raw)

arr = i8.array_const(raw)
assert arr.type.array_length == len(raw)
```

**Files to modify**: Bindings for `LLVMConstStringInContext2`, `LLVMConstArray`

**Blocked use case**: String encryption, binary patching, any byte-level manipulation

---

### 1.2 Add `Value.replace_all_uses_with()`

**Problem**: replacing all uses of a value is fundamental to SSA transforms.

**Shipped solution**: bind `LLVMReplaceAllUsesWith`.

```python
old_inst.replace_all_uses_with(new_value)
```

**C API**: `LLVMReplaceAllUsesWith(LLVMValueRef OldVal, LLVMValueRef NewVal)`

**Blocked use case**: Any instruction replacement/transformation

---

### 1.3 Add Basic Block Splitting

**Problem**: transformations often need to split a basic block at an instruction point.

**Shipped solution**:

```python
new_bb = bb.split_basic_block(inst, "tail")
new_bb = bb.split_basic_block_before(inst, "tail")
```

**C API**: Not directly available - may need custom C wrapper or use `LLVMInsertBasicBlock` + instruction movement.

**Blocked use case**: Basic block splitting passes, some control flow transforms

---

### 1.4 Single-Step Instruction Deletion

**Problem**: deleting an instruction should be a single safe operation.

**Shipped solution**:

```python
inst.erase_from_parent()
```

**C API**: `LLVMInstructionEraseFromParent` (already exists!)

**Files to modify**: Check if this is bound; if not, add binding

---

## Priority 2: API Consistency

These issues cause confusion and bugs but have workarounds.

### 2.1 Make `ptr` a Property Like Other Types

**Problem**: the default opaque pointer type should be as simple as integer types.

**Shipped solution**:

```python
ptr_ty = ctx.types.ptr
ptr_as1 = ctx.types.addrspace_ptr(1)
i32_ty = ctx.types.i32
```

**Note**: If `ptr(addrspace)` is needed for address spaces, keep method but add property for default.

---

### 2.2 Consistent Setter Patterns

**Problem**: Mixed patterns for setting properties on globals:

```python
gv.is_global_constant = False
gv.linkage = llvm.Linkage.Internal
```

**Solution**: Use property setters consistently.

```python
gv.is_global_constant = False
gv.linkage = llvm.Linkage.Internal
```

---

### 2.3 Add `.parent` Alias for Instructions

**Problem**: Instructions use `.block` to get parent, but `.parent` is more intuitive and matches C++.

```python
bb = inst.block
bb = inst.parent  # alias
```

**Solution**: Add `.parent` as an alias for `.block`.

---

### 2.4 Rename `.is_terminator_inst` to `.is_terminator`

**Problem**: The `_inst` suffix is inconsistent with other boolean properties.

```python
inst.is_terminator
```

**Solution**: Add `.is_terminator` (keep old name for compatibility or deprecate).

---

## Priority 3: Missing Conveniences

These would make the API more Pythonic and reduce boilerplate.

### 3.1 Add `.operands` Iterator

**Problem**: Must use index-based access to iterate operands.

```python
for op in inst.operands:
```

**Solution**: Add `operands` property returning an iterator.

```python
# Proposed API
for op in inst.operands:
    process(op)

# Also useful:
ops = list(inst.operands)
```

---

### 3.2 Add Instruction Movement

**Problem**: Cannot move instructions between blocks or reorder within a block.

**Solution**: Add movement methods.

```python
# Proposed API
inst.move_before(other_inst)
inst.move_after(other_inst)
```

**C API**: `LLVMInstructionRemoveFromParent` + `LLVMInsertIntoBuilder` (partial support)

---

### 3.3 Add Instruction Cloning

**Problem**: Cannot clone an instruction.

**Solution**: Bind instruction cloning.

```python
new_inst = inst.instruction_clone()
```

**C API**: `LLVMInstructionClone`

---

### 3.4 Add `.successors` Count

**Problem**: Must convert to list to count successors.

```python
num = term.num_successors
```

**Note**: `num_successors` may already exist - verify.

---

## Priority 4: Documentation

### 4.1 Document Exception Types

Document available exceptions:
- `llvm.LLVMError` (base class)
- `llvm.LLVMParseError`
- `llvm.LLVMAssertionError`
- `llvm.LLVMMemoryError`

### 4.2 Document Context Manager Patterns

Explicitly document that `ctx.parse_ir()` and `ctx.create_module()` return context managers, not direct objects.

### 4.3 Add API Reference

Generate or write comprehensive API reference documentation.

---

## Implementation Order

Suggested implementation order based on impact and effort:

### Phase 1: Quick Wins (Low effort, High impact)
1. Bind `LLVMInstructionEraseFromParent` (1.4)
2. Bind `LLVMReplaceAllUsesWith` (1.2)
3. Add `.operands` iterator (3.1)
4. Add `.parent` alias (2.3)

### Phase 2: API Consistency
1. Make `ptr` a property (2.1)
2. Consistent setters (2.2)
3. Rename `is_terminator_inst` (2.4)

### Phase 3: Major Features
1. Raw bytes support (1.1) - requires design decision
2. Basic block splitting (1.3) - may need C wrapper
3. Instruction movement (3.2)
4. Instruction cloning (3.3)

### Phase 4: Documentation
1. Exception docs (4.1)
2. Context manager docs (4.2)
3. API reference (4.3)

---

## Testing Strategy

Each improvement should include:

1. **Unit test**: Direct test of the new API
2. **Integration test**: Use in a realistic transformation
3. **Porting test**: Verify it enables previously-blocked obfuscation pass functionality

Example test for `replace_all_uses_with`:
```python
def test_replace_all_uses_with():
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            # Create: %y = add %x, 1; %z = mul %y, 2
            # Replace %y with constant 42
            # Verify %z now uses 42
```

---

## References

- `devdocs/porting-guide.md` - Full porting experience documentation
- `tools/obfuscation/` - Ported passes showing workarounds
- LLVM C API: https://llvm.org/doxygen/group__LLVMC.html
