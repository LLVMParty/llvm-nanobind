"""
Python port of llvm-c-test echo command.

This module implements the --echo command which clones an LLVM module
using the C API, validating that all types, constants, instructions,
and metadata can be properly read and recreated.

Based on: llvm-c/llvm-c-test/echo.cpp
"""

from __future__ import annotations

import sys
from typing import Optional

import llvm


class TypeCloner:
    """Clone LLVM types from one context to another."""

    def __init__(self, module: llvm.Module):
        self.module = module
        self.ctx = module.context

    def clone(self, src: llvm.Type | llvm.Value) -> llvm.Type:
        """Clone a type or a value's type."""
        if isinstance(src, llvm.Value):
            # For functions/globals, use global_get_value_type() instead of .type
            # because .type returns the pointer type, not the actual function type
            if src.is_function or src.is_global_variable:
                return self.clone(src.global_value_type)
            return self.clone(src.type)

        kind = src.kind
        types = self.ctx.types
        if kind == llvm.TypeKind.Void:
            return types.void
        elif kind == llvm.TypeKind.Half:
            return types.f16
        elif kind == llvm.TypeKind.BFloat:
            return types.bf16
        elif kind == llvm.TypeKind.Float:
            return types.f32
        elif kind == llvm.TypeKind.Double:
            return types.f64
        elif kind == llvm.TypeKind.X86_FP80:
            return types.x86_fp80
        elif kind == llvm.TypeKind.FP128:
            return types.fp128
        elif kind == llvm.TypeKind.PPC_FP128:
            return types.ppc_fp128
        elif kind == llvm.TypeKind.Label:
            return types.label
        elif kind == llvm.TypeKind.Integer:
            return types.int_n(src.int_width)
        elif kind == llvm.TypeKind.Function:
            param_count = src.param_count
            params = [self.clone(p) for p in src.param_types]
            return types.function(self.clone(src.return_type), params, src.is_vararg)
        elif kind == llvm.TypeKind.Struct:
            name = src.struct_name
            if name:
                # Try to find existing struct with this name
                existing = types.get(name)
                if existing:
                    return existing
                # Create a new named struct
                s = types.opaque_struct(name)
                if src.is_opaque_struct:
                    return s
                # Set body for named struct
                elt_count = src.struct_element_count
                elts = [
                    self.clone(src.get_struct_element_type(i)) for i in range(elt_count)
                ]
                s.set_body(elts, src.is_packed_struct)
                return s
            else:
                # Anonymous struct
                elt_count = src.struct_element_count
                elts = [
                    self.clone(src.get_struct_element_type(i)) for i in range(elt_count)
                ]
                return types.struct(elts, packed=src.is_packed_struct)
        elif kind == llvm.TypeKind.Array:
            return self.clone(src.element_type).array(src.array_length)
        elif kind == llvm.TypeKind.Pointer:
            addrspace = src.pointer_address_space
            if addrspace == 0:
                return types.ptr
            return types.addrspace_ptr(addrspace)
        elif kind == llvm.TypeKind.Vector:
            return self.clone(src.element_type).vector(src.vector_size)
        elif kind == llvm.TypeKind.ScalableVector:
            return types.scalable_vector(self.clone(src.element_type), src.vector_size)
        elif kind == llvm.TypeKind.Metadata:
            return types.metadata
        elif kind == llvm.TypeKind.X86_AMX:
            return types.x86_amx
        elif kind == llvm.TypeKind.Token:
            return types.token
        elif kind == llvm.TypeKind.TargetExt:
            name = src.target_ext_type_name
            num_type_params = src.target_ext_type_num_type_params
            num_int_params = src.target_ext_type_num_int_params
            type_params = [
                self.clone(src.get_target_ext_type_type_param(i))
                for i in range(num_type_params)
            ]
            int_params = [
                src.get_target_ext_type_int_param(i) for i in range(num_int_params)
            ]
            return types.target_ext(name, type_params, int_params)
        else:
            print(f"{kind} is not a supported typekind", file=sys.stderr)
            sys.exit(-1)


def check_value_kind(v: llvm.Value, k: llvm.ValueKind) -> None:
    """Check that a value has the expected kind."""
    if v.value_kind != k:
        raise RuntimeError("LLVMGetValueKind returned incorrect type")


def clone_constant_impl(cst: llvm.Value, m: llvm.Module) -> llvm.Value:
    """Clone a constant value."""
    if not cst.is_constant:
        raise RuntimeError("Expected a constant")

    # Maybe it is a symbol
    if cst.is_global_value:
        name = cst.name

        # Try function
        if cst.is_function:
            check_value_kind(cst, llvm.ValueKind.Function)

            dst = None
            # Try an intrinsic
            intrinsic_id = cst.intrinsic_id
            if intrinsic_id > 0 and not llvm.intrinsic_is_overloaded(intrinsic_id):
                dst = m.get_intrinsic_declaration(intrinsic_id, [])
            else:
                # Try a normal function
                dst = m.get_function(name)

            if dst:
                return dst
            raise RuntimeError("Could not find function")

        # Try global variable
        if cst.is_global_variable:
            check_value_kind(cst, llvm.ValueKind.GlobalVariable)
            dst = m.get_global(name)
            if dst:
                return dst
            raise RuntimeError("Could not find variable")

        # Try global alias
        if cst.is_global_alias:
            check_value_kind(cst, llvm.ValueKind.GlobalAlias)
            dst = m.get_named_global_alias(name)
            if dst:
                return dst
            raise RuntimeError("Could not find alias")

        print(f"Could not find @{name}", file=sys.stderr)
        sys.exit(-1)

    # Try integer literal
    if cst.is_constant_int:
        check_value_kind(cst, llvm.ValueKind.ConstantInt)
        ty = TypeCloner(m).clone(cst)
        return ty.constant(cst.const_zext_value, False)

    # Try zeroinitializer
    if cst.is_constant_aggregate_zero:
        check_value_kind(cst, llvm.ValueKind.ConstantAggregateZero)
        return TypeCloner(m).clone(cst).null()

    # Try constant data array
    if cst.is_constant_data_array:
        check_value_kind(cst, llvm.ValueKind.ConstantDataArray)
        ty = TypeCloner(m).clone(cst)
        _size, data = cst.raw_data_values
        return ty.element_type.array_const(data)

    # Try constant array
    if cst.is_constant_array:
        check_value_kind(cst, llvm.ValueKind.ConstantArray)
        ty = TypeCloner(m).clone(cst)
        elt_count = ty.array_length
        elts = [
            clone_constant(cst.get_aggregate_element(i), m) for i in range(elt_count)
        ]
        return ty.element_type.array_const(elts)

    # Try constant struct
    if cst.is_constant_struct:
        check_value_kind(cst, llvm.ValueKind.ConstantStruct)
        ty = TypeCloner(m).clone(cst)
        elt_count = ty.struct_element_count
        elts = [clone_constant(cst.get_operand(i), m) for i in range(elt_count)]
        if ty.struct_name:
            return ty.named_struct_const(elts)
        return m.context.const_struct(elts, ty.is_packed_struct)

    # Try ConstantPointerNull
    if cst.is_constant_pointer_null:
        check_value_kind(cst, llvm.ValueKind.ConstantPointerNull)
        ty = TypeCloner(m).clone(cst)
        return ty.null()

    # Try poison
    if cst.is_poison:
        check_value_kind(cst, llvm.ValueKind.PoisonValue)
        return TypeCloner(m).clone(cst).poison()

    # Try undef
    if cst.is_undef:
        check_value_kind(cst, llvm.ValueKind.UndefValue)
        return TypeCloner(m).clone(cst).undef()

    # Try null
    if cst.is_null:
        check_value_kind(cst, llvm.ValueKind.ConstantTokenNone)
        ty = TypeCloner(m).clone(cst)
        return ty.null()

    # Try float literal
    if cst.is_constant_fp:
        check_value_kind(cst, llvm.ValueKind.ConstantFP)
        ty = TypeCloner(m).clone(cst)
        cst_text = str(cst)
        parts = cst_text.split(" ", 1)
        if len(parts) != 2:
            raise RuntimeError("Could not parse ConstantFP literal")
        literal = parts[1].strip()

        def clone_fp_bitpattern_from_literal(fp_ty, fp_literal):
            bit_width = None
            hex_digits = None

            if fp_literal.startswith("0xH"):
                bit_width = 16
                hex_digits = fp_literal[3:]
            elif fp_literal.startswith("0xR"):
                bit_width = 16
                hex_digits = fp_literal[3:]
            elif fp_literal.startswith("0xK"):
                bit_width = 80
                hex_digits = fp_literal[3:]
            elif fp_literal.startswith("0xL"):
                bit_width = 128
                hex_digits = fp_literal[3:]
            elif fp_literal.startswith("0xM"):
                bit_width = 128
                hex_digits = fp_literal[3:]
            elif fp_literal.startswith("0x"):
                body = fp_literal[2:]
                if body and all(ch in "0123456789abcdefABCDEF" for ch in body):
                    kind_to_bits = {
                        llvm.TypeKind.Half: 16,
                        llvm.TypeKind.BFloat: 16,
                        llvm.TypeKind.Float: 32,
                        llvm.TypeKind.Double: 64,
                        llvm.TypeKind.X86_FP80: 80,
                        llvm.TypeKind.FP128: 128,
                        llvm.TypeKind.PPC_FP128: 128,
                    }
                    bit_width = kind_to_bits.get(fp_ty.kind)
                    hex_digits = body

            if bit_width is None or hex_digits is None:
                return None

            int_ty = m.context.types.int_n(bit_width)
            return int_ty.constant(hex_digits, 16).const_bitcast(fp_ty)

        bitpattern = clone_fp_bitpattern_from_literal(ty, literal)
        if bitpattern is not None:
            return bitpattern

        return ty.real_constant(literal)

    # Try ConstantVector or ConstantDataVector
    if cst.is_constant_vector or cst.is_constant_data_vector:
        if cst.is_constant_vector:
            check_value_kind(cst, llvm.ValueKind.ConstantVector)
        else:
            check_value_kind(cst, llvm.ValueKind.ConstantDataVector)
        ty = TypeCloner(m).clone(cst)
        elt_count = ty.vector_size
        elts = [
            clone_constant(cst.get_aggregate_element(i), m) for i in range(elt_count)
        ]
        return ty.element_type.vector_const(elts)

    # Try ConstantPtrAuth
    if cst.is_constant_ptr_auth:
        ptr = clone_constant(cst.constant_ptr_auth_pointer, m)
        key = clone_constant(cst.constant_ptr_auth_key, m)
        disc = clone_constant(cst.constant_ptr_auth_discriminator, m)
        addr_disc = clone_constant(cst.constant_ptr_auth_addr_discriminator, m)
        return ptr.const_ptr_auth(key, disc, addr_disc)

    # At this point, if it's not a constant expression, it's unsupported
    if not cst.is_constant_expr:
        raise RuntimeError("Unsupported constant kind")

    # At this point, it must be a constant expression
    check_value_kind(cst, llvm.ValueKind.ConstantExpr)

    op = cst.const_opcode
    if op == llvm.Opcode.BitCast:
        return clone_constant(cst.get_operand(0), m).const_bitcast(
            TypeCloner(m).clone(cst)
        )
    elif op == llvm.Opcode.GetElementPtr:
        elem_ty = TypeCloner(m).clone(cst.gep_source_element_type)
        ptr = clone_constant(cst.get_operand(0), m)
        num_idx = cst.num_indices
        idx = [clone_constant(cst.get_operand(i + 1), m) for i in range(num_idx)]
        return elem_ty.gep_const(ptr, idx, cst.gep_no_wrap_flags)
    else:
        print(
            f"{op} is not a supported opcode for constant expressions", file=sys.stderr
        )
        sys.exit(-1)


def clone_constant(cst: llvm.Value, m: llvm.Module) -> llvm.Value:
    """Clone a constant and verify its value kind."""
    ret = clone_constant_impl(cst, m)
    check_value_kind(ret, cst.value_kind)
    return ret


def clone_inline_asm(asm: llvm.Value, m: llvm.Module) -> llvm.Value:
    """Clone an inline assembly value."""
    if not asm.is_inline_asm:
        raise RuntimeError("Expected inline assembly")

    asm_string = asm.inline_asm_asm_string
    constraint_string = asm.inline_asm_constraint_string
    dialect = asm.inline_asm_dialect
    fn_ty = asm.inline_asm_function_type
    has_side_effects = asm.inline_asm_has_side_effects
    needs_aligned_stack = asm.inline_asm_needs_aligned_stack
    can_unwind = asm.inline_asm_can_unwind

    cloned_fn_ty = TypeCloner(m).clone(fn_ty)
    return cloned_fn_ty.inline_asm(
        asm_string,
        constraint_string,
        has_side_effects,
        needs_aligned_stack,
        dialect,
        can_unwind,
    )


class FunCloner:
    """Clone a function's contents from source to destination."""

    def __init__(self, src: llvm.Function, dst: llvm.Function, module: llvm.Module):
        self.fun = dst
        self.module = module  # NOTE: get_global_parent not bound, pass module directly
        # Use Value/BasicBlock objects directly as keys since __hash__ and __eq__
        # are implemented based on the underlying LLVM pointer, not Python object id
        self.vmap: dict[llvm.Value, llvm.Value] = {}  # Map src value -> dst value
        self.bb_map: dict[llvm.BasicBlock, llvm.BasicBlock] = {}  # Map src bb -> dst bb

        # Clone parameters
        self._clone_params(src, dst)

    def _clone_params(self, src: llvm.Function, dst: llvm.Function) -> None:
        """Clone function parameters."""
        count = src.param_count
        if count != dst.param_count:
            raise RuntimeError("Parameter count mismatch")

        if count == 0:
            return

        src_first = src.first_param()
        dst_first = dst.first_param()
        src_last = src.last_param()
        dst_last = dst.last_param()

        if src_first is None or dst_first is None:
            raise RuntimeError("Expected non-null first params")
        if src_last is None or dst_last is None:
            raise RuntimeError("Expected non-null last params")

        src_cur: llvm.Value = src_first
        dst_cur: llvm.Value = dst_first
        remaining = count

        while True:
            name = src_cur.name
            dst_cur.name = name
            self.vmap[src_cur] = dst_cur

            remaining -= 1
            src_next = src_cur.next_param
            dst_next = dst_cur.next_param

            if src_next is None and dst_next is None:
                if src_cur != src_last:
                    raise RuntimeError("SrcLast param does not match End")
                if dst_cur != dst_last:
                    raise RuntimeError("DstLast param does not match End")
                break

            if src_next is None:
                raise RuntimeError("SrcNext was unexpectedly null")
            if dst_next is None:
                raise RuntimeError("DstNext was unexpectedly null")

            src_prev = src_next.prev_param
            if src_prev != src_cur:
                raise RuntimeError("SrcNext.Previous param is not Current")

            dst_prev = dst_next.prev_param
            if dst_prev != dst_cur:
                raise RuntimeError("DstNext.Previous param is not Current")

            src_cur = src_next
            dst_cur = dst_next

        if remaining != 0:
            raise RuntimeError("Parameter count does not match iteration")

    def clone_type(self, src: llvm.Type | llvm.Value) -> llvm.Type:
        """Clone a type."""
        return TypeCloner(self.module).clone(src)

    def clone_block_address_constant(self, src: llvm.Value) -> llvm.Value:
        """Clone a BlockAddress constant using the destination function/block."""
        check_value_kind(src, llvm.ValueKind.BlockAddress)

        if src in self.vmap:
            return self.vmap[src]

        num_ops = src.num_operands
        if num_ops not in (1, 2):
            raise RuntimeError("BlockAddress should have one or two operands")

        bb_op_index = num_ops - 1
        bb_val = src.get_operand(bb_op_index)
        if not bb_val.value_is_basic_block:
            raise RuntimeError("BlockAddress operand is not a basic block")

        src_bb = bb_val.value_as_basic_block()
        if num_ops == 2:
            fn_op = src.get_operand(0)
            if not fn_op.is_function:
                raise RuntimeError("BlockAddress function operand is not a function")
            src_fn = fn_op
        else:
            src_fn = src_bb.function

        if src_bb.function != src_fn:
            raise RuntimeError("BlockAddress function/block mismatch")

        dst_fn = self.module.get_function(src_fn.name)
        if not dst_fn:
            raise RuntimeError("Could not find function for block address")

        if dst_fn != self.fun:
            raise RuntimeError("Cross-function blockaddress is not supported")

        dst_bb = self.declare_bb(src_bb)
        dst = dst_fn.block_address(dst_bb)
        self.vmap[src] = dst
        return dst

    def clone_value(self, src: llvm.Value) -> llvm.Value:
        """Clone a value, handling constants, params, and instructions."""
        # Handle BlockAddress by value kind first. Some LLVM revisions may not
        # classify BlockAddress as a constant in the IsA helpers.
        if src.value_kind == llvm.ValueKind.BlockAddress:
            return self.clone_block_address_constant(src)

        # First, the value may be constant
        if src.is_constant:
            return clone_constant(src, self.module)

        # Function argument should always be in the map
        if src in self.vmap:
            return self.vmap[src]

        # Inline assembly is a Value, but not an Instruction
        if src.is_inline_asm:
            return clone_inline_asm(src, self.module)

        if not src.is_instruction:
            raise RuntimeError("Expected an instruction")

        # Create the instruction in the right basic block
        bb = self.declare_bb(src.block)
        with bb.create_builder() as builder:
            return self.clone_instruction(src, builder)

    def clone_attrs(self, src: llvm.Value, dst: llvm.Value) -> None:
        """Clone call/invoke attributes from src to dst."""
        src_slots = [src.callsite_attributes, src.callsite_return_attributes]
        dst_slots = [dst.callsite_attributes, dst.callsite_return_attributes]
        for i in range(src.num_arg_operands):
            src_slots.append(src.callsite_param_attributes(i))
            dst_slots.append(dst.callsite_param_attributes(i))

        for src_slot, dst_slot in zip(src_slots, dst_slots):
            for attr in src_slot:
                dst_slot.add(attr)

    def clone_instruction(self, src: llvm.Value, builder: llvm.Builder) -> llvm.Value:
        """Clone a single instruction."""
        check_value_kind(src, llvm.ValueKind.Instruction)
        if not src.is_instruction:
            raise RuntimeError("Expected an instruction")

        name = src.name

        # Check if already cloned
        if src in self.vmap:
            # Instruction was generated as a dependency, reorder it
            instr = self.vmap[src]
            instr.remove_from_parent()
            builder.insert_into_builder_with_name(instr, name)
            return instr

        dst: Optional[llvm.Value] = None
        op = src.opcode

        if op == llvm.Opcode.Ret:
            op_count = src.num_operands
            if op_count == 0:
                dst = builder.ret_void()
            else:
                dst = builder.ret(self.clone_value(src.get_operand(0)))

        elif op == llvm.Opcode.Br:
            if not src.is_conditional:
                src_op = src.get_operand(0)
                src_bb = src_op.value_as_basic_block()
                dst = builder.br(self.declare_bb(src_bb))
            else:
                cond = src.condition
                else_val = src.get_operand(1)
                else_bb = self.declare_bb(else_val.value_as_basic_block())
                then_val = src.get_operand(2)
                then_bb = self.declare_bb(then_val.value_as_basic_block())
                dst = builder.cond_br(self.clone_value(cond), then_bb, else_bb)

        elif op == llvm.Opcode.Switch:
            cond = self.clone_value(src.get_operand(0))
            succ_count = src.num_successors
            if succ_count < 1:
                raise RuntimeError("Switch: expected at least a default successor")

            default_bb = self.declare_bb(src.get_successor(0))
            num_cases = succ_count - 1
            dst = builder.switch_(cond, default_bb, num_cases)

            # Operand layout: [cond, default_dest, case0_val, case0_dest, ...]
            for i in range(num_cases):
                case_val = self.clone_value(src.get_operand(2 + i * 2))
                case_bb = self.declare_bb(src.get_successor(i + 1))
                dst.add_case(case_val, case_bb)

        elif op == llvm.Opcode.IndirectBr:
            addr = self.clone_value(src.get_operand(0))
            num_dests = src.num_successors
            dst = builder.indirect_br(addr, num_dests)
            for i in range(num_dests):
                dst.add_destination(self.declare_bb(src.get_successor(i)))

        elif op == llvm.Opcode.Invoke:
            args = []
            arg_count = src.num_arg_operands
            for i in range(arg_count):
                args.append(self.clone_value(src.get_operand(i)))

            bundles = []
            bundle_count = src.num_operand_bundles
            for i in range(bundle_count):
                bundle = src.get_operand_bundle_at_index(i)
                bundles.append(self.clone_ob(bundle))

            fn_ty = self.clone_type(src.called_function_type)
            fn = self.clone_value(src.called_value)
            then = self.declare_bb(src.normal_dest)
            unwind_bb = src.unwind_dest
            assert unwind_bb is not None, (
                "Invoke instruction must have unwind destination"
            )
            unwind = self.declare_bb(unwind_bb)
            dst = builder.invoke_with_operand_bundles(
                fn_ty, fn, args, then, unwind, bundles, name
            )
            self.clone_attrs(src, dst)

        elif op == llvm.Opcode.CallBr:
            fn_ty = self.clone_type(src.called_function_type)
            fn = self.clone_value(src.called_value)
            default_dest = self.declare_bb(src.callbr_default_dest)

            indirect_dests = []
            num_indirect = src.callbr_num_indirect_dests
            for i in range(num_indirect):
                indirect_dests.append(self.declare_bb(src.get_callbr_indirect_dest(i)))

            args = []
            arg_count = src.num_arg_operands
            for i in range(arg_count):
                args.append(self.clone_value(src.get_operand(i)))

            bundles = []
            bundle_count = src.num_operand_bundles
            for i in range(bundle_count):
                bundle = src.get_operand_bundle_at_index(i)
                bundles.append(self.clone_ob(bundle))

            dst = builder.callbr(
                fn_ty, fn, default_dest, indirect_dests, args, bundles, name
            )
            self.clone_attrs(src, dst)

        elif op == llvm.Opcode.Unreachable:
            dst = builder.unreachable()

        elif op == llvm.Opcode.Add:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            nuw = src.nuw
            nsw = src.nsw
            dst = builder.add(lhs, rhs, name)
            dst.nuw = nuw
            dst.nsw = nsw

        elif op == llvm.Opcode.Sub:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            nuw = src.nuw
            nsw = src.nsw
            dst = builder.sub(lhs, rhs, name)
            dst.nuw = nuw
            dst.nsw = nsw

        elif op == llvm.Opcode.Mul:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            nuw = src.nuw
            nsw = src.nsw
            dst = builder.mul(lhs, rhs, name)
            dst.nuw = nuw
            dst.nsw = nsw

        elif op == llvm.Opcode.UDiv:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_exact = src.exact
            dst = builder.udiv(lhs, rhs, name)
            dst.exact = is_exact

        elif op == llvm.Opcode.SDiv:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_exact = src.exact
            dst = builder.sdiv(lhs, rhs, name)
            dst.exact = is_exact

        elif op == llvm.Opcode.URem:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.urem(lhs, rhs, name)

        elif op == llvm.Opcode.SRem:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.srem(lhs, rhs, name)

        elif op == llvm.Opcode.Shl:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            nuw = src.nuw
            nsw = src.nsw
            dst = builder.shl(lhs, rhs, name)
            dst.nuw = nuw
            dst.nsw = nsw

        elif op == llvm.Opcode.LShr:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_exact = src.exact
            dst = builder.lshr(lhs, rhs, name)
            dst.exact = is_exact

        elif op == llvm.Opcode.AShr:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_exact = src.exact
            dst = builder.ashr(lhs, rhs, name)
            dst.exact = is_exact

        elif op == llvm.Opcode.And:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.and_(lhs, rhs, name)

        elif op == llvm.Opcode.Or:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_disjoint = src.is_disjoint
            dst = builder.or_(lhs, rhs, name)
            dst.is_disjoint = is_disjoint

        elif op == llvm.Opcode.Xor:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.xor(lhs, rhs, name)

        elif op == llvm.Opcode.Alloca:
            ty = self.clone_type(src.allocated_type)
            dst = builder.alloca(ty, name)
            dst.alignment = src.alignment

        elif op == llvm.Opcode.Load:
            ptr = self.clone_value(src.get_operand(0))
            dst = builder.load(self.clone_type(src), ptr, name)
            dst.inst_alignment = src.inst_alignment
            dst.ordering = src.ordering
            dst.is_volatile = src.is_volatile
            if src.is_atomic:
                dst.atomic_sync_scope_id = src.atomic_sync_scope_id

        elif op == llvm.Opcode.Store:
            val = self.clone_value(src.get_operand(0))
            ptr = self.clone_value(src.get_operand(1))
            dst = builder.store(val, ptr)
            dst.inst_alignment = src.inst_alignment
            dst.ordering = src.ordering
            dst.is_volatile = src.is_volatile
            if src.is_atomic:
                dst.atomic_sync_scope_id = src.atomic_sync_scope_id

        elif op == llvm.Opcode.GetElementPtr:
            elem_ty = self.clone_type(src.gep_source_element_type)
            ptr = self.clone_value(src.get_operand(0))
            num_idx = src.num_indices
            idx = [self.clone_value(src.get_operand(i + 1)) for i in range(num_idx)]
            dst = builder.gep_with_no_wrap_flags(
                elem_ty, ptr, idx, src.gep_no_wrap_flags, name
            )

        elif op == llvm.Opcode.AtomicRMW:
            ptr = self.clone_value(src.get_operand(0))
            val = self.clone_value(src.get_operand(1))
            bin_op = src.atomic_rmw_bin_op
            ordering = src.ordering
            dst = builder.atomic_rmw_sync_scope(
                bin_op, ptr, val, ordering, src.atomic_sync_scope_id
            )
            dst.alignment = src.alignment
            dst.is_volatile = src.is_volatile
            dst.name = name

        elif op == llvm.Opcode.AtomicCmpXchg:
            ptr = self.clone_value(src.get_operand(0))
            cmp = self.clone_value(src.get_operand(1))
            new = self.clone_value(src.get_operand(2))
            succ = src.cmpxchg_success_ordering
            fail = src.cmpxchg_failure_ordering
            dst = builder.atomic_cmpxchg_sync_scope(
                ptr, cmp, new, succ, fail, src.atomic_sync_scope_id
            )
            dst.alignment = src.alignment
            dst.is_volatile = src.is_volatile
            dst.weak = src.weak
            dst.name = name

        elif op == llvm.Opcode.BitCast:
            v = self.clone_value(src.get_operand(0))
            dst = builder.bitcast(v, self.clone_type(src), name)

        elif op == llvm.Opcode.ICmp:
            pred = src.icmp_predicate
            same_sign = src.icmp_same_sign
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.icmp(pred, lhs, rhs, name)
            dst.icmp_same_sign = same_sign

        elif op == llvm.Opcode.PHI:
            # We need to aggressively set things here because of loops
            self.vmap[src] = dst = builder.phi(self.clone_type(src), name)
            incoming_count = src.num_incoming
            for i in range(incoming_count):
                block = self.declare_bb(src.get_incoming_block(i))
                value = self.clone_value(src.get_incoming_value(i))
                dst.add_incoming(value, block)
            # Copy fast math flags and return early
            if src.can_use_fast_math_flags:
                dst.fast_math_flags = src.fast_math_flags
            return dst

        elif op == llvm.Opcode.Select:
            if_val = self.clone_value(src.get_operand(0))
            then_val = self.clone_value(src.get_operand(1))
            else_val = self.clone_value(src.get_operand(2))
            dst = builder.select(if_val, then_val, else_val, name)

        elif op == llvm.Opcode.Call:
            args = []
            arg_count = src.num_arg_operands
            for i in range(arg_count):
                args.append(self.clone_value(src.get_operand(i)))

            bundles = []
            bundle_count = src.num_operand_bundles
            for i in range(bundle_count):
                bundle = src.get_operand_bundle_at_index(i)
                bundles.append(self.clone_ob(bundle))

            fn_ty = self.clone_type(src.called_function_type)
            fn = self.clone_value(src.called_value)
            dst = builder.call_with_operand_bundles(fn_ty, fn, args, bundles, name)
            dst.tail_call_kind = src.tail_call_kind
            self.clone_attrs(src, dst)

        elif op == llvm.Opcode.Resume:
            dst = builder.resume(self.clone_value(src.get_operand(0)))

        elif op == llvm.Opcode.LandingPad:
            dst = builder.landing_pad(self.clone_type(src), 0, name)
            num_clauses = src.num_clauses
            for i in range(num_clauses):
                dst.add_clause(self.clone_value(src.get_clause(i)))
            dst.is_cleanup = src.is_cleanup

        elif op == llvm.Opcode.CleanupRet:
            catch_pad = self.clone_value(src.get_operand(0))
            unwind_dest = src.unwind_dest
            unwind = self.declare_bb(unwind_dest) if unwind_dest else None
            # Note: cleanup_ret accepts None for unwind_bb
            dst = builder.cleanup_ret(catch_pad, unwind)

        elif op == llvm.Opcode.CatchRet:
            catch_pad = self.clone_value(src.get_operand(0))
            succ_bb = self.declare_bb(src.get_successor(0))
            dst = builder.catch_ret(catch_pad, succ_bb)

        elif op == llvm.Opcode.CatchPad:
            parent_pad = self.clone_value(src.parent_catch_switch)
            args = []
            arg_count = src.num_arg_operands
            for i in range(arg_count):
                args.append(self.clone_value(src.get_operand(i)))
            dst = builder.catch_pad(parent_pad, args, name)

        elif op == llvm.Opcode.CleanupPad:
            parent_pad = self.clone_value(src.get_operand(0))
            args = []
            arg_count = src.num_arg_operands
            for i in range(arg_count):
                args.append(self.clone_value(src.get_arg_operand(i)))
            dst = builder.cleanup_pad(parent_pad, args, name)

        elif op == llvm.Opcode.CatchSwitch:
            parent_pad = self.clone_value(src.get_operand(0))
            unwind_dest = src.unwind_dest
            unwind_bb = self.declare_bb(unwind_dest) if unwind_dest else None
            num_handlers = src.num_handlers
            # Note: catch_switch accepts None for unwind_bb
            dst = builder.catch_switch(parent_pad, unwind_bb, num_handlers, name)
            if num_handlers > 0:
                handlers = src.handlers
                for h in handlers:
                    dst.add_handler(self.declare_bb(h))

        elif op == llvm.Opcode.ExtractValue:
            agg = self.clone_value(src.get_operand(0))
            indices = src.indices
            if len(indices) > 1:
                raise RuntimeError("ExtractValue: Expected only one index")
            elif len(indices) < 1:
                raise RuntimeError("ExtractValue: Expected an index")
            dst = builder.extract_value(agg, indices[0], name)

        elif op == llvm.Opcode.InsertValue:
            agg = self.clone_value(src.get_operand(0))
            v = self.clone_value(src.get_operand(1))
            indices = src.indices
            if len(indices) > 1:
                raise RuntimeError("InsertValue: Expected only one index")
            elif len(indices) < 1:
                raise RuntimeError("InsertValue: Expected an index")
            dst = builder.insert_value(agg, v, indices[0], name)

        elif op == llvm.Opcode.ExtractElement:
            agg = self.clone_value(src.get_operand(0))
            index = self.clone_value(src.get_operand(1))
            dst = builder.extract_element(agg, index, name)

        elif op == llvm.Opcode.InsertElement:
            agg = self.clone_value(src.get_operand(0))
            v = self.clone_value(src.get_operand(1))
            index = self.clone_value(src.get_operand(2))
            dst = builder.insert_element(agg, v, index, name)

        elif op == llvm.Opcode.ShuffleVector:
            agg0 = self.clone_value(src.get_operand(0))
            agg1 = self.clone_value(src.get_operand(1))
            mask_elts = []
            num_mask_elts = src.num_mask_elements
            int64_ty = self.module.context.types.i64
            for i in range(num_mask_elts):
                val = src.get_mask_value(i)
                if val == llvm.get_undef_mask_elem():
                    mask_elts.append(int64_ty.undef())
                else:
                    mask_elts.append(int64_ty.constant(val, True))
            mask = int64_ty.vector_const(mask_elts)
            dst = builder.shuffle_vector(agg0, agg1, mask, name)

        elif op == llvm.Opcode.Freeze:
            arg = self.clone_value(src.get_operand(0))
            dst = builder.freeze(arg, name)

        elif op == llvm.Opcode.Fence:
            ordering = src.ordering
            dst = builder.fence_sync_scope(ordering, src.atomic_sync_scope_id, name)

        elif op == llvm.Opcode.ZExt:
            val = self.clone_value(src.get_operand(0))
            dest_ty = self.clone_type(src)
            nneg = src.nneg
            dst = builder.zext(val, dest_ty, name)
            dst.nneg = nneg

        elif op == llvm.Opcode.FAdd:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.fadd(lhs, rhs, name)

        elif op == llvm.Opcode.FSub:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.fsub(lhs, rhs, name)

        elif op == llvm.Opcode.FMul:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.fmul(lhs, rhs, name)

        elif op == llvm.Opcode.FDiv:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.fdiv(lhs, rhs, name)

        elif op == llvm.Opcode.FRem:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.frem(lhs, rhs, name)

        elif op == llvm.Opcode.FNeg:
            val = self.clone_value(src.get_operand(0))
            dst = builder.fneg(val, name)

        elif op == llvm.Opcode.FCmp:
            pred = src.fcmp_predicate
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.fcmp(pred, lhs, rhs, name)

        # Handle remaining cast operations
        elif op == llvm.Opcode.Trunc:
            val = self.clone_value(src.get_operand(0))
            dst = builder.trunc(val, self.clone_type(src), name)

        elif op == llvm.Opcode.SExt:
            val = self.clone_value(src.get_operand(0))
            dst = builder.sext(val, self.clone_type(src), name)

        elif op == llvm.Opcode.FPTrunc:
            val = self.clone_value(src.get_operand(0))
            dst = builder.fptrunc(val, self.clone_type(src), name)

        elif op == llvm.Opcode.FPExt:
            val = self.clone_value(src.get_operand(0))
            dst = builder.fpext(val, self.clone_type(src), name)

        elif op == llvm.Opcode.FPToSI:
            val = self.clone_value(src.get_operand(0))
            dst = builder.fptosi(val, self.clone_type(src), name)

        elif op == llvm.Opcode.FPToUI:
            val = self.clone_value(src.get_operand(0))
            dst = builder.fptoui(val, self.clone_type(src), name)

        elif op == llvm.Opcode.SIToFP:
            val = self.clone_value(src.get_operand(0))
            dst = builder.sitofp(val, self.clone_type(src), name)

        elif op == llvm.Opcode.UIToFP:
            val = self.clone_value(src.get_operand(0))
            dst = builder.uitofp(val, self.clone_type(src), name)

        elif op == llvm.Opcode.PtrToInt:
            val = self.clone_value(src.get_operand(0))
            dst = builder.ptrtoint(val, self.clone_type(src), name)

        elif op == llvm.Opcode.IntToPtr:
            val = self.clone_value(src.get_operand(0))
            dst = builder.inttoptr(val, self.clone_type(src), name)

        if dst is None:
            print(f"{op} is not a supported opcode", file=sys.stderr)
            sys.exit(-1)

        # After sys.exit(-1), dst is guaranteed to be non-None
        # Type narrow for type checkers that don't understand NoReturn
        assert dst is not None

        # Copy fast-math flags on instructions that support them
        if src.can_use_fast_math_flags:
            dst.fast_math_flags = src.fast_math_flags

        # Copy instruction metadata.
        src.metadata.copy_to(dst)

        check_value_kind(dst, llvm.ValueKind.Instruction)
        self.vmap[src] = dst
        return dst

    def clone_ob(self, src: llvm.OperandBundle) -> llvm.OperandBundle:
        """Clone an operand bundle."""
        tag = src.tag
        args = []
        for i in range(src.num_args):
            args.append(self.clone_value(src.get_arg_at_index(i)))
        return llvm.create_operand_bundle(tag, args, self.module.context)

    def declare_bb(self, src: llvm.BasicBlock) -> llvm.BasicBlock:
        """Declare a basic block, creating if necessary."""
        if src in self.bb_map:
            return self.bb_map[src]

        # Verify it's a valid basic block
        v = src.as_value()
        if not v.value_is_basic_block or v.value_as_basic_block().name != src.name:
            raise RuntimeError("Basic block is not a basic block")

        name = src.name
        bb = self.fun.append_basic_block(name)
        self.bb_map[src] = bb
        return bb

    def clone_bb(self, src: llvm.BasicBlock) -> llvm.BasicBlock:
        """Clone a basic block's contents."""
        bb = self.declare_bb(src)

        # Make sure ordering is correct
        prev = src.prev_block
        if prev:
            bb.move_after(self.declare_bb(prev))

        first = src.first_instruction
        last = src.last_instruction

        if first is None:
            if last is not None:
                raise RuntimeError("Has no first instruction, but last one")
            return bb

        ctx = self.module.context
        with bb.create_builder() as builder:
            cur = first
            while True:
                self.clone_instruction(cur, builder)
                next_instr = cur.next_instruction
                if next_instr is None:
                    if cur != last:
                        raise RuntimeError("Final instruction does not match Last")
                    break

                prev_instr = next_instr.prev_instruction
                if prev_instr != cur:
                    raise RuntimeError("Next.Previous instruction is not Current")

                cur = next_instr

        return bb

    def clone_bbs(self, src: llvm.Function) -> None:
        """Clone all basic blocks from source function."""
        count = src.basic_block_count
        if count == 0:
            return

        first = src.first_basic_block
        last = src.last_basic_block

        if first is None or last is None:
            raise RuntimeError("Expected non-null first/last basic blocks")

        cur: llvm.BasicBlock = first
        remaining = count
        while True:
            self.clone_bb(cur)
            remaining -= 1
            next_bb = cur.next_block
            if next_bb is None:
                if cur != last:
                    raise RuntimeError("Final basic block does not match Last")
                break

            prev_bb = next_bb.prev_block
            if prev_bb != cur:
                raise RuntimeError("Next.Previous basic bloc is not Current")

            cur = next_bb

        if remaining != 0:
            raise RuntimeError("Basic block count does not match iterration")


def declare_symbols(src: llvm.Module, m: llvm.Module) -> None:
    """Declare all global symbols in the destination module."""
    ctx = m.context

    # Declare global variables
    cur = src.first_global
    end = src.last_global
    if cur is None:
        if end is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur.name
            if m.get_global(name):
                raise RuntimeError("GlobalVariable already cloned")
            m.add_global(TypeCloner(m).clone(cur.global_value_type), name)

            next_g = cur.next_global
            if next_g is None:
                if cur != end:
                    raise RuntimeError("")
                break

            prev_g = next_g.prev_global
            if prev_g != cur:
                raise RuntimeError("Next.Previous global is not Current")

            cur = next_g

    # Declare functions
    cur = src.first_function
    while cur is not None:
        name = cur.name
        if m.get_function(name):
            raise RuntimeError("Function already cloned")
        ty = TypeCloner(m).clone(cur.global_value_type)
        f = m.add_function(name, ty)

        # Copy attributes.
        src_slots = [cur.attributes, cur.return_attributes]
        dst_slots = [f.attributes, f.return_attributes]
        for i in range(cur.param_count):
            src_slots.append(cur.param_attributes(i))
            dst_slots.append(f.param_attributes(i))
        for src_slot, dst_slot in zip(src_slots, dst_slots):
            for attr in src_slot:
                dst_slot.add(attr)

        cur = cur.next_function

    # Declare global aliases
    cur = src.first_global_alias
    end = src.last_global_alias
    if cur is None:
        if end is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur.name
            if m.get_named_global_alias(name):
                raise RuntimeError("Global alias already cloned")
            ptr_type = TypeCloner(m).clone(cur)
            val_type = TypeCloner(m).clone(cur.global_value_type)
            addr_space = ptr_type.pointer_address_space
            # FIXME: Allow NULL aliasee
            m.add_alias(val_type, addr_space, ptr_type.undef(), name)

            next_a = cur.next_global_alias
            if next_a is None:
                if cur != end:
                    raise RuntimeError("")
                break

            prev_a = next_a.prev_global_alias
            if prev_a != cur:
                raise RuntimeError("Next.Previous global is not Current")

            cur = next_a

    # Declare global ifuncs
    cur = src.first_global_ifunc
    end = src.last_global_ifunc
    if cur is None:
        if end is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur.name
            if m.get_named_global_ifunc(name):
                raise RuntimeError("Global ifunc already cloned")
            cur_type = TypeCloner(m).clone(cur.global_value_type)
            # FIXME: Allow NULL resolver
            m.add_global_ifunc(name, cur_type, 0, cur_type.undef())

            next_i = cur.next_global_ifunc
            if next_i is None:
                if cur != end:
                    raise RuntimeError("")
                break

            prev_i = next_i.prev_global_ifunc
            if prev_i != cur:
                raise RuntimeError("Next.Previous global is not Current")

            cur = next_i

    # Declare named metadata.
    for name in src.named_metadata:
        if m.named_metadata.get(name) is not None:
            raise RuntimeError("Named Metadata Node already cloned")
        _ = m.named_metadata[name]


def clone_symbols(src: llvm.Module, m: llvm.Module) -> None:
    """Clone the contents of all global symbols."""
    # Clone global variable contents
    cur = src.first_global
    end = src.last_global
    if cur is None:
        if end is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur.name
            g = m.get_global(name)
            if not g:
                raise RuntimeError("GlobalVariable must have been declared already")

            init = cur.initializer
            if init:
                g.initializer = clone_constant(init, m)

            # Copy global metadata.
            cur.metadata.copy_to(g)

            g.is_global_constant = cur.is_global_constant
            g.is_thread_local = cur.is_thread_local
            g.is_externally_initialized = cur.is_externally_initialized
            g.linkage = cur.linkage
            g.section = cur.section
            g.visibility = cur.visibility
            g.unnamed_address = cur.unnamed_address
            g.alignment = cur.alignment

            next_g = cur.next_global
            if next_g is None:
                if cur != end:
                    raise RuntimeError("")
                break

            prev_g = next_g.prev_global
            if prev_g != cur:
                raise RuntimeError("Next.Previous global is not Current")

            cur = next_g

    # Clone function contents
    cur = src.first_function
    end = src.last_function
    if cur is None:
        if end is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur.name
            fun = m.get_function(name)
            if not fun:
                raise RuntimeError("Function must have been declared already")

            if cur.has_personality_fn:
                personality_fn = cur.personality_fn
                assert personality_fn is not None  # Guaranteed by has_personality_fn
                p_name = personality_fn.name
                p = m.get_function(p_name)
                if not p:
                    raise RuntimeError("Could not find personality function")
                fun.personality_fn = p

            # Copy function metadata.
            cur.metadata.copy_to(fun)

            # Copy prefix and prologue data
            if cur.has_prefix_data:
                prefix_data = cur.prefix_data
                assert prefix_data is not None  # Guaranteed by has_prefix_data
                fun.prefix_data = clone_constant(prefix_data, m)

            if cur.has_prologue_data:
                prologue_data = cur.prologue_data
                assert prologue_data is not None  # Guaranteed by has_prologue_data
                fun.prologue_data = clone_constant(prologue_data, m)

            fc = FunCloner(cur, fun, m)
            fc.clone_bbs(cur)

            next_f = cur.next_function
            if next_f is None:
                if cur != end:
                    raise RuntimeError("Last function does not match End")
                break

            prev_f = next_f.prev_function
            if prev_f != cur:
                raise RuntimeError("Next.Previous function is not Current")

            cur = next_f

    # Clone global alias contents
    cur = src.first_global_alias
    end = src.last_global_alias
    if cur is None:
        if end is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur.name
            alias = m.get_named_global_alias(name)
            if not alias:
                raise RuntimeError("Global alias must have been declared already")

            aliasee = cur.aliasee
            if aliasee:
                alias.alias_set_aliasee(clone_constant(aliasee, m))

            alias.linkage = cur.linkage
            alias.unnamed_address = cur.unnamed_address

            next_a = cur.next_global_alias
            if next_a is None:
                if cur != end:
                    raise RuntimeError("Last global alias does not match End")
                break

            prev_a = next_a.prev_global_alias
            if prev_a != cur:
                raise RuntimeError("Next.Previous global alias is not Current")

            cur = next_a

    # Clone global ifunc contents
    cur = src.first_global_ifunc
    end = src.last_global_ifunc
    if cur is None:
        if end is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur.name
            ifunc = m.get_named_global_ifunc(name)
            if not ifunc:
                raise RuntimeError("Global ifunc must have been declared already")

            resolver = cur.global_ifunc_resolver
            if resolver:
                ifunc.global_ifunc_resolver = clone_constant(resolver, m)

            ifunc.linkage = cur.linkage
            ifunc.unnamed_address = cur.unnamed_address

            next_i = cur.next_global_ifunc
            if next_i is None:
                if cur != end:
                    raise RuntimeError("Last global alias does not match End")
                break

            prev_i = next_i.prev_global_ifunc
            if prev_i != cur:
                raise RuntimeError("Next.Previous global alias is not Current")

            cur = next_i

    # Clone named metadata contents.
    for name in src.named_metadata:
        named_md = m.named_metadata.get(name)
        if named_md is None:
            raise RuntimeError("Named MD Node must have been declared already")
        for md in src.named_metadata[name]:
            named_md.append(md)


def echo() -> int:
    """Main echo command entry point."""
    # NOTE: enable_pretty_stack_trace is not bound (not critical for functionality)
    # llvm.enable_pretty_stack_trace()

    with llvm.create_context() as ctx:
        # Load the source module from stdin
        bitcode = sys.stdin.buffer.read()

        with ctx.parse_bitcode_from_bytes(bitcode) as src:
            source_filename = src.source_filename
            # HACK: When parsing from bytes, LLVM sets module ID to '<bytes>'.
            # For llvm-c-test compatibility, we use '<stdin>' to match the C version
            # which reads directly from stdin via LLVMCreateMemoryBufferWithSTDIN.
            module_name = "<stdin>"

            # Create destination module
            with ctx.create_module(module_name) as m:
                m.source_filename = source_filename
                m.name = module_name
                m.target_triple = src.target_triple
                m.data_layout = src.data_layout

                if m.data_layout != src.data_layout:
                    raise RuntimeError("Inconsistent DataLayout string representation")

                m.inline_asm = src.inline_asm

                declare_symbols(src, m)
                clone_symbols(src, m)

                output = str(m)

    print(output, end="")
    return 0
