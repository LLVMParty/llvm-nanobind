# Python API Reference

This project's comprehensive API reference is the generated type stub:

- `.venv/Lib/site-packages/llvm/__init__.pyi`

That file is generated from the nanobind bindings and contains the full public
surface area (functions, classes, methods, properties, argument names, and
types) for the currently built version.

## How To Regenerate

```bash
uv sync
```

The stub is regenerated during rebuild.

## How To Browse Quickly

```bash
# List public classes
rg "^class " .venv/Lib/site-packages/llvm/__init__.pyi

# List top-level functions
rg "^def " .venv/Lib/site-packages/llvm/__init__.pyi

# Jump to one API quickly
rg "replace_all_uses_with|erase_from_parent|split_basic_block|const_string" .venv/Lib/site-packages/llvm/__init__.pyi
```

## Notes

- This is the canonical reference for exact signatures.
- Narrative usage guidance and caveats remain in:
  - `README.md`
  - `devdocs/porting-guide.md`
  - `devdocs/lit-tests.md`
  - `devdocs/DEBUGGING.md`

## High-Level UX Helpers

These APIs wrap common workflows that previously required LLVM-C-style boilerplate:

```python
# Intrinsics by name.
builder.intrinsic("llvm.sqrt", [x], overloaded_types=[x.type])

# Explicit PassBuilder pipeline optimization.
mod.optimize("default<O2>", target_machine=tm)
func.optimize("mem2reg,instcombine,simplifycfg", target_machine=tm)

# Direct object/assembly emission. Optimize explicitly first when desired.
obj = mod.emit_object(target_machine=tm)
asm = mod.emit_assembly(target_machine=tm)

# Host target machine convenience.
tm = llvm.TargetMachine.host()

# LLVM-C ORC LLJIT.
with llvm.JIT.host() as jit:
    jit.add_module(mod)  # invalidates mod on success
    addr = jit.lookup("compiled_function")

# Metadata by name, without public kind IDs.
text = ctx.md_string("frontend")
assert text.is_string
assert text.string == "frontend"
node = ctx.md_node([text])
for operand in node.operands:
    assert operand.string == "frontend"
inst.metadata["llvm.loop"] = loop_md
md = inst.metadata.get("llvm.loop")
src_inst.metadata.copy_to(dst_inst)
del inst.metadata["llvm.loop"]

# Named metadata and module flags.
mod.named_metadata["llvm.dbg.cu"].append(compile_unit)
mod.module_flags.add("Debug Info Version", llvm.ModuleFlagBehavior.Warning, md)

# Debug-location scopes.
with builder.debug_location(line=12, column=4, scope=subprogram):
    inst = builder.add(a, b, "sum")
```

`builder.intrinsic(..., overloaded_types=[...])` uses LLVM's intrinsic
overload-disambiguation type list. This list selects the intrinsic declaration;
it is not the same as the call operand list.

## Value API Validity Matrix

This section documents when `llvm.Value` accessors are valid to call. Invalid
calls should raise `llvm.LLVMAssertionError` (not crash).

### Always Valid (for any live Value)

- Generic identity/introspection:
  - `type`, `name`, `value_kind`, `is_constant`, `is_undef`, `is_poison`
- Use/operand graph:
  - `uses`, `users`, `has_uses`, `num_operands`, `operands`, `get_operand`,
    `set_operand`, `get_operand_use`
  - Prefer semantic accessors over raw operand indexing whenever available.
    Raw operand layout is instruction-specific and can differ from printed IR;
    see `devdocs/operands.md`.
- Type predicates:
  - `is_*` predicate properties (function/global/constant/etc.)

### Global Families

- Global variable only:
  - `next_global`, `prev_global`
  - `initializer` (get/set)
  - `set_constant`, `is_global_constant`
  - `set_thread_local`, `is_thread_local`
  - `set_externally_initialized`, `is_externally_initialized`
  - `delete_global`, `delete`
- Global value:
  - `global_value_type`, `unnamed_address`
  - `linkage`, `visibility`, `dll_storage_class`
  - `metadata` mapping view; use `value.metadata.copy_to(other)` for bulk copies
- Global object:
  - `comdat`, `set_comdat`
  - `section`
  - `alignment` (global path)

### Function Families

- Function only:
  - `has_personality_fn`, `personality_fn`, `set_personality_fn`
  - `has_prefix_data`, `prefix_data`, `set_prefix_data`
  - `has_prologue_data`, `prologue_data`, `set_prologue_data`
  - `function_type`
  - Accessors `personality_fn` / `prefix_data` / `prologue_data` are
    exception-first:
    - use `has_personality_fn` / `has_prefix_data` / `has_prologue_data`
      before accessing when absence is possible.

### Instruction Families

- Any instruction:
  - `opcode`, `opcode_name`
  - `metadata` mapping view; use `inst.metadata.copy_to(other)` for bulk copies
  - `next_instruction`, `prev_instruction`
  - `remove_from_parent`, `erase_from_parent`, `delete_instruction`,
    `instruction_clone`
- Branch:
  - `is_conditional`, `condition` require `br`
- Terminator:
  - `num_successors`, `get_successor`, `successors`, `unwind_dest`
  - BranchInst gotcha (conditional `br`):
    - successor order is `[true, false]`
    - raw operand order is `[cond, false, true]`
    - mapping: `get_successor(0) == get_operand(2).value_as_basic_block()`
      and `get_successor(1) == get_operand(1).value_as_basic_block()`
- PHI:
  - `num_incoming`, `get_incoming_value`, `get_incoming_block`, `incoming`
- LandingPad:
  - `num_clauses`, `get_clause`, `is_cleanup`, `set_cleanup`, `add_clause`
- CatchPad/CatchSwitch:
  - `parent_catch_switch`, `num_handlers`, `handlers`, `add_handler`
- ShuffleVector:
  - `num_mask_elements`, `get_mask_value`

### Opcode-Specific Instruction Accessors

- `icmp_predicate`: `icmp`
- `fcmp_predicate`: `fcmp`
- `nsw` / `set_nsw`: `add`, `sub`, `mul`, `shl`
- `nuw` / `set_nuw`: `add`, `sub`, `mul`, `shl`
- `exact` / `set_exact`: `udiv`, `sdiv`, `lshr`, `ashr`
- `nneg` / `set_nneg`: `zext`
- `is_disjoint` / `set_is_disjoint`: `or`
- `icmp_same_sign` / `set_icmp_same_sign`: `icmp`
- `allocated_type`: `alloca`

### Call/Invoke/CallBr Families

- Call-like (`call`, `invoke`, `callbr`):
  - `num_operand_bundles`, `get_operand_bundle_at_index`
  - `called_function_type`, `called_value`, `set_called_operand`
- Arg operand instructions (`call`, `invoke`, `callbr`, `catchpad`,
  `cleanuppad`):
  - `num_arg_operands`, `get_arg_operand`
- Tail call kind (`call`, `invoke`):
  - `tail_call_kind`, `set_tail_call_kind`
- Callsite attributes use slot accessors:
  - `callsite_attributes` for callsite-level attributes.
  - `callsite_return_attributes` for return-value attributes.
  - `callsite_param_attributes(i)` for argument attributes, using a 0-based Python index.
  - Requires a call-like instruction.

### Atomic/Memory Families

- Volatile accessors:
  - `is_volatile`, `set_volatile` require `load`/`store`
- Ordering accessors:
  - `ordering`, `set_ordering` require atomic-capable memory instruction
    (`load`, `store`, `fence`, `atomicrmw`, `cmpxchg`)
- Atomic metadata:
  - `is_atomic`: instruction only
  - `atomic_sync_scope_id`, `set_atomic_sync_scope_id`: atomic-capable memory
    instruction
  - `atomic_rmw_bin_op`: `atomicrmw`
  - `cmpxchg_success_ordering`, `cmpxchg_failure_ordering`, `weak`, `set_weak`:
    `cmpxchg`

### Indexed/GEP Families

- `num_indices`:
  - `getelementptr`, `extractvalue`, `insertvalue` (instruction or const-expr)
- `indices`:
  - `extractvalue`/`insertvalue` instruction, or const-expr
    `getelementptr`/`extractvalue`/`insertvalue`
- `gep_source_element_type`, `gep_no_wrap_flags`:
  - GEP value (instruction or const-expr)

### Constant-Only / Type-Safe Mutators

- Constant-only:
  - `const_bitcast` requires a constant value
- Replacement:
  - `replace_all_uses_with` requires same context and identical type
- Fast-math:
  - `fast_math_flags`, `set_fast_math_flags` require
    `can_use_fast_math_flags == True`

## Non-Value API Validity Matrix

This section captures guard preconditions for wrapper classes other than
`llvm.Value`.

### Type (`llvm.Type`)

- Struct-only:
  - `is_packed_struct`, `is_opaque_struct`, `is_literal_struct`
  - `struct_name`, `struct_element_count`, `get_struct_element_type`
- Function-only:
  - `is_vararg`, `return_type`, `param_count`, `param_types`
- Pointer-only:
  - `is_opaque_pointer`, `pointer_address_space`
- Array-only:
  - `array_length`
- Vector-only:
  - `vector_size`
- Target-extension-only:
  - `target_ext_type_name`, `target_ext_type_num_type_params`,
    `target_ext_type_num_int_params`,
    `get_target_ext_type_type_param`, `get_target_ext_type_int_param`
  - target-ext parameter accessors require in-range indices.
- Element-type family:
  - `element_type` requires pointer/vector/array type.
- Constant constructors:
  - `constant` requires an integer type.
  - `real_constant` requires a floating type.
  - `constant(str, radix)` requires `2 <= radix <= 36`.
- Named struct creation:
  - `ctx.types.struct("Name", [field_types...])` defaults to
    `reuse_existing=True`.
  - Reusing the same name with the same body returns the existing identified
    struct; reusing an opaque declaration completes it; conflicting bodies or
    packing raise `LLVMAssertionError`.
  - Pass `reuse_existing=False` for raw LLVM insertion behavior, which may append
    a suffix to duplicate names.
- Struct body mutation:
  - `set_body` requires identified opaque struct type (not literal, not already
    non-opaque).

### Type factory aliases

- `types` is available on `Context`, `Module`, `Function`, `BasicBlock`, and `Value`.
- All aliases for objects in the same context compare equal:
  `ctx.types == mod.types == fn.types == bb.types == value.types`.

### BasicBlock (`llvm.BasicBlock`)

- `terminator` requires the block to have a terminator.
- `first_non_phi` is semantic-optional:
  - returns `None` when the block has no non-PHI instruction.
- For insertion before first non-PHI without optional handling:
  - use `create_builder(first_non_phi=True)`, which falls back to block-end
    when no non-PHI instruction exists.
- Parent navigation:
  - `function` requires block attached to a function.
  - `module`/`context` require function parent and module parent.
- Split helpers:
  - `split_basic_block` / `split_basic_block_before` require:
    - instruction operand is an instruction in this block,
    - not a PHI split point,
    - source block already has a terminator.

### Function (`llvm.Function`)

- `append_basic_block` requires function parent module and module context.
- `append_existing_basic_block` requires an unattached block.
- Basic block accessors are exception-first:
  - `entry_block` requires `is_declaration == False`.
  - `first_basic_block` / `last_basic_block` require `basic_block_count > 0`.
- Attribute APIs use slot accessors, not raw LLVM indices:
  - `attributes` for function-level attributes.
  - `return_attributes` for return-value attributes.
  - `param_attributes(i)` for parameter attributes, using a 0-based Python index.
  - Use `llvm.Attribute.enum/type/string(...)` or `slot.add("noreturn")`.
  - Use `llvm.Attribute.memory(ctx, "none")` or `slot.add_memory("read")` for `memory(...)` effects without raw encoded integers.
- `create_builder()` positions in the function entry block and creates an
  `entry` block when the function has no blocks yet.
- `block_address` requires block ownership by that function.
  Prefer `bb.block_address()` when the function can be inferred.
- Parent navigation:
  - `module`/`context` require function has a parent module.

### Builder (`llvm.Builder`)

- Builder creation from instruction:
  - `Context.create_builder(inst)` and `Value.create_builder()` require
    instruction values attached to a basic block.
- Positioning:
  - `position_before(inst)` requires an instruction attached to a block.
  - `position_at(bb, inst)` requires `inst` to be an instruction in `bb`.
- Memory allocation:
  - `alloca(ty, name="")` creates a scalar alloca.
  - `alloca(ty, count, name="")` creates an array alloca.
- Instruction-only insertion helpers:
  - `insert_into_builder_with_name(instr)` requires instruction value.
  - `add_metadata_to_inst(instr)` requires instruction value.

### BasicBlock `block_address()`

- Requires the block to be attached to a function.
- `Function.block_address(bb)` is also available when explicitly checking a
  block against a specific function.

### OperandBundle (`llvm.OperandBundle`)

- `get_arg_at_index(index)`:
  - requires `0 <= index < num_args`

### Binary/Object Iterators (`llvm.Binary`)

- `sections` and `symbols`:
  - require object-file binary types (`COFF`, `ELF*`, `MachO*`, `Wasm`).
  - non-object binaries (e.g., `IR`, archive, import file) raise
    `LLVMAssertionError` with the concrete binary type in the message.
