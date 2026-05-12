#!/usr/bin/env -S uv run
"""
Interactive Self-Assessment Quiz

Run this to test your understanding of llvm-nanobind and the transformation API.
The quiz adapts based on your answers and provides explanations.

Usage:
    uv run devdocs/transformation-api/interactive-quiz.py
"""

import random
import sys

# ANSI colors for terminal output
GREEN = "\033[92m"
RED = "\033[91m"
YELLOW = "\033[93m"
CYAN = "\033[96m"
BOLD = "\033[1m"
RESET = "\033[0m"


def ask(
    question: str, options: list[str], correct: int, explanation: str
) -> bool | None:
    """Ask a multiple choice question. Returns True if correct."""
    print(f"\n{CYAN}{BOLD}Question:{RESET}")
    print(f"  {question}\n")

    for i, opt in enumerate(options, 1):
        print(f"  {i}. {opt}")

    while True:
        try:
            answer = input(f"\n{YELLOW}Your answer (1-{len(options)}): {RESET}").strip()
            if answer.lower() == "q":
                return None  # Signal to quit
            choice = int(answer)
            if 1 <= choice <= len(options):
                break
            print(f"Please enter a number between 1 and {len(options)}")
        except ValueError:
            print(
                f"Please enter a number between 1 and {len(options)} (or 'q' to quit)"
            )

    if choice == correct:
        print(f"\n{GREEN}{BOLD}Correct!{RESET}")
        print(f"{GREEN}{explanation}{RESET}")
        return True
    else:
        print(f"\n{RED}{BOLD}Not quite.{RESET} The answer is: {options[correct - 1]}")
        print(f"{YELLOW}{explanation}{RESET}")
        return False


def section_intro(title: str, description: str):
    """Print a section header."""
    print(f"\n{'=' * 60}")
    print(f"{BOLD}{title}{RESET}")
    print(f"{'=' * 60}")
    print(f"\n{description}\n")
    input(f"{YELLOW}Press Enter to begin...{RESET}")


# =============================================================================
# Question Bank
# =============================================================================

LLVM_IR_BASICS = [
    {
        "question": "In LLVM IR, what must every basic block end with?",
        "options": [
            "A return instruction",
            "A terminator instruction (br, ret, switch, etc.)",
            "A call instruction",
            "Nothing special - it's just the last instruction",
        ],
        "correct": 2,
        "explanation": "Every basic block must end with exactly one terminator instruction. "
        "This is invariant in LLVM IR. Terminators define control flow: where "
        "execution can go next. Without a terminator, LLVM wouldn't know what "
        "happens after the block.",
    },
    {
        "question": "What does SSA (Static Single Assignment) mean in practice?",
        "options": [
            "Each variable can only be read once",
            "Each value can only be assigned once",
            "Each function can only have one return",
            "Each block can only have one predecessor",
        ],
        "correct": 2,
        "explanation": "SSA means every value is defined exactly once. You can't reassign: "
        "%x = add i32 1, 2; %x = add i32 %x, 3 is INVALID. "
        "Instead, use new names: %x = ...; %y = add i32 %x, 3. "
        "This enables powerful optimizations because the compiler knows "
        "exactly where each value comes from.",
    },
    {
        "question": "What's a PHI node used for?",
        "options": [
            "Representing function parameters",
            "Selecting a value based on which predecessor block we came from",
            "Performing floating-point operations",
            "Calling external functions",
        ],
        "correct": 2,
        "explanation": "PHI nodes exist because of SSA + control flow. When two blocks merge, "
        "and each defines a different version of a value, how do we pick? "
        "PHI nodes: %result = phi i32 [ %x, %block1 ], [ %y, %block2 ]. "
        "The value depends on which predecessor we came from. "
        "They're crucial for loops and conditionals.",
    },
    {
        "question": "Which is NOT a valid LLVM type?",
        "options": [
            "i1 (1-bit integer, boolean)",
            "i32 (32-bit integer)",
            "ptr (opaque pointer)",
            "str (string type)",
        ],
        "correct": 4,
        "explanation": "LLVM has no native string type. Strings are represented as arrays of i8 "
        "([13 x i8]) or pointers to i8 (ptr). i1 is valid (booleans), i32 is valid "
        "(common integer), and ptr is the opaque pointer type in modern LLVM.",
    },
]

BINDING_API = [
    {
        "question": "How do you get the opaque pointer type for address space 0 in the bindings?",
        "options": [
            "ctx.types.pointer",
            "ctx.types.ptr",
            "ctx.types.pointer(0)",
            "ctx.types.addrspace_ptr(0)",
        ],
        "correct": 2,
        "explanation": "ctx.types.ptr is a property that returns the opaque pointer type in "
        "address space 0, consistent with other type properties like ctx.types.i32. "
        "For non-default address spaces, use ctx.types.addrspace_ptr(address_space).",
    },
    {
        "question": "To delete an instruction, you must:",
        "options": [
            "Call inst.delete()",
            "Call inst.erase_from_parent()",
            "Call inst.remove_from_parent() then inst.delete_instruction()",
            "Set inst to None",
        ],
        "correct": 2,
        "explanation": "Use inst.erase_from_parent(), which matches the common C++ eraseFromParent() pattern. "
        "Lower-level remove_from_parent() and delete_instruction() still exist, "
        "but most code should not need the two-step sequence.",
    },
    {
        "question": "What does replace_all_uses_with() do?",
        "options": [
            "Replaces the instruction with a new one",
            "Changes every use of a value to point to a different value",
            "Replaces the function containing the value",
            "Copies the value to all users",
        ],
        "correct": 2,
        "explanation": "RAUW is fundamental to SSA-based transformations. When you create a new "
        "value that should replace an old one, you use replace_all_uses_with() "
        "to update every instruction that uses the old value.",
    },
    {
        "question": "What happens if you access a Module after exiting its 'with' block?",
        "options": [
            "You get garbage data",
            "Python segfaults",
            "You get a clean Python exception (due to validity tokens)",
            "Nothing - the module is copied",
        ],
        "correct": 3,
        "explanation": "The bindings use 'validity tokens' to track object lifetime. When the "
        "context manager exits, the underlying LLVM object is disposed, and "
        "all Python wrappers are marked invalid. Accessing them raises a clean "
        "exception instead of the undefined behavior you'd get in C++.",
    },
    {
        "question": "To iterate over an instruction's operands, you use:",
        "options": [
            "for op in inst.operands:",
            "for op in inst.get_operands():",
            "for i in range(inst.num_operands): op = inst.get_operand(i)",
            "for op in inst:",
        ],
        "correct": 1,
        "explanation": "inst.operands provides Pythonic iteration over an operand snapshot. "
        "Use index-based get_operand()/set_operand() when you need to replace operands.",
    },
]

OBFUSCATION = [
    {
        "question": "The MBA substitution X - Y = (X ^ -Y) + 2*(X & -Y) works because:",
        "options": [
            "XOR always equals subtraction",
            "It exploits the relationship between arithmetic and bitwise operations in two's complement",
            "It's an approximation that's close enough",
            "The extra operations cancel out",
        ],
        "correct": 2,
        "explanation": "In two's complement representation, there are deep connections between "
        "arithmetic and bitwise operations. The identity is exact for all inputs "
        "in fixed-width integers. The obfuscation works because decompilers "
        "pattern-match 'sub' to '-', but don't recognize this equivalent form.",
    },
    {
        "question": "Control flow flattening hides the original CFG by:",
        "options": [
            "Encrypting all instructions",
            "Introducing a state machine dispatcher that controls block execution",
            "Removing all branches",
            "Inlining all functions",
        ],
        "correct": 2,
        "explanation": "CFF creates a dispatcher that reads a state variable and branches to "
        "the appropriate block. Each block updates the state and jumps back to "
        "the dispatcher. The original 'if A then B else C' structure becomes "
        "'switch(state) { ... }' - much harder to analyze.",
    },
    {
        "question": "Why does the CFF pass demote PHI nodes to stack variables?",
        "options": [
            "PHI nodes are too slow",
            "PHI nodes encode predecessor information that's lost after flattening",
            "The LLVM API doesn't support PHI nodes",
            "Stack variables are more secure",
        ],
        "correct": 2,
        "explanation": "PHI nodes say 'if we came from block1, use %x; if from block2, use %y'. "
        "After flattening, we always come from the dispatcher! The predecessor "
        "information is meaningless. By converting to explicit memory operations, "
        "we preserve the semantics without relying on control flow.",
    },
    {
        "question": "When creating encrypted string data, which input type preserves raw bytes?",
        "options": [
            "str decoded with latin-1",
            "bytes passed to const_string() or const_data_array()",
            "A Python list of characters",
            "LLVM does not support byte constants",
        ],
        "correct": 2,
        "explanation": "Use bytes for binary payloads: llvm.const_string(ctx, data, ...) or "
        "i8.array_const(data). The str overload is for text and follows UTF-8 encoding.",
    },
]

CRITIQUE = [
    {
        "question": "Which improvement has the HIGHEST priority according to the plan?",
        "options": [
            "Adding documentation for exceptions",
            "Documenting that ptr is a property",
            "Binding LLVMReplaceAllUsesWith",
            "Adding an .operands iterator",
        ],
        "correct": 3,
        "explanation": "The plan categorizes issues by priority. Priority 1 (Critical Blockers) "
        "includes RAUW, erase_from_parent, split_basic_block, and raw bytes support. "
        "These block real use cases. API consistency issues were P2, "
        "conveniences are P3, documentation is P4.",
    },
    {
        "question": "The porting guide rates the API '7/10 for code generation, 5/10 for transforms'. Why the difference?",
        "options": [
            "Transforms are inherently harder",
            "The Builder API is good, but operations like RAUW and block splitting are missing",
            "Python is slow for transforms",
            "The documentation is better for code generation",
        ],
        "correct": 2,
        "explanation": "Code generation (creating new IR) mainly uses the Builder, which is "
        "well-designed (add, sub, br, etc.). Transforms (modifying existing IR) "
        "need operations like replace_all_uses_with, erase_from_parent, split_block. "
        "These are missing or cumbersome, making transform work painful.",
    },
    {
        "question": "When reviewing API design, 'pit of success' means:",
        "options": [
            "The API should fail loudly on errors",
            "The easy/natural path should be the correct path",
            "The API should have extensive documentation",
            "The API should be minimal",
        ],
        "correct": 2,
        "explanation": "A 'pit of success' API design makes it hard to do the wrong thing. "
        "The current two-step instruction deletion violates this - the natural "
        "thing (just call delete) crashes. A good API would make the safe path "
        "the obvious path: inst.erase_from_parent() does everything correctly.",
    },
]


def run_section(title: str, description: str, questions: list[dict]) -> tuple[int, int]:
    """Run a quiz section. Returns (correct, total)."""
    section_intro(title, description)

    random.shuffle(questions)
    correct = 0
    total = 0

    for q in questions:
        result = ask(q["question"], q["options"], q["correct"], q["explanation"])
        if result is None:  # User quit
            return correct, total
        total += 1
        if result:
            correct += 1

    return correct, total


def main():
    print(f"""
{BOLD}{"=" * 60}
       LLVM-NANOBIND SELF-ASSESSMENT QUIZ
{"=" * 60}{RESET}

This quiz tests your understanding of the llvm-nanobind project
and its transformation API. It's designed to:

  1. Identify gaps in your understanding
  2. Provide explanations for each concept
  3. Prepare you to critically review the work

Answer by entering the number of your choice.
Type 'q' at any time to quit.

Sections:
  1. LLVM IR Basics
  2. Python Binding API
  3. Obfuscation Passes
  4. Critical Evaluation
""")

    input(f"{YELLOW}Press Enter to start...{RESET}")

    results = []

    # Section 1: LLVM IR
    c, t = run_section(
        "Section 1: LLVM IR Fundamentals",
        "These questions test your understanding of LLVM's intermediate representation.\n"
        "This knowledge is essential for understanding what the bindings expose.",
        LLVM_IR_BASICS,
    )
    results.append(("LLVM IR Basics", c, t))

    if t == 0:  # User quit early
        return

    # Section 2: Binding API
    c, t = run_section(
        "Section 2: The Python Bindings API",
        "These questions test your understanding of the llvm-nanobind API design,\n"
        "including its strengths, weaknesses, and idiosyncrasies.",
        BINDING_API,
    )
    results.append(("Binding API", c, t))

    if t == 0:
        return

    # Section 3: Obfuscation
    c, t = run_section(
        "Section 3: Obfuscation Passes",
        "These questions test your understanding of the MBA and CFF obfuscation\n"
        "passes, which serve as case studies for the transformation API.",
        OBFUSCATION,
    )
    results.append(("Obfuscation", c, t))

    if t == 0:
        return

    # Section 4: Critique
    c, t = run_section(
        "Section 4: Critical Evaluation",
        "These questions test your ability to critically evaluate the work,\n"
        "including understanding priorities and trade-offs.",
        CRITIQUE,
    )
    results.append(("Critical Evaluation", c, t))

    # Final results
    print(f"\n\n{'=' * 60}")
    print(f"{BOLD}FINAL RESULTS{RESET}")
    print(f"{'=' * 60}\n")

    total_correct = 0
    total_questions = 0

    for name, c, t in results:
        if t > 0:
            pct = c / t * 100
            color = GREEN if pct >= 80 else YELLOW if pct >= 60 else RED
            print(f"  {name:25} {color}{c}/{t} ({pct:.0f}%){RESET}")
            total_correct += c
            total_questions += t

    print(
        f"\n  {'OVERALL':25} {total_correct}/{total_questions} ({total_correct / total_questions * 100:.0f}%)"
    )

    # Recommendations
    print(f"\n{BOLD}Recommendations:{RESET}")

    for name, c, t in results:
        if t > 0 and c / t < 0.7:
            if name == "LLVM IR Basics":
                print(f"  - Review LLVM Language Reference for IR fundamentals")
            elif name == "Binding API":
                print(f"  - Read devdocs/porting-guide.md for API quirks")
            elif name == "Obfuscation":
                print(f"  - Study the passes in tools/obfuscation/")
            elif name == "Critical Evaluation":
                print(f"  - Review devdocs/transformation-api/plan.md")

    if total_correct / total_questions >= 0.8:
        print(f"\n{GREEN}You're ready to review and critique the work!{RESET}")
    else:
        print(
            f"\n{YELLOW}Consider reviewing the learning guide before diving into code review.{RESET}"
        )


if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print(f"\n\n{YELLOW}Quiz ended early.{RESET}")
        sys.exit(0)
