# Understanding llvm-nanobind: A Personalized Learning Guide

This is your comprehensive learning guide for understanding and critiquing the llvm-nanobind transformation API work. It's structured as a progressive journey, not a reference manual.

**How to use this guide:**
- Read actively: try the exercises, don't just skim
- The quizzes aren't tests—they're thinking prompts
- Skip sections you already know, but be honest with yourself
- The final sections invite your critique; that's the real goal

---

## Part 1: The Foundation Layer

### Chapter 1.1: What Problem Are We Solving?

Before understanding the code, understand the *need*.

**The Setup:**
LLVM is a compiler infrastructure. It represents programs in a language-agnostic form called "IR" (Intermediate Representation). Compilers translate high-level languages → IR → machine code.

```
C/C++/Rust/Swift → LLVM IR → x86/ARM/etc.
      ↓
   Front-end        ↓           ↓
              Middle-end    Back-end
```

**The Key Insight:**
If you can *modify* the IR, you can transform programs:
- Optimize them (make them faster)
- Obfuscate them (make them harder to reverse-engineer)
- Instrument them (add profiling, sanitizers)
- Translate them (emit different targets)

**The Problem:**
LLVM's API is C++. To write transformations, you traditionally need:
- C++ expertise
- Build system complexity
- Long compile times
- Manual memory management

**The Solution:**
Python bindings let you write transformations in a high-level language. Faster iteration, easier experimentation, more accessible.

**Quiz 1.1 - Thinking Check:**
> Why might someone *not* want to use Python for LLVM work? What are we trading off?

<details>
<summary>Click to reveal answer</summary>

**Performance**: Python is ~100x slower than C++ for CPU-bound work. For production compiler passes that run on millions of lines, this matters.

**Completeness**: Bindings can't expose everything. Some LLVM APIs are C++-only.

**Integration**: Real compilers are C++ codebases. Python passes are harder to integrate.

The trade-off: We sacrifice performance and completeness for developer velocity and accessibility.

**When Python makes sense**: prototyping, research, education, one-off transformations, tools that run infrequently.

**When C++ makes sense**: production compiler passes, performance-critical paths, when you need APIs not exposed to C.
</details>

---

### Chapter 1.2: LLVM IR - The Language You're Transforming

You can't transform what you don't understand.

**The Key Mental Model:**
LLVM IR is like a typed, structured assembly language. It has:
- **Modules**: Contain functions and global variables
- **Functions**: Contain basic blocks
- **Basic Blocks**: Contain instructions, end with a terminator
- **Instructions**: The actual operations (add, load, store, branch...)

```
Module
├── Global Variables (@global_var)
├── Function Declarations (declare @printf)
└── Function Definitions (define @main)
    └── Basic Blocks (%entry, %loop, %exit)
        └── Instructions
            ├── %x = add i32 %a, %b
            ├── store i32 %x, ptr %addr
            └── br label %loop  (terminator)
```

**Critical Concept: SSA Form**

LLVM IR uses "Static Single Assignment" (SSA) form. Every value is assigned exactly once.

```llvm
; NOT valid LLVM IR (same variable assigned twice):
%x = add i32 1, 2
%x = add i32 %x, 3   ; ERROR: %x already defined

; Valid LLVM IR (different names):
%x = add i32 1, 2
%y = add i32 %x, 3   ; OK: %y is a new value
```

**Why SSA matters for you:**
When you transform code, you can't just "change" a value. You create a *new* value and *replace uses* of the old one.

This is why `replace_all_uses_with()` is so fundamental:
```python
# Create new value
new_val = builder.add(a, b, "new")
# Replace all uses of old value with new value
old_val.replace_all_uses_with(new_val)
# Delete old value
old_val.erase_from_parent()
```

**Exercise 1.2:**
Look at this IR. How many basic blocks are there? What's the terminator of each?

```llvm
define i32 @factorial(i32 %n) {
entry:
  %cmp = icmp sle i32 %n, 1
  br i1 %cmp, label %base, label %recurse

base:
  ret i32 1

recurse:
  %sub = sub i32 %n, 1
  %call = call i32 @factorial(i32 %sub)
  %mul = mul i32 %n, %call
  ret i32 %mul
}
```

<details>
<summary>Answer</summary>

**3 basic blocks:**
1. `entry` - terminator: `br` (conditional branch)
2. `base` - terminator: `ret` (return)
3. `recurse` - terminator: `ret` (return)

Every basic block ends with exactly one terminator instruction. This is invariant.
</details>

---

### Chapter 1.3: The Transformation Pattern

Almost every transformation follows the same pattern:

```
1. TRAVERSE:   Walk over modules/functions/blocks/instructions
2. IDENTIFY:   Find the pattern you want to transform
3. BUILD:      Create the replacement IR
4. REPLACE:    Swap out old IR for new IR
5. CLEAN UP:   Delete unreachable code, verify module
```

**Concrete Example - MBA Substitution:**

The `mba_sub.py` pass transforms `X - Y` into `(X ^ -Y) + 2*(X & -Y)`.

Why? They're mathematically equivalent, but the second form is harder for a human (or decompiler) to recognize as subtraction.

Let's trace through:

```python
# 1. TRAVERSE: Walk basic blocks and instructions
for bb in func.basic_blocks:
    for inst in bb.instructions:

        # 2. IDENTIFY: Find subtraction instructions
        if inst.opcode == llvm.Opcode.Sub:

            # 3. BUILD: Create replacement
            a = inst.get_operand(0)
            b = inst.get_operand(1)
            with bb.create_builder() as builder:
                builder.position_before(inst)
                neg_b = builder.neg(b)
                xor_val = builder.xor(a, neg_b)
                and_val = builder.and_(a, neg_b)
                mul_val = builder.mul(ty.constant(2), and_val)
                result = builder.add(xor_val, mul_val)

            # 4. REPLACE: Swap old for new
            inst.replace_all_uses_with(result)
            inst.erase_from_parent()

# 5. CLEAN UP: Verify
assert mod.verify()
```

**Quiz 1.3:**
Why do we need `builder.position_before(inst)` before creating the replacement?

<details>
<summary>Answer</summary>

The new instructions need to be inserted *before* the old subtraction, so they dominate it. If we inserted them after, the old subtraction would execute first, and our new instructions would be useless.

More subtly: the new instructions might reference the operands of the old instruction. Those operands must be defined (dominate) the point of use. Inserting before the old instruction guarantees this.
</details>

---

## Part 2: The Python Bindings Layer

### Chapter 2.1: nanobind and the C API

**The Architecture:**

```
┌─────────────────────────────────────────────────────┐
│                   Your Python Code                  │
├─────────────────────────────────────────────────────┤
│              llvm-nanobind (Python bindings)        │
│   - Pythonic wrapper classes (Module, Function...)  │
│   - Memory safety (validity tokens)                 │
│   - Context managers                                │
├─────────────────────────────────────────────────────┤
│                 nanobind (C++/Python bridge)        │
│   - Type conversions                                │
│   - Exception handling                              │
│   - Object lifetime management                      │
├─────────────────────────────────────────────────────┤
│                    LLVM C API                       │
│   - LLVMCreateBuilder, LLVMBuildAdd, etc.          │
│   - Raw C functions, opaque pointers                │
├─────────────────────────────────────────────────────┤
│                    LLVM C++ Core                    │
│   - The actual compiler infrastructure              │
└─────────────────────────────────────────────────────┘
```

**Why the C API?**

LLVM has both C++ and C APIs. The C API is:
- Stable (ABI-compatible across versions)
- Bindable (easy to wrap from any language)
- Limited (not all features are exposed)

The bindings wrap the C API, which wraps the C++ implementation.

**Key Insight:**
When you find a missing feature, the question is: "Does the C API expose this?" If yes, we can add it. If no, we'd need a C wrapper first.

### Chapter 2.2: Memory Model and Validity Tokens

**The Problem:**
LLVM objects have complex ownership. A `Module` owns its `Function`s. A `Function` owns its `BasicBlock`s. When a parent is destroyed, children become invalid.

In C++, using a dangling pointer crashes. In Python bindings, we need something safer.

**The Solution: Validity Tokens**

```python
with llvm.create_context() as ctx:
    with ctx.parse_ir(ir_text) as mod:
        func = list(mod.functions)[0]
        # func is valid here

    # Module is disposed
    print(func.name)  # SAFE: Raises exception, not crash
```

The bindings track which objects belong to which context/module. When the parent is disposed, all children are marked invalid.

**Quiz 2.2:**
What would happen if we didn't have validity tokens and you accessed a function after its module was disposed?

<details>
<summary>Answer</summary>

In the best case: a crash (segfault).
In the worst case: reading garbage memory, which might look like valid data but isn't. This leads to silent corruption, wrong results, or security vulnerabilities.

Validity tokens turn undefined behavior into a clean Python exception. This is the "memory-safe" claim in the README.
</details>

### Chapter 2.3: API Design Philosophy

The bindings try to be Pythonic while staying close to LLVM's model.

**Properties vs Methods:**

```python
# Properties for simple getters:
bb.terminator      # not get_terminator()
inst.opcode        # not get_opcode()
func.name          # not get_name()

# Methods for operations with side effects:
builder.add(a, b)  # creates instruction
inst.erase_from_parent()  # modifies IR
```

**Context Managers:**

```python
# Resources that need cleanup use `with`:
with llvm.create_context() as ctx:
    with ctx.parse_ir(ir_text) as mod:
        with bb.create_builder() as builder:
            ...
```

**Iteration:**

```python
# Iteration is Pythonic:
for func in mod.functions:
    for bb in func.basic_blocks:
        for inst in bb.instructions:
            ...
```

**Consistency improvements:**

Several early inconsistencies have been cleaned up. Current code can use:

```python
ptr_ty = ctx.types.ptr
for op in inst.operands:
    ...
inst.is_terminator
inst.parent  # alias for inst.block
gv.is_global_constant = True
```

The porting guide still documents remaining differences from the C++ API, but the common transformation path now uses Pythonic properties and single-step helpers.

**Exercise 2.3:**
You want to replace one operand of an instruction. Should you use `inst.operands` or indexed access?

<details>
<summary>Answer</summary>

Use `inst.operands` for read-only iteration:
```python
for op in inst.operands:
    print(op)
```

Use indexed access for mutation:
```python
for i in range(inst.num_operands):
    if inst.get_operand(i) == old:
        inst.set_operand(i, new)
```
</details>

---

## Part 3: The Obfuscation Passes (Case Study)

### Chapter 3.1: Why Obfuscation?

Obfuscation transforms code to resist reverse engineering. It's used for:
- DRM and license protection
- Malware (unfortunately)
- Protecting proprietary algorithms
- CTF challenges and security education

The obfuscation passes in `tools/obfuscation/` are *educational examples*, not production tools. They demonstrate transformation patterns and stress-test the bindings.

### Chapter 3.2: MBA Substitution Deep Dive

**The Math:**

Mixed Boolean Arithmetic uses the fact that arithmetic and bitwise operations are related:

```
X - Y = (X ^ -Y) + 2*(X & -Y)

Proof:
  Let -Y represent two's complement negation.
  (X ^ -Y) gives the "exclusive" bits
  2*(X & -Y) adds back the "shared" bits, doubled because...

  Actually, let's verify empirically:
  X = 5 (0101), Y = 3 (0011)
  -Y = -3 = (1101 in 4-bit two's complement)

  X ^ -Y = 0101 ^ 1101 = 1000
  X & -Y = 0101 & 1101 = 0101
  2*(X & -Y) = 2*5 = 10 = 1010

  Result: 1000 + 1010 = 10010 (but in 4-bit: 0010 = 2)
  Expected: 5 - 3 = 2 ✓
```

The key insight: decompilers pattern-match on operations. `sub` decompiles to `-`. But `(x ^ neg_y) + 2*(x & neg_y)` doesn't—it looks like arbitrary bitwise soup.

**The Implementation Pattern:**

```python
def obfuscate_sub(builder, a, b, name=""):
    """X - Y == (X ^ -Y) + 2*(X & -Y)"""
    neg_b = builder.neg(b, "mba.neg")          # Step 1: -Y
    xor_val = builder.xor(a, neg_b, "mba.xor") # Step 2: X ^ -Y
    and_val = builder.and_(a, neg_b, "mba.and") # Step 3: X & -Y
    two = a.type.constant(2)
    mul_val = builder.mul(two, and_val, "mba.mul")  # Step 4: 2*(X & -Y)
    result = builder.add(xor_val, mul_val, name)    # Step 5: combine
    return result
```

**Quiz 3.2:**
The pass has multiple XOR obfuscations (v1-v4). Why randomize between them?

<details>
<summary>Answer</summary>

If all XORs became the same pattern, a reverse engineer could write a pattern matcher to undo them. By randomly choosing between mathematically-equivalent forms, we create variety that's harder to automate away.

This is a general obfuscation principle: **polymorphism defeats pattern matching**.
</details>

### Chapter 3.3: Control Flow Flattening Deep Dive

**The Concept:**

Normal control flow:
```
     ┌──────────┐
     │  entry   │
     └────┬─────┘
          │ if (cond)
     ┌────┴────┐
     ▼         ▼
┌────────┐ ┌────────┐
│ block1 │ │ block2 │
└───┬────┘ └────┬───┘
    └──────┬────┘
           ▼
     ┌──────────┐
     │   exit   │
     └──────────┘
```

Flattened control flow:
```
     ┌──────────┐
     │  entry   │
     └────┬─────┘
          │ state = INITIAL
          ▼
     ┌──────────┐◄─────────────┐
     │dispatcher│              │
     └────┬─────┘              │
          │ switch(state)      │
    ┌─────┼─────┐              │
    ▼     ▼     ▼              │
┌──────┐┌──────┐┌──────┐       │
│block1││block2││ exit │       │
│state=││state=│└──────┘       │
│ NEXT ││ NEXT │               │
└──┬───┘└──┬───┘               │
   └───────┴───────────────────┘
```

Every block updates a state variable, then jumps to the dispatcher. The dispatcher decides which block runs next.

**Why it works:**
- CFG analysis sees one giant switch, not the original structure
- Data flow analysis becomes much harder
- Decompilers produce horrible output

**The Implementation:**

The pass does:
1. **Demote PHI nodes**: PHI nodes encode control flow. Replace them with explicit memory (alloca/load/store).
2. **Create dispatcher**: A block that branches based on state.
3. **Assign states**: Each original block gets a unique random state value.
4. **Rewrite terminators**: Replace branches with state updates + jump to dispatcher.

**The PHI Demotion Subtlety:**

PHI nodes exist because of SSA form. In normal code:

```llvm
; PHI node: value depends on which predecessor we came from
%result = phi i32 [ %x, %block1 ], [ %y, %block2 ]
```

After flattening, we don't know the "predecessor" anymore—we came from the dispatcher! So we convert to memory:

```llvm
; In block1:
store i32 %x, ptr %result.addr
br label %dispatcher

; In block2:
store i32 %y, ptr %result.addr
br label %dispatcher

; Where the PHI was:
%result = load i32, ptr %result.addr
```

**Quiz 3.3:**
Why does the pass shuffle condition block order (`--shuffle` flag)?

<details>
<summary>Answer</summary>

Without shuffling, the condition blocks check states in the order blocks appear in the original function. An analyst could observe: "state 42 is checked first, 89 second, 17 third" and infer the original block order.

Shuffling randomizes this correlation, making it harder to recover the original structure.

This is defense in depth: the state values are already random, but their *check order* leaks information too.
</details>

### Chapter 3.4: Missing APIs Discovered

The porting process revealed API gaps:

| Previously Missing API | Current Python API |
|-----|----------------|
| Replace all uses | `value.replace_all_uses_with(new_value)` |
| Delete instruction | `inst.erase_from_parent()` |
| Split block | `bb.split_basic_block(...)` / `bb.split_basic_block_before(...)` |
| Raw byte constants | `llvm.const_string(ctx, b"...", ...)` and `llvm.const_data_array(i8, b"...")` |

**Raw bytes example:**

```python
encrypted = bytes([0xFF, 0x80, 0x42])
const = llvm.const_string(ctx, encrypted, dont_null_terminate=True)
assert const.type.array_length == len(encrypted)
```

Use the `bytes` overload for binary payloads. The `str` overload is for text and follows UTF-8 encoding.

---

## Part 4: Critical Evaluation Framework

Now you understand the work. Let's develop your ability to *critique* it.

### Chapter 4.1: API Design Critique

**Lens 1: Consistency**

An API should have predictable patterns. Test: Can you predict the name of something you haven't used?

| Test | Current API |
|------|-------------|
| Get parent block of instruction | `.block` or `.parent` |
| Get all operands | `.operands` |
| Is it a terminator? | `.is_terminator` |
| Get pointer type | `ctx.types.ptr` |

**Your Task:**
Add rows to this table. What else would you try to predict? Check the porting guide—does it exist, and does it match your expectation?

**Lens 2: Pit of Success**

A good API makes it hard to do the wrong thing. The "pit of success" means the easy path is the correct path.

| Operation | Current API | Pit of Success? |
|-----------|-------------|-----------------|
| Delete instruction | `inst.erase_from_parent()` | Yes |
| Parse module | `ctx.parse_ir()` returns context manager, not module | Partial—must use `with` |
| Create pointer type | `ctx.types.ptr` | Yes |

**Your Task:**
For each failure, design a better API. What would make the easy path correct?

**Lens 3: Completeness**

Does the API let you do everything you need?

The porting guide rates the API "7/10 for code generation, 5/10 for transforms."

**Your Task:**
What's the minimum set of additions to get to 8/10 for transforms? Refer to the plan.md priorities.

### Chapter 4.2: Obfuscation Quality Critique

**Lens 1: Semantic Preservation**

Does the transformation preserve program behavior?

**Test:**
```bash
# Original
echo '5 - 3' | bc  # 2

# After MBA substitution:
# (5 ^ -3) + 2*(5 & -3) = ?
```

**Your Task:**
Verify this algebraically for a few examples. Are there edge cases where the MBA identity might fail? (Hint: overflow, signed vs unsigned)

**Lens 2: Strength of Obfuscation**

How hard is it to undo?

| Obfuscation | Attack |
|-------------|--------|
| MBA substitution | Pattern matching, symbolic execution, or just... run the code |
| Control flow flattening | Symbolic execution to recover original CFG |

**Your Task:**
Research "deobfuscation" techniques. What tools exist? (KLEE, angr, Triton, Miasm). How would they attack these passes?

**Lens 3: Performance Impact**

Obfuscation has costs:

| Pass | Overhead |
|------|----------|
| MBA | More instructions per operation |
| CFF | Indirect branches, memory access for state |

**Your Task:**
Estimate the overhead. If a hot loop has 10 subtractions, how many extra instructions does MBA add? What's the impact on branch prediction from CFF?

### Chapter 4.3: Process Critique

**How was this work done?**

The README says: "This project is 90%+ vibe coded. It is mostly an experiment to see what LLMs can do when you set things up properly."

**Questions for reflection:**

1. What does "set things up properly" mean for LLM-assisted development?
2. The porting guide discovered many API issues. Was the porting exercise a form of "dogfooding"?
3. The plan.md has clear priorities. Is this the right order? Would you prioritize differently?

---

## Part 5: Exercises and Self-Assessment

### Exercise 5.1: Read and Trace

Read `tools/obfuscation/mba_sub.py` line by line. For each function:
1. What pattern does it implement?
2. Can you prove the mathematical identity?
3. What happens if `a` or `b` is a constant vs a variable?

### Exercise 5.2: Break It

Try to find inputs that cause incorrect output:
1. Create an IR file with various operations
2. Run the MBA pass
3. Compare original vs obfuscated execution

```bash
# Create test
cat > test.ll << 'EOF'
define i32 @test(i32 %a, i32 %b) {
  %sub = sub i32 %a, %b
  ret i32 %sub
}
EOF

# Run MBA pass
uv run tools/obfuscation/mba_sub.py test.ll -o obf.ll

# Compare (using lli to execute)
echo "Original: $(echo 'define i32 @main() { %r = call i32 @test(i32 5, i32 3) ret i32 %r }' | cat test.ll - | lli)"
echo "Obfuscated: $(echo 'define i32 @main() { %r = call i32 @test(i32 5, i32 3) ret i32 %r }' | cat obf.ll - | lli)"
```

### Exercise 5.3: Extend

Choose one:
1. Add a new MBA substitution (e.g., for AND operation)
2. Add a new metric to measure obfuscation strength
3. Write a test that verifies semantic preservation

### Exercise 5.4: Fix a Bug

The plan.md mentions that `replace_all_uses_with` isn't bound. The passes implement it manually. Find the manual implementation and:
1. Identify any edge cases it might miss
2. What would the proper C API binding look like?
3. Why might the manual version be slower than the native one?

---

## Part 6: Synthesis Questions

These questions don't have single right answers. They're for developing your judgment.

### Q1: API Evolution
The bindings are "not yet stable." What process would you use to stabilize them? What constitutes a "breaking change" in this context?

### Q2: Testing Strategy
The plan mentions "unit test, integration test, porting test" for each improvement. Is this enough? What would you add?

### Q3: Documentation Philosophy
The porting guide is extensive. Is it the right format? Should this be API docs, examples, tutorials, or something else?

### Q4: Scope Decision
Should the bindings aim for 100% C API coverage, or focus on the "useful subset"? What's the trade-off?

### Q5: Alternative Approaches
What if instead of binding LLVM's C API, we:
- Used MLIR?
- Wrote a pure-Python IR library?
- Used a different binding technology (SWIG, Cython, PyO3)?

---

## Appendix A: Vocabulary

| Term | Definition |
|------|------------|
| IR | Intermediate Representation - the language between source and machine code |
| SSA | Static Single Assignment - each value defined exactly once |
| Basic Block | Straight-line code with one entry and one exit |
| Terminator | The last instruction in a basic block (branch, return, etc.) |
| PHI Node | Instruction that selects between values based on predecessor |
| Domination | A block A dominates B if all paths to B go through A |
| Use | A reference to a value (every instruction "uses" its operands) |
| RAUW | Replace All Uses With - substitute one value for another everywhere |
| CFG | Control Flow Graph - blocks as nodes, branches as edges |
| MBA | Mixed Boolean Arithmetic - identities between arithmetic and bitwise ops |
| CFF | Control Flow Flattening - hide CFG structure with state machine |

## Appendix B: Further Reading

**LLVM:**
- [LLVM Language Reference](https://llvm.org/docs/LangRef.html) - The authoritative IR spec
- [LLVM Programmer's Manual](https://llvm.org/docs/ProgrammersManual.html) - How to write transforms in C++
- [Writing an LLVM Pass](https://llvm.org/docs/WritingAnLLVMPass.html) - Tutorial

**Obfuscation:**
- "Obfuscating C++ Programs via Control Flow Flattening" (Laszlo, 2009)
- "Mixed Boolean-Arithmetic" (Zhou et al., 2007)
- Quarkslab's blog on deobfuscation

**Python Bindings:**
- [nanobind documentation](https://nanobind.readthedocs.io/)
- [LLVM C API](https://llvm.org/doxygen/group__LLVMC.html)

## Appendix C: Your Critique Template

Use this structure when reviewing the work:

```markdown
## Summary
[One paragraph: what is this, what does it do]

## Strengths
- [What works well]
- [Good design decisions]
- [Things to preserve]

## Weaknesses
- [API issues]
- [Missing functionality]
- [Documentation gaps]

## Recommendations
- [High priority: do first]
- [Medium priority: do soon]
- [Low priority: nice to have]

## Questions for Discussion
- [Things you're uncertain about]
- [Trade-offs worth discussing]
```

---

*This guide was generated to help you understand, not just verify. The goal is that you can now critique this work with confidence—and contribute better ideas to what comes next.*
