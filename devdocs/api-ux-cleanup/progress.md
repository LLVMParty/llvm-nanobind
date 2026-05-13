# API UX Cleanup Progress

## Status

Implemented and tested the active UX work:

1. generic intrinsic helper,
2. explicit module optimization helper,
3. object/assembly emission convenience,
4. JIT through LLVM-C ORC LLJIT,
5. metadata/debug-info cleanup.

## Implemented APIs

### Phase 1: Generic intrinsic helper

- [x] Added `Builder.intrinsic(name, args, *, overloaded_types=..., name_hint="")`.
- [x] Added no-overload form `Builder.intrinsic(name, args, *, name_hint="")`.
- [x] Internally looks up the intrinsic ID by name.
- [x] Uses `overloaded_types` as LLVM's overload-disambiguation type list.
- [x] Raises a clear error for unknown intrinsic names.
- [x] Raises a clear error when an overloaded intrinsic needs `overloaded_types`.
- [x] Keeps existing lower-level intrinsic APIs public.
- [x] Tests cover non-overloaded, overloaded, and memory intrinsics.

### Phase 2: Module/function optimization helpers

- [x] Added `Module.optimize(pipeline, *, target_machine=None, options=None)`.
- [x] Added `Function.optimize(pipeline, *, target_machine=None, options=None)` for function-level PassBuilder pipelines.
- [x] Uses LLVM PassBuilder pipeline strings directly.
- [x] Mutates the module or function in place.
- [x] Includes the failed pipeline string in error messages.
- [x] Keeps existing lower-level pass APIs public.
- [x] Fixed optional pass options by creating a default `PassBuilderOptions` internally when none is provided.
- [x] Tests cover success and failure paths.

### Phase 3: Object/assembly emission convenience

- [x] Added `TargetMachine.host(...)`.
- [x] Added `Module.emit_object(*, target_machine=None)`.
- [x] Added `Module.emit_assembly(*, target_machine=None)`.
- [x] If `target_machine` is omitted, creates a host target machine internally.
- [x] Emission methods do not run optimization passes.
- [x] Keeps `TargetMachine.emit_to_memory_buffer` public.
- [x] Tests cover object, assembly, host target, explicit target machine, and BinaryManager parsing.

### Phase 4: JIT through LLVM-C

- [x] Inspected available LLVM-C headers and chose ORC LLJIT C API.
- [x] Added `llvm.JIT.host()` context manager.
- [x] Implemented module ownership transfer for `jit.add_module(mod)`.
- [x] `jit.add_module(mod)` invalidates the Python `Module` wrapper on success.
- [x] Added `jit.lookup(name) -> int`.
- [x] Added `jit.ctypes_function(...)` callable wrapper.
- [x] `jit.ctypes_function(...)` keeps the JIT object alive while the returned callable wrapper is alive.
- [x] Added `jit.add_symbol(...)` for integer addresses and ctypes callbacks.
- [x] ctypes callback objects are pinned while the JIT is alive and released on dispose.
- [x] Unsupported host target/JIT setup raises `LLVMError`; tests skip target-dependent checks cleanly when unavailable.

### Phase 5: Metadata/debug-info cleanup

- [x] Added `Value.metadata` mapping view by metadata kind name.
- [x] Added `Metadata.kind`, `is_string`, `is_node`, `is_value`, `string`, `operands`, and `value` accessors.
- [x] Stubbed `Metadata.value` with `NotImplementedError` because LLVM-C cannot unwrap ValueAsMetadata to Value.
- [x] Added `MetadataMap.copy_to(target, include_debug_location=False)` so transforms can copy arbitrary attached metadata without exposing kind IDs.
- [x] Added support for detached instruction metadata by deriving the context from the value type.
- [x] Added `Module.named_metadata` mapping/list view.
- [x] Added `NamedMetadataMap.keys()` and iteration.
- [x] Added `Module.module_flags` view.
- [x] Added `Context.debug_location(...)`.
- [x] Added `Builder.debug_location(...)` context manager.
- [x] Added DIBuilder recipes: `file`, `compile_unit`, `function`, `local_variable`.
- [x] Removed redundant public low-level metadata APIs: raw metadata kind lookup, raw `Value.set_metadata`, public `ValueMetadataEntries`, public `NamedMDNode`, `Metadata.as_value`, raw named-metadata methods, and raw module-flag methods.
- [x] Removed redundant DIBuilder aliases covered by recipes: `create_file`, `create_compile_unit`.
- [x] Kept advanced DIBuilder `create_*` methods where the recipes do not provide full coverage.

## Tests added

- `tests/regressions/test_api_ux_cleanup.py`
- `tests/regressions/test_metadata_ux_cleanup.py`

Coverage:

- [x] non-overloaded intrinsic call,
- [x] overloaded floating-point intrinsic call,
- [x] memory intrinsic call,
- [x] unknown intrinsic error,
- [x] missing overload types error,
- [x] module optimization success and invalid-pipeline failure,
- [x] optimization with target machine,
- [x] object and assembly emission,
- [x] emitted object opens with `BinaryManager`,
- [x] JIT integer function lookup and ctypes call,
- [x] module invalidation after JIT transfer,
- [x] missing JIT symbol error,
- [x] JIT ctypes callable keeps the JIT alive,
- [x] JIT callback symbol registration and callback lifetime pinning.

## Documentation and examples updated

- [x] `README.md` current capabilities, known limitations, and example links.
- [x] `devdocs/api-reference.md` high-level UX helper examples.
- [x] `devdocs/api-ux-cleanup/plan.md` remains the task design reference.
- [x] `examples/intrinsic_memcpy.py` shows `Builder.intrinsic(...)`.
- [x] `examples/optimize_module.py` shows `Module.optimize(...)`.
- [x] `examples/optimize_function.py` shows `Function.optimize(...)`.
- [x] `examples/emit_object_assembly.py` shows `TargetMachine.host()`, `emit_object()`, and `emit_assembly()`.
- [x] `examples/jit_add.py` shows `JIT.host()`, `add_module()`, `lookup` via `ctypes_function()`, and `add_symbol()`.

## Validation performed

```bash
cmake --build build
uv run tests/regressions/test_api_ux_cleanup.py
uv run pytest tests/regressions/test_api_ux_cleanup.py -q
uv run tests/regressions/test_metadata_ux_cleanup.py
uv run pytest tests/regressions/test_metadata_ux_cleanup.py tests/test_examples.py -q
uv run run_tests.py --regressions
uv run run_tests.py
uv run run_llvm_c_tests.py --use-python
uvx ty check
```

All commands passed.
