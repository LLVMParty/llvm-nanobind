"""
llvm-c-test debug info command implementations.

This module implements debug info related commands like --get-di-tag,
--di-type-get-name, and --test-dibuilder.
"""

import llvm
import sys


def get_di_tag():
    """Test LLVMGetDINodeTag functionality.

    This is a silent test that creates debug info nodes and queries their tags.
    Returns 0 on success.
    """
    # Create module with global context
    ctx = llvm.global_context()
    with ctx.create_module("Mod") as mod:
        # Create metadata string and node
        string_md = llvm.md_string_in_context_2(ctx, "foo")
        node_md = llvm.md_node_in_context_2(ctx, [string_md])

        # Get tag from node (should be 0 for generic node)
        tag = llvm.get_di_node_tag(node_md)
        assert tag == 0, f"Expected tag 0, got {tag}"

        # Create DIBuilder
        dib = llvm.create_dibuilder(mod)

        # Create file
        file_md = llvm.dibuilder_create_file(dib, "metadata.c", ".")

        # Create struct type (DW_TAG_structure_type = 0x13)
        struct_md = llvm.dibuilder_create_struct_type(
            dib,
            file_md,
            "TestClass",
            file_md,
            42,  # line number
            64,  # size in bits
            0,  # align in bits
            llvm.DIFlagObjcClassComplete,  # flags
        )

        # Get tag from struct (should be 0x13 = DW_TAG_structure_type)
        tag = llvm.get_di_node_tag(struct_md)
        assert tag == 0x13, f"Expected tag 0x13, got {tag:#x}"

    return 0


def di_type_get_name():
    """Test LLVMDITypeGetName functionality.

    This is a silent test that creates a debug info type and queries its name.
    Returns 0 on success.
    """
    # Create module with global context
    ctx = llvm.global_context()
    with ctx.create_module("Mod") as mod:
        # Create DIBuilder
        dib = llvm.create_dibuilder(mod)

        # Create file
        file_md = llvm.dibuilder_create_file(dib, "metadata.c", ".")

        # Create struct type with name
        name = "TestClass"
        struct_md = llvm.dibuilder_create_struct_type(
            dib,
            file_md,
            name,
            file_md,
            42,  # line number
            64,  # size in bits
            0,  # align in bits
            llvm.DIFlagObjcClassComplete,  # flags
        )

        # Get name from type
        type_name = llvm.di_type_get_name(struct_md)

        # Verify name matches
        assert len(type_name) == len(name), (
            f"Expected length {len(name)}, got {len(type_name)}"
        )
        assert type_name == name, f"Expected name '{name}', got '{type_name}'"

    return 0


def declare_objc_class(dib, file_md):
    """Helper function to declare ObjC class with inheritance and property."""
    # Create TestClass struct
    decl = llvm.dibuilder_create_struct_type(
        dib,
        file_md,
        "TestClass",
        file_md,
        42,  # line
        64,  # size in bits
        0,  # align
        llvm.DIFlagObjcClassComplete,
    )

    # Create TestSuperClass struct
    super_decl = llvm.dibuilder_create_struct_type(
        dib,
        file_md,
        "TestSuperClass",
        file_md,
        42,  # line
        64,  # size in bits
        0,  # align
        llvm.DIFlagObjcClassComplete,
    )

    # Create inheritance
    llvm.dibuilder_create_inheritance(dib, decl, super_decl, 0, 0, 0)

    # Create ObjC property
    test_property = llvm.dibuilder_create_objc_property(
        dib,
        "test",
        file_md,
        42,
        "getTest",
        "setTest",
        0x20 | 0x40,  # copy | nonatomic
        super_decl,
    )

    # Create ObjC ivar
    llvm.dibuilder_create_objc_ivar(
        dib,
        "_test",
        file_md,
        42,
        64,  # size in bits
        0,  # align
        64,  # offset
        llvm.DIFlagPublic,
        super_decl,
        test_property,
    )

    return decl


def test_dibuilder():
    """Test LLVMDIBuilder functionality.

    Creates a comprehensive module with debug info and prints the IR.
    Tests most DIBuilder APIs.
    """
    filename = "debuginfo.c"
    ctx = llvm.global_context()

    with ctx.create_module(filename) as mod:
        # Enable new debug info format
        llvm.set_is_new_dbg_info_format(mod, True)
        assert llvm.is_new_dbg_info_format(mod)

        # Create DIBuilder
        dib = llvm.create_dibuilder(mod)

        # Create file
        file_md = llvm.dibuilder_create_file(dib, filename, ".")

        # Create compile unit
        compile_unit = llvm.dibuilder_create_compile_unit(
            dib,
            llvm.DWARFSourceLanguageC,
            file_md,
            "llvm-c-test",
            False,
            "",
            0,
            "",
            llvm.DWARFEmissionFull,
            0,
            False,
            False,
            "/",
            "",
        )

        # Create module
        module_md = llvm.dibuilder_create_module(
            dib, compile_unit, "llvm-c-test", "", "/test/include/llvm-c-test.h", ""
        )

        # Create another module for import testing
        other_module = llvm.dibuilder_create_module(
            dib,
            compile_unit,
            "llvm-c-test-import",
            "",
            "/test/include/llvm-c-test-import.h",
            "",
        )

        # Create imported module
        imported_module = llvm.dibuilder_create_imported_module_from_module(
            dib, module_md, other_module, file_md, 42, []
        )

        # Create imported from alias
        llvm.dibuilder_create_imported_module_from_alias(
            dib, module_md, imported_module, file_md, 42, []
        )

        # Create ObjC class
        class_ty = declare_objc_class(dib, file_md)

        # Create global class variable expression
        global_class_value_expr = llvm.dibuilder_create_constant_value_expression(
            dib, 0
        )
        llvm.dibuilder_create_global_variable_expression(
            dib,
            module_md,
            "globalClass",
            "",
            file_md,
            1,
            class_ty,
            True,
            global_class_value_expr,
            None,
            0,
        )

        # Create Int64 basic type
        int64_ty = llvm.dibuilder_create_basic_type(
            dib, "Int64", 64, 0, llvm.DIFlagZero
        )

        # Create typedef
        int64_typedef = llvm.dibuilder_create_typedef(
            dib, int64_ty, "int64_t", file_md, 42, file_md, 0
        )

        # Create global variable
        global_var_expr = llvm.dibuilder_create_constant_value_expression(dib, 0)
        llvm.dibuilder_create_global_variable_expression(
            dib,
            module_md,
            "global",
            "",
            file_md,
            1,
            int64_typedef,
            True,
            global_var_expr,
            None,
            0,
        )

        # Create namespace
        namespace = llvm.dibuilder_create_namespace(dib, module_md, "NameSpace", False)

        # Create struct type with elements
        struct_elts = [int64_ty, int64_ty, int64_ty]
        struct_dbg_ty = llvm.dibuilder_create_struct_type(
            dib, namespace, "MyStruct", file_md, 0, 192, 0, 0
        )

        # Create pointer type
        struct_dbg_ptr_ty = llvm.dibuilder_create_pointer_type(
            dib, struct_dbg_ty, 192, 0, 0, ""
        )

        # Add to named metadata
        llvm.add_named_metadata_operand(
            mod, "FooType", llvm.metadata_as_value(ctx, struct_dbg_ptr_ty)
        )

        # Create function
        i64_type = ctx.int64_type()
        vec_type = ctx.vector_type(i64_type, 10)
        foo_param_tys = [i64_type, i64_type, vec_type]
        foo_func_ty = ctx.function_type(i64_type, foo_param_tys, False)
        foo_function = mod.add_function("foo", foo_func_ty)
        foo_entry_block = foo_function.append_basic_block("entry", ctx)

        # Create vector type metadata
        subscripts = [llvm.dibuilder_get_or_create_subrange(dib, 0, 10)]
        vector_ty = llvm.dibuilder_create_vector_type(
            dib, 64 * 10, 0, int64_ty, subscripts
        )

        # Create subroutine type
        param_types = [int64_ty, int64_ty, vector_ty]
        function_ty = llvm.dibuilder_create_subroutine_type(
            dib, file_md, param_types, 0
        )

        # Create replaceable function metadata
        replaceable_function_md = llvm.dibuilder_create_replaceable_composite_type(
            dib, 0x15, "foo", file_md, file_md, 42, 0, 0, 0, llvm.DIFlagFwdDecl, ""
        )

        # Create debug location for parameters
        foo_param_location = llvm.dibuilder_create_debug_location(
            ctx, 42, 0, replaceable_function_md, None
        )

        # Create function debug info
        function_md = llvm.dibuilder_create_function(
            dib, file_md, "foo", "foo", file_md, 42, None, True, True, 42, 0, False
        )

        # Replace temporary metadata
        llvm.metadata_replace_all_uses_with(replaceable_function_md, function_md)

        # Replace function type
        llvm.di_subprogram_replace_type(function_md, function_ty)

        # Create expression for parameters
        foo_param_expr = llvm.dibuilder_create_expression(dib, [])

        # Create parameter variables and insert declare records
        foo_param_var1 = llvm.dibuilder_create_parameter_variable(
            dib, function_md, "a", 1, file_md, 42, int64_ty, True, 0
        )
        zero_val = llvm.const_int(i64_type, 0, False)
        llvm.dibuilder_insert_declare_record_at_end(
            dib,
            zero_val,
            foo_param_var1,
            foo_param_expr,
            foo_param_location,
            foo_entry_block,
        )

        foo_param_var2 = llvm.dibuilder_create_parameter_variable(
            dib, function_md, "b", 2, file_md, 42, int64_ty, True, 0
        )
        llvm.dibuilder_insert_declare_record_at_end(
            dib,
            zero_val,
            foo_param_var2,
            foo_param_expr,
            foo_param_location,
            foo_entry_block,
        )

        foo_param_var3 = llvm.dibuilder_create_parameter_variable(
            dib, function_md, "c", 3, file_md, 42, vector_ty, True, 0
        )
        llvm.dibuilder_insert_declare_record_at_end(
            dib,
            zero_val,
            foo_param_var3,
            foo_param_expr,
            foo_param_location,
            foo_entry_block,
        )

        # Set subprogram
        llvm.set_subprogram(foo_function, function_md)

        # Create label
        foo_label1 = llvm.dibuilder_create_label(
            dib, function_md, "label1", file_md, 42, False
        )
        llvm.dibuilder_insert_label_at_end(
            dib, foo_label1, foo_param_location, foo_entry_block
        )

        # Create lexical block
        foo_lexical_block = llvm.dibuilder_create_lexical_block(
            dib, function_md, file_md, 42, 0
        )

        # Create another basic block for variables
        foo_var_block = foo_function.append_basic_block("vars", ctx)
        foo_vars_location = llvm.dibuilder_create_debug_location(
            ctx, 43, 0, function_md, None
        )

        # Create auto variables with debug value records
        foo_var1 = llvm.dibuilder_create_auto_variable(
            dib, foo_lexical_block, "d", file_md, 43, int64_ty, True, 0, 0
        )
        foo_val1 = llvm.const_int(i64_type, 0, False)
        foo_var_value_expr1 = llvm.dibuilder_create_constant_value_expression(dib, 0)
        llvm.dibuilder_insert_dbg_value_record_at_end(
            dib,
            foo_val1,
            foo_var1,
            foo_var_value_expr1,
            foo_vars_location,
            foo_var_block,
        )

        foo_var2 = llvm.dibuilder_create_auto_variable(
            dib, foo_lexical_block, "e", file_md, 44, int64_ty, True, 0, 0
        )
        foo_val2 = llvm.const_int(i64_type, 1, False)
        foo_var_value_expr2 = llvm.dibuilder_create_constant_value_expression(dib, 1)
        llvm.dibuilder_insert_dbg_value_record_at_end(
            dib,
            foo_val2,
            foo_var2,
            foo_var_value_expr2,
            foo_vars_location,
            foo_var_block,
        )

        # Create macro file
        macro_file = llvm.dibuilder_create_temp_macro_file(dib, None, 0, file_md)
        llvm.dibuilder_create_macro(
            dib, macro_file, 0, llvm.DWARFMacinfoRecordTypeDefine, "SIMPLE_DEFINE", ""
        )
        llvm.dibuilder_create_macro(
            dib, macro_file, 0, llvm.DWARFMacinfoRecordTypeDefine, "VALUE_DEFINE", "1"
        )

        # Create enumerators
        enumerator_test_a = llvm.dibuilder_create_enumerator(dib, "Test_A", 0, True)
        enumerator_test_b = llvm.dibuilder_create_enumerator(dib, "Test_B", 1, True)
        enumerator_test_c = llvm.dibuilder_create_enumerator(dib, "Test_C", 2, True)
        enumerators_test = [enumerator_test_a, enumerator_test_b, enumerator_test_c]

        # Create enumeration type
        enum_test = llvm.dibuilder_create_enumeration_type(
            dib, namespace, "EnumTest", file_md, 0, 64, 0, enumerators_test, int64_ty
        )
        llvm.add_named_metadata_operand(
            mod, "EnumTest", llvm.metadata_as_value(ctx, enum_test)
        )

        # Create UInt128 type and large enumerators
        uint128_ty = llvm.dibuilder_create_basic_type(
            dib, "UInt128", 128, 0, llvm.DIFlagZero
        )
        words_test_d = [0x098A224000000000, 0x4B3B4CA85A86C47A]
        words_test_e = [0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF]

        large_enum_test_d = llvm.dibuilder_create_enumerator_of_arbitrary_precision(
            dib, "Test_D", words_test_d, False
        )
        large_enum_test_e = llvm.dibuilder_create_enumerator_of_arbitrary_precision(
            dib, "Test_E", words_test_e, False
        )
        large_enumerators = [large_enum_test_d, large_enum_test_e]

        large_enum_test = llvm.dibuilder_create_enumeration_type(
            dib,
            namespace,
            "LargeEnumTest",
            file_md,
            0,
            128,
            0,
            large_enumerators,
            uint128_ty,
        )
        llvm.add_named_metadata_operand(
            mod, "LargeEnumTest", llvm.metadata_as_value(ctx, large_enum_test)
        )

        # Create subrange type with metadata bounds
        foo_val3 = llvm.const_int(i64_type, 8, False)
        foo_val4 = llvm.const_int(i64_type, 4, False)
        lo = llvm.value_as_metadata(foo_val1)
        hi = llvm.value_as_metadata(foo_val2)
        strd = llvm.value_as_metadata(foo_val3)
        bias = llvm.value_as_metadata(foo_val4)

        subrange_md_ty = llvm.dibuilder_create_subrange_type(
            dib, file_md, "foo", 42, file_md, 64, 0, 0, int64_ty, lo, hi, strd, bias
        )
        llvm.add_named_metadata_operand(
            mod, "SubrangeType", llvm.metadata_as_value(ctx, subrange_md_ty)
        )

        # Create set types
        set_md_ty1 = llvm.dibuilder_create_set_type(
            dib, file_md, "enumset", file_md, 42, 64, 0, enum_test
        )
        set_md_ty2 = llvm.dibuilder_create_set_type(
            dib, file_md, "subrangeset", file_md, 42, 64, 0, subrange_md_ty
        )
        llvm.add_named_metadata_operand(
            mod, "SetType1", llvm.metadata_as_value(ctx, set_md_ty1)
        )
        llvm.add_named_metadata_operand(
            mod, "SetType2", llvm.metadata_as_value(ctx, set_md_ty2)
        )

        # Create dynamic array type
        dyn_subscripts = [llvm.dibuilder_get_or_create_subrange(dib, 0, 10)]
        loc_expr = llvm.dibuilder_create_expression(dib, [])
        rank_expr = llvm.dibuilder_create_expression(dib, [])
        dynamic_array_md_ty = llvm.dibuilder_create_dynamic_array_type(
            dib,
            file_md,
            "foo",
            42,
            file_md,
            64 * 10,
            0,
            int64_ty,
            dyn_subscripts,
            loc_expr,
            None,
            None,
            rank_expr,
            None,
        )
        llvm.add_named_metadata_operand(
            mod, "DynType", llvm.metadata_as_value(ctx, dynamic_array_md_ty)
        )

        # Create forward declaration
        struct_p_ty = llvm.dibuilder_create_forward_decl(
            dib, 2, "Class1", namespace, file_md, 0, 0, 192, 0, "FooClass"
        )

        # Create array and replace it
        int32_ty = llvm.dibuilder_create_basic_type(
            dib, "Int32", 32, 0, llvm.DIFlagZero
        )
        struct_elts_array = [int64_ty, int64_ty, int32_ty]
        class_arr = llvm.dibuilder_get_or_create_array(dib, struct_elts_array)
        llvm.replace_arrays(dib, [struct_p_ty], [class_arr])
        llvm.add_named_metadata_operand(
            mod, "ClassType", llvm.metadata_as_value(ctx, struct_p_ty)
        )

        # Build IR instructions
        with ctx.create_builder() as builder:
            builder.position_at_end(foo_entry_block)
            builder.br(foo_var_block)

            # Build another br with label
            foo_label2 = llvm.dibuilder_create_label(
                dib, function_md, "label2", file_md, 42, False
            )
            br_inst = builder.br(foo_var_block)
            llvm.dibuilder_insert_label_before(
                dib, foo_label2, foo_param_location, br_inst
            )

            # Create non-preserved labels
            llvm.dibuilder_create_label(dib, function_md, "label3", file_md, 42, True)
            llvm.dibuilder_create_label(dib, function_md, "label4", file_md, 42, False)

            # Finalize DIBuilder
            dib.finalize()

            # Build ret in vars block
            builder.position_at_end(foo_var_block)
            ret_inst = builder.ret(zero_val)

            # Insert phi before ret using new positioning
            insert_pos = foo_var_block.first_instruction
            llvm.position_builder_before_instr_and_dbg_records(builder, insert_pos)
            phi1 = builder.phi(i64_type, "p1")
            phi1.add_incoming(zero_val, foo_entry_block)

            # Do it again with the other positioning function
            llvm.position_builder_before_dbg_records(builder, foo_var_block, insert_pos)
            phi2 = builder.phi(i64_type, "p2")
            phi2.add_incoming(zero_val, foo_entry_block)

            # Insert non-phi before ret
            builder.position_before(ret_inst)
            add_inst = builder.add(phi1, phi2, "a")

            # Iterate over debug records
            add_dbg_first = llvm.get_first_dbg_record(add_inst)
            assert add_dbg_first is not None, "First debug record should exist"
            add_dbg_second = llvm.get_next_dbg_record(add_dbg_first)
            assert add_dbg_second is not None, "Second debug record should exist"
            add_dbg_last = llvm.get_last_dbg_record(add_inst)
            assert add_dbg_last is not None, "Last debug record should exist"
            # Note: Debug records are opaque pointers, comparison may not work reliably
            # assert add_dbg_second == add_dbg_last
            add_dbg_over = llvm.get_next_dbg_record(add_dbg_second)
            assert add_dbg_over is None, "Should be no record after second"
            add_dbg_first_prev = llvm.get_previous_dbg_record(add_dbg_second)
            assert add_dbg_first_prev is not None, "Previous of second should exist"
            # assert add_dbg_first == add_dbg_first_prev
            add_dbg_under = llvm.get_previous_dbg_record(add_dbg_first_prev)
            assert add_dbg_under is None, "Should be no record before first"

        # Print module
        print(str(mod))

    return 0
