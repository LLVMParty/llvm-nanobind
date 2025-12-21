# Development Documentation

This directory contains development documentation for the llvm-nanobind project.

## Directory Structure

```
devdocs/
├── README.md           # This file
├── DEBUGGING.md        # Guidelines for debugging crashes and memory issues
├── memory-model.md     # Lifetime management and validity token design
├── lit-tests.md        # LLVM lit test infrastructure documentation
├── archive/            # Completed task summaries (key learnings only)
│   ├── bindings.md
│   ├── fixing-tests.md
│   ├── llvm-c-test.md
│   └── parsing-refactor.md
└── <task-name>/        # In-progress tasks
    ├── design.md
    ├── plan.md
    └── progress.md
```

## Reference Documentation

Active documentation for ongoing development:

| File | Purpose |
|------|---------|
| `DEBUGGING.md` | How to debug crashes, isolate memory issues, add safety checks |
| `memory-model.md` | Validity token system, ownership patterns, lifetime guarantees |
| `lit-tests.md` | Lit test infrastructure, test categories, how to run/add tests |

## Task Tracking Methodology

### In-Progress Tasks

For larger multi-phase tasks, create a subdirectory with:

```
devdocs/<task-name>/
├── design.md     # Optional: user-provided design document
├── plan.md       # Goals, phases, required APIs, testing strategy
└── progress.md   # Current status, completed items, blockers
```

**During development:**
1. Read `progress.md` to find current state
2. Work on incomplete items
3. Update `progress.md` as you complete items (use checkboxes)

### Completed Tasks → Archive

When a task is complete and the user asks you to archive:

1. **Extract key learnings** into `archive/<task-name>.md`:
   - Goal and scope (1 paragraph)
   - Key architectural decisions
   - Technical insights and gotchas
   - API summary (what was built)
   - References to related docs

2. **Delete the task directory** (`plan.md`, `progress.md`)
   - Detailed progress is preserved in git history
   - Only essential knowledge lives in the archive

3. **Archive the design** (`design.md`)
   - Ask the user whether to move `design.md` to `../<taskname>.md` as permanent documentation

### Archive Contents

The `archive/` directory contains summaries of completed work:

| File | Summary |
|------|---------|
| `bindings.md` | Core Python bindings: ownership model, exception hierarchy, wrapper patterns |
| `fixing-tests.md` | Test fixing work: borrowed contexts, diagnostic registry, syncscope gotchas |
| `llvm-c-test.md` | Python port of llvm-c-test: 22 commands, echo architecture, testing |
| `module-api.md` | API refactor: moving global functions to Module/Builder/Function methods |
| `parsing-refactor.md` | New parsing API: buffer ownership, diagnostics, migration patterns |

These are **reference documents**, not progress trackers. They capture:
- What was built and why
- Design decisions that affect future work
- Technical gotchas to avoid repeating mistakes

## Agent Instructions

**Important:** Task creation and archival are **user-initiated**. The agent should not create new task directories or archive completed tasks without explicit user request.

**When working on an existing task:**
1. Read `progress.md` to find current state
2. Work on incomplete items
3. Update `progress.md` as work progresses
4. Suggest when a task might be ready for archival (but don't archive without asking)

**When user asks to create a new task:**
1. Create `devdocs/<task-name>/plan.md` with goals and phases
2. Create `devdocs/<task-name>/progress.md` for tracking

**When user asks to archive a completed task:**
1. Create `archive/<task-name>.md` with key learnings
2. Delete the task directory (plan.md, progress.md)

**Looking up past decisions:**
1. Check `archive/` for relevant completed tasks
2. Check reference docs (DEBUGGING.md, memory-model.md, lit-tests.md)
