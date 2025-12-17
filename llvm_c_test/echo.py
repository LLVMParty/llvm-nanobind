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
        self.ctx = llvm.get_module_context(module)

    def clone(self, src: llvm.Type | llvm.Value) -> llvm.Type:
        """Clone a type or a value's type."""
        if isinstance(src, llvm.Value):
            # For functions/globals, use global_get_value_type() instead of .type
            # because .type returns the pointer type, not the actual function type
            if src.is_a_function() or src.is_a_global_variable():
                return self.clone(src.global_get_value_type())
            return self.clone(src.type)

        kind = src.kind
        if kind == llvm.TypeKind.Void:
            return self.ctx.void_type()
        elif kind == llvm.TypeKind.Half:
            return self.ctx.half_type()
        elif kind == llvm.TypeKind.BFloat:
            return self.ctx.bfloat_type()
        elif kind == llvm.TypeKind.Float:
            return self.ctx.float_type()
        elif kind == llvm.TypeKind.Double:
            return self.ctx.double_type()
        elif kind == llvm.TypeKind.X86_FP80:
            return self.ctx.x86_fp80_type()
        elif kind == llvm.TypeKind.FP128:
            return self.ctx.fp128_type()
        elif kind == llvm.TypeKind.PPC_FP128:
            return self.ctx.ppc_fp128_type()
        elif kind == llvm.TypeKind.Label:
            return self.ctx.label_type()
        elif kind == llvm.TypeKind.Integer:
            return self.ctx.int_type(src.int_width)
        elif kind == llvm.TypeKind.Function:
            param_count = src.param_count
            params = [self.clone(p) for p in src.param_types]
            return self.ctx.function_type(
                self.clone(src.return_type), params, src.is_vararg
            )
        elif kind == llvm.TypeKind.Struct:
            name = src.struct_name
            if name:
                # Try to find existing struct with this name
                existing = self.ctx.get_type_by_name(name)
                if existing:
                    return existing
                # Create a new named struct
                s = self.ctx.named_struct_type(name)
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
                return self.ctx.struct_type(elts, src.is_packed_struct)
        elif kind == llvm.TypeKind.Array:
            return self.ctx.array_type(self.clone(src.element_type), src.array_length)
        elif kind == llvm.TypeKind.Pointer:
            if src.is_opaque_pointer:
                return self.ctx.pointer_type(src.pointer_address_space)
            else:
                # Legacy typed pointer (shouldn't happen with opaque pointers)
                return self.ctx.pointer_type(src.pointer_address_space)
        elif kind == llvm.TypeKind.Vector:
            return self.ctx.vector_type(self.clone(src.element_type), src.vector_size)
        elif kind == llvm.TypeKind.ScalableVector:
            return self.ctx.scalable_vector_type(
                self.clone(src.element_type), src.vector_size
            )
        elif kind == llvm.TypeKind.Metadata:
            return self.ctx.metadata_type()
        elif kind == llvm.TypeKind.X86_AMX:
            return self.ctx.x86_amx_type()
        elif kind == llvm.TypeKind.Token:
            return self.ctx.token_type()
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
            return self.ctx.target_ext_type(name, type_params, int_params)
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
    if cst.is_a_global_value():
        name = cst.name

        # Try function
        if cst.is_a_function():
            check_value_kind(cst, llvm.ValueKind.Function)

            dst = None
            # Try an intrinsic
            intrinsic_id = cst.get_intrinsic_id()
            if intrinsic_id > 0 and not llvm.intrinsic_is_overloaded(intrinsic_id):
                dst = llvm.get_intrinsic_declaration(m, intrinsic_id, [])
            else:
                # Try a normal function
                dst = m.get_function(name)

            if dst:
                return dst
            raise RuntimeError("Could not find function")

        # Try global variable
        if cst.is_a_global_variable():
            check_value_kind(cst, llvm.ValueKind.GlobalVariable)
            dst = m.get_global(name)
            if dst:
                return dst
            raise RuntimeError("Could not find variable")

        # Try global alias
        if cst.is_a_global_alias():
            check_value_kind(cst, llvm.ValueKind.GlobalAlias)
            dst = m.get_named_global_alias(name)
            if dst:
                return dst
            raise RuntimeError("Could not find alias")

        print(f"Could not find @{name}", file=sys.stderr)
        sys.exit(-1)

    # Try integer literal
    if cst.is_a_constant_int():
        check_value_kind(cst, llvm.ValueKind.ConstantInt)
        return llvm.const_int(
            TypeCloner(m).clone(cst), llvm.const_int_get_zext_value(cst), False
        )

    # Try zeroinitializer
    if cst.is_a_constant_aggregate_zero():
        check_value_kind(cst, llvm.ValueKind.ConstantAggregateZero)
        return llvm.const_null(TypeCloner(m).clone(cst))

    # Try constant data array
    if cst.is_a_constant_data_array():
        check_value_kind(cst, llvm.ValueKind.ConstantDataArray)
        ty = TypeCloner(m).clone(cst)
        size, data = cst.get_raw_data_values()
        return llvm.const_data_array(ty.element_type, data)

    # Try constant array
    if cst.is_a_constant_array():
        check_value_kind(cst, llvm.ValueKind.ConstantArray)
        ty = TypeCloner(m).clone(cst)
        elt_count = ty.array_length
        elts = [
            clone_constant(cst.get_aggregate_element(i), m) for i in range(elt_count)
        ]
        return llvm.const_array(ty.element_type, elts)

    # Try constant struct
    if cst.is_a_constant_struct():
        check_value_kind(cst, llvm.ValueKind.ConstantStruct)
        ty = TypeCloner(m).clone(cst)
        elt_count = ty.struct_element_count
        elts = [clone_constant(cst.get_operand(i), m) for i in range(elt_count)]
        if ty.struct_name:
            return llvm.const_named_struct(ty, elts)
        return llvm.const_struct(elts, ty.is_packed_struct, llvm.get_module_context(m))

    # Try ConstantPointerNull
    if cst.is_a_constant_pointer_null():
        check_value_kind(cst, llvm.ValueKind.ConstantPointerNull)
        ty = TypeCloner(m).clone(cst)
        return llvm.const_null(ty)

    # Try undef
    if cst.is_undef:
        check_value_kind(cst, llvm.ValueKind.UndefValue)
        return llvm.undef(TypeCloner(m).clone(cst))

    # Try poison
    if cst.is_poison:
        check_value_kind(cst, llvm.ValueKind.PoisonValue)
        return llvm.poison(TypeCloner(m).clone(cst))

    # Try null
    if cst.is_null():
        check_value_kind(cst, llvm.ValueKind.ConstantTokenNone)
        ty = TypeCloner(m).clone(cst)
        return llvm.const_null(ty)

    # Try float literal
    if cst.is_a_constant_fp():
        check_value_kind(cst, llvm.ValueKind.ConstantFP)
        raise RuntimeError("ConstantFP is not supported")

    # Try ConstantVector or ConstantDataVector
    if cst.is_a_constant_vector() or cst.is_a_constant_data_vector():
        if cst.is_a_constant_vector():
            check_value_kind(cst, llvm.ValueKind.ConstantVector)
        else:
            check_value_kind(cst, llvm.ValueKind.ConstantDataVector)
        ty = TypeCloner(m).clone(cst)
        elt_count = ty.vector_size
        elts = [
            clone_constant(cst.get_aggregate_element(i), m) for i in range(elt_count)
        ]
        return llvm.const_vector(elts)

    # Try ConstantPtrAuth
    if cst.is_a_constant_ptr_auth():
        ptr = clone_constant(cst.get_constant_ptr_auth_pointer(), m)
        key = clone_constant(cst.get_constant_ptr_auth_key(), m)
        disc = clone_constant(cst.get_constant_ptr_auth_discriminator(), m)
        addr_disc = clone_constant(cst.get_constant_ptr_auth_addr_discriminator(), m)
        return llvm.const_ptr_auth(ptr, key, disc, addr_disc)

    # At this point, if it's not a constant expression, it's unsupported
    if not cst.is_a_constant_expr():
        raise RuntimeError("Unsupported constant kind")

    # At this point, it must be a constant expression
    check_value_kind(cst, llvm.ValueKind.ConstantExpr)

    op = cst.get_const_opcode()
    if op == llvm.Opcode.BitCast:
        return llvm.const_bitcast(
            clone_constant(cst.get_operand(0), m), TypeCloner(m).clone(cst)
        )
    elif op == llvm.Opcode.GetElementPtr:
        elem_ty = TypeCloner(m).clone(cst.get_gep_source_element_type())
        ptr = clone_constant(cst.get_operand(0), m)
        num_idx = cst.get_num_indices()
        idx = [clone_constant(cst.get_operand(i + 1), m) for i in range(num_idx)]
        return llvm.const_gep_with_no_wrap_flags(
            elem_ty, ptr, idx, cst.get_gep_no_wrap_flags()
        )
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
    if not asm.is_a_inline_asm():
        raise RuntimeError("Expected inline assembly")

    asm_string = asm.get_inline_asm_asm_string()
    constraint_string = asm.get_inline_asm_constraint_string()
    dialect = asm.get_inline_asm_dialect()
    fn_ty = asm.get_inline_asm_function_type()
    has_side_effects = asm.get_inline_asm_has_side_effects()
    needs_aligned_stack = asm.get_inline_asm_needs_aligned_stack()
    can_unwind = asm.get_inline_asm_can_unwind()

    cloned_fn_ty = TypeCloner(m).clone(fn_ty)
    return llvm.get_inline_asm(
        cloned_fn_ty,
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

    def clone_value(self, src: llvm.Value) -> llvm.Value:
        """Clone a value, handling constants, params, and instructions."""
        # First, the value may be constant
        if src.is_constant:
            return clone_constant(src, self.module)

        # Function argument should always be in the map
        if src in self.vmap:
            return self.vmap[src]

        # Inline assembly is a Value, but not an Instruction
        if src.is_a_inline_asm():
            return clone_inline_asm(src, self.module)

        if not src.is_a_instruction():
            raise RuntimeError("Expected an instruction")

        # Create the instruction in the right basic block
        ctx = llvm.get_module_context(self.module)
        with ctx.create_builder() as builder:
            bb = self.declare_bb(src.get_instruction_parent())
            builder.position_at_end(bb)
            return self.clone_instruction(src, builder)

    def clone_attrs(self, src: llvm.Value, dst: llvm.Value) -> None:
        """Clone call/invoke attributes from src to dst."""
        ctx = llvm.get_module_context(self.module)
        last_kind = llvm.get_last_enum_attribute_kind()

        # Clone attributes for all indices (return, function, and each param)
        # Index 0 is return, index -1 (AttributeFunctionIndex) is function
        # Params start at index 1
        for idx in range(llvm.AttributeFunctionIndex, src.get_num_arg_operands() + 1):
            for kind in range(1, last_kind + 1):
                attr = llvm.get_callsite_enum_attribute(src, idx, kind)
                if attr is not None:
                    # Create a copy of the attribute in the destination context
                    new_attr = llvm.create_enum_attribute(ctx, attr.kind, attr.value)
                    llvm.add_callsite_attribute(dst, idx, new_attr)

    def clone_instruction(self, src: llvm.Value, builder: llvm.Builder) -> llvm.Value:
        """Clone a single instruction."""
        check_value_kind(src, llvm.ValueKind.Instruction)
        if not src.is_a_instruction():
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
        op = src.get_instruction_opcode()

        if op == llvm.Opcode.Ret:
            op_count = src.get_num_operands()
            if op_count == 0:
                dst = builder.ret_void()
            else:
                dst = builder.ret(self.clone_value(src.get_operand(0)))

        elif op == llvm.Opcode.Br:
            if not src.is_conditional():
                src_op = src.get_operand(0)
                src_bb = src_op.value_as_basic_block()
                dst = builder.br(self.declare_bb(src_bb))
            else:
                cond = src.get_condition()
                else_val = src.get_operand(1)
                else_bb = self.declare_bb(else_val.value_as_basic_block())
                then_val = src.get_operand(2)
                then_bb = self.declare_bb(then_val.value_as_basic_block())
                dst = builder.cond_br(self.clone_value(cond), then_bb, else_bb)

        elif op == llvm.Opcode.Switch:
            # Not fully supported
            pass

        elif op == llvm.Opcode.IndirectBr:
            # Not fully supported
            pass

        elif op == llvm.Opcode.Invoke:
            args = []
            arg_count = src.get_num_arg_operands()
            for i in range(arg_count):
                args.append(self.clone_value(src.get_operand(i)))

            bundles = []
            bundle_count = src.get_num_operand_bundles()
            for i in range(bundle_count):
                bundle = src.get_operand_bundle_at_index(i)
                bundles.append(self.clone_ob(bundle))

            fn_ty = self.clone_type(src.get_called_function_type())
            fn = self.clone_value(src.get_called_value())
            then = self.declare_bb(src.get_normal_dest())
            unwind = self.declare_bb(src.get_unwind_dest())
            dst = builder.invoke_with_operand_bundles(
                fn_ty, fn, args, then, unwind, bundles, name
            )
            self.clone_attrs(src, dst)

        elif op == llvm.Opcode.CallBr:
            fn_ty = self.clone_type(src.get_called_function_type())
            fn = self.clone_value(src.get_called_value())
            default_dest = self.declare_bb(src.get_callbr_default_dest())

            indirect_dests = []
            num_indirect = src.get_callbr_num_indirect_dests()
            for i in range(num_indirect):
                indirect_dests.append(self.declare_bb(src.get_callbr_indirect_dest(i)))

            args = []
            arg_count = src.get_num_arg_operands()
            for i in range(arg_count):
                args.append(self.clone_value(src.get_operand(i)))

            bundles = []
            bundle_count = src.get_num_operand_bundles()
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
            nuw = src.get_nuw()
            nsw = src.get_nsw()
            dst = builder.add(lhs, rhs, name)
            dst.set_nuw(nuw)
            dst.set_nsw(nsw)

        elif op == llvm.Opcode.Sub:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            nuw = src.get_nuw()
            nsw = src.get_nsw()
            dst = builder.sub(lhs, rhs, name)
            dst.set_nuw(nuw)
            dst.set_nsw(nsw)

        elif op == llvm.Opcode.Mul:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            nuw = src.get_nuw()
            nsw = src.get_nsw()
            dst = builder.mul(lhs, rhs, name)
            dst.set_nuw(nuw)
            dst.set_nsw(nsw)

        elif op == llvm.Opcode.UDiv:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_exact = src.get_exact()
            dst = builder.udiv(lhs, rhs, name)
            dst.set_exact(is_exact)

        elif op == llvm.Opcode.SDiv:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_exact = src.get_exact()
            dst = builder.sdiv(lhs, rhs, name)
            dst.set_exact(is_exact)

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
            nuw = src.get_nuw()
            nsw = src.get_nsw()
            dst = builder.shl(lhs, rhs, name)
            dst.set_nuw(nuw)
            dst.set_nsw(nsw)

        elif op == llvm.Opcode.LShr:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_exact = src.get_exact()
            dst = builder.lshr(lhs, rhs, name)
            dst.set_exact(is_exact)

        elif op == llvm.Opcode.AShr:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_exact = src.get_exact()
            dst = builder.ashr(lhs, rhs, name)
            dst.set_exact(is_exact)

        elif op == llvm.Opcode.And:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.and_(lhs, rhs, name)

        elif op == llvm.Opcode.Or:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            is_disjoint = src.get_is_disjoint()
            dst = builder.or_(lhs, rhs, name)
            dst.set_is_disjoint(is_disjoint)

        elif op == llvm.Opcode.Xor:
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.xor_(lhs, rhs, name)

        elif op == llvm.Opcode.Alloca:
            ty = self.clone_type(src.get_allocated_type())
            dst = builder.alloca(ty, name)
            dst.set_alignment(src.get_alignment())

        elif op == llvm.Opcode.Load:
            ptr = self.clone_value(src.get_operand(0))
            dst = builder.load(self.clone_type(src), ptr, name)
            dst.set_inst_alignment(src.get_inst_alignment())
            dst.set_ordering(src.get_ordering())
            dst.set_volatile(src.get_volatile())
            if src.is_atomic():
                dst.set_atomic_sync_scope_id(src.get_atomic_sync_scope_id())

        elif op == llvm.Opcode.Store:
            val = self.clone_value(src.get_operand(0))
            ptr = self.clone_value(src.get_operand(1))
            dst = builder.store(val, ptr)
            dst.set_inst_alignment(src.get_inst_alignment())
            dst.set_ordering(src.get_ordering())
            dst.set_volatile(src.get_volatile())
            if src.is_atomic():
                dst.set_atomic_sync_scope_id(src.get_atomic_sync_scope_id())

        elif op == llvm.Opcode.GetElementPtr:
            elem_ty = self.clone_type(src.get_gep_source_element_type())
            ptr = self.clone_value(src.get_operand(0))
            num_idx = src.get_num_indices()
            idx = [self.clone_value(src.get_operand(i + 1)) for i in range(num_idx)]
            dst = builder.gep_with_no_wrap_flags(
                elem_ty, ptr, idx, src.get_gep_no_wrap_flags(), name
            )

        elif op == llvm.Opcode.AtomicRMW:
            ptr = self.clone_value(src.get_operand(0))
            val = self.clone_value(src.get_operand(1))
            bin_op = src.get_atomic_rmw_bin_op()
            ordering = src.get_ordering()
            dst = builder.atomic_rmw_sync_scope(
                bin_op, ptr, val, ordering, src.get_atomic_sync_scope_id()
            )
            dst.set_alignment(src.get_alignment())
            dst.set_volatile(src.get_volatile())
            dst.name = name

        elif op == llvm.Opcode.AtomicCmpXchg:
            ptr = self.clone_value(src.get_operand(0))
            cmp = self.clone_value(src.get_operand(1))
            new = self.clone_value(src.get_operand(2))
            succ = src.get_cmpxchg_success_ordering()
            fail = src.get_cmpxchg_failure_ordering()
            dst = builder.atomic_cmpxchg_sync_scope(
                ptr, cmp, new, succ, fail, src.get_atomic_sync_scope_id()
            )
            dst.set_alignment(src.get_alignment())
            dst.set_volatile(src.get_volatile())
            dst.set_weak(src.get_weak())
            dst.name = name

        elif op == llvm.Opcode.BitCast:
            v = self.clone_value(src.get_operand(0))
            dst = builder.bitcast(v, self.clone_type(src), name)

        elif op == llvm.Opcode.ICmp:
            pred = src.get_icmp_predicate()
            same_sign = src.get_icmp_same_sign()
            lhs = self.clone_value(src.get_operand(0))
            rhs = self.clone_value(src.get_operand(1))
            dst = builder.icmp(pred, lhs, rhs, name)
            dst.set_icmp_same_sign(same_sign)

        elif op == llvm.Opcode.PHI:
            # We need to aggressively set things here because of loops
            self.vmap[src] = dst = builder.phi(self.clone_type(src), name)
            incoming_count = src.count_incoming()
            for i in range(incoming_count):
                block = self.declare_bb(src.get_incoming_block(i))
                value = self.clone_value(src.get_incoming_value(i))
                dst.add_incoming(value, block)
            # Copy fast math flags and return early
            if src.can_use_fast_math_flags():
                dst.set_fast_math_flags(src.get_fast_math_flags())
            return dst

        elif op == llvm.Opcode.Select:
            if_val = self.clone_value(src.get_operand(0))
            then_val = self.clone_value(src.get_operand(1))
            else_val = self.clone_value(src.get_operand(2))
            dst = builder.select(if_val, then_val, else_val, name)

        elif op == llvm.Opcode.Call:
            args = []
            arg_count = src.get_num_arg_operands()
            for i in range(arg_count):
                args.append(self.clone_value(src.get_operand(i)))

            bundles = []
            bundle_count = src.get_num_operand_bundles()
            for i in range(bundle_count):
                bundle = src.get_operand_bundle_at_index(i)
                bundles.append(self.clone_ob(bundle))

            fn_ty = self.clone_type(src.get_called_function_type())
            fn = self.clone_value(src.get_called_value())
            dst = builder.call_with_operand_bundles(fn_ty, fn, args, bundles, name)
            dst.set_tail_call_kind(src.get_tail_call_kind())
            self.clone_attrs(src, dst)

        elif op == llvm.Opcode.Resume:
            dst = builder.resume(self.clone_value(src.get_operand(0)))

        elif op == llvm.Opcode.LandingPad:
            dst = builder.landing_pad(self.clone_type(src), 0, name)
            num_clauses = src.get_num_clauses()
            for i in range(num_clauses):
                dst.add_clause(self.clone_value(src.get_clause(i)))
            dst.set_cleanup(src.is_cleanup())

        elif op == llvm.Opcode.CleanupRet:
            catch_pad = self.clone_value(src.get_operand(0))
            unwind_dest = src.get_unwind_dest()
            unwind = self.declare_bb(unwind_dest) if unwind_dest else None
            # Note: cleanup_ret accepts None for unwind_bb (stub is incorrect)
            dst = builder.cleanup_ret(catch_pad, unwind)  # type: ignore[arg-type]

        elif op == llvm.Opcode.CatchRet:
            catch_pad = self.clone_value(src.get_operand(0))
            succ_bb = self.declare_bb(src.get_successor(0))
            dst = builder.catch_ret(catch_pad, succ_bb)

        elif op == llvm.Opcode.CatchPad:
            parent_pad = self.clone_value(src.get_parent_catch_switch())
            args = []
            arg_count = src.get_num_arg_operands()
            for i in range(arg_count):
                args.append(self.clone_value(src.get_operand(i)))
            dst = builder.catch_pad(parent_pad, args, name)

        elif op == llvm.Opcode.CleanupPad:
            parent_pad = self.clone_value(src.get_operand(0))
            args = []
            arg_count = src.get_num_arg_operands()
            for i in range(arg_count):
                args.append(self.clone_value(src.get_arg_operand(i)))
            dst = builder.cleanup_pad(parent_pad, args, name)

        elif op == llvm.Opcode.CatchSwitch:
            parent_pad = self.clone_value(src.get_operand(0))
            unwind_dest = src.get_unwind_dest()
            unwind_bb = self.declare_bb(unwind_dest) if unwind_dest else None
            num_handlers = src.get_num_handlers()
            # Note: catch_switch accepts None for unwind_bb (stub is incorrect)
            dst = builder.catch_switch(parent_pad, unwind_bb, num_handlers, name)  # type: ignore[arg-type]
            if num_handlers > 0:
                handlers = src.get_handlers()
                for h in handlers:
                    dst.add_handler(self.declare_bb(h))

        elif op == llvm.Opcode.ExtractValue:
            agg = self.clone_value(src.get_operand(0))
            indices = src.get_indices()
            if len(indices) > 1:
                raise RuntimeError("ExtractValue: Expected only one index")
            elif len(indices) < 1:
                raise RuntimeError("ExtractValue: Expected an index")
            dst = builder.extract_value(agg, indices[0], name)

        elif op == llvm.Opcode.InsertValue:
            agg = self.clone_value(src.get_operand(0))
            v = self.clone_value(src.get_operand(1))
            indices = src.get_indices()
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
            num_mask_elts = src.get_num_mask_elements()
            int64_ty = llvm.get_module_context(self.module).int64_type()
            for i in range(num_mask_elts):
                val = src.get_mask_value(i)
                if val == llvm.get_undef_mask_elem():
                    mask_elts.append(llvm.undef(int64_ty))
                else:
                    mask_elts.append(llvm.const_int(int64_ty, val, True))
            mask = llvm.const_vector(mask_elts)
            dst = builder.shuffle_vector(agg0, agg1, mask, name)

        elif op == llvm.Opcode.Freeze:
            arg = self.clone_value(src.get_operand(0))
            dst = builder.freeze(arg, name)

        elif op == llvm.Opcode.Fence:
            ordering = src.get_ordering()
            dst = builder.fence_sync_scope(
                ordering, src.get_atomic_sync_scope_id(), name
            )

        elif op == llvm.Opcode.ZExt:
            val = self.clone_value(src.get_operand(0))
            dest_ty = self.clone_type(src)
            nneg = src.get_nneg()
            dst = builder.zext(val, dest_ty, name)
            dst.set_nneg(nneg)

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
            pred = src.get_fcmp_predicate()
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
        if src.can_use_fast_math_flags():
            dst.set_fast_math_flags(src.get_fast_math_flags())

        # Copy instruction metadata
        ctx = llvm.get_module_context(self.module)
        all_metadata = src.instruction_get_all_metadata_other_than_debug_loc()
        for i in range(len(all_metadata)):
            kind = all_metadata.get_kind(i)
            md = all_metadata.get_metadata(i)
            llvm.set_metadata(dst, kind, llvm.metadata_as_value(ctx, md))

        check_value_kind(dst, llvm.ValueKind.Instruction)
        self.vmap[src] = dst
        return dst

    def clone_ob(self, src: llvm.OperandBundle) -> llvm.OperandBundle:
        """Clone an operand bundle."""
        tag = src.tag
        args = []
        for i in range(src.num_args):
            args.append(self.clone_value(src.get_arg_at_index(i)))
        return llvm.create_operand_bundle(
            tag, args, llvm.get_module_context(self.module)
        )

    def declare_bb(self, src: llvm.BasicBlock) -> llvm.BasicBlock:
        """Declare a basic block, creating if necessary."""
        if src in self.bb_map:
            return self.bb_map[src]

        # Verify it's a valid basic block
        v = src.as_value()
        if not v.value_is_basic_block() or v.value_as_basic_block().name != src.name:
            raise RuntimeError("Basic block is not a basic block")

        name = src.name
        bb = self.fun.append_basic_block(name, llvm.get_module_context(self.module))
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

        ctx = llvm.get_module_context(self.module)
        with ctx.create_builder() as builder:
            builder.position_at_end(bb)

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
    ctx = llvm.get_module_context(m)

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
            m.add_global(TypeCloner(m).clone(cur.global_get_value_type()), name)

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
        ty = TypeCloner(m).clone(cur.global_get_value_type())
        f = m.add_function(name, ty)

        # Copy attributes
        last_kind = llvm.get_last_enum_attribute_kind()
        for i in range(llvm.AttributeFunctionIndex, cur.param_count + 1):
            for k in range(1, last_kind + 1):
                src_a = llvm.get_enum_attribute_at_index(cur, i, k)
                if src_a:
                    val = src_a.value
                    dst_a = llvm.create_enum_attribute(ctx, k, val)
                    llvm.add_attribute_at_index(f, i, dst_a)

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
            val_type = TypeCloner(m).clone(cur.global_get_value_type())
            addr_space = ptr_type.pointer_address_space
            # FIXME: Allow NULL aliasee
            m.add_alias(val_type, addr_space, llvm.undef(ptr_type), name)

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
            cur_type = TypeCloner(m).clone(cur.global_get_value_type())
            # FIXME: Allow NULL resolver
            m.add_global_ifunc(name, cur_type, 0, llvm.undef(cur_type))

            next_i = cur.next_global_ifunc
            if next_i is None:
                if cur != end:
                    raise RuntimeError("")
                break

            prev_i = next_i.prev_global_ifunc
            if prev_i != cur:
                raise RuntimeError("Next.Previous global is not Current")

            cur = next_i

    # Declare named metadata
    cur_md = src.first_named_metadata
    end_md = src.last_named_metadata
    if cur_md is None:
        if end_md is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur_md.name
            if m.get_named_metadata(name):
                raise RuntimeError("Named Metadata Node already cloned")
            m.get_or_insert_named_metadata(name)

            next_md = cur_md.next
            if next_md is None:
                if cur_md != end_md:
                    raise RuntimeError("")
                break

            prev_md = next_md.prev
            if prev_md != cur_md:
                raise RuntimeError("Next.Previous global is not Current")

            cur_md = next_md


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

            init = cur.get_initializer()
            if init:
                g.set_initializer(clone_constant(init, m))

            # Copy global metadata
            all_metadata = cur.global_copy_all_metadata()
            for i in range(len(all_metadata)):
                kind = all_metadata.get_kind(i)
                md = all_metadata.get_metadata(i)
                llvm.global_set_metadata(global_val=g, kind=kind, md=md)

            g.set_constant(cur.is_global_constant())
            g.set_thread_local(cur.is_thread_local())
            g.set_externally_initialized(cur.is_externally_initialized())
            g.set_linkage(cur.get_linkage())
            g.set_section(cur.get_section())
            g.set_visibility(cur.get_visibility())
            g.set_unnamed_address(cur.get_unnamed_address())
            g.set_alignment(cur.get_alignment())

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

            if cur.has_personality_fn():
                personality_fn = cur.get_personality_fn()
                assert personality_fn is not None  # Guaranteed by has_personality_fn
                p_name = personality_fn.name
                p = m.get_function(p_name)
                if not p:
                    raise RuntimeError("Could not find personality function")
                fun.set_personality_fn(p)

            # Copy function metadata
            all_metadata = cur.global_copy_all_metadata()
            for i in range(len(all_metadata)):
                kind = all_metadata.get_kind(i)
                md = all_metadata.get_metadata(i)
                llvm.global_set_metadata(global_val=fun, kind=kind, md=md)

            # Copy prefix and prologue data
            if cur.has_prefix_data():
                prefix_data = cur.get_prefix_data()
                assert prefix_data is not None  # Guaranteed by has_prefix_data
                fun.set_prefix_data(clone_constant(prefix_data, m))

            if cur.has_prologue_data():
                prologue_data = cur.get_prologue_data()
                assert prologue_data is not None  # Guaranteed by has_prologue_data
                fun.set_prologue_data(clone_constant(prologue_data, m))

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

            aliasee = cur.alias_get_aliasee()
            if aliasee:
                alias.alias_set_aliasee(clone_constant(aliasee, m))

            alias.set_linkage(cur.get_linkage())
            alias.set_unnamed_address(cur.get_unnamed_address())

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

            resolver = cur.get_global_ifunc_resolver()
            if resolver:
                ifunc.set_global_ifunc_resolver(clone_constant(resolver, m))

            ifunc.set_linkage(cur.get_linkage())
            ifunc.set_unnamed_address(cur.get_unnamed_address())

            next_i = cur.next_global_ifunc
            if next_i is None:
                if cur != end:
                    raise RuntimeError("Last global alias does not match End")
                break

            prev_i = next_i.prev_global_ifunc
            if prev_i != cur:
                raise RuntimeError("Next.Previous global alias is not Current")

            cur = next_i

    # Clone named metadata contents
    cur_md = src.first_named_metadata
    end_md = src.last_named_metadata
    if cur_md is None:
        if end_md is not None:
            raise RuntimeError("Range has an end but no beginning")
    else:
        while True:
            name = cur_md.name
            named_md = m.get_named_metadata(name)
            if not named_md:
                raise RuntimeError("Named MD Node must have been declared already")

            operand_count = src.get_named_metadata_num_operands(name)
            operands = src.get_named_metadata_operands(name)
            for op in operands:
                llvm.add_named_metadata_operand(m, name, op)

            next_md = cur_md.next
            if next_md is None:
                if cur_md != end_md:
                    raise RuntimeError("Last Named MD Node does not match End")
                break

            prev_md = next_md.prev
            if prev_md != cur_md:
                raise RuntimeError("Next.Previous Named MD Node is not Current")

            cur_md = next_md


def echo() -> int:
    """Main echo command entry point."""
    # NOTE: enable_pretty_stack_trace is not bound (not critical for functionality)
    # llvm.enable_pretty_stack_trace()

    with llvm.create_context() as ctx:
        # Load the source module from stdin
        buf = llvm.create_memory_buffer_with_stdin()
        src = llvm.parse_bitcode_in_context(ctx, buf)

        source_filename = src.source_filename
        module_name = src.name

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

        # Explicitly delete src before context exits to ensure proper cleanup order
        del src

    print(output, end="")
    return 0
