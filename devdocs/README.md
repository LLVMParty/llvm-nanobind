# Development Documentation Methodology

This directory contains structured documentation for larger, multi-phase development tasks. The methodology enables tracking progress across multiple agent sessions and provides context for continuing work.

## Structure

Each major task gets its own subdirectory with standardized files:

```
devdocs/
├── README.md                    # This file - methodology overview
├── <task-name>/
│   ├── plan.md                  # Detailed implementation plan
│   └── progress.md              # Progress tracking and milestones
└── <another-task>/
    ├── plan.md
    └── progress.md
```

## Workflow

### 1. Research/Planning Phase

Before implementation begins:

1. **Create task subdirectory** (e.g., `devdocs/llvm-c-test/`)
2. **Create `plan.md`** with:
   - Overview and goals
   - Phased implementation approach
   - Required APIs/bindings/features per phase
   - Testing strategy
   - Estimated effort per phase
3. **Create `progress.md`** with:
   - Quick summary section
   - Status overview table
   - Checkbox lists for each phase
   - Empty "Completed Milestones" section

### 2. Implementation Phase

During each development session:

1. **Read progress.md** to understand current state
2. **Work on the current phase** using the plan as a guide
3. **Update progress.md** incrementally as tasks complete

### 3. End of Phase Update

At the end of each development phase, update both files:

**plan.md updates:**
- Mark completed phases with ✅
- Update statistics table with actual counts
- Add any learnings or changes to future phases

**progress.md updates:**
- Update quick summary with current phase status
- Update status overview table
- Mark completed items with [x]
- Add detailed milestone entry under "Completed Milestones" with:
  - Date of completion
  - Commands/features implemented
  - Key bindings/APIs added
  - Technical highlights and challenges
  - Test results

## Example: llvm-c-test

The `llvm-c-test/` subdirectory demonstrates this methodology for porting the LLVM C test suite to Python:

### plan.md Structure
```markdown
# llvm-c-test Python Port Plan

## Overview
- Goal, motivation, scope

## CLI Design
- Command interface specification

## Architecture
- Directory structure
- Command to module mapping

## Phase 1: Foundation
- Commands to implement
- Required bindings (with tables)

## Phase 2: Metadata & Attributes
...

## Testing Strategy
- Golden-master testing approach
- Lit integration

## Summary Statistics
| Phase | Commands | Bindings | Status |
|-------|----------|----------|--------|
| Phase 1 | 8 | 30 | ✅ Complete |
...
```

### progress.md Structure
```markdown
# llvm-c-test Python Port Progress

**Last Updated:** December 17, 2025 (Phase 4 Complete)

## Quick Summary
✅ **Phase 1 Complete** - Description
✅ **Phase 2 Complete** - Description
⏳ **Phase 3** - In Progress

**Progress:** 21/22 commands (95%)

## Status Overview
| Phase | Commands | Bindings | Status |
...

## Phase 1: Foundation (8/8 commands) ✅
- [x] `--targets-list`
- [x] `--calc`
...

## Completed Milestones

### Phase 4: Platform-Specific - December 17, 2025 ✅
Successfully implemented...

**Commands Implemented:**
- ✅ `--disassemble`
...

**Key Bindings Added:**
- Disassembler (6 bindings): ...
...

**Technical Highlights:**
- Challenge faced and solution
...
```

## Benefits

1. **Context Preservation**: New agent sessions can quickly understand project state
2. **Progress Visibility**: Clear tracking of what's done vs. remaining
3. **Technical Documentation**: Captures implementation decisions and lessons learned
4. **Incremental Updates**: Small updates each phase prevent documentation debt
5. **Scope Management**: Phases can be adjusted based on learnings

## Best Practices

1. **Update at phase boundaries**: Don't wait until the end - update after each phase
2. **Be specific in milestones**: Include actual counts, dates, and technical details
3. **Document blockers**: Note any issues that required changes to the plan
4. **Keep plan.md stable**: Major structure shouldn't change; update status markers
5. **Keep progress.md detailed**: This is the living record of what was accomplished

## Agent Instructions

When continuing work on a task documented here:

1. Read `plan.md` to understand the overall approach
2. Read `progress.md` to find the current phase and remaining work
3. Work on incomplete items in the current phase
4. Update `progress.md` as you complete items
5. At phase completion, add a detailed milestone entry
6. Update `plan.md` statistics and status markers
