# llvm-nanobind Cheat Sheet

Quick reference for common operations. Keep this open while coding!

---

## Module Operations

```python
# Create from scratch
with llvm.create_context() as ctx:
    with ctx.create_module("name") as mod:
        ...

# Parse existing IR
with llvm.create_context() as ctx:
    with ctx.parse_ir(ir_text) as mod:
        ...

# Output
ir_string = mod.to_string()
is_valid = mod.verify()
error = mod.get_verification_error()
```

## Types

```python
# Integer types
i1 = ctx.types.i1      # bool
i8 = ctx.types.i8      # byte
i32 = ctx.types.i32    # int
i64 = ctx.types.i64    # long

# Pointer
ptr = ctx.types.ptr

# Composite types
arr = ctx.types.array(elem_ty, count)
fn = ctx.types.function(ret_ty, [param_ty, ...])
struct = ctx.types.struct([field_ty, ...])

# Type properties
ty.kind == llvm.TypeKind.Integer
ty.int_width  # for integers
```

## Functions

```python
# Create
fn = mod.add_function("name", fn_type)
fn.get_param(0).name = "arg_name"

# Iterate
for func in mod.functions:
    if func.is_declaration:
        continue
    # func is a definition
```

## Basic Blocks

```python
# Create
bb = func.append_basic_block("name")

# Iterate
for bb in func.basic_blocks:
    term = bb.terminator  # last instruction
    for inst in bb.instructions:
        ...

# Movement
bb.move_before(other_bb)
bb.move_after(other_bb)
```

## Instructions

```python
# Properties
inst.opcode == llvm.Opcode.Add
inst.is_terminator
inst.name
inst.type
bb = inst.block   # explicit parent block
bb = inst.parent  # alias

# Operands
for op in inst.operands:
    ...
inst.set_operand(i, new_val)  # indexed mutation

# Uses
for use in inst.uses:
    user = use.user
    # user is an instruction that uses inst

# Deletion
inst.erase_from_parent()
```

## Builder

```python
# Create
with bb.create_builder() as builder:
    ...

# Positioning
builder.position_at_end(bb)
builder.position_before(inst)

# Arithmetic
builder.add(a, b, "name")
builder.sub(a, b, "name")
builder.mul(a, b, "name")
builder.neg(a, "name")
builder.not_(a, "name")  # underscore!
builder.and_(a, b, "name")  # underscore!
builder.or_(a, b, "name")
builder.xor(a, b, "name")

# Memory
builder.alloca(ty, name="name")
builder.load(ty, ptr, "name")
builder.store(val, ptr)
builder.gep(ty, ptr, indices, "name")

# Control flow
builder.br(dest_bb)
builder.cond_br(cond, true_bb, false_bb)
builder.ret(val)
builder.ret_void()

# Comparison
builder.icmp(llvm.IntPredicate.EQ, a, b, "name")
# Predicates: EQ, NE, UGT, UGE, ULT, ULE, SGT, SGE, SLT, SLE

# PHI
phi = builder.phi(ty, "name")
phi.add_incoming(val, from_bb)
```

## Constants

```python
# Integers
c = i32.constant(42)
c = i32.constant(-1)  # signed OK

# Strings and raw bytes
c = ctx.const_string("text", dont_null_terminate=False)
raw = ctx.const_string(b"\xff\x80B", dont_null_terminate=True)

# Null pointer
null = llvm.ConstantPointerNull.get(ptr_ty)

# Block address (for indirect branches)
addr = llvm.block_address(func, bb)
```

## PHI Nodes

```python
# Create
phi = builder.phi(ty, "name")
phi.add_incoming(val1, from_bb1)
phi.add_incoming(val2, from_bb2)

# Read
for i in range(phi.num_incoming):
    val = phi.get_incoming_value(i)
    bb = phi.get_incoming_block(i)
```

## Globals

```python
# Create
gv = mod.add_global(ty, "name")
gv.initializer = const_val
gv.linkage = llvm.Linkage.Private
gv.is_global_constant = True

# Iterate
for gv in mod.globals:
    init = gv.initializer  # None if no initializer
    ty = gv.global_value_type  # type of contents
```

---

## Common Patterns

### Replace All Uses With

```python
old_val.replace_all_uses_with(new_val)
```

### Safe Instruction Replacement

```python
def replace_instruction(old_inst, new_val):
    """Replace instruction with new value and delete."""
    old_inst.replace_all_uses_with(new_val)
    old_inst.erase_from_parent()
```

### Collect Then Modify

```python
# When modifying while iterating: collect first!
to_process = []
for bb in func.basic_blocks:
    for inst in bb.instructions:
        if should_process(inst):
            to_process.append(inst)

for inst in to_process:
    transform(inst)
```

### Find Instructions by Opcode

```python
def find_by_opcode(func, opcode):
    result = []
    for bb in func.basic_blocks:
        for inst in bb.instructions:
            if inst.opcode == opcode:
                result.append(inst)
    return result

subs = find_by_opcode(func, llvm.Opcode.Sub)
```

---

## Gotchas Reference

| What you try | What happens | Fix |
|-------------|--------------|-----|
| `mod = ctx.parse_ir(...)` | Gets manager, not module | Use `with ... as mod` |
| `bb.terminator` on unterminated block | Raises `LLVMAssertionError` | Check `bb.has_terminator` first |
| `inst.set_operand(i, value_of_wrong_type)` | Raises `LLVMAssertionError` | Use a value with the exact expected type |
| `ctx.const_string("\xff")` for raw bytes | Uses text/UTF-8 path | Pass `bytes`: `b"\xff"` |

---

## Opcodes Reference

```
Terminators:    Ret, Br, Switch, IndirectBr, Invoke, Unreachable
Binary:         Add, Sub, Mul, UDiv, SDiv, URem, SRem
                FAdd, FSub, FMul, FDiv, FRem
Bitwise:        Shl, LShr, AShr, And, Or, Xor
Memory:         Alloca, Load, Store, GetElementPtr, Fence
                AtomicCmpXchg, AtomicRMW
Casts:          Trunc, ZExt, SExt, FPToUI, FPToSI
                UIToFP, SIToFP, FPTrunc, FPExt
                PtrToInt, IntToPtr, BitCast, AddrSpaceCast
Other:          ICmp, FCmp, PHI, Call, Select, VAArg
                ExtractElement, InsertElement, ShuffleVector
                ExtractValue, InsertValue, LandingPad
```

---

## Quick Debugging

```python
# Print IR
print(mod.to_string())
print(func)  # just the function
print(inst)  # just the instruction

# Check what you have
print(type(thing))
print(dir(thing))

# Verify validity
if not mod.verify():
    print(mod.get_verification_error())
```

---

*Keep this handy while you code!*
