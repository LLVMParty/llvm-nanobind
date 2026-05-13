# API UX Cleanup Plan

## Goal

Make the remaining high-value workflows clean from a Python user's perspective while keeping useful LLVM controls available. This plan covers five areas:

1. generic intrinsic calls,
2. explicit module optimization by pass pipeline,
3. object/assembly emission convenience,
4. JIT through LLVM-C,
5. metadata/debug-info cleanup.

The API should let users write the operation they mean without requiring LLVM-C boilerplate.

## Design rules

- Keep useful LLVM concepts public.
- Hide accidental LLVM-C scaffolding from common workflows.
- Prefer object methods over global helper sequences.
- Prefer names over registry IDs when the user's input is naturally a name.
- Prefer explicit ownership rules over implicit invalidation.
- Raise Python exceptions before LLVM can crash or leave IR half-mutated.

## Phase 1: Generic intrinsic calls

### User problem

Calling an intrinsic currently requires a multi-step sequence: look up an intrinsic ID, get a declaration from a module, construct or know the function type, then call the declaration. That is too much ceremony for a user who already knows the intrinsic name and call operands.

### Target UX

```python
builder.intrinsic(
    "llvm.memcpy",
    [dst, src, n, is_volatile],
    overloaded_types=[dst.type, src.type, n.type],
)

builder.intrinsic("llvm.sqrt", [x], overloaded_types=[x.type])
builder.intrinsic("llvm.trap", [])
```

### What `overloaded_types` means

`overloaded_types` is LLVM's overload-disambiguation type list. It is not the same as the call argument list.

LLVM has intrinsics whose declarations depend on one or more types. LLVM-C requires those types when retrieving the intrinsic declaration. Examples:

- `llvm.sqrt` is overloaded by floating-point or vector type.
- overflow intrinsics are overloaded by integer type.
- memory intrinsics are overloaded by pointer address spaces and length type.

The helper should pass `overloaded_types` to the existing intrinsic declaration machinery internally.

### Public API shape

```python
builder.intrinsic(
    name: str,
    args: Iterable[llvm.Value],
    *,
    overloaded_types: Iterable[llvm.Type] = (),
    name_hint: str = "",
) -> llvm.Value
```

### Behavior

- Look up the intrinsic by name internally.
- Raise `LLVMAssertionError` if the name is unknown.
- Raise a clear error if the intrinsic is overloaded and `overloaded_types` is empty.
- Get the intrinsic declaration in the current module.
- Build and return the call instruction.
- Keep existing lower-level intrinsic APIs public for advanced users.

### Tests

- Non-overloaded intrinsic call.
- Overloaded floating-point intrinsic call.
- Memory intrinsic call.
- Unknown intrinsic name error.
- Missing overload types error.
- Generated module verifies.

## Phase 2: Explicit module optimization helper

### User problem

Running passes is possible, but the current method name is low-level and does not read like a user task. Users should be able to say “optimize this module with this pipeline” directly.

### Target UX

```python
mod.optimize("default<O2>")
mod.optimize("default<Os>", target_machine=tm)
mod.optimize("function(mem2reg),default<O2>", target_machine=tm, options=opts)
```

### Public API shape

```python
mod.optimize(
    pipeline: str,
    *,
    target_machine: llvm.TargetMachine | None = None,
    options: llvm.PassBuilderOptions | None = None,
) -> None
```

### Behavior

- `pipeline` is the LLVM PassBuilder pipeline string.
- The method mutates the module in place.
- The method wraps the existing pass-running implementation.
- Existing lower-level pass APIs remain available for advanced users.
- Error messages should include the failed pipeline string when LLVM rejects it.

### Tests

- `default<O0>` succeeds on a simple module.
- `default<O2>` succeeds on a simple module.
- A custom pipeline succeeds where supported.
- Invalid pipeline raises a Python exception with the pipeline in the message.
- Passing a target machine works.

## Phase 3: Object and assembly emission convenience

### User problem

Emitting object code or assembly is possible, but the common path requires too much setup. Users should be able to emit from a module directly. Optimization should remain a separate explicit step.

### Target UX

```python
mod.optimize("default<O2>", target_machine=tm)
obj = mod.emit_object(target_machine=tm)
asm = mod.emit_assembly(target_machine=tm)
```

For the host target:

```python
tm = llvm.TargetMachine.host()
obj = mod.emit_object(target_machine=tm)
```

### Public API shape

```python
llvm.TargetMachine.host(
    cpu: str = "",
    features: str = "",
    opt_level: llvm.CodeGenOptLevel = llvm.CodeGenOptLevel.Default,
    reloc_mode: llvm.RelocMode = llvm.RelocMode.Default,
    code_model: llvm.CodeModel = llvm.CodeModel.Default,
) -> llvm.TargetMachine

mod.emit_object(*, target_machine: llvm.TargetMachine | None = None) -> bytes
mod.emit_assembly(*, target_machine: llvm.TargetMachine | None = None) -> bytes
```

### Behavior

- If `target_machine` is provided, use it.
- If `target_machine` is omitted, create a host target machine internally.
- Do not run optimization passes inside emission methods.
- Return bytes, matching the existing memory-buffer emission API.
- Keep `TargetMachine.emit_to_memory_buffer` public for advanced users.

### Tests

- Emit object for a simple function.
- Emit assembly for a simple function.
- Emitted object can be opened by `BinaryManager`.
- Explicit `target_machine=` path works.
- Omitted `target_machine` path works when host target support is available.
- Missing target support raises a clear Python exception or skips in tests where appropriate.

## Phase 4: JIT through LLVM-C

### User problem

In-process JIT execution is not available. This blocks DSL/compiler workflows that need to compile IR and call generated code from Python.

### Constraint

Use LLVM-C APIs only. The implementation must not bind LLVM C++ ORC or ExecutionEngine classes directly.

The first implementation step is to inspect the LLVM-C headers available in this project build and choose the best supported C API:

- ORC/LLJIT C API if available and stable enough,
- otherwise MCJIT/ExecutionEngine C API if available.

### Target UX

```python
with llvm.JIT.host() as jit:
    jit.add_module(mod)
    addr = jit.lookup("add_i32")
```

ctypes convenience:

```python
with llvm.JIT.host() as jit:
    jit.add_module(mod)
    add_i32 = jit.ctypes_function(
        "add_i32",
        restype=ctypes.c_int32,
        argtypes=[ctypes.c_int32, ctypes.c_int32],
    )
    assert add_i32(2, 3) == 5
```

Callback support if the chosen LLVM-C API exposes symbol registration cleanly:

```python
callback = ctypes.CFUNCTYPE(ctypes.c_int32, ctypes.c_int32)(py_func)
jit.add_symbol("py_func", callback)
```

### Ownership rules

- `jit.add_module(mod)` transfers module ownership to the JIT.
- The Python `Module` wrapper becomes invalid after transfer.
- `jit.lookup(name)` returns an integer address.
- `jit.ctypes_function(...)` wraps `lookup` and keeps required owner references alive.
- ctypes callback objects registered with the JIT must be pinned for at least as long as the JIT can call them.

### Tests

- JIT a pure integer function and call it through ctypes.
- Lookup of a missing symbol raises a clean exception.
- Module wrapper use after transfer raises a memory/lifetime error.
- Callback test if symbol registration is implemented.
- Unsupported platforms skip cleanly.

## Phase 5: Metadata/debug-info cleanup

### User problem

Common metadata operations should not require numeric metadata kind IDs or raw LLVM-C named-metadata handles.

### Target UX

```python
inst.metadata["llvm.loop"] = loop_md
md = inst.metadata.get("llvm.loop")
del inst.metadata["llvm.loop"]

mod.named_metadata["llvm.dbg.cu"].append(compile_unit)
for md in mod.named_metadata["llvm.dbg.cu"]:
    ...

mod.module_flags.add("Debug Info Version", llvm.ModuleFlagBehavior.Warning, value_md)

loc = ctx.debug_location(line=12, column=4, scope=subprogram)
with builder.debug_location(loc):
    value = builder.add(a, b, "sum")
```

### Behavior

- Hide metadata kind IDs from normal use.
- Provide a bulk metadata copy path for transforms without exposing kind IDs.
- Expose named metadata as a mapping from name to appendable list.
- Expose module flags as a keyed view.
- Keep advanced DIBuilder creation methods that model useful debug-info concepts.
- Remove redundant raw APIs where the new views have parity.

### Tests

- Metadata set/get/delete by name.
- Detached instruction metadata.
- Metadata bulk copy.
- Named metadata append, iteration, key iteration.
- Module flags add/get/key iteration.
- Debug-location context manager.
- DIBuilder recipes for file, compile unit, function, and local variable.
- Removed raw APIs stay absent from the public surface.

## API audit process

After each phase:

1. Rebuild to regenerate `.pyi`.
2. Review new public symbols.
3. Confirm examples express user tasks directly.
4. Add intentionally removed symbols to `tests/regressions/test_api_surface_cleanup.py`.
5. Keep advanced lower-level APIs when they correspond to useful LLVM controls.

## Suggested implementation order

1. Add `Builder.intrinsic(...)`.
2. Add `Module.optimize(...)`.
3. Add `TargetMachine.host()`.
4. Add `Module.emit_object(...)` and `Module.emit_assembly(...)`.
5. Design and implement `llvm.JIT` using LLVM-C.
6. Add metadata/debug-info mapping views and DIBuilder recipes.

## Acceptance criteria

- Intrinsic examples use `builder.intrinsic(...)` for generic intrinsic calls.
- Optimization examples use `mod.optimize(<pass pipeline>)`.
- Object/assembly examples use `mod.emit_object()` / `mod.emit_assembly()`.
- JIT design and implementation use LLVM-C only.
- Metadata examples avoid raw metadata kind IDs.
- Low-level APIs remain available when they are useful advanced controls.
- Tests pass:

```bash
cmake --build build
uv run run_tests.py
uv run run_tests.py --regressions
uv run run_llvm_c_tests.py --use-python
uvx ty check
```
