# Feature Matrix Summary

Comprehensive tracking of LLVM-C API implementation in llvm-nanobind Python bindings.

**Last Updated:** 2024-12-25 (updated)

---

## Quick Reference

```python
import llvm

# Initialize native target
llvm.initialize_native_target()
llvm.initialize_native_asm_printer()

# Create context and module
with llvm.create_context() as ctx:
    with ctx.create_module("example") as mod:
        # Types
        i32 = ctx.types.i32
        fn_ty = ctx.types.function(i32, [i32, i32])
        
        # Function and basic block
        fn = mod.add_function("add", fn_ty)
        bb = fn.append_basic_block("entry")
        
        # Build instructions
        with ctx.create_builder(bb) as b:
            result = b.add(fn.get_param(0), fn.get_param(1))
            b.ret(result)
        
        # Output IR
        print(mod)
        
        # Generate object code
        triple = llvm.get_default_target_triple()
        target = llvm.get_target_from_triple(triple)
        tm = llvm.create_target_machine(
            target, triple, 
            llvm.get_host_cpu_name(),
            llvm.get_host_cpu_features(),
            llvm.CodeGenOptLevel.Default,
            llvm.RelocMode.Default,
            llvm.CodeModel.Default
        )
        
        # Optimize
        opts = llvm.PassBuilderOptions()
        llvm.run_passes(mod, "default<O2>", tm, opts)
        
        # Emit to bytes
        obj_bytes = tm.emit_to_memory_buffer(mod, llvm.CodeGenFileType.Object)
```

---

## Coverage Summary

| Header | Total | ✅ Impl | 🚫 Skip | ❌ TODO | Coverage |
|--------|-------|---------|---------|---------|----------|
| **Core.h** | 640 | **472** | 45 | 123 | **74%** |
| **DebugInfo.h** | 99 | **~75** | 0 | ~24 | **~76%** |
| **Target.h** | 22 | 22 | 0 | 0 | **100%** |
| **TargetMachine.h** | 29 | 14 | 9 | 6 | **79%** |
| **Object.h** | 31 | 23 | 0 | 8 | 74% |
| **Analysis.h** | 4 | 2 | 2 | 0 | **100%** |
| **BitReader.h** | 8 | 3 | 5 | 0 | 37.5% |
| **BitWriter.h** | 4 | 2 | 2 | 0 | **100%** |
| **IRReader.h** | 1 | 1 | 0 | 0 | 100% |
| **PassBuilder.h** | 15 | 15 | 0 | 0 | **100%** |
| **Disassembler.h** | 6 | 4 | 0 | 2 | **67%** |
| **Linker.h** | 1 | 1 | 0 | 0 | **100%** |
| **Error.h** | 7 | 0 | 7 | 0 | 0%* |
| **Other** | 12 | 0 | 0 | 12 | 0% |
| **Total** | **~880** | **~634** | **~71** | **~175** | **~80%** |

*Error.h uses Python exceptions instead of C-style error handling.

---

## Implementation Status by Category

### ✅ Well Covered (>70%)

| Feature | Coverage | Notes |
|---------|----------|-------|
| Target initialization | 100% | All native/all target functions |
| PassBuilder | 100% | All optimization options |
| BitWriter | 100% | File and memory buffer output |
| Linker | 100% | Module linking |
| Module creation/properties | 95% | Full CRUD support |
| Type system | 90% | All common types |
| Target Machine | 79% | Create, emit, configure |
| Basic block operations | 85% | Iteration, manipulation |
| Builder - common instructions | 80% | Arithmetic, memory, control flow |
| Global variables | 80% | Properties, initializers |
| Functions | 75% | Parameters, attributes, intrinsics |
| Object file reading | 74% | Sections, symbols, relocations |

### ⚠️ Partial Coverage (30-70%)

| Feature | Coverage | Notes |
|---------|----------|-------|
| Core.h | 64.5% | Most common APIs done |
| Debug info | ~50% | Core DIBuilder, needs more types |
| Disassembler | 50% | Basic disassembly works |
| Analysis | 50% | Module/function verification |
| Bitcode reading | 37.5% | Context versions only |
| Metadata | 40% | Basic creation and attachment |
| Attributes | 40% | Enum attributes, needs string/type |

### ❌ Not Implemented (<30%)

| Feature | Coverage | Priority | Notes |
|---------|----------|----------|-------|
| TargetMachineOptions | 0% | Low | Use create_target_machine() |
| Comdat | 0% | Low | Windows/COFF support |
| Error.h | 0% | N/A | Python exceptions used instead |

---

## Core Workflows - All Supported ✅

### 1. ✅ Bitcode Writing
```python
mod.write_bitcode_to_file("output.bc")
bc_bytes = mod.write_bitcode_to_memory_buffer()
```

### 2. ✅ Optimization Passes
```python
opts = llvm.PassBuilderOptions()
opts.set_loop_vectorization(True)
opts.set_slp_vectorization(True)
llvm.run_passes(mod, "default<O2>", target_machine, opts)
```

### 3. ✅ Code Generation
```python
tm = llvm.create_target_machine(target, triple, cpu, features, ...)
obj_bytes = tm.emit_to_memory_buffer(mod, llvm.CodeGenFileType.Object)
tm.emit_to_file(mod, "output.o", llvm.CodeGenFileType.Object)
```

### 4. ✅ Host Queries
```python
triple = llvm.get_default_target_triple()
cpu = llvm.get_host_cpu_name()
features = llvm.get_host_cpu_features()
```

### 5. ✅ Module Linking
```python
mod.link_module(other_mod)  # other_mod is consumed
```

### 6. ✅ Function/Module Verification
```python
mod.verify()                    # Raises on error
fn.verify()                     # Returns bool
fn.verify_and_print()           # Returns (bool, error_msg)
```

### 7. ✅ Target Data Queries
```python
td = tm.create_data_layout()
size = td.abi_size_of_type(struct_ty)
align = td.abi_alignment_of_type(i64_ty)
ptr_int = td.int_ptr_type(ctx)  # i64 on 64-bit
```

---

## Intentionally Skipped (🚫)

### Global Context APIs
All functions using `LLVMGetGlobalContext()` - safety risk.

### Legacy Pass Manager  
`LLVMCreatePassManager`, `LLVMRunPassManager`, etc. - use PassBuilder.

### Deprecated Functions
`LLVMBuildLoad`, `LLVMBuildGEP`, `LLVMBuildCall`, etc. - use `*2` versions.

### Unsafe for Embedding
`LLVMShutdown` - would corrupt Python process.

### Internal APIs
`LLVMCreateMessage`, `LLVMDisposeMessage` - internal memory management.

### Low-Level File APIs
`LLVMWriteBitcodeToFD`, `LLVMWriteBitcodeToFileHandle` - use file/memory buffer.

### Debugging Only
`LLVMViewFunctionCFG`, `LLVMViewFunctionCFGOnly` - requires graphviz.

---

## Remaining Features (Low Priority)

### Not Yet Implemented

| Category | Functions | Priority | Notes |
|----------|-----------|----------|-------|
| Type Attributes | 3 | Medium | `create_type_attribute`, range attributes |
| Module Flag Iteration | 5 | Medium | Iterate all module flags |
| Attribute Management | 5 | Medium | Get/remove attributes at index |
| Builder Position | 4 | Medium | Position control, FP math tags |
| Cast Convenience | 7 | Low | ZExtOrBitCast, etc. |
| Indirect Branch | 2 | Low | Computed goto support |
| Memory Buffer | 2 | Low | stdin, memory range |

### Advanced Features (Not Tracked)

| Feature | Functions | Use Case |
|---------|-----------|----------|
| ORC JIT | ~90 | Runtime code generation |
| COMDAT | 5 | Windows COFF linking |
| Error Handling | 3 | Custom fatal error handlers |
| Support | 4 | JIT symbol resolution |
| Remarks | ~24 | Optimization diagnostics |

See [progress.md](progress.md) for detailed breakdown.

---

## Detailed Matrix Files

| File | Contents | 
|------|----------|
| [core.md](core.md) | Core.h - All 640 functions with Python API |
| [debuginfo.md](debuginfo.md) | DebugInfo.h - 99 functions with examples |
| [target.md](target.md) | Target/TargetMachine - 68 functions |
| [misc.md](misc.md) | All other headers + advanced features |

---

## API Design Patterns

### Context Managers
```python
with llvm.create_context() as ctx:
    with ctx.create_module("mod") as mod:
        with ctx.create_builder(bb) as b:
            # Resources automatically cleaned up
```

### Properties for Get/Set
```python
mod.name = "my_module"        # LLVMSetModuleIdentifier
print(mod.name)               # LLVMGetModuleIdentifier
```

### Pythonic Iteration
```python
for fn in mod.functions:       # LLVMGetFirst/NextFunction
    for bb in fn.basic_blocks: # LLVMGetFirst/NextBasicBlock
        for inst in bb.instructions:
            print(inst)
```

### Safety Checks
```python
# Raises LLVMMemoryError instead of crashing
try:
    dead_module.functions  # Module was disposed
except llvm.LLVMMemoryError:
    print("Module no longer valid")
```

### Rich Exceptions
```python
try:
    mod = ctx.parse_ir("invalid")
except llvm.LLVMParseError as e:
    print(e)  # Detailed error message
```
