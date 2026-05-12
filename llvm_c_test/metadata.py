"""
Implementation of metadata testing commands.

Includes:
- --add-named-metadata-operand
- --set-metadata
- --replace-md-operand
- --is-a-value-as-metadata
"""

import sys
import llvm


def add_named_metadata_operand():
    """Test adding named metadata operand (no output expected)."""
    try:
        # Create module
        with llvm.create_context() as ctx:
            with ctx.create_module("Mod") as mod:
                # Create integer constant
                i32 = ctx.types.i32
                val = i32.constant(0, False)

                # Create metadata node and add to named metadata
                # First convert value to metadata, then create node
                md = val.as_metadata()
                md_node = ctx.md_node([md])
                mod.add_named_metadata_operand("name", md_node)

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


def set_metadata():
    """Test setting metadata on instruction (no output expected)."""
    try:
        # Create a builder manager but don't enter it (create unattached builder)
        with llvm.create_context() as ctx:
            with ctx.create_module("test") as mod:
                func_ty = ctx.types.function(ctx.types.void, [])
                func = mod.add_function("test", func_ty)
                bb = func.append_basic_block("entry")

                # Create builder manager but don't use it - we need to create
                # an instruction without inserting it into a block
                # This is a special case for testing
                builder_mgr = bb.create_builder()
                # Enter temporarily to get access to builder methods
                with builder_mgr as builder:
                    # Position at end, create instruction, then remove it
                    ret_inst = builder.ret_void()

                # Now remove the instruction from the basic block
                ret_inst.remove_from_parent()

                # Create metadata and set it on the instruction
                i32 = ctx.types.i32
                val = i32.constant(0, False)
                md = val.as_metadata()
                md_node = ctx.md_node([md])

                kind_id = llvm.get_md_kind_id("kind")
                ret_inst.set_metadata(kind_id, md_node, ctx)

                # Delete the instruction
                ret_inst.delete_instruction()

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


def replace_md_operand():
    """Test replacing metadata operand."""
    try:
        with llvm.create_context() as ctx:
            with ctx.create_module("Mod"):
                # Build MDNode("foo"), then replace operand 0 with MDString("bar").
                string1_md = ctx.md_string("foo")
                node_md = ctx.md_node([string1_md])
                value = node_md.as_value(ctx)

                string2_md = ctx.md_string("bar")
                value.replace_md_node_operand_with(0, string2_md)

                # Verify replacement took effect.
                operand = value.get_operand(0)
                if operand.as_metadata() != string2_md:
                    raise AssertionError("Metadata operand replacement did not apply")

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1


def is_a_value_as_metadata():
    """Test checking if value is ValueAsMetadata."""
    try:
        with llvm.create_context() as ctx:
            with ctx.create_module("Mod") as mod:
                # MDNode built from a Value should be ValueAsMetadata.
                i32 = ctx.types.i32
                val = i32.constant(0, False).as_metadata()
                md_node = ctx.md_node([val])
                md_val = md_node.as_value(ctx)

                if not md_val.is_value_as_metadata:
                    raise AssertionError("Expected ValueAsMetadata for value-backed MD")

                # MDNode built from MDString should NOT be ValueAsMetadata.
                string_md = ctx.md_string("foo")
                string_node = ctx.md_node([string_md])
                string_val = string_node.as_value(ctx)

                if string_val.is_value_as_metadata:
                    raise AssertionError("Expected non-ValueAsMetadata for string-backed MD")

        return 0
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
