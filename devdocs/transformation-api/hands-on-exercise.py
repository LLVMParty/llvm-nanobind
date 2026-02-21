#!/usr/bin/env -S uv run
"""
Hands-On Learning Exercise: Understanding LLVM IR Transformations

This script walks you through transforming LLVM IR step by step,
with explanations at each stage. Run it interactively to build intuition.

Usage:
    uv run devdocs/transformation-api/hands-on-exercise.py
"""

import sys
from textwrap import dedent

# Check if we can import llvm
try:
    import llvm
except ImportError:
    print("Error: llvm module not found. Run 'uv sync' first.")
    sys.exit(1)

# ANSI colors
CYAN = "\033[96m"
GREEN = "\033[92m"
YELLOW = "\033[93m"
RED = "\033[91m"
BOLD = "\033[1m"
DIM = "\033[2m"
RESET = "\033[0m"


def section(title: str):
    """Print a section header."""
    print(f"\n{BOLD}{'=' * 60}{RESET}")
    print(f"{BOLD}{CYAN}{title}{RESET}")
    print(f"{BOLD}{'=' * 60}{RESET}\n")


def explain(text: str):
    """Print an explanation."""
    print(f"{DIM}{dedent(text).strip()}{RESET}\n")


def show_ir(label: str, ir: str):
    """Display IR with a label."""
    print(f"{YELLOW}{label}:{RESET}")
    print(f"{GREEN}{ir}{RESET}")


def pause(prompt: str = "Press Enter to continue..."):
    """Wait for user."""
    input(f"\n{YELLOW}{prompt}{RESET}\n")


def exercise_1_basic_ir():
    """Exercise 1: Understanding IR Structure"""
    section("Exercise 1: Understanding LLVM IR Structure")

    explain("""
    Let's start by creating a simple function and examining its structure.
    We'll create a function that computes: result = (a + b) * 2
    """)

    pause()

    # Create the IR
    with llvm.create_context() as ctx:
        with ctx.create_module("exercise1") as mod:
            # Get types
            i32 = ctx.types.i32

            # Create function type: i32 (i32, i32)
            fn_type = ctx.types.function(i32, [i32, i32])

            # Create function
            fn = mod.add_function("compute", fn_type)
            fn.get_param(0).name = "a"
            fn.get_param(1).name = "b"

            # Create entry block
            entry = fn.append_basic_block("entry")

            # Build instructions
            with entry.create_builder() as builder:
                a = fn.get_param(0)
                b = fn.get_param(1)

                sum_val = builder.add(a, b, "sum")
                two = i32.constant(2)
                result = builder.mul(sum_val, two, "result")
                builder.ret(result)

            show_ir("Generated IR", mod.to_string())

    explain("""
    OBSERVATION POINTS:

    1. The function signature: define i32 @compute(i32 %a, i32 %b)
       - Return type (i32) comes first
       - Parameters have types and names

    2. The basic block: entry:
       - Every block has a label
       - Instructions inside are in order

    3. SSA form: Each value (%sum, %result) is assigned once
       - %sum = add i32 %a, %b
       - %result = mul i32 %sum, 2

    4. The terminator: ret i32 %result
       - Every block MUST end with a terminator
       - This one returns, others might branch
    """)

    pause("Press Enter for the next exercise...")


def exercise_2_iteration():
    """Exercise 2: Iterating Over IR"""
    section("Exercise 2: Iterating Over IR Elements")

    explain("""
    Now let's traverse an existing IR and examine its structure.
    This is what a transformation does first: find things to change.
    """)

    ir_text = dedent("""
        define i32 @example(i32 %n) {
        entry:
          %cmp = icmp sgt i32 %n, 0
          br i1 %cmp, label %positive, label %negative

        positive:
          %add = add i32 %n, 1
          br label %exit

        negative:
          %sub = sub i32 0, %n
          br label %exit

        exit:
          %result = phi i32 [ %add, %positive ], [ %sub, %negative ]
          ret i32 %result
        }
    """).strip()

    show_ir("Input IR", ir_text)
    pause()

    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir_text) as mod:
            print(f"\n{CYAN}Traversing the module:{RESET}\n")

            for func in mod.functions:
                print(f"Function: {BOLD}{func.name}{RESET}")
                print(f"  Parameters: {func.param_count}")
                print(f"  Blocks: {len(list(func.basic_blocks))}")

                for bb in func.basic_blocks:
                    print(f"\n  Block: {BOLD}{bb.name}{RESET}")
                    for inst in bb.instructions:
                        op_name = (
                            inst.opcode.name
                            if hasattr(inst.opcode, "name")
                            else str(inst.opcode)
                        )
                        is_term = " (TERMINATOR)" if inst.is_terminator else ""
                        print(f"    {op_name}: {inst}{is_term}")

    explain("""
    KEY OBSERVATIONS:

    1. Modules contain functions
    2. Functions contain basic blocks
    3. Basic blocks contain instructions
    4. The LAST instruction in each block is always a terminator

    Notice the PHI node in 'exit':
      %result = phi i32 [ %add, %positive ], [ %sub, %negative ]

    This says: "If we came from %positive, use %add. If from %negative, use %sub."
    PHI nodes are how SSA handles control flow merges.
    """)

    pause("Press Enter for the next exercise...")


def exercise_3_simple_transform():
    """Exercise 3: A Simple Transformation"""
    section("Exercise 3: Your First Transformation")

    explain("""
    Let's perform a simple transformation: multiply every constant by 2.
    This demonstrates the transform pattern:
      1. Find patterns to change
      2. Build replacement
      3. Substitute
      4. Clean up
    """)

    ir_text = dedent("""
        define i32 @addconst(i32 %x) {
        entry:
          %a = add i32 %x, 5
          %b = add i32 %a, 10
          ret i32 %b
        }
    """).strip()

    show_ir("Before transformation", ir_text)
    pause()

    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir_text) as mod:
            i32 = ctx.types.i32

            for func in mod.functions:
                if func.is_declaration:
                    continue

                # Collect instructions to transform
                to_transform = []
                for bb in func.basic_blocks:
                    for inst in bb.instructions:
                        if inst.opcode == llvm.Opcode.Add:
                            # Check if second operand is a constant
                            if inst.num_operands >= 2:
                                op1 = inst.get_operand(1)
                                # Try to detect if it's a constant integer
                                # (This is a simplified check)
                                if (
                                    hasattr(op1, "type")
                                    and op1.type.kind == llvm.TypeKind.Integer
                                ):
                                    to_transform.append(inst)

                print(f"{CYAN}Found {len(to_transform)} add instructions{RESET}")

                # Transform: double the constant operand
                for inst in to_transform:
                    bb = inst.block
                    op0 = inst.get_operand(0)
                    op1 = inst.get_operand(1)

                    with bb.create_builder() as builder:
                        builder.position_before(inst)

                        # Double the constant by adding it to itself
                        doubled_const = builder.add(op1, op1, "doubled")
                        new_add = builder.add(op0, doubled_const, inst.name + ".new")

                        # Replace uses
                        for use in list(inst.uses):
                            user = use.user
                            for i in range(user.num_operands):
                                if user.get_operand(i) == inst:
                                    user.set_operand(i, new_add)

                        # Remove old instruction
                        inst.remove_from_parent()
                        inst.delete_instruction()

            show_ir("After transformation", mod.to_string())

    explain("""
    WHAT WE DID:

    1. FOUND: Instructions with opcode 'Add'
    2. BUILT: New instructions that double the constant
    3. REPLACED: Uses of old instruction with new one
    4. CLEANED UP: Removed old instruction

    Notice the manual replace_all_uses_with pattern:
      for use in list(inst.uses):
          user = use.user
          for i in range(user.num_operands):
              if user.get_operand(i) == inst:
                  user.set_operand(i, new_add)

    This is verbose because the binding doesn't expose RAUW directly.
    That's one of the planned improvements!
    """)

    pause("Press Enter for the next exercise...")


def exercise_4_mba_by_hand():
    """Exercise 4: MBA Substitution Step by Step"""
    section("Exercise 4: MBA Substitution By Hand")

    explain("""
    Now let's do a real MBA substitution manually.
    We'll transform: X - Y  ->  (X ^ -Y) + 2*(X & -Y)

    First, let's verify the math:
      X = 7, Y = 3
      Expected: 7 - 3 = 4

      -Y = -3 (in binary, two's complement)
      X ^ -Y = 7 XOR -3
      X & -Y = 7 AND -3
      (X ^ -Y) + 2*(X & -Y) = ?

    Let's compute with 8-bit integers:
      7 = 0000_0111
     -3 = 1111_1101 (two's complement)

      7 XOR -3 = 0000_0111 XOR 1111_1101 = 1111_1010 = -6 (signed)
      7 AND -3 = 0000_0111 AND 1111_1101 = 0000_0101 = 5

      -6 + 2*5 = -6 + 10 = 4  ✓
    """)

    ir_text = dedent("""
        define i32 @subtract(i32 %x, i32 %y) {
        entry:
          %diff = sub i32 %x, %y
          ret i32 %diff
        }
    """).strip()

    show_ir("Before MBA", ir_text)
    pause("Let's transform this...")

    with llvm.create_context() as ctx:
        with ctx.parse_ir(ir_text) as mod:
            i32 = ctx.types.i32

            for func in mod.functions:
                if func.is_declaration:
                    continue

                # Find sub instructions
                subs = []
                for bb in func.basic_blocks:
                    for inst in bb.instructions:
                        if inst.opcode == llvm.Opcode.Sub:
                            subs.append(inst)

                print(f"{CYAN}Found {len(subs)} subtraction(s) to obfuscate{RESET}\n")

                for inst in subs:
                    bb = inst.block
                    x = inst.get_operand(0)
                    y = inst.get_operand(1)

                    print(f"Transforming: {inst}")
                    print(f"  X = {x}")
                    print(f"  Y = {y}")

                    with bb.create_builder() as builder:
                        builder.position_before(inst)

                        # Step 1: neg_y = -Y
                        neg_y = builder.neg(y, "mba.neg_y")
                        print(f"  Step 1: %mba.neg_y = neg %y")

                        # Step 2: xor_val = X ^ -Y
                        xor_val = builder.xor(x, neg_y, "mba.xor")
                        print(f"  Step 2: %mba.xor = xor %x, %mba.neg_y")

                        # Step 3: and_val = X & -Y
                        and_val = builder.and_(x, neg_y, "mba.and")
                        print(f"  Step 3: %mba.and = and %x, %mba.neg_y")

                        # Step 4: mul_val = 2 * (X & -Y)
                        two = i32.constant(2)
                        mul_val = builder.mul(two, and_val, "mba.mul")
                        print(f"  Step 4: %mba.mul = mul 2, %mba.and")

                        # Step 5: result = (X ^ -Y) + 2*(X & -Y)
                        result = builder.add(xor_val, mul_val, "mba.result")
                        print(f"  Step 5: %mba.result = add %mba.xor, %mba.mul")

                        # Replace uses
                        for use in list(inst.uses):
                            user = use.user
                            for i in range(user.num_operands):
                                if user.get_operand(i) == inst:
                                    user.set_operand(i, result)

                        inst.remove_from_parent()
                        inst.delete_instruction()

            print()
            show_ir("After MBA", mod.to_string())

    explain("""
    THE TRANSFORMATION:

    Before: %diff = sub i32 %x, %y

    After:
      %mba.neg_y = sub i32 0, %y       ; -Y
      %mba.xor = xor i32 %x, %mba.neg_y ; X ^ -Y
      %mba.and = and i32 %x, %mba.neg_y ; X & -Y
      %mba.mul = mul i32 2, %mba.and    ; 2 * (X & -Y)
      %mba.result = add i32 %mba.xor, %mba.mul ; Final result

    The result is mathematically equivalent, but a decompiler won't
    recognize it as subtraction. It will show the complex expression.

    This is the core of the mba_sub.py obfuscation pass!
    """)

    pause("Press Enter for the final exercise...")


def exercise_5_understand_cff():
    """Exercise 5: Understanding Control Flow Flattening"""
    section("Exercise 5: Control Flow Flattening Visualization")

    explain("""
    Control Flow Flattening is harder to implement, so let's just understand it.

    Original CFG:
                     ┌─────────┐
                     │  entry  │
                     └────┬────┘
                          │ if (n > 0)
                    ┌─────┴─────┐
                    ▼           ▼
              ┌──────────┐ ┌──────────┐
              │ positive │ │ negative │
              └────┬─────┘ └────┬─────┘
                   └──────┬─────┘
                          ▼
                    ┌──────────┐
                    │   exit   │
                    └──────────┘

    After flattening:
                     ┌─────────┐
                     │  entry  │
                     │ state=1 │
                     └────┬────┘
                          │
                          ▼
                   ┌─────────────┐ ◄───────────────────┐
                   │  dispatcher │                     │
                   │ switch(st)  │                     │
                   └──────┬──────┘                     │
            ┌─────────────┼─────────────┐              │
            ▼             ▼             ▼              │
      ┌──────────┐ ┌──────────┐ ┌──────────┐          │
      │ positive │ │ negative │ │   exit   │          │
      │ state=3  │ │ state=3  │ │   ret    │          │
      └────┬─────┘ └────┬─────┘ └──────────┘          │
           └──────┬─────┘                              │
                  └────────────────────────────────────┘

    The key insight: the original structure is hidden. Instead of
    "if positive then X else Y", we see "switch on opaque state".
    """)

    ir_text = dedent("""
        define i32 @abs(i32 %n) {
        entry:
          %cmp = icmp sgt i32 %n, 0
          br i1 %cmp, label %positive, label %negative

        positive:
          br label %exit

        negative:
          %neg = sub i32 0, %n
          br label %exit

        exit:
          %result = phi i32 [ %n, %positive ], [ %neg, %negative ]
          ret i32 %result
        }
    """).strip()

    print(f"{CYAN}Let's trace what CFF would do to this function:{RESET}\n")
    show_ir("Original", ir_text)

    pause()

    print(f"""
{CYAN}Step 1: Demote PHI nodes{RESET}

The PHI node in 'exit' depends on knowing the predecessor.
After flattening, we always come from dispatcher, so PHI is meaningless.

Convert to:
  - Create alloca in entry: %result.addr = alloca i32
  - In 'positive': store %n to %result.addr
  - In 'negative': store %neg to %result.addr
  - In 'exit': load from %result.addr

{CYAN}Step 2: Assign state values{RESET}

  entry    -> (initial, sets state for first real block)
  positive -> state = 0x4A3B (random)
  negative -> state = 0x7D2F (random)
  exit     -> state = 0x1E8C (random)

{CYAN}Step 3: Create dispatcher{RESET}

  dispatcher:
    %state = load from state variable
    compare with 0x4A3B -> positive
    compare with 0x7D2F -> negative
    compare with 0x1E8C -> exit
    default -> loop to dispatcher

{CYAN}Step 4: Rewrite terminators{RESET}

  Original 'entry': br %cmp, positive, negative
  Becomes:
    state = %cmp ? 0x4A3B : 0x7D2F
    br dispatcher

  Original 'positive': br exit
  Becomes:
    state = 0x1E8C
    br dispatcher

  etc.
""")

    explain("""
    THE RESULT:

    The original if/else structure is completely hidden.
    Static analysis sees a giant switch statement.
    Dynamic analysis must trace execution to understand flow.

    This is powerful but:
    - Adds overhead (state variable, indirect branches)
    - Defeats branch prediction
    - Makes code larger

    Used in real obfuscators like OLLVM, Tigress, etc.
    """)

    pause("Press Enter for summary...")


def summary():
    """Final summary and next steps."""
    section("Summary: What You've Learned")

    print(f"""
{BOLD}Core Concepts:{RESET}

  1. {CYAN}LLVM IR Structure{RESET}
     - Modules > Functions > Basic Blocks > Instructions
     - SSA form: each value assigned once
     - Terminators end every basic block
     - PHI nodes handle control flow merges

  2. {CYAN}Transformation Pattern{RESET}
     - Traverse: walk the IR
     - Identify: find patterns to change
     - Build: create replacement instructions
     - Replace: substitute old with new (RAUW)
     - Clean up: delete old, verify

  3. {CYAN}API Quirks{RESET}
     - Context managers for modules and builders
     - No direct RAUW (must implement manually)
     - Two-step instruction deletion
     - Inconsistent property vs method patterns

  4. {CYAN}Obfuscation Techniques{RESET}
     - MBA: hide operations in equivalent bitwise soup
     - CFF: hide control flow with state machine

{BOLD}Next Steps:{RESET}

  1. Read the source code in tools/obfuscation/
  2. Run the interactive quiz: interactive-quiz.py
  3. Study the learning guide: learning-guide.md
  4. Review the improvement plan: plan.md
  5. Try implementing a simple pass yourself!

{BOLD}Key Files to Study:{RESET}

  - devdocs/porting-guide.md     - API differences and pitfalls
  - devdocs/transformation-api/plan.md    - Improvement roadmap
  - tools/obfuscation/mba_sub.py          - MBA implementation
  - tools/obfuscation/control_flow_flattening.py - CFF implementation
""")


def main():
    print(f"""
{BOLD}{"=" * 60}
    LLVM-NANOBIND HANDS-ON LEARNING EXERCISES
{"=" * 60}{RESET}

This interactive exercise will walk you through:

  1. Creating LLVM IR from scratch
  2. Traversing and examining IR
  3. Performing a simple transformation
  4. Understanding MBA substitution
  5. Understanding control flow flattening

Each exercise builds on the previous one.
Take your time and understand each step.

{DIM}Tip: The code in these exercises is simplified for learning.
The actual passes in tools/obfuscation/ are more robust.{RESET}
""")

    pause("Press Enter to begin Exercise 1...")

    try:
        exercise_1_basic_ir()
        exercise_2_iteration()
        exercise_3_simple_transform()
        exercise_4_mba_by_hand()
        exercise_5_understand_cff()
        summary()
    except KeyboardInterrupt:
        print(f"\n\n{YELLOW}Exercise ended early.{RESET}")
        sys.exit(0)


if __name__ == "__main__":
    main()
