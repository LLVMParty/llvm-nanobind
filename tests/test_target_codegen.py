#!/usr/bin/env -S uv run
"""
Test: test_target_codegen
Tests for Target, TargetData, TargetMachine, and code generation

This is the Python equivalent of tests/test_target_codegen.cpp.
Output should match the C++ golden master test.

LLVM Python APIs tested:
- llvm.initialize_all_targets(), llvm.initialize_all_target_mcs()
- llvm.initialize_all_asm_printers(), llvm.initialize_all_asm_parsers()
- llvm.initialize_native_target(), llvm.initialize_native_asm_printer()
- llvm.get_default_target_triple(), llvm.normalize_target_triple()
- llvm.get_host_cpu_name(), llvm.get_host_cpu_features()
- llvm.get_target_from_triple(), llvm.get_target_from_name()
- Target.name, Target.description, Target.has_jit, etc.
- llvm.create_target_machine()
- TargetMachine.triple, TargetMachine.cpu, TargetMachine.feature_string
- TargetMachine.create_data_layout()
- TargetData.size_of_type_in_bits(), TargetData.abi_size_of_type(), etc.
- TargetMachine.emit_to_memory_buffer()
"""

import sys
import llvm


def main():
    # Initialize all targets
    llvm.initialize_all_target_infos()
    llvm.initialize_all_targets()
    llvm.initialize_all_target_mcs()
    llvm.initialize_all_asm_printers()
    llvm.initialize_all_asm_parsers()

    print("; Test: test_target_codegen")
    print("; Tests target initialization, queries, and code generation")
    print(";")

    # ==========================================================================
    # Host CPU queries
    # ==========================================================================
    print("; Host queries:")

    default_triple = llvm.get_default_target_triple()
    print(f";   Default triple: {default_triple}")

    normalized = llvm.normalize_target_triple(default_triple)
    print(f";   Normalized triple: {normalized}")

    cpu_name = llvm.get_host_cpu_name()
    print(f";   Host CPU: {cpu_name}")

    cpu_features = llvm.get_host_cpu_features()
    # Features can be very long, just check it's not empty
    print(f";   Host features length: {len(cpu_features)}")

    # ==========================================================================
    # Target lookup
    # ==========================================================================
    print(";")
    print("; Target lookup:")

    target = llvm.get_target_from_triple(default_triple)
    if target is None:
        print("; ERROR: Failed to get target", file=sys.stderr)
        return 1

    print(f";   Target name: {target.name}")
    print(f";   Target description: {target.description}")
    print(f";   Has JIT: {'yes' if target.has_jit else 'no'}")
    print(f";   Has TargetMachine: {'yes' if target.has_target_machine else 'no'}")
    print(f";   Has AsmBackend: {'yes' if target.has_asm_backend else 'no'}")

    # ==========================================================================
    # Create TargetMachine
    # ==========================================================================
    print(";")
    print("; TargetMachine:")

    tm = llvm.create_target_machine(
        target, default_triple, "generic", "",
        llvm.CodeGenOptLevel.Default,
        llvm.RelocMode.Default,
        llvm.CodeModel.Default
    )

    print(f";   TM Triple: {tm.triple}")
    print(f";   TM CPU: {tm.cpu}")
    features = tm.feature_string
    print(f";   TM Features: {'(has features)' if len(features) > 0 else '(empty)'}")

    # ==========================================================================
    # Target Data Layout
    # ==========================================================================
    print(";")
    print("; TargetData:")

    td = tm.create_data_layout()
    print(f";   Data layout: {td}")

    # Create a context and some types for size queries
    with llvm.create_context() as ctx:
        i8 = ctx.types.i8
        i32 = ctx.types.i32
        i64 = ctx.types.i64
        f32 = ctx.types.f32
        f64 = ctx.types.f64
        ptr = ctx.types.ptr()

        print(";")
        print("; Type sizes and alignments:")
        print(f";   i8:  size={td.size_of_type_in_bits(i8)} bits, "
              f"abi_size={td.abi_size_of_type(i8)} bytes, "
              f"alignment={td.abi_alignment_of_type(i8)}")
        print(f";   i32: size={td.size_of_type_in_bits(i32)} bits, "
              f"abi_size={td.abi_size_of_type(i32)} bytes, "
              f"alignment={td.abi_alignment_of_type(i32)}")
        print(f";   i64: size={td.size_of_type_in_bits(i64)} bits, "
              f"abi_size={td.abi_size_of_type(i64)} bytes, "
              f"alignment={td.abi_alignment_of_type(i64)}")
        print(f";   f32: size={td.size_of_type_in_bits(f32)} bits, "
              f"abi_size={td.abi_size_of_type(f32)} bytes, "
              f"alignment={td.abi_alignment_of_type(f32)}")
        print(f";   f64: size={td.size_of_type_in_bits(f64)} bits, "
              f"abi_size={td.abi_size_of_type(f64)} bytes, "
              f"alignment={td.abi_alignment_of_type(f64)}")
        print(f";   ptr: size={td.size_of_type_in_bits(ptr)} bits, "
              f"abi_size={td.abi_size_of_type(ptr)} bytes, "
              f"alignment={td.abi_alignment_of_type(ptr)}")

        # Struct type
        struct_ty = ctx.types.struct([i8, i32, f64])
        print(f";   struct {{i8, i32, f64}}: size={td.size_of_type_in_bits(struct_ty)} bits, "
              f"abi_size={td.abi_size_of_type(struct_ty)} bytes")

        # Packed struct
        packed_struct = ctx.types.struct([i8, i32, f64], packed=True)
        print(f";   packed struct {{i8, i32, f64}}: size={td.size_of_type_in_bits(packed_struct)} bits, "
              f"abi_size={td.abi_size_of_type(packed_struct)} bytes")

        # ==========================================================================
        # Code generation test
        # ==========================================================================
        print(";")
        print("; Code generation:")

        # Create a simple module with a function
        with ctx.create_module("codegen_test") as mod:
            mod.target_triple = default_triple

            # Create: i32 @add(i32 %a, i32 %b) { return a + b }
            fn_ty = ctx.types.function(i32, [i32, i32])
            fn = mod.add_function("add", fn_ty)

            entry = fn.append_basic_block("entry")
            with entry.create_builder() as builder:
                a = fn.get_param(0)
                b = fn.get_param(1)
                sum_val = builder.add(a, b, "sum")
                builder.ret(sum_val)

            # Verify module
            if not mod.verify():
                print(f"; ERROR: Module verification failed: {mod.get_verification_error()}",
                      file=sys.stderr)
                return 1

            # Emit to memory buffer (object file)
            obj_bytes = tm.emit_to_memory_buffer(mod, llvm.CodeGenFileType.ObjectFile)
            obj_size = len(obj_bytes)
            print(f";   Object file size: {obj_size} bytes")
            print(f";   Object file generated: {'yes' if obj_size > 0 else 'no'}")

            # Emit to memory buffer (assembly)
            asm_bytes = tm.emit_to_memory_buffer(mod, llvm.CodeGenFileType.AssemblyFile)
            asm_size = len(asm_bytes)
            print(f";   Assembly size: {asm_size} bytes")
            print(f";   Assembly generated: {'yes' if asm_size > 0 else 'no'}")

            # Print first few lines of assembly (portable check)
            asm_text = asm_bytes.decode('utf-8', errors='replace')
            print(";")
            print("; Assembly output (first 200 chars or first 5 lines):")
            lines = asm_text.split('\n')
            printed = 0
            for i, line in enumerate(lines[:5]):
                if printed + len(line) + 1 > 200:
                    break
                print(line)
                printed += len(line) + 1
            print("; ... (truncated)")

    print(";")
    print("; Test completed successfully")

    return 0


if __name__ == "__main__":
    sys.exit(main())
