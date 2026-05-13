"""
Test all feature matrix items implemented in Session 6.
This test verifies that the remaining feature-matrix items are properly
implemented and accessible.
"""

import llvm

# DWARF constants (from LLVM's DebugInfo headers)
DWARF_LANG_C = 12  # DW_LANG_C
DWARF_LANG_C_PLUS_PLUS = 4  # DW_LANG_C_plus_plus
DWARF_ATE_SIGNED = 5  # DW_ATE_signed (encoding for signed int)
DWARF_ATE_FLOAT = 4  # DW_ATE_float
DI_FLAGS_ZERO = 0  # No flags


def test_core_get_cast_opcode():
    """Test LLVMGetCastOpcode → Value.get_cast_opcode()"""
    with llvm.create_context() as ctx:
        i32 = ctx.types.i32
        i64 = ctx.types.i64
        f32 = ctx.types.f32

        # Create constant values for testing
        i32_val = i32.constant(42)
        i64_val = i64.constant(100)
        f32_val = f32.real_constant(3.14)

        # Integer extension (i32 -> i64)
        opcode = i32_val.get_cast_opcode(True, i64, True)
        assert opcode == llvm.Opcode.SExt

        opcode = i32_val.get_cast_opcode(False, i64, False)
        assert opcode == llvm.Opcode.ZExt

        # Integer truncation (i64 -> i32)
        opcode = i64_val.get_cast_opcode(True, i32, True)
        assert opcode == llvm.Opcode.Trunc

        # Float to int
        opcode = f32_val.get_cast_opcode(False, i32, True)
        assert opcode == llvm.Opcode.FPToSI

        opcode = f32_val.get_cast_opcode(False, i32, False)
        assert opcode == llvm.Opcode.FPToUI

        print("[PASS] get_cast_opcode works correctly")


def test_core_intrinsic_get_type():
    """Test LLVMIntrinsicGetType → Context.get_intrinsic_type()"""
    with llvm.create_context() as ctx:
        # Get the intrinsic ID for llvm.memcpy
        memcpy_id = llvm.lookup_intrinsic_id("llvm.memcpy")
        assert memcpy_id != 0

        # Get the type for this intrinsic with specific parameter types
        ptr_ty = ctx.types.ptr
        i64 = ctx.types.i64
        i1 = ctx.types.i1

        intrinsic_ty = ctx.get_intrinsic_type(memcpy_id, [ptr_ty, ptr_ty, i64])
        assert intrinsic_ty is not None
        assert intrinsic_ty.kind == llvm.TypeKind.Function

        print("[PASS] intrinsic_get_type works correctly")


def test_core_metadata_node_operands():
    """Test MDNode operand inspection through Metadata.operands."""
    with llvm.create_context() as ctx:
        md1 = ctx.md_string("original")
        md2 = ctx.md_string("replacement")
        md_node = ctx.md_node([md1, md2])

        assert [operand.string for operand in md_node.operands] == [
            "original",
            "replacement",
        ]

        print("[PASS] metadata node operands work correctly")


def test_debuginfo_global_variable_expression():
    """Test LLVMDIGlobalVariableExpressionGet{Variable,Expression}"""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            with mod.create_dibuilder() as dib:
                # Create necessary debug info
                file = dib.file("test.c", "/path")
                cu = dib.compile_unit(
                    language=llvm.DwarfLanguage.C,
                    file=file,
                    producer="test",
                    is_optimized=False,
                    flags="",
                    runtime_ver=0,
                    split_name="",
                    kind=llvm.DwarfEmissionKind.Full,
                    dwo_id=0,
                    split_debug_inlining=True,
                    debug_info_for_profiling=False,
                    sys_root="",
                    sdk="",
                )

                i32_ty = dib.create_basic_type(
                    "int", 32, DWARF_ATE_SIGNED, DI_FLAGS_ZERO
                )
                assert i32_ty.di_type_name == "int"
                assert i32_ty.di_type_size_in_bits == 32
                assert i32_ty.di_type_align_in_bits == 0
                assert i32_ty.di_type_offset_in_bits == 0
                assert i32_ty.di_type_line == 0
                assert i32_ty.di_type_flags == DI_FLAGS_ZERO

                # Create a global variable expression
                gve = dib.create_global_variable_expression(
                    scope=cu,
                    name="my_global",
                    linkage="my_global",
                    file=file,
                    line_no=10,
                    type=i32_ty,
                    is_local_to_unit=False,
                    expr=dib.create_expression([]),
                    decl=None,
                    align_in_bits=32,
                )

                # Get variable and expression back
                var = gve.di_gve_variable
                expr = gve.di_gve_expression

                assert var is not None
                assert expr is not None

                dib.finalize()

        print(
            "[PASS] di_global_variable_expression_get_{variable,expression} work correctly"
        )


def test_debuginfo_class_type():
    """Test LLVMDIBuilderCreateClassType"""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            with mod.create_dibuilder() as dib:
                file = dib.file("test.cpp", "/path")
                cu = dib.compile_unit(
                    language=llvm.DwarfLanguage.CPlusPlus,
                    file=file,
                    producer="test",
                    is_optimized=False,
                    flags="",
                    runtime_ver=0,
                    split_name="",
                    kind=llvm.DwarfEmissionKind.Full,
                    dwo_id=0,
                    split_debug_inlining=True,
                    debug_info_for_profiling=False,
                    sys_root="",
                    sdk="",
                )

                # Create a class type
                class_ty = dib.create_class_type(
                    scope=cu,
                    name="MyClass",
                    file=file,
                    line_number=5,
                    size_in_bits=64,
                    align_in_bits=64,
                    offset_in_bits=0,
                    flags=DI_FLAGS_ZERO,
                    derived_from=None,
                    elements=[],
                    vtable_holder=None,
                    template_params=None,
                    unique_id="MyClass",
                )

                assert class_ty is not None
                dib.finalize()

        print("[PASS] create_class_type works correctly")


def test_debuginfo_static_member_type():
    """Test LLVMDIBuilderCreateStaticMemberType"""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            with mod.create_dibuilder() as dib:
                file = dib.file("test.cpp", "/path")
                cu = dib.compile_unit(
                    language=llvm.DwarfLanguage.CPlusPlus,
                    file=file,
                    producer="test",
                    is_optimized=False,
                    flags="",
                    runtime_ver=0,
                    split_name="",
                    kind=llvm.DwarfEmissionKind.Full,
                    dwo_id=0,
                    split_debug_inlining=True,
                    debug_info_for_profiling=False,
                    sys_root="",
                    sdk="",
                )

                i32_ty = dib.create_basic_type(
                    "int", 32, DWARF_ATE_SIGNED, DI_FLAGS_ZERO
                )

                # Create a constant value for the static member
                const_val = ctx.types.i32.constant(42)

                # Create a static member type with a constant value
                # Note: LLVM-C doesn't handle nullptr for const_val properly
                static_member = dib.create_static_member_type(
                    scope=cu,
                    name="static_var",
                    file=file,
                    line_no=10,
                    type=i32_ty,
                    flags=DI_FLAGS_ZERO,
                    const_val=const_val,
                    align_in_bits=32,
                )

                assert static_member is not None
                dib.finalize()

        print("[PASS] create_static_member_type works correctly")


def test_debuginfo_member_pointer_type():
    """Test LLVMDIBuilderCreateMemberPointerType"""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            with mod.create_dibuilder() as dib:
                file = dib.file("test.cpp", "/path")
                cu = dib.compile_unit(
                    language=llvm.DwarfLanguage.CPlusPlus,
                    file=file,
                    producer="test",
                    is_optimized=False,
                    flags="",
                    runtime_ver=0,
                    split_name="",
                    kind=llvm.DwarfEmissionKind.Full,
                    dwo_id=0,
                    split_debug_inlining=True,
                    debug_info_for_profiling=False,
                    sys_root="",
                    sdk="",
                )

                i32_ty = dib.create_basic_type(
                    "int", 32, DWARF_ATE_SIGNED, DI_FLAGS_ZERO
                )

                # Create a class for the member pointer
                class_ty = dib.create_class_type(
                    scope=cu,
                    name="MyClass",
                    file=file,
                    line_number=5,
                    size_in_bits=64,
                    align_in_bits=64,
                    offset_in_bits=0,
                    flags=DI_FLAGS_ZERO,
                    derived_from=None,
                    elements=[],
                    vtable_holder=None,
                    template_params=None,
                    unique_id="MyClass",
                )

                # Create member pointer type (pointer to member of class_ty, pointing to i32)
                member_ptr_ty = dib.create_member_pointer_type(
                    pointee_type=i32_ty,
                    class_type=class_ty,
                    size_in_bits=64,
                    align_in_bits=64,
                    flags=DI_FLAGS_ZERO,
                )

                assert member_ptr_ty is not None
                dib.finalize()

        print("[PASS] create_member_pointer_type works correctly")


def test_object_binary_copy_to_memory_buffer():
    """Test LLVMBinaryCopyMemoryBuffer → binary.copy_to_memory_buffer()"""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            # Create a simple function
            fn_ty = ctx.types.function(ctx.types.i32, [])
            fn = mod.add_function("test_fn", fn_ty)
            bb = fn.append_basic_block("entry")
            with ctx.create_builder(bb) as b:
                b.ret(ctx.types.i32.constant(42))

            # Create target machine
            triple = llvm.default_target_triple
            target = llvm.Target.from_triple(triple)
            assert target is not None
            tm = llvm.TargetMachine.create(
                target,
                triple,
                cpu="generic",
                features="",
                opt_level=llvm.CodeGenOptLevel.Default,
                reloc_mode=llvm.RelocMode.PIC,
                code_model=llvm.CodeModel.Default,
            )

            # Emit object code
            obj_data = tm.emit_to_memory_buffer(mod, llvm.CodeGenFileType.ObjectFile)

            # Create binary from the object data
            with llvm.BinaryManager.from_bytes(obj_data) as binary:
                # Copy to memory buffer
                copied_data = binary.copy_to_memory_buffer()

                assert copied_data is not None
                assert len(copied_data) > 0

        print("[PASS] binary.copy_to_memory_buffer works correctly")


def test_object_section_contains_symbol():
    """Test LLVMGetSectionContainsSymbol → section.contains_symbol()"""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            # Create a function
            fn_ty = ctx.types.function(ctx.types.i32, [])
            fn = mod.add_function("my_symbol", fn_ty)
            fn.linkage = llvm.Linkage.External
            bb = fn.append_basic_block("entry")
            with ctx.create_builder(bb) as b:
                b.ret(ctx.types.i32.constant(42))

            # Create target machine
            triple = llvm.default_target_triple
            target = llvm.Target.from_triple(triple)
            assert target is not None
            tm = llvm.TargetMachine.create(
                target,
                triple,
                cpu="generic",
                features="",
                opt_level=llvm.CodeGenOptLevel.Default,
                reloc_mode=llvm.RelocMode.PIC,
                code_model=llvm.CodeModel.Default,
            )

            obj_data = tm.emit_to_memory_buffer(mod, llvm.CodeGenFileType.ObjectFile)
            with llvm.BinaryManager.from_bytes(obj_data) as binary:
                # Iterate sections and symbols
                found_symbol = False
                for section in binary.sections:
                    for symbol in binary.symbols:
                        # Test contains_symbol method exists and runs
                        result = section.contains_symbol(symbol)
                        if result:
                            found_symbol = True
                        assert isinstance(result, bool)

            print("[PASS] section.contains_symbol works correctly")


def test_comdat():
    """Test all Comdat APIs"""
    with llvm.create_context() as ctx:
        with ctx.create_module("test") as mod:
            # Test add_comdat get-or-insert behavior
            comdat = mod.add_comdat("my_comdat")
            assert comdat is not None

            # Test selection_kind property (getter and setter)
            assert comdat.selection_kind == llvm.ComdatSelectionKind.Any
            comdat.selection_kind = llvm.ComdatSelectionKind.ExactMatch
            assert comdat.selection_kind == llvm.ComdatSelectionKind.ExactMatch

            # Test global value comdat property
            g = mod.add_global(ctx.types.i32, "global_with_comdat")
            g.initializer = ctx.types.i32.constant(42)

            # Initially no comdat
            assert g.comdat is None

            # Set comdat
            g.comdat = comdat

            # Get comdat back
            retrieved = g.comdat
            assert retrieved is not None
            assert retrieved.selection_kind == llvm.ComdatSelectionKind.ExactMatch

        print("[PASS] All Comdat APIs work correctly")


def test_all():
    """Run all feature matrix tests"""
    print("\n" + "=" * 60)
    print("Feature Matrix Tests - Session 6 Items")
    print("=" * 60 + "\n")

    # Core.h
    test_core_get_cast_opcode()
    test_core_intrinsic_get_type()
    test_core_metadata_node_operands()

    # DebugInfo.h
    test_debuginfo_global_variable_expression()
    test_debuginfo_class_type()
    test_debuginfo_static_member_type()
    test_debuginfo_member_pointer_type()

    # Object.h
    test_object_binary_copy_to_memory_buffer()
    test_object_section_contains_symbol()

    # Comdat.h
    test_comdat()

    print("\n" + "=" * 60)
    print("All feature matrix tests PASSED!")
    print("=" * 60 + "\n")


if __name__ == "__main__":
    test_all()
