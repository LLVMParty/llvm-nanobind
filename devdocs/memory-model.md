# LLVM Python Bindings: Lifetime Management Strategy

## The Problem

LLVM has a strict parent-owns-children model:

```
Module
  └── Function
        └── BasicBlock
              └── Instruction
```

When a parent is destroyed, all children become invalid. In C++, accessing a child after its parent is destroyed causes a segfault. We need to detect this in Python and raise an exception instead.

Additionally, objects can be:
- **Detached**: Removed from parent but still alive (caller owns it) - *Future Design*
- **Erased**: Removed from parent and deleted - *Future Design*

Python's garbage collector provides no ordering guarantees, so we can't rely on destructor order.

---

## Solution: Validity Tokens

Every wrapper holds a `shared_ptr<ValidityToken>` pointing to its parent's token. When a parent is destroyed, it invalidates its token, and all children detect this on their next access.

```cpp
struct ValidityToken {
    std::atomic<bool> valid{true};
    void invalidate() { valid = false; }
    bool is_valid() const { return valid.load(); }
};
```

---

## Exception Hierarchy

The bindings use a three-tier exception hierarchy to distinguish between different error severities:

### LLVMError (Recoverable Runtime Errors)
**Base:** `Exception`

Recoverable errors that can be caught and handled. These are external failures that can happen even with correct code:
- I/O errors when reading files
- Bitcode/IR parsing failures
- Binary creation errors

```python
try:
    mod = ctx.parse_bitcode_from_file("missing.bc")
except llvm.LLVMError as e:
    print(f"I/O error: {e}")  # Can continue execution
```

### LLVMAssertionError (Programming Mistakes - Non-Lifetime)
**Base:** Python's `AssertionError`

Programming errors unrelated to object lifetimes. These indicate bugs in your code but are recoverable:
- Type mismatches: "Type is not an integer type"
- Invalid indices: "Parameter index out of range"
- Invalid operations: "Value is not inline assembly"

```python
try:
    width = some_type.int_width  # But some_type is float!
except llvm.LLVMAssertionError as e:
    print(f"Logic error: {e}")  # Can continue but should fix code
```

### LLVMMemoryError (Lifetime/Memory Violations)
**Base:** Python's `SystemExit`

**NOT CATCHABLE WITH `except Exception`** - these indicate lifetime or memory safety violations.

All lifetime-related errors:
- Context destroyed: "Value used after context was destroyed"
- Module/Builder disposed: "Module has been disposed"
- Context manager misuse: "Module manager already entered"

```python
with llvm.create_context() as ctx:
    val = llvm.const_int(ctx.int32_type(), 42)

# Context is destroyed
val.is_constant  # raises LLVMMemoryError - PROGRAM TERMINATES
```

Since `LLVMMemoryError` derives from `SystemExit`, catching it requires explicit handling:

```python
try:
    val.is_constant
except llvm.LLVMMemoryError:
    print("This will execute")
except Exception:
    print("This will NOT execute - SystemExit is not an Exception")
```

This design prevents accidental continuation after memory safety violations.

### LLVMParseError (Parsing Failures with Diagnostics)
**Base:** `LLVMError`

Special exception for bitcode/IR parsing that carries diagnostic information:

```python
try:
    mod = ctx.parse_bitcode_from_bytes(bad_bitcode)
except llvm.LLVMParseError as e:
    print(f"Parse error: {e}")
    for diag in e.get_diagnostics():
        print(f"  {diag.severity}: {diag.message}")
```

---

## Current Implementation: Flat Token Model

### Token Structure

All wrapper objects track only the **Context token**. When the context is destroyed, all objects (modules, functions, basic blocks, values, types, builders) become invalid.

```
Context Token (invalidated on context destruction)
    │
    ├── Module (checks context token)
    ├── Function (checks context token)
    ├── BasicBlock (checks context token)
    ├── Value/Instruction (checks context token)
    ├── Type (checks context token)
    └── Builder (checks context token)
```

### Implementation Details

Current `check_valid()` implementation:

```cpp
void check_valid() const {
  if (!m_ref)
    throw LLVMMemoryError("Value is null");
  if (!m_context_token || !m_context_token->is_valid())
    throw LLVMMemoryError("Value used after context was destroyed");
}
```

### What Works Today

- ✅ Context managers for Context, Module, Builder
- ✅ `LLVMMemoryError` when accessing objects after context destruction
- ✅ Module safely handles being garbage collected after context (warning + leak, no crash)
- ✅ `func.erase()` to delete a function
- ✅ ModuleManager/BuilderManager check context validity before use
- ✅ `LLVMAssertionError` for type mismatches and invalid parameters

### What's Not Implemented

- ❌ Hierarchical token checking (module → function → block → instruction)
- ❌ `detach()` / `insert_into()` for instructions or basic blocks
- ❌ `erase()` for basic blocks or instructions
- ❌ Granular error messages per ancestor level
- ❌ Module-level token invalidation (only context-level works)

---

## Python API Patterns

### Context and Module Lifecycle

```python
import llvm

# Pattern 1: Context manager (recommended)
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        func = mod.add_function("foo", func_type)
        # mod is valid here
    # mod is now disposed
    
    # This raises: LLVMError: Module has been disposed
    print(mod.name)
# ctx is now disposed

# Pattern 2: Explicit .dispose() method
with llvm.create_context() as ctx:
    mod_manager = ctx.create_module("example")
    mod_manager.dispose()

    # This raises: LLVMError: Module has already been disposed
    mod_manager.dispose()

# Pattern 3: Forget to dispose
with llvm.create_context() as ctx:
    mod_manager = ctx.create_module("example")
# This raises: LLVMError: Module has never been entered

# Pattern 4: Double dispose
with llvm.create_context() as ctx:
    mod_manager = ctx.create_module("example")
    with mod_manager as mod:
        print(mod.name)
    # This raises: LLVMError: Module has already been disposed
    mod_manager.dispose()
```

### Holding Invalid References

```python
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        func = mod.add_function("foo", func_type)
        entry = func.append_basic_block("entry")
        
        with ctx.create_builder() as builder:
            builder.position_at_end(entry)
            inst = builder.add(a, b, name="sum")
        
        # Save references
        saved_func = func
        saved_block = entry
        saved_inst = inst

# Everything is disposed now. Accessing raises exceptions:

saved_inst.name      # LLVMMemoryError: Value used after context was destroyed
saved_block.name     # LLVMMemoryError: BasicBlock used after context was destroyed  
saved_func.name      # LLVMMemoryError: Function used after context was destroyed
```

### Module Cloning

```python
with llvm.create_context() as ctx:
    with ctx.create_module("original") as mod:
        func = mod.add_function("foo", func_type)
        # ... build module ...
        
        # Clone returns a module manager (same as ctx.create_module)
        clone_manager = mod.clone()
    
    # Original module is disposed, but clone is still valid
    
    # Use cloned module with context manager
    with clone_manager as cloned:
        cloned.name  # "original"
        # ... modify clone ...
    # clone is disposed here
    # NOTE: without the `with` and `clone_manager.dispose()` we raise an exception
```

### Builder Lifetime

Builders have independent lifetime from the blocks they operate on:

```python
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        func = mod.add_function("foo", func_type)
        entry = func.append_basic_block("entry")
        
        # Builder can outlive or be shorter-lived than blocks
        with ctx.create_builder() as builder:
            builder.position_at_end(entry)
            inst = builder.add(a, b)
        # Builder disposed, but entry and inst still valid
        
        inst.name  # Works fine
        
        # Can create new builder later
        with ctx.create_builder() as builder2:
            builder2.position_before(inst)
            # Insert more instructions
```

### Type Mismatches Raise AssertionError

```python
import llvm

with llvm.create_context() as ctx:
    int_ty = ctx.int32_type()
    float_ty = ctx.float_type()
    
    # Correct usage
    width = int_ty.int_width  # Works: 32
    
    # Programming mistake
    try:
        width = float_ty.int_width  # Float is not an integer type!
    except llvm.LLVMAssertionError as e:
        print(f"Logic error: {e}")  # "Type is not an integer type"
    
    # Invalid index
    try:
        param = func.get_param(100)  # Index out of range
    except llvm.LLVMAssertionError as e:
        print(f"Logic error: {e}")  # "Parameter index out of range"
```

---

## Future Design: Hierarchical Token Model

> **Note:** The following describes the aspirational design for finer-grained lifetime tracking. It is not yet implemented but represents the target architecture.

### Token Hierarchy

```
Context Token ──────────────────────────────────┐
    │                                           │
Module Token ───────────────────────┐           │
    │                               │           │
    ├── Function (shares module token) ─┼───────┤
    │       │                           │       │
    │       └── BasicBlock ─────────────┼───────┤
    │               │                   │       │
    │               └── Instruction ────┘       │
    │                                           │
Builder Token ──────────────────────────────────┘
```

Each object would check **all ancestor tokens** before any operation:

```cpp
void LLVMInstruction::check_valid() const {
    if (!m_ref)
        throw LLVMMemoryError("Instruction has been erased");
    if (m_detached)
        return;  // Detached instructions have no parent to check
    if (!m_block_token || !m_block_token->is_valid())
        throw LLVMError("Instruction's basic block has been erased");
    if (!m_func_token || !m_func_token->is_valid())
        throw LLVMError("Instruction's function has been erased");
    if (!m_module_token || !m_module_token->is_valid())
        throw LLVMError("Instruction's module has been disposed");
    if (!m_context_token || !m_context_token->is_valid())
        throw LLVMMemoryError("Instruction's context has been disposed");
}
```

### Object States

| State | Description | Owned By | On Destruction |
|-------|-------------|----------|----------------|
| Attached | Normal state, has parent | Parent | Nothing (parent will clean up) |
| Detached | After `.detach()`, no parent | Python wrapper | `LLVMDeleteX()` called |
| Erased | After `.erase()` | None (`m_ref=nullptr`) | Nothing |

### Parent Erasure Invalidates Children

```python
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        func = mod.add_function("foo", func_type)
        bb1 = func.append_basic_block("bb1")
        bb2 = func.append_basic_block("bb2")
        
        with ctx.create_builder() as builder:
            builder.position_at_end(bb1)
            inst1 = builder.add(a, b, name="sum")
            inst2 = builder.mul(inst1, c, name="prod")
        
        # Erase the basic block - all its instructions become invalid
        bb1.erase()
        
        inst1.name  # LLVMError: Instruction's basic block has been erased
        inst2.name  # LLVMError: Instruction's basic block has been erased
        
        # Erase the function - all its blocks become invalid
        func.erase()
        
        bb2.name    # LLVMError: BasicBlock's function has been erased
```

### Detach vs Erase

```python
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        func = mod.add_function("foo", func_type)
        entry = func.append_basic_block("entry")
        other = func.append_basic_block("other")
        
        with ctx.create_builder() as builder:
            builder.position_at_end(entry)
            add = builder.add(a, b, name="sum")
            mul = builder.mul(add, c, name="prod")
            builder.ret(mul)
            
            # --- ERASE: Remove and delete ---
            mul.erase()  # Instruction is gone forever
            mul.name     # LLVMError: Instruction has been erased
            
            # --- DETACH: Remove but keep alive ---
            add.detach()  # Instruction is now "floating"
            add.is_detached  # True
            add.parent       # None
            add.name         # Still works! "sum"
            
            # Detached instruction can be reinserted
            builder.position_at_end(other)
            add.insert_into(builder)  # Now attached to 'other' block
            add.is_detached  # False
            add.parent       # <BasicBlock 'other'>
            
            # --- DETACH without reinsertion ---
            another = builder.sub(x, y, name="diff")
            another.detach()
            # If we don't reinsert, destructor will clean up:
            del another  # Calls LLVMDeleteInstruction internally

# Detaching a basic block
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        func = mod.add_function("foo", func_type)
        bb = func.append_basic_block("movable")
        
        # Detach block from function
        bb.detach()
        bb.parent  # None
        bb.name    # Still works: "movable"
        
        # Reinsert into same or different function
        bb.insert_into(func)  # Back in the function
        
        # Or insert before/after another block
        bb.detach()
        bb.insert_before(other_block)
```

### Implementation Details (Future)

```cpp
struct LLVMInstruction : NoMoveCopy {
    LLVMValueRef m_ref = nullptr;
    
    // Token hierarchy - check all on access
    std::shared_ptr<ValidityToken> m_context_token;
    std::shared_ptr<ValidityToken> m_module_token;
    std::shared_ptr<ValidityToken> m_func_token;
    std::shared_ptr<ValidityToken> m_block_token;
    
    bool m_detached = false;
    
    void check_valid() const {
        if (!m_ref)
            throw LLVMMemoryError("Instruction has been erased");
        if (m_detached)
            return;  // No parent to validate
        if (!m_block_token || !m_block_token->is_valid())
            throw LLVMError("Instruction's basic block has been erased");
        if (!m_func_token || !m_func_token->is_valid())
            throw LLVMError("Instruction's function has been erased");
        if (!m_module_token || !m_module_token->is_valid())
            throw LLVMError("Instruction's module has been disposed");
        if (!m_context_token || !m_context_token->is_valid())
            throw LLVMMemoryError("Instruction's context has been disposed");
    }
    
    void erase() {
        check_valid();
        LLVMInstructionEraseFromParent(m_ref);
        m_ref = nullptr;
        clear_tokens();
    }
    
    void detach() {
        check_valid();
        if (m_detached)
            throw LLVMError("Instruction is already detached");
        LLVMInstructionRemoveFromParent(m_ref);
        m_detached = true;
        clear_tokens();  // No longer has parent
    }
    
    void insert_into(LLVMBuilder* builder) {
        if (!m_ref)
            throw LLVMMemoryError("Instruction has been erased");
        if (!m_detached)
            throw LLVMError("Instruction is not detached");
        builder->check_valid();
        
        LLVMInsertIntoBuilder(builder->m_ref, m_ref);
        m_detached = false;
        adopt_tokens_from(builder);  // Get new parent's tokens
    }
    
    ~LLVMInstruction() {
        // Only clean up if WE own it (detached state)
        if (m_detached && m_ref) {
            LLVMDeleteInstruction(m_ref);
        }
    }
};
```

### BasicBlock with Child Tracking

When a BasicBlock is erased, it must invalidate all Instruction wrappers:

```cpp
struct LLVMBasicBlock : NoMoveCopy {
    LLVMBasicBlockRef m_ref = nullptr;
    std::shared_ptr<ValidityToken> m_token;  // This block's token
    
    // Parent tokens
    std::shared_ptr<ValidityToken> m_context_token;
    std::shared_ptr<ValidityToken> m_module_token;
    std::shared_ptr<ValidityToken> m_func_token;
    
    bool m_detached = false;
    
    void erase() {
        check_valid();
        m_token->invalidate();  // Invalidates all child instructions
        LLVMDeleteBasicBlock(m_ref);
        m_ref = nullptr;
    }
    
    void detach() {
        check_valid();
        LLVMRemoveBasicBlockFromParent(m_ref);
        m_detached = true;
        // Keep m_token valid - children are still valid
        // Clear parent tokens
        m_func_token = nullptr;
        m_module_token = nullptr;
    }
    
    ~LLVMBasicBlock() {
        if (m_detached && m_ref) {
            m_token->invalidate();
            LLVMDeleteBasicBlock(m_ref);
        }
    }
};
```

---

## Summary Table

### Current Behavior

| Operation | Effect on Self | Effect on Children | Errors Raised |
|-----------|---------------|-------------------|---------------|
| Context destroyed | Invalidated | All raise `LLVMMemoryError` | `LLVMMemoryError` |
| Module disposed | Disposed | Functions still accessible until context destroyed | `LLVMError` |
| `func.erase()` | Deleted (`m_ref = nullptr`) | Blocks/instructions may be accessible (unsafe) | `LLVMError` |
| Exit `with` block | Disposed | All descendants invalidated | `LLVMError` |

### Future Behavior (Not Yet Implemented)

| Operation | Effect on Self | Effect on Children |
|-----------|---------------|-------------------|
| `module.dispose()` / `del module` | Invalidated | All functions/globals invalidated |
| `func.erase()` | Deleted | All blocks invalidated |
| `func.detach()` | Removed from module, still valid | Children still valid |
| `block.erase()` | Deleted | All instructions invalidated |
| `block.detach()` | Removed from function, still valid | Children still valid |
| `inst.erase()` | Deleted | N/A |
| `inst.detach()` | Removed from block, still valid | N/A |
