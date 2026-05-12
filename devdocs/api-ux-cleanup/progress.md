# API UX Cleanup Progress

## Status

Plan narrowed to the active UX work:

1. generic intrinsic helper,
2. explicit module optimization helper,
3. object/assembly emission convenience,
4. JIT through LLVM-C.

No implementation has been started for these phases.

## Last known validation baseline

```bash
cmake --build build
uv run run_tests.py --regressions
uv run run_tests.py
uv run run_llvm_c_tests.py --use-python
uvx ty check
```

## Active work items

### Phase 1: Generic intrinsic helper

- [ ] Add `Builder.intrinsic(name, args, *, overloaded_types=(), name_hint="")`.
- [ ] Internally look up the intrinsic ID by name.
- [ ] Use `overloaded_types` as LLVM's overload-disambiguation type list.
- [ ] Raise a clear error for unknown intrinsic names.
- [ ] Raise a clear error when an overloaded intrinsic needs `overloaded_types`.
- [ ] Keep existing lower-level intrinsic APIs public.
- [ ] Add tests for non-overloaded, overloaded, and memory intrinsics.

### Phase 2: Module optimization helper

- [ ] Add `Module.optimize(pipeline, *, target_machine=None, options=None)`.
- [ ] Use LLVM PassBuilder pipeline strings directly.
- [ ] Mutate the module in place.
- [ ] Include the failed pipeline string in error messages.
- [ ] Keep existing lower-level pass APIs public.
- [ ] Add success and failure tests.

### Phase 3: Object/assembly emission convenience

- [ ] Add `TargetMachine.host(...)`.
- [ ] Add `Module.emit_object(*, target_machine=None)`.
- [ ] Add `Module.emit_assembly(*, target_machine=None)`.
- [ ] If `target_machine` is omitted, create a host target machine internally.
- [ ] Do not run optimization passes inside emission methods.
- [ ] Keep `TargetMachine.emit_to_memory_buffer` public.
- [ ] Add object, assembly, and BinaryManager tests.

### Phase 4: JIT through LLVM-C

- [ ] Inspect available LLVM-C headers for ORC/LLJIT and MCJIT/ExecutionEngine APIs.
- [ ] Choose the best C API available in this LLVM build.
- [ ] Design `llvm.JIT.host()` manager.
- [ ] Design module ownership transfer for `jit.add_module(mod)`.
- [ ] Add `jit.lookup(name) -> int`.
- [ ] Add `jit.ctypes_function(...)`.
- [ ] Add `jit.add_symbol(...)` if callback/symbol registration is exposed cleanly by LLVM-C.
- [ ] Add unsupported-platform skip behavior.

## Open questions

- For `builder.intrinsic`, should `overloaded_types` remain fully explicit, or should the helper infer simple cases later?
- For host emission, should `TargetMachine.host()` initialize native targets internally or require callers/tests to initialize targets first?
- Which LLVM-C JIT API is available and usable in this project's LLVM build?
