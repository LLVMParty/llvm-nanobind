# LLVM Python Bindings Design Document

## Project Overview

This project provides Pythonic bindings for the LLVM-C API using nanobind. The goal is to expose LLVM's intermediate representation (IR) capabilities to Python in a way that feels natural to Python developers while maintaining safety guarantees around object lifetimes.

### Design Goals

1. **Pythonic API**: Convert C-style factory functions to object methods
2. **Safe Lifetime Management**: Prevent use-after-free and double-free errors
3. **Deterministic Resource Cleanup**: Support context managers for explicit lifetime control
4. **Graceful Error Handling**: Raise clear Python exceptions instead of crashing

### Key Challenges

- LLVM objects have strict parent-child lifetime requirements (children must be freed before parents)
- Python's garbage collector provides no ordering guarantees for destructor calls
- The LLVM-C API uses raw pointers with manual memory management
- Some operations transfer ownership (e.g., `removeFromParent`)

---

## Architecture

### Object Hierarchy

```
LLVMContext
├── owns Types (immortal within context)
├── owns Modules
│   ├── owns Functions
│   │   ├── owns BasicBlocks
│   │   │   └── owns Instructions
│   │   └── owns Arguments (read-only references)
│   └── owns GlobalVariables
└── owns Builders (independent lifetime)
```

### Ownership Model

We use a **hybrid ownership model**:

1. **Context Managers** for explicit lifetime control of major resources
2. **Validity Tokens** to detect use-after-parent-destruction
3. **Reference Counting** via nanobind to prevent premature GC of parents

### Core Components

```
┌─────────────────────────────────────────────────────────────────┐
│                        Python Layer                              │
├─────────────────────────────────────────────────────────────────┤
│  LLVMContextManager  │  LLVMModuleHandle  │  LLVMBuilderHandle  │
│  (context manager)   │  (context manager) │  (context manager)  │
├─────────────────────────────────────────────────────────────────┤
│                      Wrapper Objects                             │
├─────────────────────────────────────────────────────────────────┤
│  LLVMContext  │  LLVMModule  │  LLVMFunction  │  LLVMInstruction │
│  LLVMType     │  LLVMValue   │  LLVMBasicBlock│  LLVMBuilder     │
├─────────────────────────────────────────────────────────────────┤
│                      Validity Tokens                             │
│  (shared_ptr<ValidityToken> for lifetime tracking)              │
├─────────────────────────────────────────────────────────────────┤
│                        LLVM-C API                                │
└─────────────────────────────────────────────────────────────────┘
```

---

## Lifetime Management

### Validity Tokens

Every wrapper object holds a `shared_ptr<ValidityToken>` that tracks whether its parent is still alive:

```cpp
struct ValidityToken {
    std::atomic<bool> valid{true};
    
    void invalidate() { valid = false; }
    bool is_valid() const { return valid; }
};
```

When a parent is destroyed (via `__exit__` or destructor), it invalidates its token. All children sharing that token will then raise exceptions on access.

### Token Hierarchy

```
Context Token
    │
    ├── Module Token (invalidated when module exits)
    │       │
    │       ├── Function (shares module token)
    │       ├── BasicBlock (shares module token)
    │       └── Instruction (shares module token, or nullptr if detached)
    │
    └── Builder Token (independent, invalidated when builder exits)
```

### Ownership States for Instructions

Instructions have a special `m_detached` flag:

| State | Owner | On Destruction |
|-------|-------|----------------|
| Attached | Parent BasicBlock | Do nothing |
| Detached | Python wrapper | Call `LLVMDeleteInstruction` |
| Erased | None (m_ref = nullptr) | Do nothing |

---

## API Design

### Module-Level Functions

```python
import llvm

# Create a new context (preferred)
with llvm.create_context() as ctx:
    pass

# Get global context (use sparingly)
global_ctx = llvm.global_context()
```

### Context API

```python
with llvm.create_context() as ctx:
    # Type factory methods
    i1 = ctx.int1_type()
    i8 = ctx.int8_type()
    i16 = ctx.int16_type()
    i32 = ctx.int32_type()
    i64 = ctx.int64_type()
    i128 = ctx.int128_type()
    i256 = ctx.int_type(256)  # Arbitrary width
    
    f16 = ctx.half_type()
    f32 = ctx.float_type()
    f64 = ctx.double_type()
    
    void = ctx.void_type()
    
    # Composite types
    ptr = ctx.pointer_type(address_space=0)
    arr = ctx.array_type(i32, 10)
    vec = ctx.vector_type(f32, 4)
    struct = ctx.struct_type([i32, i64, ptr], packed=False)
    named_struct = ctx.named_struct_type("MyStruct")
    named_struct.set_body([i32, i64], packed=False)
    
    # Function types
    func_ty = ctx.function_type(i32, [i64, ptr], vararg=False)
    
    # Module creation
    with ctx.create_module("my_module") as mod:
        pass
    
    # Builder creation
    with ctx.create_builder() as builder:
        pass
    
    # Properties
    ctx.discard_value_names = True  # Optimize memory usage
```

### Module API

```python
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        # Properties
        mod.name = "renamed_module"
        mod.source_filename = "example.c"
        mod.data_layout = "e-m:e-p270:32:32-..."
        mod.target_triple = "x86_64-unknown-linux-gnu"
        
        # Add functions
        func_ty = ctx.function_type(ctx.int32_type(), [])
        main = mod.add_function("main", func_ty)
        
        # Get existing function
        existing = mod.get_function("main")  # Returns None if not found
        
        # Add global variables
        global_var = mod.add_global(ctx.int32_type(), "counter")
        global_var.initializer = llvm.const_int(ctx.int32_type(), 0)
        global_var.linkage = llvm.Linkage.Internal
        
        # Iterate functions
        for func in mod.functions:
            print(func.name)
        
        # Clone module
        with mod.clone() as cloned:
            pass
        
        # Output
        print(mod.to_string())  # Get IR as string
        mod.write_bitcode("output.bc")
        mod.write_ir("output.ll")
        
        # Verification
        if not mod.verify():
            print(mod.get_verification_error())
```

### Function API

```python
with ctx.create_module("example") as mod:
    # Create function type
    param_types = [ctx.int32_type(), ctx.pointer_type()]
    func_ty = ctx.function_type(ctx.int32_type(), param_types)
    
    # Add function to module
    func = mod.add_function("calculate", func_ty)
    
    # Properties
    func.linkage = llvm.Linkage.External
    func.visibility = llvm.Visibility.Default
    func.calling_convention = llvm.CallConv.C
    func.section = ".text"
    
    # Parameters
    func.params[0].name = "x"
    func.params[1].name = "data"
    
    # Add attributes
    func.add_attribute(llvm.Attribute.NoUnwind)
    func.add_attribute(llvm.Attribute.ReadOnly)
    func.add_param_attribute(0, llvm.Attribute.ZExt)
    
    # Basic blocks
    entry = func.append_basic_block("entry")
    loop = func.append_basic_block("loop")
    exit = func.append_basic_block("exit")
    
    # Iterate basic blocks
    for bb in func.basic_blocks:
        print(bb.name)
    
    # Entry block
    print(func.entry_block.name)
    
    # Delete function
    func.erase()  # Removes from module and deletes
```

### BasicBlock API

```python
entry = func.append_basic_block("entry")

# Properties
entry.name = "renamed_entry"
parent_func = entry.parent  # Get owning function

# Navigation
next_bb = entry.next_block      # None if last
prev_bb = entry.prev_block      # None if first

# Instructions
first_inst = entry.first_instruction
last_inst = entry.last_instruction
terminator = entry.terminator   # None if not well-formed

# Iterate instructions
for inst in entry.instructions:
    print(inst.opcode)

# Move block
entry.move_before(other_block)
entry.move_after(other_block)

# Delete block
entry.erase()  # Removes from function and deletes
```

### Builder API

```python
with ctx.create_builder() as builder:
    # Positioning
    builder.position_at_end(entry_block)
    builder.position_before(some_instruction)
    builder.position_after(some_instruction)
    
    current_block = builder.insert_block
    
    # Arithmetic
    add = builder.add(lhs, rhs, name="sum")
    sub = builder.sub(lhs, rhs, name="diff")
    mul = builder.mul(lhs, rhs, name="prod")
    div = builder.sdiv(lhs, rhs, name="quot")  # Signed
    div = builder.udiv(lhs, rhs, name="quot")  # Unsigned
    rem = builder.srem(lhs, rhs, name="rem")
    neg = builder.neg(val, name="negated")
    
    # With overflow flags
    add_nsw = builder.nsw_add(lhs, rhs, name="sum")  # No signed wrap
    add_nuw = builder.nuw_add(lhs, rhs, name="sum")  # No unsigned wrap
    
    # Floating point
    fadd = builder.fadd(lhs, rhs, name="fsum")
    fsub = builder.fsub(lhs, rhs, name="fdiff")
    fmul = builder.fmul(lhs, rhs, name="fprod")
    fdiv = builder.fdiv(lhs, rhs, name="fquot")
    fneg = builder.fneg(val, name="fnegated")
    
    # Bitwise
    and_ = builder.and_(lhs, rhs, name="and")
    or_ = builder.or_(lhs, rhs, name="or")
    xor = builder.xor(lhs, rhs, name="xor")
    shl = builder.shl(val, amount, name="shifted")
    lshr = builder.lshr(val, amount, name="lshifted")  # Logical
    ashr = builder.ashr(val, amount, name="ashifted")  # Arithmetic
    not_ = builder.not_(val, name="inverted")
    
    # Comparisons
    icmp = builder.icmp(llvm.IntPredicate.EQ, lhs, rhs, name="equal")
    icmp = builder.icmp(llvm.IntPredicate.SLT, lhs, rhs, name="less")
    fcmp = builder.fcmp(llvm.RealPredicate.OEQ, lhs, rhs, name="fequal")
    
    # Memory
    alloca = builder.alloca(ctx.int32_type(), name="local")
    alloca_array = builder.alloca(ctx.int32_type(), size=10, name="array")
    load = builder.load(ctx.int32_type(), ptr, name="loaded")
    builder.store(value, ptr)
    
    # GEP (Get Element Pointer)
    gep = builder.gep(elem_ty, ptr, [idx0, idx1], name="element")
    gep = builder.inbounds_gep(elem_ty, ptr, [idx0, idx1], name="element")
    struct_gep = builder.struct_gep(struct_ty, ptr, 0, name="field")
    
    # Casts
    trunc = builder.trunc(val, ctx.int8_type(), name="truncated")
    zext = builder.zext(val, ctx.int64_type(), name="zextended")
    sext = builder.sext(val, ctx.int64_type(), name="sextended")
    fptrunc = builder.fptrunc(val, ctx.float_type(), name="fptruncated")
    fpext = builder.fpext(val, ctx.double_type(), name="fpextended")
    fptosi = builder.fptosi(val, ctx.int32_type(), name="fp_to_int")
    fptoui = builder.fptoui(val, ctx.int32_type(), name="fp_to_uint")
    sitofp = builder.sitofp(val, ctx.double_type(), name="int_to_fp")
    uitofp = builder.uitofp(val, ctx.double_type(), name="uint_to_fp")
    ptrtoint = builder.ptrtoint(ptr, ctx.int64_type(), name="ptr_to_int")
    inttoptr = builder.inttoptr(val, ctx.pointer_type(), name="int_to_ptr")
    bitcast = builder.bitcast(val, other_type, name="reinterpreted")
    
    # Control flow
    builder.ret(value)
    builder.ret_void()
    builder.br(target_block)
    builder.cond_br(condition, true_block, false_block)
    
    # Switch
    switch = builder.switch(value, default_block, num_cases=10)
    switch.add_case(llvm.const_int(ctx.int32_type(), 0), case0_block)
    switch.add_case(llvm.const_int(ctx.int32_type(), 1), case1_block)
    
    # Function calls
    result = builder.call(func_type, func_ptr, [arg0, arg1], name="result")
    
    # Invoke (for exception handling)
    result = builder.invoke(func_type, func_ptr, [args], 
                           normal_dest, unwind_dest, name="result")
    
    # PHI nodes
    phi = builder.phi(ctx.int32_type(), name="merged")
    phi.add_incoming(val1, block1)
    phi.add_incoming(val2, block2)
    
    # Select (ternary)
    selected = builder.select(condition, true_val, false_val, name="selected")
    
    # Unreachable
    builder.unreachable()
    
    # Atomics
    atomic_load = builder.atomic_load(ptr, ordering=llvm.AtomicOrdering.Acquire)
    builder.atomic_store(val, ptr, ordering=llvm.AtomicOrdering.Release)
    old = builder.atomicrmw(llvm.AtomicRMWOp.Add, ptr, val, 
                           ordering=llvm.AtomicOrdering.SeqCst)
    result = builder.cmpxchg(ptr, expected, desired,
                            success_ordering=llvm.AtomicOrdering.SeqCst,
                            failure_ordering=llvm.AtomicOrdering.Acquire)
    builder.fence(ordering=llvm.AtomicOrdering.SeqCst)
```

### Instruction API

```python
# Common properties (all instructions)
inst.opcode              # LLVMOpcode enum
inst.name = "result"     # Set name
inst.parent              # Owning BasicBlock
inst.type                # Result type
inst.num_operands        # Number of operands
inst.get_operand(0)      # Get operand by index
inst.set_operand(0, val) # Set operand by index

# Navigation
next_inst = inst.next_instruction
prev_inst = inst.prev_instruction

# Metadata
inst.set_metadata("dbg", debug_loc)
inst.get_metadata("dbg")
inst.has_metadata()

# Clone (creates detached copy)
cloned = inst.clone()

# Remove and delete
inst.erase()  # Remove from parent and delete

# Detach (for moving)
inst.remove_from_parent()  # Now detached, wrapper owns it
inst.insert_into(builder)  # Re-insert at builder position

# Instruction-specific properties
if inst.opcode == llvm.Opcode.Call:
    inst.calling_convention
    inst.is_tail_call
    inst.called_function

if inst.opcode == llvm.Opcode.Load:
    inst.is_volatile
    inst.alignment
    inst.atomic_ordering

if inst.opcode == llvm.Opcode.GetElementPtr:
    inst.is_inbounds
    inst.source_element_type
```

### Constants API

```python
# Integer constants
zero = llvm.const_int(ctx.int32_type(), 0)
neg_one = llvm.const_int(ctx.int32_type(), -1, sign_extend=True)
big = llvm.const_int_arbitrary(ctx.int128_type(), [low_bits, high_bits])

# Floating point constants
pi = llvm.const_real(ctx.double_type(), 3.14159265359)
inf = llvm.const_real(ctx.double_type(), float('inf'))

# Special values
null = llvm.const_null(ctx.pointer_type())
undef = llvm.undef(ctx.int32_type())
poison = llvm.poison(ctx.int32_type())
all_ones = llvm.const_all_ones(ctx.int32_type())

# Aggregate constants
array = llvm.const_array(ctx.int32_type(), [const1, const2, const3])
struct = llvm.const_struct([field1, field2], packed=False)
named_struct = llvm.const_named_struct(struct_type, [field1, field2])
vector = llvm.const_vector([elem1, elem2, elem3, elem4])

# String constants
str_const = llvm.const_string(ctx, "hello", null_terminate=True)

# Constant expressions
gep = llvm.const_gep(elem_ty, base, [idx0, idx1])
bitcast = llvm.const_bitcast(val, dest_type)
```

### Enumerations

```python
# Linkage types
class Linkage(Enum):
    External = ...
    AvailableExternally = ...
    LinkOnceAny = ...
    LinkOnceODR = ...
    WeakAny = ...
    WeakODR = ...
    Appending = ...
    Internal = ...
    Private = ...
    ExternalWeak = ...
    Common = ...

# Visibility
class Visibility(Enum):
    Default = ...
    Hidden = ...
    Protected = ...

# Calling conventions
class CallConv(Enum):
    C = ...
    Fast = ...
    Cold = ...
    X86Stdcall = ...
    X86Fastcall = ...
    # ... etc

# Integer predicates
class IntPredicate(Enum):
    EQ = ...   # Equal
    NE = ...   # Not equal
    UGT = ...  # Unsigned greater than
    UGE = ...  # Unsigned greater or equal
    ULT = ...  # Unsigned less than
    ULE = ...  # Unsigned less or equal
    SGT = ...  # Signed greater than
    SGE = ...  # Signed greater or equal
    SLT = ...  # Signed less than
    SLE = ...  # Signed less or equal

# Float predicates
class RealPredicate(Enum):
    PredicateFalse = ...  # Always false
    OEQ = ...  # Ordered and equal
    OGT = ...  # Ordered and greater than
    OGE = ...  # Ordered and greater or equal
    OLT = ...  # Ordered and less than
    OLE = ...  # Ordered and less or equal
    ONE = ...  # Ordered and not equal
    ORD = ...  # Ordered (no NaNs)
    UNO = ...  # Unordered (either is NaN)
    UEQ = ...  # Unordered or equal
    UGT = ...  # Unordered or greater than
    UGE = ...  # Unordered or greater or equal
    ULT = ...  # Unordered or less than
    ULE = ...  # Unordered or less or equal
    UNE = ...  # Unordered or not equal
    PredicateTrue = ...   # Always true

# Atomic ordering
class AtomicOrdering(Enum):
    NotAtomic = ...
    Unordered = ...
    Monotonic = ...
    Acquire = ...
    Release = ...
    AcquireRelease = ...
    SequentiallyConsistent = ...

# Atomic RMW operations
class AtomicRMWOp(Enum):
    Xchg = ...
    Add = ...
    Sub = ...
    And = ...
    Nand = ...
    Or = ...
    Xor = ...
    Max = ...
    Min = ...
    UMax = ...
    UMin = ...
    FAdd = ...
    FSub = ...
```

---

## Complete Example

```python
import llvm

def create_factorial_module():
    """Create a module with iterative and recursive factorial functions."""
    
    with llvm.create_context() as ctx:
        # Types
        i64 = ctx.int64_type()
        i1 = ctx.int1_type()
        
        with ctx.create_module("factorial") as mod:
            mod.target_triple = "x86_64-unknown-linux-gnu"
            
            # Function type: i64 factorial(i64)
            func_ty = ctx.function_type(i64, [i64])
            
            # ==========================================
            # Iterative factorial
            # ==========================================
            fact_iter = mod.add_function("factorial_iter", func_ty)
            fact_iter.params[0].name = "n"
            n = fact_iter.params[0]
            
            entry = fact_iter.append_basic_block("entry")
            loop = fact_iter.append_basic_block("loop")
            exit = fact_iter.append_basic_block("exit")
            
            with ctx.create_builder() as builder:
                # Entry block: initialize result=1, i=1
                builder.position_at_end(entry)
                result_ptr = builder.alloca(i64, name="result")
                i_ptr = builder.alloca(i64, name="i")
                builder.store(llvm.const_int(i64, 1), result_ptr)
                builder.store(llvm.const_int(i64, 1), i_ptr)
                builder.br(loop)
                
                # Loop block
                builder.position_at_end(loop)
                i_val = builder.load(i64, i_ptr, name="i_val")
                result_val = builder.load(i64, result_ptr, name="result_val")
                
                # result *= i
                new_result = builder.mul(result_val, i_val, name="new_result")
                builder.store(new_result, result_ptr)
                
                # i++
                new_i = builder.add(i_val, llvm.const_int(i64, 1), name="new_i")
                builder.store(new_i, i_ptr)
                
                # if i <= n, continue loop
                cmp = builder.icmp(llvm.IntPredicate.SLE, new_i, n, name="cmp")
                builder.cond_br(cmp, loop, exit)
                
                # Exit block: return result
                builder.position_at_end(exit)
                final_result = builder.load(i64, result_ptr, name="final")
                builder.ret(final_result)
            
            # ==========================================
            # Recursive factorial
            # ==========================================
            fact_rec = mod.add_function("factorial_rec", func_ty)
            fact_rec.params[0].name = "n"
            n = fact_rec.params[0]
            
            entry = fact_rec.append_basic_block("entry")
            base_case = fact_rec.append_basic_block("base_case")
            recursive = fact_rec.append_basic_block("recursive")
            
            with ctx.create_builder() as builder:
                # Entry: check if n <= 1
                builder.position_at_end(entry)
                is_base = builder.icmp(llvm.IntPredicate.SLE, n, 
                                       llvm.const_int(i64, 1), name="is_base")
                builder.cond_br(is_base, base_case, recursive)
                
                # Base case: return 1
                builder.position_at_end(base_case)
                builder.ret(llvm.const_int(i64, 1))
                
                # Recursive: return n * factorial(n-1)
                builder.position_at_end(recursive)
                n_minus_1 = builder.sub(n, llvm.const_int(i64, 1), name="n_minus_1")
                rec_result = builder.call(func_ty, fact_rec, [n_minus_1], 
                                         name="rec_result")
                result = builder.mul(n, rec_result, name="result")
                builder.ret(result)
            
            # Verify and output
            if not mod.verify():
                raise RuntimeError(mod.get_verification_error())
            
            print(mod.to_string())
            mod.write_bitcode("factorial.bc")
            
            return mod.clone()  # Return cloned module


def create_struct_example():
    """Demonstrate struct manipulation."""
    
    with llvm.create_context() as ctx:
        with ctx.create_module("structs") as mod:
            # Define a Point struct
            i32 = ctx.int32_type()
            point_ty = ctx.named_struct_type("Point")
            point_ty.set_body([i32, i32], packed=False)  # {x, y}
            
            point_ptr_ty = ctx.pointer_type()
            
            # Function: void point_add(Point* a, Point* b, Point* result)
            void = ctx.void_type()
            func_ty = ctx.function_type(void, [point_ptr_ty, point_ptr_ty, point_ptr_ty])
            
            func = mod.add_function("point_add", func_ty)
            func.params[0].name = "a"
            func.params[1].name = "b"
            func.params[2].name = "result"
            
            a, b, result = func.params[0], func.params[1], func.params[2]
            
            entry = func.append_basic_block("entry")
            
            with ctx.create_builder() as builder:
                builder.position_at_end(entry)
                
                # Load a.x and a.y
                a_x_ptr = builder.struct_gep(point_ty, a, 0, name="a_x_ptr")
                a_y_ptr = builder.struct_gep(point_ty, a, 1, name="a_y_ptr")
                a_x = builder.load(i32, a_x_ptr, name="a_x")
                a_y = builder.load(i32, a_y_ptr, name="a_y")
                
                # Load b.x and b.y
                b_x_ptr = builder.struct_gep(point_ty, b, 0, name="b_x_ptr")
                b_y_ptr = builder.struct_gep(point_ty, b, 1, name="b_y_ptr")
                b_x = builder.load(i32, b_x_ptr, name="b_x")
                b_y = builder.load(i32, b_y_ptr, name="b_y")
                
                # Compute result
                sum_x = builder.add(a_x, b_x, name="sum_x")
                sum_y = builder.add(a_y, b_y, name="sum_y")
                
                # Store result
                result_x_ptr = builder.struct_gep(point_ty, result, 0, name="result_x_ptr")
                result_y_ptr = builder.struct_gep(point_ty, result, 1, name="result_y_ptr")
                builder.store(sum_x, result_x_ptr)
                builder.store(sum_y, result_y_ptr)
                
                builder.ret_void()
            
            print(mod.to_string())


def demonstrate_instruction_manipulation():
    """Show instruction cloning and moving."""
    
    with llvm.create_context() as ctx:
        with ctx.create_module("manipulation") as mod:
            i32 = ctx.int32_type()
            func_ty = ctx.function_type(i32, [i32, i32])
            
            func = mod.add_function("example", func_ty)
            a, b = func.params[0], func.params[1]
            
            entry = func.append_basic_block("entry")
            other = func.append_basic_block("other")
            
            with ctx.create_builder() as builder:
                builder.position_at_end(entry)
                
                add = builder.add(a, b, name="sum")
                mul = builder.mul(add, a, name="product")
                
                # Clone an instruction
                add_clone = add.clone()
                
                # Move to other block
                builder.position_at_end(other)
                add_clone.insert_into(builder, name="sum_clone")
                
                result = builder.add(add_clone, b, name="result")
                builder.ret(result)
                
                # Back to entry - finish it
                builder.position_at_end(entry)
                builder.br(other)
            
            print(mod.to_string())


if __name__ == "__main__":
    create_factorial_module()
    create_struct_example()
    demonstrate_instruction_manipulation()
```

---

## Error Handling

### Exception Hierarchy

```python
class LLVMError(Exception):
    """Base class for all LLVM-related errors."""
    pass

class LLVMInvalidOperationError(LLVMError):
    """Operation not valid in current state."""
    pass

class LLVMUseAfterFreeError(LLVMError):
    """Object used after parent was destroyed."""
    pass

class LLVMVerificationError(LLVMError):
    """Module/function verification failed."""
    pass
```

### Error Messages

```python
# Use after parent destruction
>>> mod.to_string()
LLVMUseAfterFreeError: Module used after context was destroyed

# Invalid operation
>>> attached_inst.insert_into(builder)
LLVMInvalidOperationError: Cannot insert: instruction is not detached

# Already consumed
>>> detached.insert_into(builder)
>>> detached.insert_into(builder)  # Second time
LLVMInvalidOperationError: Cannot insert: instruction was already inserted

# Context manager violation
>>> with ctx.create_module("test") as mod:
...     clone = mod.clone()
... # Exiting with outstanding module handle
LLVMInvalidOperationError: Cannot exit context: 1 module(s) still alive
```

---

## Implementation Notes

### C++ Wrapper Structure

```cpp
// Forward declarations
struct ValidityToken;
struct LLVMContextManager;
struct LLVMModule;

// Prevent copy, allow move via explicit methods only
struct NoMoveCopy {
    NoMoveCopy() = default;
    NoMoveCopy(const NoMoveCopy&) = delete;
    NoMoveCopy& operator=(const NoMoveCopy&) = delete;
};

// Validity tracking
struct ValidityToken {
    std::atomic<bool> valid{true};
    void invalidate() { valid = false; }
    bool is_valid() const { return valid; }
};

// Example wrapper
struct LLVMModule : NoMoveCopy {
    LLVMModuleRef m_ref = nullptr;
    std::shared_ptr<ValidityToken> m_token;
    std::shared_ptr<ValidityToken> m_context_token;
    
    void check_valid() const {
        if (!m_ref) 
            throw LLVMException("Module has been disposed");
        if (!m_context_token || !m_context_token->is_valid())
            throw LLVMException("Module used after context was destroyed");
        if (!m_token || !m_token->is_valid())
            throw LLVMException("Module has been disposed");
    }
    
    // Methods call check_valid() first
    std::string to_string() const {
        check_valid();
        char* str = LLVMPrintModuleToString(m_ref);
        std::string result(str);
        LLVMDisposeMessage(str);
        return result;
    }
};
```

### nanobind Registration Pattern

```cpp
NB_MODULE(llvm, m) {
    // Register exception first
    nanobind::register_exception<LLVMException>(m, "LLVMError");
    
    // Enums
    nanobind::enum_<LLVMLinkage>(m, "Linkage")
        .value("External", LLVMExternalLinkage)
        .value("Internal", LLVMInternalLinkage)
        // ...
        .export_values();
    
    // Context manager classes
    nanobind::class_<LLVMContextManager>(m, "LLVMContextManager")
        .def("__enter__", &LLVMContextManager::enter,
             nanobind::rv_policy::reference_internal)
        .def("__exit__", &LLVMContextManager::exit,
             "exc_type"_a.none(), "exc_value"_a.none(), "traceback"_a.none())
        .def("create_module", &LLVMContextManager::create_module,
             nanobind::rv_policy::take_ownership, "name"_a);
    
    // Factory function
    m.def("create_context", []() { 
        return new LLVMContextManager(); 
    }, nanobind::rv_policy::take_ownership);
}
```

---

## Testing Strategy

### Golden Master Test Harness

The project uses a "golden master" testing approach:

1. **C++ tests are the golden master**: They call the LLVM-C API directly and output LLVM IR to stdout
2. **Python tests must produce identical output**: The Python bindings are verified by comparing their stdout to the C++ golden master
3. **Comparison is byte-for-byte**: Outputs must be deterministic and identical

#### How It Works

```
┌─────────────────┐      stdout      ┌──────────────────────┐
│  C++ Test       │ ───────────────> │  tests/output/*.ll   │
│  (golden master)│                  │  (stored reference)  │
└─────────────────┘                  └──────────────────────┘
                                              │
                                              │ diff
                                              ▼
┌─────────────────┐      stdout      ┌──────────────────────┐
│  Python Test    │ ───────────────> │  (compared directly) │
│  (must match)   │                  │                      │
└─────────────────┘                  └──────────────────────┘
```

#### Why Outputs Must Be Deterministic

- **No timestamps** - dates/times change between runs
- **No process IDs** - PIDs change between runs  
- **No memory addresses** - addresses change between runs
- **No content added by test harness** - raw stdout only
- Running `./build/test_foo` directly must produce the exact same output as stored in `tests/output/test_foo.ll`

#### Directory Structure

```
llvm-nanobind/
├── run_tests.py                      # Test runner (runs C++ and Python tests)
├── test_context.py                   # Python equivalent of test_context.cpp
├── test_factorial.py                 # Python equivalent of test_factorial.cpp
├── tests/                            # C++ test sources
│   ├── test_context.cpp              
│   ├── test_module.cpp               
│   ├── test_types.cpp                
│   ├── test_function.cpp             
│   ├── test_basic_block.cpp          
│   ├── test_builder_arithmetic.cpp   
│   ├── test_builder_memory.cpp       
│   ├── test_builder_control_flow.cpp 
│   ├── test_builder_casts.cpp        
│   ├── test_builder_cmp.cpp          
│   ├── test_constants.cpp            
│   ├── test_globals.cpp              
│   ├── test_phi.cpp                  
│   ├── test_factorial.cpp            
│   ├── test_struct.cpp               
│   └── output/                       # Golden master outputs (gitignored)
│       ├── test_context.ll
│       ├── test_module.ll
│       └── ...
└── playground.py                     # Human experimentation (not part of test suite)
```

#### Running Tests

```bash
# Build everything
cmake --build build

# Run all tests: C++ golden masters + Python comparison
python3 run_tests.py
```

The test runner:
1. Runs each C++ test executable and saves stdout to `tests/output/<name>.ll`
2. Runs corresponding Python tests (if they exist)
3. Compares Python output to C++ output (must be identical)
4. Reports pass/fail for both C++ execution and Python comparison

#### Manual Testing and Debugging

```bash
# Run C++ test directly (output should match tests/output/test_foo.ll)
./build/test_factorial

# Run Python test directly
python3 test_factorial.py

# Compare outputs manually
./build/test_factorial > /tmp/cpp.ll
python3 test_factorial.py > /tmp/py.ll
diff /tmp/cpp.ll /tmp/py.ll
```

#### Test Output Format

Each test outputs valid LLVM IR with diagnostic comments to stdout:

```llvm
; Test: test_context
; Context created: yes
; Discard value names (before): false
; Discard value names (after set to true): true

; ModuleID = 'test_context'
source_filename = "test_context"
```

This format ensures:
- Human-readable test identification via comments
- Machine-parsable diagnostic data
- Valid LLVM IR that can be verified with the LLVM verifier

#### C++ Test Template

```cpp
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_xxx", ctx);
    
    // ... build IR using LLVM-C API ...
    
    // Verify module
    char *error = nullptr;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "; Verification failed: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return 1;
    }
    LLVMDisposeMessage(error);
    
    // Print diagnostic comments to stdout
    printf("; Test: test_xxx\n");
    printf("; ... additional diagnostics ...\n");
    printf("\n");
    
    // Print module IR to stdout
    char *ir = LLVMPrintModuleToString(mod);
    printf("%s", ir);
    LLVMDisposeMessage(ir);
    
    // Cleanup
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);
    
    return 0;
}
```

#### Python Test Template

```python
#!/usr/bin/env python3
import sys
sys.path.insert(0, "build")
import llvm

def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_xxx") as mod:
            with ctx.create_builder() as builder:
                # ... build IR using Python bindings ...
                pass
            
            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}", 
                      file=sys.stderr)
                return 1
            
            # Print diagnostic comments to stdout (must match C++ exactly!)
            print("; Test: test_xxx")
            print("; ... additional diagnostics ...")
            print()
            
            # Print module IR to stdout
            print(mod.to_string(), end="")
    
    return 0

if __name__ == "__main__":
    sys.exit(main())
```

#### Test Coverage by Category

| Category | Test File | LLVM-C APIs Covered |
|----------|-----------|---------------------|
| Context | `test_context.cpp` | `LLVMContextCreate`, `LLVMContextDispose`, `LLVMGetGlobalContext`, `LLVMContextShouldDiscardValueNames`, `LLVMContextSetDiscardValueNames` |
| Module | `test_module.cpp` | `LLVMModuleCreateWithNameInContext`, `LLVMGetModuleIdentifier`, `LLVMSetModuleIdentifier`, `LLVMGetSourceFileName`, `LLVMSetSourceFileName`, `LLVMGetDataLayoutStr`, `LLVMSetDataLayout`, `LLVMGetTarget`, `LLVMSetTarget`, `LLVMCloneModule`, `LLVMPrintModuleToString` |
| Types | `test_types.cpp` | `LLVMInt*TypeInContext`, `LLVMHalfTypeInContext`, `LLVMFloatTypeInContext`, `LLVMDoubleTypeInContext`, `LLVMVoidTypeInContext`, `LLVMPointerTypeInContext`, `LLVMArrayType2`, `LLVMVectorType`, `LLVMFunctionType`, `LLVMStructTypeInContext`, `LLVMStructCreateNamed`, `LLVMStructSetBody`, `LLVMPrintTypeToString`, `LLVMGetTypeKind` |
| Functions | `test_function.cpp` | `LLVMAddFunction`, `LLVMGetNamedFunction`, `LLVMCountParams`, `LLVMGetParam`, `LLVMSetValueName2`, `LLVMGetFunctionCallConv`, `LLVMSetFunctionCallConv`, `LLVMGetLinkage`, `LLVMSetLinkage`, `LLVMDeleteFunction` |
| Basic Blocks | `test_basic_block.cpp` | `LLVMAppendBasicBlockInContext`, `LLVMGetBasicBlockName`, `LLVMGetBasicBlockParent`, `LLVMGetEntryBasicBlock`, `LLVMCountBasicBlocks`, `LLVMGetFirstBasicBlock`, `LLVMGetNextBasicBlock` |
| Arithmetic | `test_builder_arithmetic.cpp` | `LLVMBuildAdd`, `LLVMBuildSub`, `LLVMBuildMul`, `LLVMBuildSDiv`, `LLVMBuildUDiv`, `LLVMBuildFAdd`, `LLVMBuildFSub`, `LLVMBuildFMul`, `LLVMBuildFDiv`, `LLVMBuildShl`, `LLVMBuildLShr`, `LLVMBuildAShr`, `LLVMBuildAnd`, `LLVMBuildOr`, `LLVMBuildXor`, `LLVMBuildNeg`, `LLVMBuildNot` |
| Memory | `test_builder_memory.cpp` | `LLVMBuildAlloca`, `LLVMBuildLoad2`, `LLVMBuildStore`, `LLVMBuildGEP2`, `LLVMBuildInBoundsGEP2`, `LLVMBuildStructGEP2` |
| Control Flow | `test_builder_control_flow.cpp` | `LLVMBuildRetVoid`, `LLVMBuildRet`, `LLVMBuildBr`, `LLVMBuildCondBr`, `LLVMBuildSwitch`, `LLVMAddCase`, `LLVMBuildCall2`, `LLVMBuildUnreachable` |
| Casts | `test_builder_casts.cpp` | `LLVMBuildTrunc`, `LLVMBuildZExt`, `LLVMBuildSExt`, `LLVMBuildFPToUI`, `LLVMBuildFPToSI`, `LLVMBuildUIToFP`, `LLVMBuildSIToFP`, `LLVMBuildFPTrunc`, `LLVMBuildFPExt`, `LLVMBuildPtrToInt`, `LLVMBuildIntToPtr`, `LLVMBuildBitCast` |
| Comparisons | `test_builder_cmp.cpp` | `LLVMBuildICmp`, `LLVMBuildFCmp`, `LLVMBuildSelect` |
| Constants | `test_constants.cpp` | `LLVMConstInt`, `LLVMConstReal`, `LLVMConstNull`, `LLVMConstAllOnes`, `LLVMGetUndef`, `LLVMGetPoison`, `LLVMConstStringInContext2`, `LLVMConstArray2`, `LLVMConstStructInContext` |
| Globals | `test_globals.cpp` | `LLVMAddGlobal`, `LLVMGetNamedGlobal`, `LLVMSetInitializer`, `LLVMSetGlobalConstant`, `LLVMSetLinkage`, `LLVMSetAlignment` |
| PHI Nodes | `test_phi.cpp` | `LLVMBuildPhi`, `LLVMAddIncoming`, `LLVMCountIncoming`, `LLVMGetIncomingValue`, `LLVMGetIncomingBlock` |
| Integration | `test_factorial.cpp` | Complete iterative factorial function |
| Integration | `test_struct.cpp` | Point struct with `point_add` function |

### Unit Tests

```python
def test_context_lifecycle():
    """Context should be valid inside with block, invalid after."""
    with llvm.create_context() as ctx:
        assert ctx.is_valid()
        mod_handle = ctx.create_module("test")
    
    # After exiting, operations should raise
    with pytest.raises(llvm.LLVMUseAfterFreeError):
        ctx.int32_type()


def test_module_clone_independent():
    """Cloned module should have independent lifetime."""
    with llvm.create_context() as ctx:
        with ctx.create_module("original") as mod:
            clone_handle = mod.clone()
        
        # Original is gone, clone should still work
        with clone_handle as cloned:
            assert cloned.name == "original"


def test_instruction_detach_reattach():
    """Instructions can be detached and reattached."""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            # ... create function with instruction ...
            
            inst.remove_from_parent()
            assert inst.is_detached
            
            with ctx.create_builder() as builder:
                builder.position_at_end(other_block)
                inst.insert_into(builder)
                assert not inst.is_detached
```

### Integration Tests

```python
def test_compile_and_run():
    """Create module, compile with LLVM, execute."""
    # Create factorial module
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            # ... build factorial function ...
            
            # Write bitcode
            mod.write_bitcode("/tmp/test.bc")
    
    # Compile with clang
    subprocess.run(["clang", "/tmp/test.bc", "-o", "/tmp/test"])
    
    # Run and verify
    result = subprocess.run(["/tmp/test"], capture_output=True)
    assert result.returncode == 0
```

---

## Future Enhancements

1. **Debug Info**: Support for `DIBuilder` and debug metadata
2. **Pass Manager**: Expose optimization passes
3. **JIT Compilation**: Integration with LLVM's ORC JIT
4. **Target Machine**: Code generation to native assembly/object files
5. **Intrinsics**: Easy access to LLVM intrinsic functions
6. **Exception Handling**: `invoke`, `landingpad`, personality functions
7. **Metadata**: Full metadata node support
8. **Threading**: Thread-safe context management

---

## References

- [LLVM-C API Documentation](https://llvm.org/doxygen/group__LLVMC.html)
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html)
- [nanobind Documentation](https://nanobind.readthedocs.io/)
- [LLVM Programmer's Manual](https://llvm.org/docs/ProgrammersManual.html)