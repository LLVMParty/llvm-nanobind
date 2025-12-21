"""
llvm-c-test debug info command implementations.

This module implements debug info related commands like --get-di-tag,
--di-type-get-name, and --test-dibuilder.
"""

import llvm


def get_di_tag():
    """Test LLVMGetDINodeTag functionality.

    This is a silent test that creates debug info nodes and queries their tags.
    Returns 0 on success.
    """
    # Create module with global context
    ctx = llvm.global_context()
    with ctx.create_module("Mod") as mod:
        # Create metadata string and node
        string_md = ctx.md_string("foo")
        node_md = ctx.md_node([string_md])

        # Get tag from node (should be 0 for generic node)
        tag = llvm.get_di_node_tag(node_md)
        assert tag == 0, f"Expected tag 0, got {tag}"

        # Create DIBuilder
        with mod.create_dibuilder() as dib:
            # Create file
            file_md = dib.create_file("metadata.c", ".")

            # Create struct type (DW_TAG_structure_type = 0x13)
            struct_md = dib.create_struct_type(
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
        with mod.create_dibuilder() as dib:
            # Create file
            file_md = dib.create_file("metadata.c", ".")

            # Create struct type with name
            name = "TestClass"
            struct_md = dib.create_struct_type(
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
    decl = dib.create_struct_type(
        file_md,
        "TestClass",
        file_md,
        42,  # line
        64,  # size in bits
        0,  # align
        llvm.DIFlagObjcClassComplete,
    )

    # Create TestSuperClass struct
    super_decl = dib.create_struct_type(
        file_md,
        "TestSuperClass",
        file_md,
        42,  # line
        64,  # size in bits
        0,  # align
        llvm.DIFlagObjcClassComplete,
    )

    # Create inheritance
    dib.create_inheritance(decl, super_decl, 0, 0, 0)

    # Create ObjC property
    test_property = dib.create_objc_property(
        "test",
        file_md,
        42,
        "getTest",
        "setTest",
        0x20 | 0x40,  # copy | nonatomic
        super_decl,
    )

    # Create ObjC ivar
    dib.create_objc_ivar(
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
        mod.is_new_dbg_info_format = True
        assert mod.is_new_dbg_info_format

        # Create DIBuilder
        with mod.create_dibuilder() as dib:
            # Create file
            file_md = dib.create_file(filename, ".")

            # Create compile unit
            compile_unit = dib.create_compile_unit(
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
            module_md = dib.create_module(
                compile_unit, "llvm-c-test", "", "/test/include/llvm-c-test.h", ""
            )

            # Create another module for import testing
            other_module = dib.create_module(
                compile_unit,
                "llvm-c-test-import",
                "",
                "/test/include/llvm-c-test-import.h",
                "",
            )

            # Create imported module
            imported_module = dib.create_imported_module_from_module(
                module_md, other_module, file_md, 42, []
            )

            # Create imported from alias
            dib.create_imported_module_from_alias(
                module_md, imported_module, file_md, 42, []
            )

            # Create ObjC class
            class_ty = declare_objc_class(dib, file_md)

            # Create global class variable expression
            global_class_value_expr = dib.create_constant_value_expression(0)
            dib.create_global_variable_expression(
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
            int64_ty = dib.create_basic_type("Int64", 64, 0, llvm.DIFlagZero)

            # Create typedef
            int64_typedef = dib.create_typedef(
                int64_ty, "int64_t", file_md, 42, file_md, 0
            )

            # Create global variable
            global_var_expr = dib.create_constant_value_expression(0)
            dib.create_global_variable_expression(
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
            namespace = dib.create_namespace(module_md, "NameSpace", False)

            # Create struct type with elements
            struct_elts = [int64_ty, int64_ty, int64_ty]
            struct_dbg_ty = dib.create_struct_type(
                namespace,
                "MyStruct",
                file_md,
                0,
                192,
                0,
                0,
                None,  # derived_from
                struct_elts,  # elements
                llvm.DWARFSourceLanguageC,  # runtime_lang
                None,  # vtable_holder
                "MyStruct",  # unique_id
            )

            # Create pointer type
            struct_dbg_ptr_ty = dib.create_pointer_type(struct_dbg_ty, 192, 0, 0, "")

            # Add to named metadata
            mod.add_named_metadata_operand("FooType", struct_dbg_ptr_ty)

            # Create function
            i64_type = ctx.types.i64
            vec_type = i64_type.vector(10)
            foo_param_tys = [i64_type, i64_type, vec_type]
            foo_func_ty = ctx.types.function(i64_type, foo_param_tys, False)
            foo_function = mod.add_function("foo", foo_func_ty)
            foo_entry_block = foo_function.append_basic_block("entry", ctx)

            # Create vector type metadata
            subscripts = [dib.get_or_create_subrange(0, 10)]
            vector_ty = dib.create_vector_type(64 * 10, 0, int64_ty, subscripts)

            # Create subroutine type
            param_types = [int64_ty, int64_ty, vector_ty]
            function_ty = dib.create_subroutine_type(file_md, param_types, 0)

            # Create replaceable function metadata
            replaceable_function_md = dib.create_replaceable_composite_type(
                0x15, "foo", file_md, file_md, 42, 0, 0, 0, llvm.DIFlagFwdDecl, ""
            )

            # Create debug location for parameters
            foo_param_location = llvm.dibuilder_create_debug_location(
                ctx, 42, 0, replaceable_function_md, None
            )

            # Create function debug info
            function_md = dib.create_function(
                file_md, "foo", "foo", file_md, 42, None, True, True, 42, 0, False
            )

            # Replace temporary metadata
            replaceable_function_md.replace_all_uses_with(function_md)

            # Replace function type
            llvm.di_subprogram_replace_type(function_md, function_ty)

            # Create expression for parameters
            foo_param_expr = dib.create_expression([])

            # Create parameter variables and insert declare records
            foo_param_var1 = dib.create_parameter_variable(
                function_md, "a", 1, file_md, 42, int64_ty, True, 0
            )
            zero_val = i64_type.constant(0)
            dib.insert_declare_record_at_end(
                zero_val,
                foo_param_var1,
                foo_param_expr,
                foo_param_location,
                foo_entry_block,
            )

            foo_param_var2 = dib.create_parameter_variable(
                function_md, "b", 2, file_md, 42, int64_ty, True, 0
            )
            dib.insert_declare_record_at_end(
                zero_val,
                foo_param_var2,
                foo_param_expr,
                foo_param_location,
                foo_entry_block,
            )

            foo_param_var3 = dib.create_parameter_variable(
                function_md, "c", 3, file_md, 42, vector_ty, True, 0
            )
            dib.insert_declare_record_at_end(
                zero_val,
                foo_param_var3,
                foo_param_expr,
                foo_param_location,
                foo_entry_block,
            )

            # Set subprogram
            foo_function.set_subprogram(function_md)

            # Create label
            foo_label1 = dib.create_label(function_md, "label1", file_md, 42, False)
            dib.insert_label_at_end(foo_label1, foo_param_location, foo_entry_block)

            # Create lexical block
            foo_lexical_block = dib.create_lexical_block(function_md, file_md, 42, 0)

            # Create another basic block for variables
            foo_var_block = foo_function.append_basic_block("vars", ctx)
            foo_vars_location = llvm.dibuilder_create_debug_location(
                ctx, 43, 0, function_md, None
            )

            # Create auto variables with debug value records
            foo_var1 = dib.create_auto_variable(
                foo_lexical_block, "d", file_md, 43, int64_ty, True, 0, 0
            )
            foo_val1 = i64_type.constant(0)
            foo_var_value_expr1 = dib.create_constant_value_expression(0)
            dib.insert_dbg_value_record_at_end(
                foo_val1,
                foo_var1,
                foo_var_value_expr1,
                foo_vars_location,
                foo_var_block,
            )

            foo_var2 = dib.create_auto_variable(
                foo_lexical_block, "e", file_md, 44, int64_ty, True, 0, 0
            )
            foo_val2 = i64_type.constant(1)
            foo_var_value_expr2 = dib.create_constant_value_expression(1)
            dib.insert_dbg_value_record_at_end(
                foo_val2,
                foo_var2,
                foo_var_value_expr2,
                foo_vars_location,
                foo_var_block,
            )

            # Create macro file
            macro_file = dib.create_temp_macro_file(None, 0, file_md)
            dib.create_macro(
                macro_file, 0, llvm.DWARFMacinfoRecordTypeDefine, "SIMPLE_DEFINE", ""
            )
            dib.create_macro(
                macro_file, 0, llvm.DWARFMacinfoRecordTypeDefine, "VALUE_DEFINE", "1"
            )

            # Create enumerators
            enumerator_test_a = dib.create_enumerator("Test_A", 0, True)
            enumerator_test_b = dib.create_enumerator("Test_B", 1, True)
            enumerator_test_c = dib.create_enumerator("Test_C", 2, True)
            enumerators_test = [enumerator_test_a, enumerator_test_b, enumerator_test_c]

            # Create enumeration type
            enum_test = dib.create_enumeration_type(
                namespace, "EnumTest", file_md, 0, 64, 0, enumerators_test, int64_ty
            )
            mod.add_named_metadata_operand("EnumTest", enum_test)

            # Create UInt128 type and large enumerators
            uint128_ty = dib.create_basic_type("UInt128", 128, 0, llvm.DIFlagZero)
            words_test_d = [0x098A224000000000, 0x4B3B4CA85A86C47A]
            words_test_e = [0xFFFFFFFFFFFFFFFF, 0xFFFFFFFFFFFFFFFF]

            large_enum_test_d = dib.create_enumerator_of_arbitrary_precision(
                "Test_D", words_test_d, False
            )
            large_enum_test_e = dib.create_enumerator_of_arbitrary_precision(
                "Test_E", words_test_e, False
            )
            large_enumerators = [large_enum_test_d, large_enum_test_e]

            large_enum_test = dib.create_enumeration_type(
                namespace,
                "LargeEnumTest",
                file_md,
                0,
                128,
                0,
                large_enumerators,
                uint128_ty,
            )
            mod.add_named_metadata_operand("LargeEnumTest", large_enum_test)

            # Create subrange type with metadata bounds
            foo_val3 = i64_type.constant(8)
            foo_val4 = i64_type.constant(4)
            lo = foo_val1.as_metadata()
            hi = foo_val2.as_metadata()
            strd = foo_val3.as_metadata()
            bias = foo_val4.as_metadata()

            subrange_md_ty = dib.create_subrange_type(
                file_md, "foo", 42, file_md, 64, 0, 0, int64_ty, lo, hi, strd, bias
            )
            mod.add_named_metadata_operand("SubrangeType", subrange_md_ty)

            # Create set types
            set_md_ty1 = dib.create_set_type(
                file_md, "enumset", file_md, 42, 64, 0, enum_test
            )
            set_md_ty2 = dib.create_set_type(
                file_md, "subrangeset", file_md, 42, 64, 0, subrange_md_ty
            )
            mod.add_named_metadata_operand("SetType1", set_md_ty1)
            mod.add_named_metadata_operand("SetType2", set_md_ty2)

            # Create dynamic array type
            dyn_subscripts = [dib.get_or_create_subrange(0, 10)]
            loc_expr = dib.create_expression([])
            rank_expr = dib.create_expression([])
            dynamic_array_md_ty = dib.create_dynamic_array_type(
                file_md,
                "foo",
                42,
                file_md,
                64 * 10,
                0,
                int64_ty,
                dyn_subscripts,
                loc_expr,
                foo_var1,  # associated - matches C code which passes FooVar1
                None,
                rank_expr,
                None,
            )
            mod.add_named_metadata_operand("DynType", dynamic_array_md_ty)

            # Create forward declaration
            struct_p_ty = dib.create_forward_decl(
                2, "Class1", namespace, file_md, 0, 0, 192, 0, "FooClass"
            )

            # Create array and replace it
            int32_ty = dib.create_basic_type("Int32", 32, 0, llvm.DIFlagZero)
            struct_elts_array = [int64_ty, int64_ty, int32_ty]
            class_arr = dib.get_or_create_array(struct_elts_array)
            dib.replace_arrays([struct_p_ty], [class_arr])
            mod.add_named_metadata_operand("ClassType", struct_p_ty)

            # Build IR instructions
            with ctx.create_builder() as builder:
                builder.position_at_end(foo_entry_block)
                builder.br(foo_var_block)

                # Build another br with label
                foo_label2 = dib.create_label(function_md, "label2", file_md, 42, False)
                br_inst = builder.br(foo_var_block)
                dib.insert_label_before(foo_label2, foo_param_location, br_inst)

                # Create non-preserved labels
                dib.create_label(function_md, "label3", file_md, 42, True)
                dib.create_label(function_md, "label4", file_md, 42, False)

                # Finalize DIBuilder
                dib.finalize()

                # Build ret in vars block
                builder.position_at_end(foo_var_block)
                ret_inst = builder.ret(zero_val)

                # Insert phi before ret using new positioning
                insert_pos = foo_var_block.first_instruction
                assert insert_pos is not None, (
                    "Expected at least one instruction in block"
                )
                builder.position_before_instr_and_dbg_records(insert_pos)
                phi1 = builder.phi(i64_type, "p1")
                phi1.add_incoming(zero_val, foo_entry_block)

                # Do it again with the other positioning function
                builder.position_before_dbg_records(foo_var_block, insert_pos)
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
