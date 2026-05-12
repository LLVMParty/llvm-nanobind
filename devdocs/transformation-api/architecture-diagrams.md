# Architecture Diagrams

Visual reference materials for understanding llvm-nanobind.

---

## 1. System Layers

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           YOUR PYTHON CODE                                  │
│                                                                             │
│   with llvm.create_context() as ctx:                                        │
│       with ctx.parse_ir(ir_text) as mod:                                    │
│           for func in mod.functions:                                        │
│               for bb in func.basic_blocks:                                  │
│                   # transform...                                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                         llvm-nanobind (This Project)                        │
│                                                                             │
│  ┌─────────────────────────────────────────────────────────────────────┐   │
│  │  Python Classes          │  Lifetime Management  │  Type Helpers    │   │
│  │  ─────────────────       │  ────────────────────  │  ────────────    │   │
│  │  Module                  │  Validity tokens      │  ctx.types.i32   │   │
│  │  Function                │  Context managers     │  ctx.types.ptr   │   │
│  │  BasicBlock              │  Ref counting         │  ctx.types.array │   │
│  │  Value/Instruction       │                       │                  │   │
│  │  Builder                 │                       │                  │   │
│  └─────────────────────────────────────────────────────────────────────┘   │
├─────────────────────────────────────────────────────────────────────────────┤
│                              nanobind                                       │
│                                                                             │
│  C++/Python bridge using modern C++ techniques                              │
│  - Type conversions (Python str <-> C char*)                                │
│  - Exception translation (C++ exceptions -> Python)                         │
│  - Automatic ref counting                                                   │
├─────────────────────────────────────────────────────────────────────────────┤
│                            LLVM C API                                       │
│                                                                             │
│  LLVMContextCreate()     LLVMBuildAdd()        LLVMGetBasicBlocks()         │
│  LLVMParseIRInContext()  LLVMBuildBr()         LLVMGetInstructions()        │
│  LLVMCreateBuilder()     LLVMBuildRet()        LLVMReplaceAllUsesWith()     │
├─────────────────────────────────────────────────────────────────────────────┤
│                            LLVM C++ Core                                    │
│                                                                             │
│  The actual compiler infrastructure                                         │
│  ~3.5 million lines of C++                                                  │
│  Optimizations, code generation, analysis                                   │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## 2. LLVM IR Structure

```
MODULE
│
├── Target Triple: "x86_64-unknown-linux-gnu"
├── Data Layout: "e-m:e-p270:32:32-..."
│
├── GLOBALS
│   ├── @global_string = private constant [14 x i8] c"Hello, world!\00"
│   └── @counter = global i32 0
│
├── DECLARATIONS (external functions)
│   ├── declare i32 @printf(ptr, ...)
│   └── declare ptr @malloc(i64)
│
└── DEFINITIONS (function bodies)
    │
    └── define i32 @main() {
        │
        ├── BASIC BLOCK: entry
        │   ├── %x = alloca i32                    ← instruction
        │   ├── store i32 42, ptr %x               ← instruction
        │   └── br label %loop                     ← TERMINATOR
        │
        ├── BASIC BLOCK: loop
        │   ├── %i = phi i32 [ 0, %entry ], [ %next, %loop ]
        │   ├── %val = load i32, ptr %x
        │   ├── %next = add i32 %i, 1
        │   ├── %cond = icmp slt i32 %next, %val
        │   └── br i1 %cond, label %loop, label %exit   ← TERMINATOR
        │
        └── BASIC BLOCK: exit
            └── ret i32 0                          ← TERMINATOR
        }

KEY INVARIANTS:
  - Every basic block ends with EXACTLY ONE terminator
  - SSA: every %value is assigned exactly once
  - PHI nodes must be at the START of a block
  - All paths must lead to a terminator (no fall-through)
```

---

## 3. The Transformation Pattern

```
                    ┌────────────────────────────────┐
                    │         ORIGINAL IR            │
                    │                                │
                    │   %result = sub i32 %x, %y     │
                    └───────────────┬────────────────┘
                                    │
          ┌─────────────────────────┼─────────────────────────┐
          │                         │                         │
          ▼                         ▼                         ▼
┌─────────────────────┐   ┌─────────────────────┐   ┌─────────────────────┐
│     1. TRAVERSE     │   │    2. IDENTIFY      │   │     3. BUILD        │
│                     │   │                     │   │                     │
│ for bb in blocks:   │   │ if opcode == Sub:   │   │ builder.position    │
│   for inst in bb:   │   │   collect(inst)     │   │   _before(inst)     │
│     examine(inst)   │   │                     │   │ new = builder.add   │
│                     │   │                     │   │   (xor, mul)        │
└─────────────────────┘   └─────────────────────┘   └─────────────────────┘
                                    │
                                    ▼
                    ┌────────────────────────────────┐
                    │         4. REPLACE             │
                    │                                │
                    │  old.replace_all_uses_with(new)│
                    └───────────────┬────────────────┘
                                    │
                                    ▼
                    ┌────────────────────────────────┐
                    │         5. CLEAN UP            │
                    │                                │
                    │  old.erase_from_parent()       │
                    │  assert mod.verify()           │
                    └───────────────┬────────────────┘
                                    │
                                    ▼
                    ┌────────────────────────────────┐
                    │        TRANSFORMED IR          │
                    │                                │
                    │  %neg = sub i32 0, %y          │
                    │  %xor = xor i32 %x, %neg       │
                    │  %and = and i32 %x, %neg       │
                    │  %mul = mul i32 2, %and        │
                    │  %result = add i32 %xor, %mul  │
                    └────────────────────────────────┘
```

---

## 4. Control Flow Flattening

```
                    BEFORE                                    AFTER

                 ┌─────────┐                              ┌─────────┐
                 │  entry  │                              │  entry  │
                 └────┬────┘                              │ state=A │
                      │                                   └────┬────┘
              ┌───────┴───────┐                                │
              ▼               ▼                                ▼
        ┌──────────┐   ┌──────────┐                     ┌─────────────┐
        │ block_A  │   │ block_B  │                     │  dispatcher │◄──────┐
        └────┬─────┘   └────┬─────┘                     └──────┬──────┘       │
             │              │                                  │              │
             └──────┬───────┘                    ┌─────────────┼──────────┐   │
                    ▼                            │             │          │   │
              ┌──────────┐              ┌────────▼───┐  ┌──────▼────┐  ┌──▼───┴──┐
              │ block_C  │              │ if st==A   │  │ if st==B  │  │ if st==C │
              └────┬─────┘              │ goto A     │  │ goto B    │  │ goto C   │
                   │                    └──────┬─────┘  └─────┬─────┘  └─────┬───┘
                   ▼                           │              │              │
              ┌──────────┐                     ▼              ▼              ▼
              │   exit   │              ┌──────────┐  ┌──────────┐    ┌──────────┐
              └──────────┘              │ block_A  │  │ block_B  │    │ block_C  │
                                        │ st = ?   │  │ st = ?   │    │ st = ?   │
                                        │ goto disp│  │ goto disp│    │ goto disp│
                                        └──────────┘  └──────────┘    └─────┬────┘
                                                                            │
                                                                            ▼
                                                                      ┌──────────┐
                                                                      │   exit   │
                                                                      └──────────┘

STATE VARIABLE FLOW:
  entry:    state = STATE_A (initial)
  block_A:  state = (condition ? STATE_B : STATE_C)
  block_B:  state = STATE_C
  block_C:  state = STATE_EXIT

OBFUSCATION EFFECT:
  - Original: Clear if/else structure visible
  - After: All blocks go to dispatcher, state determines next
  - CFG analysis sees one giant switch, not the original structure
```

---

## 5. PHI Node Demotion (for CFF)

```
BEFORE DEMOTION:

        ┌───────────┐         ┌───────────┐
        │  block_A  │         │  block_B  │
        │           │         │           │
        │  %x = ... │         │  %y = ... │
        └─────┬─────┘         └─────┬─────┘
              │                     │
              └──────────┬──────────┘
                         │
                         ▼
                  ┌─────────────┐
                  │   merge     │
                  │             │
                  │  %result = phi i32 [ %x, %block_A ], [ %y, %block_B ]
                  │             │
                  └─────────────┘

PHI says: "If we came from block_A, use %x. If from block_B, use %y."


AFTER DEMOTION:

        ┌───────────┐         ┌───────────┐
        │  entry    │         │           │
        │           │         │           │
        │ %slot = alloca i32  │           │
        └─────┬─────┘         │           │
              │               │           │
              ▼               │           │
        ┌───────────┐         ┌───────────┐
        │  block_A  │         │  block_B  │
        │           │         │           │
        │  %x = ... │         │  %y = ... │
        │  store %x │         │  store %y │
        │   to %slot│         │   to %slot│
        └─────┬─────┘         └─────┬─────┘
              │                     │
              └──────────┬──────────┘
                         │
                         ▼
                  ┌─────────────┐
                  │   merge     │
                  │             │
                  │  %result = load from %slot
                  │             │
                  └─────────────┘

No more PHI! Each predecessor STORES its value, merge LOADS it.
This works with CFF because we don't need to know the predecessor.
```

---

## 6. MBA Substitution Identity

```
SUBTRACTION:  X - Y

EQUIVALENT:   (X XOR -Y) + 2*(X AND -Y)


PROOF BY EXAMPLE (8-bit):

  X = 7  = 0000_0111
  Y = 3  = 0000_0011

  Step 1: Compute -Y (two's complement)
    -Y = NOT(Y) + 1 = 1111_1100 + 1 = 1111_1101 = -3

  Step 2: Compute X XOR -Y
    0000_0111  (X = 7)
    1111_1101  (-Y = -3)
    ─────────
    1111_1010  = -6 (signed)

  Step 3: Compute X AND -Y
    0000_0111  (X = 7)
    1111_1101  (-Y = -3)
    ─────────
    0000_0101  = 5

  Step 4: Compute 2 * (X AND -Y)
    2 * 5 = 10 = 0000_1010

  Step 5: Add them
    1111_1010  (-6)
    0000_1010  (10)
    ─────────
    0000_0100  = 4

  VERIFY: 7 - 3 = 4 ✓


WHY IT WORKS:

  In binary addition, X + Y = (X XOR Y) + 2*(X AND Y)

  This is because:
    - XOR gives bits that differ (no carry)
    - AND gives bits where both are 1 (would cause carry)
    - Multiply by 2 shifts left (carry propagation)

  For subtraction, X - Y = X + (-Y)
  So: X - Y = (X XOR -Y) + 2*(X AND -Y)
```

---

## 7. API Improvement Priorities

```
PRIORITY 1: CRITICAL BLOCKERS
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
│                                                          │
│  Raw Bytes Support ─────── String encryption impossible  │
│  replace_all_uses_with ─── Core SSA operation missing    │
│  split_basic_block ──────── Some transforms impossible   │
│  erase_from_parent ──────── Error-prone deletion         │
│                                                          │
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

PRIORITY 2: API CONSISTENCY
────────────────────────────────────────────────────────────
│                                                          │
│  ptr() vs ptr ───────────── Inconsistent access pattern  │
│  property setters ───────── Consistent mutation style    │
│  .block vs .parent ──────── Confusing naming             │
│  .is_terminator ─────────── Terminator predicate         │
│                                                          │
────────────────────────────────────────────────────────────

PRIORITY 3: CONVENIENCES
────────────────────────────────────────────────────────────
│                                                          │
│  .operands ──────────────── Pythonic operand iteration   │
│  move_before/after ──────── Instruction movement         │
│  .clone() ───────────────── Instruction copying          │
│  .num_successors ────────── Direct count access          │
│                                                          │
────────────────────────────────────────────────────────────

PRIORITY 4: DOCUMENTATION
............................
│                          │
│  Exception types         │
│  Context managers        │
│  API reference           │
│                          │
............................


IMPLEMENTATION PHASES:

Phase 1 (Quick Wins)
  └─ Bind existing C API functions
     └─ LLVMInstructionEraseFromParent
     └─ LLVMReplaceAllUsesWith
     └─ Add iterators/properties

Phase 2 (Consistency)
  └─ Refactor property access
  └─ May require deprecation warnings

Phase 3 (Major Features)
  └─ Raw bytes: design decision needed
  └─ Block splitting: may need C wrapper

Phase 4 (Documentation)
  └─ Can be done incrementally
```

---

## 8. Memory Safety Model

```
┌────────────────────────────────────────────────────────────────────────┐
│                         PYTHON LAND                                    │
│                                                                        │
│   ctx = llvm.create_context()  ──────────────────────────────┐        │
│        │                                                      │        │
│        │  creates                                             │        │
│        ▼                                                      │        │
│   ┌──────────────────────────────────────────────────────┐   │        │
│   │  Context Wrapper                                      │   │        │
│   │  ┌─────────────────────────────────────────────────┐ │   │        │
│   │  │  validity_token = VALID                         │ │   │        │
│   │  │  owned_modules = []                             │ │   │        │
│   │  └─────────────────────────────────────────────────┘ │   │        │
│   └──────────────────────────────────────────────────────┘   │        │
│        │                                                      │        │
│        │  creates                                             │        │
│        ▼                                                      │        │
│   ┌──────────────────────────────────────────────────────┐   │        │
│   │  Module Wrapper                                       │   │        │
│   │  ┌─────────────────────────────────────────────────┐ │   │        │
│   │  │  parent_token = ctx.validity_token (reference)  │ │   │        │
│   │  │  owned_functions = []                           │ │   │        │
│   │  └─────────────────────────────────────────────────┘ │   │        │
│   └──────────────────────────────────────────────────────┘   │        │
│                                                               │        │
│   When ctx.__exit__() is called:                             │        │
│     1. ctx.validity_token = INVALID                          │        │
│     2. All children's parent_token now points to INVALID     │        │
│     3. Any access checks parent_token first                  │        │
│                                                               │        │
│   func = list(mod.functions)[0]                              │        │
│   # Later, after ctx is closed...                            │        │
│   func.name  ─────────────────────────────────────────────────        │
│        │                                                              │
│        └──► Check parent_token                                        │
│             └──► INVALID!                                             │
│                  └──► Raise Python exception (not segfault!)          │
│                                                                        │
└────────────────────────────────────────────────────────────────────────┘
                              │
                              │ C++ boundary
                              ▼
┌────────────────────────────────────────────────────────────────────────┐
│                          LLVM LAND                                     │
│                                                                        │
│   LLVMContextRef ─────────────────────────────────────────────┐       │
│        │                                                       │       │
│        │ owns                                                  │       │
│        ▼                                                       │       │
│   LLVMModuleRef ─────────────────────────────────────────┐    │       │
│        │                                                  │    │       │
│        │ owns                                             │    │       │
│        ▼                                                  │    │       │
│   LLVMValueRef (functions, globals, etc.)                │    │       │
│                                                           │    │       │
│   When LLVMContextDispose() is called:                   │    │       │
│     All owned memory is freed                            │    │       │
│     All pointers become dangling                         │    │       │
│     Any access = undefined behavior (crash, corruption)  │    │       │
│                                                           │    │       │
└────────────────────────────────────────────────────────────────────────┘

THE SAFETY GUARANTEE:
  Python exceptions instead of segfaults
  Clean error messages instead of corruption
  Debuggable instead of mysterious
```

---

*These diagrams are designed to be viewable in any terminal or text editor.*
