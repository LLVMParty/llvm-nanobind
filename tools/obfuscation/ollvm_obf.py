#!/usr/bin/env -S uv run
"""
Python/nanobind port of omill/tools/ollvm-obf.

Public pipeline flags implemented:
- --string-encrypt
- --code-clone
- --substitute
- --if-convert
- --loop-to-recursion
- --flatten
- --opaque-predicates
- --bogus-control-flow
- --bmi-mutate
- --const-unfold
- --schedule-instructions
- --outline-functions
- --arith-encode
- --stack-randomize
- --vectorize
- --reg-pressure
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import random
import sys
from pathlib import Path
from typing import Iterable, Sequence

import llvm


# =============================================================================
# Shared pipeline helpers
# =============================================================================


@dataclass(slots=True)
class FilterConfig:
    min_instructions: int = 0
    skip_inline_asm: bool = False
    transform_percent: int = 100


@dataclass(slots=True)
class PipelineOptions:
    code_clone: bool = False
    substitute: bool = False
    if_convert: bool = False
    flatten: bool = False
    opaque_predicates: bool = False
    bogus_control_flow: bool = False
    const_unfold: bool = False
    schedule_instructions: bool = False
    string_encrypt: bool = False
    bmi_mutate: bool = False
    outline_functions: bool = False
    stack_randomize: bool = False
    arith_encode: bool = False
    loop_to_recursion: bool = False
    reg_pressure: bool = False
    vectorize: bool = False

    vectorize_data: bool = True
    vectorize_bitwise: bool = False
    vectorize_i64: bool = True
    vectorize_percent: int = 40

    seed: int = 0xB16B00B5
    verify_each: bool = False
    min_instructions: int = 0
    transform_percent: int = 100
    skip_inline_asm: bool = False


UNSUPPORTED_FLAGS: dict[str, str] = {}


SUBSTITUTE_SALT = 0x5B2E6D4F
IF_CONVERT_SALT = 0x2F7D9C45
FLATTEN_SALT = 0xA1F3707B
OPAQUE_PREDICATES_SALT = 0xE4D29B13
BOGUS_CONTROL_FLOW_SALT = 0x7F1A83C5
CONST_UNFOLD_SALT = 0xC93A1E27
CODE_CLONE_SALT = 0xD4A2E7B1
SCHEDULE_SALT = 0x8C2E5A19
STRING_ENCRYPT_SALT = 0x11A48D53
BMI_MUTATE_SALT = 0x4E8B2F71
OUTLINE_SALT = 0xA5C4F239
STACK_RANDOMIZE_SALT = 0x7C5DA128
ARITH_ENCODE_SALT = 0x1E9B47D3
LOOP_TO_RECURSION_SALT = 0x6B3E81D7
REG_PRESSURE_SALT = 0xF3B28E4D
VECTORIZE_SALT = 0x3D7C9A61

CALL_LIKE_OPCODES = {llvm.Opcode.Call, llvm.Opcode.Invoke, llvm.Opcode.CallBr}
MEMORY_OPCODES = {
    llvm.Opcode.Load,
    llvm.Opcode.Store,
    llvm.Opcode.AtomicRMW,
    llvm.Opcode.AtomicCmpXchg,
    llvm.Opcode.Fence,
    llvm.Opcode.VAArg,
    llvm.Opcode.Call,
    llvm.Opcode.Invoke,
    llvm.Opcode.CallBr,
}
SAFE_SPECULATE_OPCODES = {
    llvm.Opcode.Add,
    llvm.Opcode.Sub,
    llvm.Opcode.Mul,
    llvm.Opcode.Xor,
    llvm.Opcode.And,
    llvm.Opcode.Or,
    llvm.Opcode.Shl,
    llvm.Opcode.LShr,
    llvm.Opcode.AShr,
    llvm.Opcode.FAdd,
    llvm.Opcode.FSub,
    llvm.Opcode.FMul,
    llvm.Opcode.BitCast,
    llvm.Opcode.Trunc,
    llvm.Opcode.ZExt,
    llvm.Opcode.SExt,
    llvm.Opcode.PtrToInt,
    llvm.Opcode.IntToPtr,
    llvm.Opcode.ICmp,
    llvm.Opcode.FCmp,
    llvm.Opcode.Select,
    llvm.Opcode.Freeze,
}


def mix_seed(base: int, salt: int) -> int:
    x = (base ^ salt) & 0xFFFFFFFF
    x ^= x >> 16
    x = (x * 0x7FEB352D) & 0xFFFFFFFF
    x ^= x >> 15
    x = (x * 0x846CA68B) & 0xFFFFFFFF
    x ^= x >> 16
    return x & 0xFFFFFFFF


def iter_instructions(fn: llvm.Function) -> Iterable[llvm.Value]:
    for bb in fn.basic_blocks:
        for inst in bb.instructions:
            yield inst


def count_instructions(fn: llvm.Function) -> int:
    return sum(1 for _ in iter_instructions(fn))


def _is_call_like(inst: llvm.Value) -> bool:
    return inst.opcode in CALL_LIKE_OPCODES


def _is_inline_asm_call(inst: llvm.Value) -> bool:
    if not _is_call_like(inst):
        return False
    try:
        return inst.called_value.is_inline_asm
    except llvm.LLVMError:
        return False


def should_skip_function(fn: llvm.Function, cfg: FilterConfig) -> bool:
    if fn.is_declaration:
        return True
    if fn.linkage == llvm.Linkage.AvailableExternally:
        return True

    exclude_attr = fn.get_string_attribute(
        llvm.AttributeFunctionIndex, "ollvm_exclude"
    )
    if exclude_attr is not None:
        return True

    if cfg.min_instructions > 0 and count_instructions(fn) < cfg.min_instructions:
        return True

    if cfg.skip_inline_asm:
        for inst in iter_instructions(fn):
            if _is_inline_asm_call(inst):
                return True

    return False


def should_transform(rng: random.Random, cfg: FilterConfig) -> bool:
    if cfg.transform_percent >= 100:
        return True
    if cfg.transform_percent <= 0:
        return False
    return rng.randint(1, 100) <= cfg.transform_percent


def mask_to_width(value: int, bits: int) -> int:
    if bits >= 64:
        return value & 0xFFFFFFFFFFFFFFFF
    return value & ((1 << bits) - 1)


def int_constant(ty: llvm.Type, value: int) -> llvm.Value:
    bits = ty.int_width
    value = mask_to_width(value, bits)
    if bits < 64:
        sign_bit = 1 << (bits - 1)
        if value & sign_bit:
            value -= 1 << bits
    elif value >= (1 << 63):
        value -= 1 << 64
    return ty.constant(value)


def mod_inverse_pow2(a: int, bits: int) -> int:
    inv = a & 0xFFFFFFFFFFFFFFFF
    for _ in range(6):
        inv = (inv * (2 - a * inv)) & 0xFFFFFFFFFFFFFFFF
    return mask_to_width(inv, bits)


def build_filter_config(options: PipelineOptions) -> FilterConfig:
    return FilterConfig(
        min_instructions=options.min_instructions,
        skip_inline_asm=options.skip_inline_asm,
        transform_percent=options.transform_percent,
    )


def replace_all_uses_with_if(
    old_value: llvm.Value,
    new_value: llvm.Value,
    predicate,
) -> None:
    uses = list(old_value.uses)
    for use in uses:
        user = use.user
        if not user.is_instruction:
            continue
        if not predicate(user):
            continue
        user.replace_uses_of_with(old_value, new_value)


def position_after_instruction(builder: llvm.Builder, inst: llvm.Value) -> None:
    next_inst = inst.next_instruction
    if next_inst is not None:
        builder.position_before(next_inst)
    else:
        builder.position_at_end(inst.block)


# =============================================================================
# Opaque predicate helpers
# =============================================================================


def splitmix64(state: int) -> tuple[int, int]:
    state = (state + 0x9E3779B97F4A7C15) & 0xFFFFFFFFFFFFFFFF
    z = state
    z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & 0xFFFFFFFFFFFFFFFF
    z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & 0xFFFFFFFFFFFFFFFF
    z ^= z >> 31
    return state, z & 0xFFFFFFFFFFFFFFFF


def build_mba_zero(builder: llvm.Builder, x: llvm.Value, seed: int) -> llvm.Value:
    ty = x.type
    state = seed & 0xFFFFFFFFFFFFFFFF

    state, k_raw = splitmix64(state)
    k_raw |= 1
    k = int_constant(ty, k_raw)
    y = builder.xor(x, k, "")

    state, base = splitmix64(state)
    base %= 4

    x_and_y = builder.and_(x, y, "")
    x_or_y = builder.or_(x, y, "")
    x_xor_y = builder.xor(x, y, "")

    if base == 0:
        sum_xy = builder.add(x, y, "")
        t1 = builder.sub(sum_xy, x_xor_y, "")
        two = int_constant(ty, 2)
        t2 = builder.mul(x_and_y, two, "")
        zero = builder.sub(t1, t2, "")
    elif base == 1:
        t1 = builder.sub(x_or_y, x_xor_y, "")
        zero = builder.sub(t1, x_and_y, "")
    elif base == 2:
        t1 = builder.add(x_and_y, x_or_y, "")
        t2 = builder.sub(t1, x, "")
        zero = builder.sub(t2, y, "")
    else:
        two = int_constant(ty, 2)
        t1 = builder.mul(x_or_y, two, "")
        t2 = builder.sub(t1, x_xor_y, "")
        t3 = builder.sub(t2, x, "")
        zero = builder.sub(t3, y, "")

    state, m_raw = splitmix64(state)
    m = int_constant(ty, (m_raw % 254) + 2)
    return builder.mul(zero, m, "")


def generate_opaque_true(
    builder: llvm.Builder, x: llvm.Value, seed: int
) -> llvm.Value:
    state = seed & 0xFFFFFFFFFFFFFFFF
    state, variant = splitmix64(state)
    variant %= 5
    ty = x.type

    if variant == 0:
        zero = build_mba_zero(builder, x, seed)
        return builder.icmp(llvm.IntPredicate.EQ, zero, int_constant(ty, 0), "")
    if variant == 1:
        zero = build_mba_zero(builder, x, seed)
        return builder.icmp(llvm.IntPredicate.SLE, zero, int_constant(ty, 0), "")
    if variant == 2:
        zero = build_mba_zero(builder, x, seed)
        return builder.icmp(llvm.IntPredicate.UGE, zero, int_constant(ty, 0), "")
    if variant == 3:
        zero = build_mba_zero(builder, x, seed)
        return builder.icmp(llvm.IntPredicate.SLT, zero, int_constant(ty, 1), "")

    zero_a = build_mba_zero(builder, x, seed)
    zero_b = build_mba_zero(builder, x, seed ^ 0x13579BDF2468ACE0)
    return builder.icmp(llvm.IntPredicate.EQ, zero_a, zero_b, "")


def generate_opaque_false(
    builder: llvm.Builder, x: llvm.Value, seed: int
) -> llvm.Value:
    state = seed & 0xFFFFFFFFFFFFFFFF
    state, variant = splitmix64(state)
    variant %= 5
    ty = x.type

    if variant == 0:
        zero = build_mba_zero(builder, x, seed)
        return builder.icmp(llvm.IntPredicate.NE, zero, int_constant(ty, 0), "")
    if variant == 1:
        zero = build_mba_zero(builder, x, seed)
        return builder.icmp(llvm.IntPredicate.SGT, zero, int_constant(ty, 0), "")
    if variant == 2:
        zero = build_mba_zero(builder, x, seed)
        return builder.icmp(llvm.IntPredicate.UGT, zero, int_constant(ty, 0), "")
    if variant == 3:
        zero = build_mba_zero(builder, x, seed)
        return builder.icmp(llvm.IntPredicate.SLT, zero, int_constant(ty, 0), "")

    zero_a = build_mba_zero(builder, x, seed)
    zero_b = build_mba_zero(builder, x, seed ^ 0x13579BDF2468ACE0)
    return builder.icmp(llvm.IntPredicate.NE, zero_a, zero_b, "")


def zext_or_trunc(builder: llvm.Builder, value: llvm.Value, ty: llvm.Type) -> llvm.Value:
    if value.type == ty:
        return value
    src_bits = value.type.int_width
    dst_bits = ty.int_width
    if src_bits > dst_bits:
        return builder.trunc(value, ty, "")
    return builder.zext(value, ty, "")


def get_predicate_input(
    fn: llvm.Function,
    builder: llvm.Builder,
    fallback: list[llvm.Value | None],
) -> llvm.Value:
    for arg in fn.params:
        if arg.type.kind == llvm.TypeKind.Integer and arg.type.int_width >= 8:
            return arg

    if fallback[0] is None:
        entry = fn.entry_block
        with entry.create_builder(first_non_phi=True) as entry_builder:
            i64 = fn.context.types.i64
            slot = entry_builder.alloca(i64, name="ollvm.pred.input")
            entry_builder.store(i64.constant(0), slot)
            fallback[0] = slot

    load = builder.load(fn.context.types.i64, fallback[0], "")
    load.is_volatile = True
    return load


def create_opaque_junk_block(
    fn: llvm.Function,
    target: llvm.BasicBlock,
    rng: random.Random,
) -> llvm.BasicBlock:
    ctx = fn.context
    i32 = ctx.types.i32
    junk_bb = fn.append_basic_block("")

    with junk_bb.create_builder() as builder:
        base: llvm.Value | None = None
        for arg in fn.params:
            if arg.type.kind != llvm.TypeKind.Integer:
                continue
            if arg.type == i32:
                base = arg
            elif arg.type.int_width > 32:
                base = builder.trunc(arg, i32, "")
            else:
                base = builder.zext(arg, i32, "")
            break

        if base is None:
            entry = fn.entry_block
            with entry.create_builder(first_non_phi=True) as entry_builder:
                slot = entry_builder.alloca(i32, name="ollvm.junk.seed")
                entry_builder.store(i32.constant(0), slot)
            base = builder.load(i32, slot, "")
            base.is_volatile = True

        acc = base
        for _ in range(rng.randint(3, 6)):
            k = i32.constant(rng.randint(1, 0xFFFF))
            choice = rng.randint(0, 3)
            if choice == 0:
                acc = builder.add(acc, k, "")
            elif choice == 1:
                acc = builder.xor(acc, k, "")
            elif choice == 2:
                acc = builder.mul(acc, k, "")
            else:
                acc = builder.sub(acc, k, "")

        sink = builder.alloca(i32, name="ollvm.junk.sink")
        store = builder.store(acc, sink)
        store.is_volatile = True
        builder.br(target)

    return junk_bb


# =============================================================================
# --substitute
# =============================================================================


SUBSTITUTABLE_OPCODES = {
    llvm.Opcode.Add,
    llvm.Opcode.Sub,
    llvm.Opcode.Xor,
    llvm.Opcode.And,
    llvm.Opcode.Or,
    llvm.Opcode.Shl,
    llvm.Opcode.LShr,
    llvm.Opcode.AShr,
}


def _apply_k_inverse(
    builder: llvm.Builder,
    value: llvm.Value,
    k_inv_raw: int,
    ty: llvm.Type,
    rng: random.Random,
) -> llvm.Value:
    bw = ty.int_width
    if rng.randint(0, 1) == 0:
        k_inv_a = rng.getrandbits(min(bw, 64)) | 1
        k_inv_a = mask_to_width(k_inv_a, bw)
        if k_inv_a == 0:
            k_inv_a = 1
        k_inv_b = mask_to_width(k_inv_raw * mod_inverse_pow2(k_inv_a, bw), bw)
        tmp = builder.mul(value, int_constant(ty, k_inv_a), "")
        return builder.mul(tmp, int_constant(ty, k_inv_b), "")
    return builder.mul(value, int_constant(ty, k_inv_raw), "")


def substitute_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)

    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue

        work: list[llvm.Value] = []
        for inst in iter_instructions(fn):
            if inst.opcode not in SUBSTITUTABLE_OPCODES:
                continue
            if inst.type.kind != llvm.TypeKind.Integer:
                continue
            bw = inst.type.int_width
            if bw < 2 or bw > 64:
                continue
            a = inst.get_operand(0)
            b = inst.get_operand(1)
            if a.is_constant and b.is_constant:
                continue
            work.append(inst)

        for inst in work:
            if not should_transform(rng, cfg):
                continue
            if rng.randint(0, 1) == 0:
                continue

            a = inst.get_operand(0)
            b = inst.get_operand(1)
            ty = inst.type
            bw = ty.int_width

            with inst.block.create_builder() as builder:
                builder.position_before(inst)
                replacement: llvm.Value | None = None

                if inst.opcode == llvm.Opcode.Add:
                    variant = rng.randint(0, 2)
                    if variant == 0:
                        xv = builder.xor(a, b, "")
                        av = builder.and_(a, b, "")
                        mv = builder.add(av, av, "")
                        replacement = builder.add(xv, mv, "")
                    elif variant == 1:
                        ov = builder.or_(a, b, "")
                        av = builder.and_(a, b, "")
                        replacement = builder.add(ov, av, "")
                    else:
                        one = int_constant(ty, 1)
                        bp1 = builder.add(b, one, "")
                        am1 = builder.sub(a, one, "")
                        lhs = builder.mul(a, bp1, "")
                        rhs = builder.mul(b, am1, "")
                        replacement = builder.sub(lhs, rhs, "")

                elif inst.opcode == llvm.Opcode.Sub:
                    variant = rng.randint(0, 2)
                    if variant == 0:
                        xv = builder.xor(a, b, "")
                        na = builder.not_(a, "")
                        av = builder.and_(na, b, "")
                        mv = builder.add(av, av, "")
                        replacement = builder.sub(xv, mv, "")
                    elif variant == 1:
                        nb = builder.not_(b, "")
                        s1 = builder.add(a, nb, "")
                        replacement = builder.add(s1, int_constant(ty, 1), "")
                    else:
                        one = int_constant(ty, 1)
                        bp1 = builder.add(b, one, "")
                        ap1 = builder.add(a, one, "")
                        lhs = builder.mul(a, bp1, "")
                        rhs = builder.mul(b, ap1, "")
                        replacement = builder.sub(lhs, rhs, "")

                elif inst.opcode == llvm.Opcode.Xor:
                    variant = rng.randint(0, 2)
                    if variant == 0:
                        ov = builder.or_(a, b, "")
                        av = builder.and_(a, b, "")
                        replacement = builder.sub(ov, av, "")
                    elif variant == 1:
                        sv = builder.add(a, b, "")
                        av = builder.and_(a, b, "")
                        mv = builder.add(av, av, "")
                        replacement = builder.sub(sv, mv, "")
                    else:
                        k_raw = mask_to_width(rng.getrandbits(min(bw, 64)) | 1, bw)
                        if k_raw == 0:
                            k_raw = 1
                        k_inv_raw = mod_inverse_pow2(k_raw, bw)
                        k_val = int_constant(ty, k_raw)
                        sv = builder.add(a, b, "")
                        av = builder.and_(a, b, "")
                        sk = builder.mul(sv, k_val, "")
                        ak = builder.mul(av, k_val, "")
                        ak2 = builder.add(ak, ak, "")
                        diff = builder.sub(sk, ak2, "")
                        replacement = _apply_k_inverse(builder, diff, k_inv_raw, ty, rng)

                elif inst.opcode == llvm.Opcode.And:
                    variant = rng.randint(0, 1)
                    if variant == 0:
                        sv = builder.add(a, b, "")
                        ov = builder.or_(a, b, "")
                        replacement = builder.sub(sv, ov, "")
                    else:
                        k_raw = mask_to_width(rng.getrandbits(min(bw, 64)) | 1, bw)
                        if k_raw == 0:
                            k_raw = 1
                        k_inv_raw = mod_inverse_pow2(k_raw, bw)
                        k_val = int_constant(ty, k_raw)
                        sv = builder.add(a, b, "")
                        ov = builder.or_(a, b, "")
                        sk = builder.mul(sv, k_val, "")
                        ok = builder.mul(ov, k_val, "")
                        diff = builder.sub(sk, ok, "")
                        replacement = _apply_k_inverse(builder, diff, k_inv_raw, ty, rng)

                elif inst.opcode == llvm.Opcode.Or:
                    variant = rng.randint(0, 2)
                    if variant == 0:
                        xv = builder.xor(a, b, "")
                        av = builder.and_(a, b, "")
                        replacement = builder.add(xv, av, "")
                    elif variant == 1:
                        sv = builder.add(a, b, "")
                        av = builder.and_(a, b, "")
                        replacement = builder.sub(sv, av, "")
                    else:
                        k_raw = mask_to_width(rng.getrandbits(min(bw, 64)) | 1, bw)
                        if k_raw == 0:
                            k_raw = 1
                        k_inv_raw = mod_inverse_pow2(k_raw, bw)
                        k_val = int_constant(ty, k_raw)
                        xv = builder.xor(a, b, "")
                        av = builder.and_(a, b, "")
                        xk = builder.mul(xv, k_val, "")
                        ak = builder.mul(av, k_val, "")
                        total = builder.add(xk, ak, "")
                        replacement = _apply_k_inverse(builder, total, k_inv_raw, ty, rng)

                elif inst.opcode == llvm.Opcode.Shl:
                    one = int_constant(ty, 1)
                    pow2 = builder.shl(one, b, "")
                    replacement = builder.mul(a, pow2, "")

                elif inst.opcode == llvm.Opcode.LShr:
                    if b.is_constant_int and b.const_zext_value < bw:
                        one = int_constant(ty, 1)
                        pow2 = builder.shl(one, b, "")
                        replacement = builder.udiv(a, pow2, "")

                elif inst.opcode == llvm.Opcode.AShr:
                    if b.is_constant_int and b.const_zext_value < bw:
                        one = int_constant(ty, 1)
                        pow2 = builder.shl(one, b, "")
                        zero = int_constant(ty, 0)
                        is_neg = builder.icmp(llvm.IntPredicate.SLT, a, zero, "")
                        pos_result = builder.udiv(a, pow2, "")
                        not_a = builder.not_(a, "")
                        neg_div = builder.udiv(not_a, pow2, "")
                        neg_result = builder.not_(neg_div, "")
                        replacement = builder.select(is_neg, neg_result, pos_result, "")

            if replacement is not None:
                inst.replace_all_uses_with(replacement)
                inst.erase_from_parent()


# =============================================================================
# --if-convert
# =============================================================================


def _all_safe_to_speculate(bb: llvm.BasicBlock) -> bool:
    for inst in bb.instructions:
        if inst.opcode == llvm.Opcode.PHI or inst.is_terminator:
            continue
        if inst.opcode not in SAFE_SPECULATE_OPCODES:
            return False
    return True


def _incoming_value_for_block(phi: llvm.Value, bb: llvm.BasicBlock) -> llvm.Value | None:
    for i in range(phi.num_incoming):
        if phi.get_incoming_block(i) == bb:
            return phi.get_incoming_value(i)
    return None


def try_convert_diamond(bb: llvm.BasicBlock) -> bool:
    cond_br = bb.terminator
    if cond_br is None or cond_br.opcode != llvm.Opcode.Br or not cond_br.is_conditional:
        return False

    true_bb, false_bb = list(cond_br.successors)

    true_preds = list(true_bb.predecessors)
    false_preds = list(false_bb.predecessors)
    if len(true_preds) != 1 or true_preds[0] != bb:
        return False
    if len(false_preds) != 1 or false_preds[0] != bb:
        return False

    true_br = true_bb.terminator
    false_br = false_bb.terminator
    if true_br is None or false_br is None:
        return False
    if true_br.opcode != llvm.Opcode.Br or true_br.is_conditional:
        return False
    if false_br.opcode != llvm.Opcode.Br or false_br.is_conditional:
        return False

    merge_bb = true_br.get_successor(0)
    if false_br.get_successor(0) != merge_bb:
        return False

    if not _all_safe_to_speculate(true_bb) or not _all_safe_to_speculate(false_bb):
        return False

    phis = list(merge_bb.phis)
    for phi in phis:
        if phi.num_incoming != 2:
            return False
        has_true = False
        has_false = False
        for i in range(2):
            incoming_bb = phi.get_incoming_block(i)
            if incoming_bb == true_bb:
                has_true = True
            if incoming_bb == false_bb:
                has_false = True
        if not has_true or not has_false:
            return False

    to_move: list[llvm.Value] = []
    for src in (true_bb, false_bb):
        for inst in src.instructions:
            if inst.opcode == llvm.Opcode.PHI or inst.is_terminator:
                continue
            to_move.append(inst)

    for inst in to_move:
        inst.move_before(cond_br)

    with bb.create_builder() as builder:
        builder.position_before(cond_br)
        cond = cond_br.condition
        for phi in phis:
            true_val = _incoming_value_for_block(phi, true_bb)
            false_val = _incoming_value_for_block(phi, false_bb)
            if true_val is None or false_val is None:
                return False
            sel = builder.select(cond, true_val, false_val, phi.name)
            phi.replace_all_uses_with(sel)
            phi.erase_from_parent()

        builder.br(merge_bb)
        cond_br.erase_from_parent()

    return True


def if_convert_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)

    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue

        worklist = list(fn.basic_blocks)
        deleted: set[llvm.BasicBlock] = set()

        for bb in worklist:
            if bb in deleted:
                continue
            if not should_transform(rng, cfg):
                continue

            term = bb.terminator
            if term is None or term.opcode != llvm.Opcode.Br or not term.is_conditional:
                continue
            true_bb, false_bb = list(term.successors)
            if try_convert_diamond(bb):
                deleted.add(true_bb)
                deleted.add(false_bb)


# =============================================================================
# --flatten (simplified state-machine port)
# =============================================================================


def generate_unique_state(rng: random.Random, existing: set[int]) -> int:
    while True:
        state = rng.randint(0x000F0000, 0x7FFFFFFF)
        if state not in existing:
            existing.add(state)
            return state


def demote_phi_to_stack(fn: llvm.Function) -> None:
    phi_nodes: list[llvm.Value] = []
    for bb in fn.basic_blocks:
        for inst in bb.instructions:
            if inst.opcode == llvm.Opcode.PHI:
                phi_nodes.append(inst)

    if not phi_nodes:
        return

    entry = fn.entry_block
    first_insert = entry.first_non_phi or entry.first_instruction
    if first_insert is None:
        return

    for phi in phi_nodes:
        phi_bb = phi.block
        with entry.create_builder() as builder:
            builder.position_before(first_insert)
            alloca = builder.alloca(phi.type, name=f"{phi.name}.demoted")

        for i in range(phi.num_incoming):
            incoming_val = phi.get_incoming_value(i)
            incoming_bb = phi.get_incoming_block(i)
            incoming_term = incoming_bb.terminator
            with incoming_bb.create_builder() as builder:
                builder.position_before(incoming_term)
                builder.store(incoming_val, alloca)

        with phi_bb.create_builder() as builder:
            builder.position_before(phi)
            load = builder.load(phi.type, alloca, phi.name)

        phi.replace_all_uses_with(load)
        phi.erase_from_parent()


def flatten_function(fn: llvm.Function, rng: random.Random, cfg: FilterConfig) -> None:
    blocks = list(fn.basic_blocks)
    if should_skip_function(fn, cfg) or len(blocks) < 2:
        return
    if not should_transform(rng, cfg):
        return

    demote_phi_to_stack(fn)

    ctx = fn.context
    mod = fn.module
    i32 = ctx.types.i32
    entry = blocks[0]
    original_blocks = blocks[1:]
    if not original_blocks:
        return

    states: set[int] = set()
    block_state: dict[llvm.BasicBlock, int] = {
        bb: generate_unique_state(rng, states) for bb in original_blocks
    }

    first_inst = entry.first_non_phi or entry.first_instruction
    if first_inst is None:
        return

    with entry.create_builder() as builder:
        builder.position_before(first_inst)
        state_var = builder.alloca(i32, name="cff.state")
        builder.store(i32.constant(0), state_var)

    dispatch_bb = fn.append_basic_block("cff.dispatch")
    cond_blocks = [fn.append_basic_block(f"cff.cond.{i}") for i in range(len(original_blocks))]

    combined = list(zip(cond_blocks, original_blocks))
    rng.shuffle(combined)
    cond_blocks = [c for c, _ in combined]
    shuffled_originals = [b for _, b in combined]

    default_bb = fn.append_basic_block("cff.default")
    with default_bb.create_builder() as builder:
        builder.br(dispatch_bb)

    with dispatch_bb.create_builder() as builder:
        builder.br(cond_blocks[0])

    for i, (cond_bb, target_bb) in enumerate(zip(cond_blocks, shuffled_originals)):
        with cond_bb.create_builder() as builder:
            current_state = builder.load(i32, state_var, "cff.state.val")
            target_state = block_state[target_bb]
            target_val = i32.constant(target_state)
            cmp = builder.icmp(llvm.IntPredicate.EQ, current_state, target_val, "cff.cmp")
            next_cond = cond_blocks[i + 1] if i < len(cond_blocks) - 1 else default_bb
            builder.cond_br(cmp, target_bb, next_cond)

    for bb in original_blocks + [entry]:
        term = bb.terminator
        if term is None or term.opcode != llvm.Opcode.Br:
            continue

        successors = list(term.successors)
        if len(successors) == 1:
            target = successors[0]
            if target in block_state:
                with bb.create_builder() as builder:
                    builder.position_before(term)
                    builder.store(i32.constant(block_state[target]), state_var)
                    builder.br(dispatch_bb)
                term.erase_from_parent()
        elif len(successors) == 2:
            true_bb, false_bb = successors
            if true_bb in block_state and false_bb in block_state:
                cond = term.condition
                true_state_bb = fn.append_basic_block("cff.true_state")
                false_state_bb = fn.append_basic_block("cff.false_state")

                with true_state_bb.create_builder() as builder:
                    builder.store(i32.constant(block_state[true_bb]), state_var)
                    builder.br(dispatch_bb)

                with false_state_bb.create_builder() as builder:
                    builder.store(i32.constant(block_state[false_bb]), state_var)
                    builder.br(dispatch_bb)

                with bb.create_builder() as builder:
                    builder.position_before(term)
                    builder.cond_br(cond, true_state_bb, false_state_bb)

                term.erase_from_parent()


def flatten_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)
    for fn in mod.functions:
        flatten_function(fn, rng, cfg)


# =============================================================================
# --opaque-predicates
# =============================================================================


def insert_opaque_predicates_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)

    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue
        if len(list(fn.basic_blocks)) <= 1:
            continue

        candidates: list[llvm.BasicBlock] = []
        for bb in fn.basic_blocks:
            term = bb.terminator
            if term is None or term.opcode == llvm.Opcode.Ret:
                continue
            if term.opcode == llvm.Opcode.Br and not term.is_conditional:
                candidates.append(bb)

        if not candidates:
            continue

        fallback: list[llvm.Value | None] = [None]
        for bb in candidates:
            if not should_transform(rng, cfg):
                continue
            if rng.randint(0, 99) >= 40:
                continue

            br = bb.terminator
            original_target = br.get_successor(0)
            with bb.create_builder() as builder:
                builder.position_before(br)
                x = get_predicate_input(fn, builder, fallback)
                cond = generate_opaque_true(builder, x, rng.getrandbits(64))
                junk_bb = create_opaque_junk_block(fn, original_target, rng)
                builder.cond_br(cond, original_target, junk_bb)
                br.erase_from_parent()

            for phi in list(original_target.phis):
                incoming = _incoming_value_for_block(phi, bb)
                if incoming is not None:
                    phi.add_incoming(incoming, junk_bb)


# =============================================================================
# --bogus-control-flow
# =============================================================================


def _is_eligible_bcf_block(bb: llvm.BasicBlock) -> bool:
    fn = bb.function
    if bb == fn.entry_block:
        return False
    if len(list(bb.instructions)) <= 1:
        return False
    term = bb.terminator
    if term is None or term.opcode != llvm.Opcode.Br or term.is_conditional:
        return False
    return True


def create_bcf_junk_block(
    fn: llvm.Function,
    target: llvm.BasicBlock,
    rng: random.Random,
    sink_alloca: llvm.Value,
) -> llvm.BasicBlock:
    ctx = fn.context
    i64 = ctx.types.i64
    junk_bb = fn.append_basic_block("")

    with junk_bb.create_builder() as builder:
        sources: list[llvm.Value] = []
        sources.append(builder.load(i64, sink_alloca, ""))

        for arg in fn.params:
            if arg.type.kind == llvm.TypeKind.Integer:
                sources.append(zext_or_trunc(builder, arg, i64))
            elif arg.type.kind == llvm.TypeKind.Pointer:
                sources.append(builder.ptrtoint(arg, i64, ""))

        if len(sources) < 2:
            sources.append(int_constant(i64, rng.randint(1, 0xFFFFFFFF)))
            sources.append(int_constant(i64, rng.randint(1, 0xFFFFFFFF)))

        def pick_source() -> llvm.Value:
            return sources[rng.randrange(len(sources))]

        acc = pick_source()
        for _ in range(rng.randint(3, 6)):
            operand = pick_source()
            choice = rng.randint(0, 5)
            if choice == 0:
                acc = builder.add(acc, operand, "")
            elif choice == 1:
                acc = builder.sub(acc, operand, "")
            elif choice == 2:
                acc = builder.xor(acc, operand, "")
            elif choice == 3:
                acc = builder.mul(acc, operand, "")
            elif choice == 4:
                acc = builder.and_(acc, operand, "")
            else:
                acc = builder.or_(acc, operand, "")

        builder.store(acc, sink_alloca)
        builder.br(target)

    return junk_bb


def insert_bogus_control_flow_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)

    for fn in mod.functions:
        if should_skip_function(fn, cfg) or len(list(fn.basic_blocks)) <= 1:
            continue

        candidates = [bb for bb in fn.basic_blocks if _is_eligible_bcf_block(bb)]
        if not candidates:
            continue

        selected = [
            bb
            for bb in candidates
            if should_transform(rng, cfg) and rng.randint(0, 99) < 30
        ]
        if not selected:
            continue

        ctx = fn.context
        i32 = ctx.types.i32
        opaque_input: llvm.Value | None = None

        for arg in fn.params:
            if arg.type.kind == llvm.TypeKind.Integer:
                with fn.entry_block.create_builder(first_non_phi=True) as builder:
                    opaque_input = zext_or_trunc(builder, arg, i32)
                break

        if opaque_input is None:
            for arg in fn.params:
                if arg.type.kind == llvm.TypeKind.Pointer:
                    with fn.entry_block.create_builder(first_non_phi=True) as builder:
                        opaque_input = builder.ptrtoint(arg, i32, "")
                    break

        if opaque_input is None:
            gv = mod.add_global(i32, "__ollvm_bcf_seed")
            gv.initializer = i32.constant(0)
            gv.linkage = llvm.Linkage.Private
            gv.is_global_constant = False
            with fn.entry_block.create_builder(first_non_phi=True) as builder:
                opaque_input = builder.load(i32, gv, "")
                opaque_input.is_volatile = True

        i64 = ctx.types.i64
        with fn.entry_block.create_builder(first_non_phi=True) as builder:
            sink_alloca = builder.alloca(i64, name="ollvm.bcf.sink")
            builder.store(i64.constant(0), sink_alloca)

        for orig_bb in selected:
            br = orig_bb.terminator
            successor = br.get_successor(0)
            junk_bb = create_bcf_junk_block(fn, successor, rng, sink_alloca)

            for phi in list(successor.phis):
                phi.add_incoming(phi.type.undef(), junk_bb)

            with orig_bb.create_builder() as builder:
                builder.position_before(br)
                flip = rng.randint(0, 1) == 1
                seed_val = rng.getrandbits(64)
                cond = (
                    generate_opaque_false(builder, opaque_input, seed_val)
                    if flip
                    else generate_opaque_true(builder, opaque_input, seed_val)
                )
                if flip:
                    builder.cond_br(cond, junk_bb, successor)
                else:
                    builder.cond_br(cond, successor, junk_bb)
                br.erase_from_parent()


# =============================================================================
# --const-unfold
# =============================================================================


def _is_operand_replaceable(inst: llvm.Value, idx: int) -> bool:
    if inst.opcode in {
        llvm.Opcode.Add,
        llvm.Opcode.Sub,
        llvm.Opcode.Mul,
        llvm.Opcode.UDiv,
        llvm.Opcode.SDiv,
        llvm.Opcode.URem,
        llvm.Opcode.SRem,
        llvm.Opcode.Shl,
        llvm.Opcode.LShr,
        llvm.Opcode.AShr,
        llvm.Opcode.And,
        llvm.Opcode.Or,
        llvm.Opcode.Xor,
        llvm.Opcode.ICmp,
    }:
        return True
    return inst.opcode == llvm.Opcode.Store and idx == 0


@dataclass(slots=True)
class UnfoldCandidate:
    inst: llvm.Value
    operand_idx: int
    const: llvm.Value


def emit_anchor_load(
    builder: llvm.Builder, anchor: llvm.Value, target_ty: llvm.Type
) -> llvm.Value:
    i64 = anchor.global_value_type
    load = builder.load(i64, anchor, "")
    load.is_volatile = True
    if target_ty.int_width == 64:
        return load
    if target_ty.int_width < 64:
        return builder.trunc(load, target_ty, "")
    return builder.zext(load, target_ty, "")


def unfold_constants_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)
    ctx = mod.context
    i64 = ctx.types.i64

    anchors: list[llvm.Value] = []
    for i in range(rng.randint(3, 5)):
        init_val = rng.getrandbits(32) or 1
        gv = mod.add_global(i64, f"__ollvm_anchor_{i}")
        gv.initializer = int_constant(i64, init_val)
        gv.linkage = llvm.Linkage.Internal
        gv.is_global_constant = False
        anchors.append(gv)

    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue

        anchor = anchors[rng.randrange(len(anchors))]
        work: list[UnfoldCandidate] = []
        for bb in fn.basic_blocks:
            for inst in bb.instructions:
                if inst.opcode == llvm.Opcode.PHI:
                    continue
                for i in range(inst.num_operands):
                    ci = inst.get_operand(i)
                    if not ci.is_constant_int:
                        continue
                    bw = ci.type.int_width
                    if bw == 1 or bw > 64:
                        continue
                    val = ci.const_sext_value
                    if val in (0, 1, -1):
                        continue
                    if not _is_operand_replaceable(inst, i):
                        continue
                    work.append(UnfoldCandidate(inst, i, ci))

        if not work:
            continue

        entry = fn.entry_block
        with entry.create_builder(first_non_phi=True) as builder:
            cur_val = builder.load(i64, anchor, "")
            cur_val.is_volatile = True
            noise = build_mba_zero(builder, cur_val, rng.getrandbits(64))
            new_val = builder.xor(cur_val, noise, "")
            builder.store(new_val, anchor)

        anchor_init = anchor.initializer
        assert anchor_init is not None
        r_init = anchor_init.const_zext_value
        for cand in work:
            if not should_transform(rng, cfg):
                continue
            if rng.randint(0, 99) >= 40:
                continue

            ty = cand.const.type
            bits = ty.int_width
            c = cand.const.const_zext_value
            with cand.inst.block.create_builder() as builder:
                builder.position_before(cand.inst)
                anchor_val = emit_anchor_load(builder, anchor, ty)
                r_trunc = mask_to_width(r_init, bits)
                strat = rng.randint(0, 2)

                if strat == 0:
                    c_val = int_constant(ty, c ^ r_trunc)
                    xored = builder.xor(c_val, anchor_val, "")
                    mba_zero = build_mba_zero(builder, anchor_val, rng.getrandbits(64))
                    replacement = builder.xor(xored, mba_zero, "")
                elif strat == 1:
                    c_val = int_constant(ty, c)
                    added = builder.add(c_val, anchor_val, "")
                    subbed = builder.sub(added, anchor_val, "")
                    mba_zero = build_mba_zero(builder, anchor_val, rng.getrandbits(64))
                    replacement = builder.add(subbed, mba_zero, "")
                else:
                    c_val = int_constant(ty, c)
                    or1 = builder.or_(c_val, anchor_val, "")
                    not_anchor = builder.not_(anchor_val, "")
                    or2 = builder.or_(c_val, not_anchor, "")
                    replacement = builder.and_(or1, or2, "")

            cand.inst.set_operand(cand.operand_idx, replacement)


# =============================================================================
# --string-encrypt
# =============================================================================


@dataclass(slots=True)
class StringUseSite:
    old_value: llvm.Value
    instruction: llvm.Value


def encrypt_strings_module(mod: llvm.Module, seed: int) -> None:
    rng = random.Random(seed)
    ctx = mod.context
    i8 = ctx.types.i8
    i32 = ctx.types.i32

    strings: list[tuple[llvm.Value, bytes]] = []
    for gv in mod.globals:
        init = gv.initializer
        if init is None or not gv.is_global_constant:
            continue
        if gv.section:
            continue
        if not init.is_constant_data_array:
            continue

        size, raw = init.raw_data_values
        if len(raw) != size:
            continue
        if len(raw) <= 4:
            continue
        if raw[-1] != 0:
            continue
        strings.append((gv, raw))

    for index, (gv, raw_bytes) in enumerate(strings):
        key = rng.getrandbits(32) or 1
        state = key & 0xFF
        encrypted = bytearray(raw_bytes)
        for i, byte in enumerate(encrypted):
            encrypted[i] = byte ^ state
            state = ((state * 31) + 17 + encrypted[i]) & 0xFF

        enc_const = i8.array_const(bytes(encrypted))
        enc_gv = mod.add_global(enc_const.type, f"__ollvm_str_enc_{index}")
        enc_gv.initializer = enc_const
        enc_gv.linkage = llvm.Linkage.Private
        enc_gv.is_global_constant = True

        key_gv = mod.add_global(i32, f"__ollvm_str_key_{index}")
        key_gv.initializer = int_constant(i32, key)
        key_gv.linkage = llvm.Linkage.Private
        key_gv.is_global_constant = True

        func_uses: dict[llvm.Function, list[StringUseSite]] = {}
        for use in gv.uses:
            user = use.user
            if user.is_instruction:
                fn = user.block.function
                func_uses.setdefault(fn, []).append(StringUseSite(gv, user))
                continue
            if not user.is_constant_expr:
                continue
            for ce_use in user.uses:
                ce_user = ce_use.user
                if ce_user.is_instruction:
                    fn = ce_user.block.function
                    func_uses.setdefault(fn, []).append(StringUseSite(user, ce_user))

        for fn, sites in func_uses.items():
            entry = fn.entry_block
            arr_ty = i8.array(len(raw_bytes))
            with entry.create_builder(first_non_phi=True) as alloca_builder:
                buf = alloca_builder.alloca(arr_ty, name=f"ollvm.str.buf.{index}")

            insert_before = None
            for inst in entry.instructions:
                if inst.opcode != llvm.Opcode.Alloca:
                    insert_before = inst
                    break

            with entry.create_builder() as builder:
                if insert_before is not None:
                    builder.position_before(insert_before)
                else:
                    builder.position_at_end(entry)

                key_load = builder.load(i32, key_gv, "")
                state_val = builder.trunc(key_load, i8, "")
                c31 = int_constant(i8, 31)
                c17 = int_constant(i8, 17)

                for i, _ in enumerate(raw_bytes):
                    gep_enc = builder.gep(
                        enc_const.type,
                        enc_gv,
                        [i32.constant(0, False), i32.constant(i, False)],
                        "",
                    )
                    enc_byte = builder.load(i8, gep_enc, "")
                    dec_byte = builder.xor(enc_byte, state_val, "")
                    gep_buf = builder.gep(
                        arr_ty,
                        buf,
                        [i32.constant(0, False), i32.constant(i, False)],
                        "",
                    )
                    builder.store(dec_byte, gep_buf)
                    mul = builder.mul(state_val, c31, "")
                    add1 = builder.add(mul, c17, "")
                    state_val = builder.add(add1, enc_byte, "")

                buf_ptr = builder.gep(
                    arr_ty,
                    buf,
                    [i32.constant(0, False), i32.constant(0, False)],
                    "",
                )

                for site in sites:
                    site.instruction.replace_uses_of_with(site.old_value, buf_ptr)


# =============================================================================
# --arith-encode
# =============================================================================


def _is_candidate_alloca(ai: llvm.Value) -> bool:
    if ai.block != ai.block.function.entry_block:
        return False
    ty = ai.allocated_type
    if ty.kind != llvm.TypeKind.Integer or ty.int_width not in {32, 64}:
        return False
    if ai.is_array_allocation:
        return False

    for user in ai.users:
        if not user.is_instruction:
            return False
        if user.opcode == llvm.Opcode.Load:
            if user.is_volatile or user.is_atomic:
                return False
            if user.get_operand(0) != ai:
                return False
        elif user.opcode == llvm.Opcode.Store:
            if user.is_volatile or user.is_atomic:
                return False
            if user.get_operand(1) != ai:
                return False
            if user.get_operand(0).type != ty:
                return False
        else:
            return False
    return True


def encode_allocas_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)

    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue

        candidates = [
            inst
            for inst in fn.entry_block.instructions
            if inst.opcode == llvm.Opcode.Alloca and _is_candidate_alloca(inst)
        ]

        for ai in candidates:
            if not should_transform(rng, cfg):
                continue

            ty = ai.allocated_type
            bits = ty.int_width
            a = mask_to_width(rng.getrandbits(min(bits, 64)) | 1, bits)
            if a == 1:
                a = 3
            b = mask_to_width(rng.getrandbits(min(bits, 64)), bits)
            a_inv = mod_inverse_pow2(a, bits)

            const_a = int_constant(ty, a)
            const_b = int_constant(ty, b)
            const_a_inv = int_constant(ty, a_inv)

            loads: list[llvm.Value] = []
            stores: list[llvm.Value] = []
            for user in ai.users:
                if user.opcode == llvm.Opcode.Load:
                    loads.append(user)
                elif user.opcode == llvm.Opcode.Store:
                    stores.append(user)

            for store in stores:
                with store.block.create_builder() as builder:
                    builder.position_before(store)
                    val = store.get_operand(0)
                    mul_val = builder.mul(val, const_a, "")
                    encoded = builder.add(mul_val, const_b, "")
                store.set_operand(0, encoded)

            for load in loads:
                with load.block.create_builder() as builder:
                    position_after_instruction(builder, load)
                    sub_inst = builder.sub(load, const_b, "")
                    decoded = builder.mul(sub_inst, const_a_inv, "")
                load.replace_all_uses_with(decoded)
                sub_inst.set_operand(0, load)


# =============================================================================
# --reg-pressure
# =============================================================================


def collect_anchor_values(fn: llvm.Function) -> list[llvm.Value]:
    anchors: list[llvm.Value] = []

    def eligible_type(ty: llvm.Type) -> bool:
        return ty.kind in {llvm.TypeKind.Integer, llvm.TypeKind.Pointer}

    for arg in fn.params:
        if not eligible_type(arg.type):
            continue
        use_blocks = {
            user.block
            for user in arg.users
            if user.is_instruction
        }
        if len(use_blocks) > 1:
            anchors.append(arg)

    for inst in iter_instructions(fn):
        if not eligible_type(inst.type):
            continue
        use_blocks = {
            user.block
            for user in inst.users
            if user.is_instruction
        }
        if len(use_blocks) > 1:
            anchors.append(inst)

    return anchors


def insert_register_anchor(value: llvm.Value, use) -> None:
    user_inst = use.user
    with user_inst.block.create_builder() as builder:
        builder.position_before(user_inst)
        val_ty = value.type
        is_ptr = val_ty.kind == llvm.TypeKind.Pointer
        asm_int_ty = value.context.types.i64 if is_ptr else val_ty
        int_val = builder.ptrtoint(value, asm_int_ty, "") if is_ptr else value
        asm_ty = value.context.types.function(asm_int_ty, [asm_int_ty])
        asm_val = asm_ty.inline_asm(
            "",
            "=r,0",
            True,
            False,
            llvm.InlineAsmDialect.ATT,
            False,
        )
        result = builder.call(asm_ty, asm_val, [int_val], "")
        final_val = builder.inttoptr(result, val_ty, "") if is_ptr else result
    user_inst.replace_uses_of_with(value, final_val)



def extend_register_pressure_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)

    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue

        anchors = collect_anchor_values(fn)
        rng.shuffle(anchors)
        inserted = 0
        for value in anchors:
            if inserted >= 5:
                break
            if not should_transform(rng, cfg):
                continue
            if rng.randint(1, 100) > 40:
                continue

            def_bb = value.block if value.is_instruction else None
            cross_block_uses = []
            for use in value.uses:
                user_inst = use.user
                if not user_inst.is_instruction:
                    continue
                if user_inst.opcode == llvm.Opcode.PHI:
                    continue
                if def_bb is not None and user_inst.block == def_bb:
                    continue
                first_inst = user_inst.block.first_instruction
                if first_inst is not None and first_inst.opcode == llvm.Opcode.LandingPad:
                    continue
                cross_block_uses.append(use)

            if not cross_block_uses:
                continue

            chosen = cross_block_uses[rng.randrange(len(cross_block_uses))]
            insert_register_anchor(value, chosen)
            inserted += 1


# =============================================================================
# --stack-randomize
# =============================================================================


def is_fixed_size_alloca(ai: llvm.Value) -> bool:
    if not ai.is_array_allocation:
        return True
    return ai.array_size.is_constant_int



def random_padding_type(ctx: llvm.Context, rng: random.Random) -> llvm.Type:
    pick = rng.randint(0, 4)
    if pick == 0:
        return ctx.types.i8
    if pick == 1:
        return ctx.types.i16
    if pick == 2:
        return ctx.types.i32
    if pick == 3:
        return ctx.types.i64
    return ctx.types.array(ctx.types.i8, rng.randint(1, 64))



def randomize_stack_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)

    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue
        if not should_transform(rng, cfg):
            continue

        entry = fn.entry_block
        allocas = [
            inst
            for inst in entry.instructions
            if inst.opcode == llvm.Opcode.Alloca and is_fixed_size_alloca(inst)
        ]
        if entry.first_instruction is None:
            continue

        first_non_phi = entry.first_non_phi or entry.first_instruction
        if first_non_phi is None:
            continue

        padding_allocas: list[llvm.Value] = []
        for i in range(rng.randint(2, 6)):
            with entry.create_builder(first_non_phi=True) as builder:
                pad_ai = builder.alloca(random_padding_type(fn.context, rng), name=f"ollvm.pad.{i}")
            padding_allocas.append(pad_ai)

            with entry.create_builder() as builder:
                builder.position_before(entry.terminator)
                ty = pad_ai.allocated_type
                if ty.kind == llvm.TypeKind.Integer:
                    val = int_constant(ty, rng.getrandbits(min(ty.int_width, 64)))
                else:
                    val = ty.null()
                builder.store(val, pad_ai)

        all_allocas = allocas + padding_allocas
        rng.shuffle(all_allocas)
        for ai in all_allocas:
            ai.move_before(first_non_phi)


# =============================================================================
# --vectorize
# =============================================================================


@dataclass(slots=True)
class VectorizeOptions:
    vectorize_data: bool = True
    vectorize_bitwise: bool = False
    vectorize_i64: bool = True
    transform_percent: int = 40


SUPPORTED_VECTOR_OPS = {
    llvm.Opcode.Add,
    llvm.Opcode.Sub,
    llvm.Opcode.Xor,
    llvm.Opcode.And,
    llvm.Opcode.Or,
    llvm.Opcode.Mul,
}



def _vector_lane_count(bits: int) -> int:
    return 2 if bits == 64 else 4



def vectorize_stack_data(fn: llvm.Function, bits: int) -> None:
    ctx = fn.context
    scalar_ty = ctx.types.i64 if bits == 64 else ctx.types.i32
    vec_ty = scalar_ty.vector(_vector_lane_count(bits))
    entry = fn.entry_block

    candidates: list[llvm.Value] = []
    for alloca in entry.instructions:
        if alloca.opcode != llvm.Opcode.Alloca:
            continue
        if alloca.allocated_type != scalar_ty:
            continue
        if alloca.is_array_allocation and not alloca.array_size.is_constant_int:
            continue

        supported = True
        for user in alloca.users:
            if not user.is_instruction:
                supported = False
                break
            if user.opcode == llvm.Opcode.Load:
                if user.is_volatile or user.is_atomic or user.get_operand(0) != alloca:
                    supported = False
                    break
            elif user.opcode == llvm.Opcode.Store:
                if user.is_volatile or user.is_atomic or user.get_operand(1) != alloca:
                    supported = False
                    break
                if user.get_operand(0).type != scalar_ty:
                    supported = False
                    break
            else:
                if user.intrinsic_id == 0:
                    supported = False
                    break
        if supported:
            candidates.append(alloca)

    for alloca in candidates:
        insert_before = alloca.next_instruction or entry.terminator
        with entry.create_builder() as builder:
            builder.position_before(insert_before)
            vec_alloca = builder.alloca(vec_ty, name=f"{alloca.name}.vec")
            vec_alloca.alignment = 16

        users = [u for u in alloca.users if u.is_instruction]
        real_users = False
        for user in users:
            if user.opcode == llvm.Opcode.Load:
                real_users = True
                with user.block.create_builder() as builder:
                    builder.position_before(user)
                    vload = builder.load(vec_ty, vec_alloca, "")
                    vload.inst_alignment = 16
                    lane0 = builder.extract_element(
                        vload, ctx.types.i32.constant(0, False), ""
                    )
                user.replace_all_uses_with(lane0)
                user.erase_from_parent()
            elif user.opcode == llvm.Opcode.Store:
                real_users = True
                with user.block.create_builder() as builder:
                    builder.position_before(user)
                    packed = builder.vector_splat(_vector_lane_count(bits), user.get_operand(0), "")
                    store = builder.store(packed, vec_alloca)
                    store.inst_alignment = 16
                user.erase_from_parent()
            else:
                user.erase_from_parent()

        if len(list(alloca.users)) == 0:
            alloca.erase_from_parent()
        if not real_users and len(list(vec_alloca.users)) == 0:
            vec_alloca.erase_from_parent()



def _vector_const(builder: llvm.Builder, scalar: llvm.Value, bits: int, count: int) -> llvm.Value:
    return builder.vector_splat(count, scalar, "")



def _build_vector_bitwise_add(
    builder: llvm.Builder, lhs: llvm.Value, rhs: llvm.Value, one_vec: llvm.Value
) -> llvm.Value:
    total = builder.xor(lhs, rhs, "")
    carry = builder.and_(lhs, rhs, "")
    carry2 = builder.shl(carry, one_vec, "")
    return builder.add(total, carry2, "")



def _build_vector_bitwise_sub(
    builder: llvm.Builder,
    lhs: llvm.Value,
    rhs: llvm.Value,
    one_vec: llvm.Value,
    all_ones_vec: llvm.Value,
) -> llvm.Value:
    diff = builder.xor(lhs, rhs, "")
    not_lhs = builder.xor(lhs, all_ones_vec, "")
    borrow = builder.and_(not_lhs, rhs, "")
    borrow2 = builder.shl(borrow, one_vec, "")
    return builder.sub(diff, borrow2, "")



def _build_vector_bitwise_mul(
    builder: llvm.Builder, lhs: llvm.Value, rhs: llvm.Value, bits: int, count: int
) -> llvm.Value:
    lane_ty = lhs.type.element_type
    half = bits // 2
    half_vec = builder.vector_splat(count, int_constant(lane_ty, half), "")
    mask_vec = builder.vector_splat(count, int_constant(lane_ty, (1 << half) - 1), "")
    a_lo = builder.and_(lhs, mask_vec, "")
    a_hi = builder.lshr(lhs, half_vec, "")
    b_lo = builder.and_(rhs, mask_vec, "")
    b_hi = builder.lshr(rhs, half_vec, "")
    lo_lo = builder.mul(a_lo, b_lo, "")
    lo_hi = builder.shl(builder.mul(a_lo, b_hi, ""), half_vec, "")
    hi_lo = builder.shl(builder.mul(a_hi, b_lo, ""), half_vec, "")
    return builder.add(lo_lo, builder.add(lo_hi, hi_lo, ""), "")



def _build_vector_binop(
    builder: llvm.Builder,
    opcode: llvm.Opcode,
    va: llvm.Value,
    vb: llvm.Value,
    bits: int,
    count: int,
    opts: VectorizeOptions,
) -> llvm.Value:
    if not opts.vectorize_bitwise:
        if opcode == llvm.Opcode.Add:
            return builder.add(va, vb, "")
        if opcode == llvm.Opcode.Sub:
            return builder.sub(va, vb, "")
        if opcode == llvm.Opcode.Xor:
            return builder.xor(va, vb, "")
        if opcode == llvm.Opcode.And:
            return builder.and_(va, vb, "")
        if opcode == llvm.Opcode.Or:
            return builder.or_(va, vb, "")
        return builder.mul(va, vb, "")

    lane_ty = va.type.element_type
    one_vec = builder.vector_splat(count, int_constant(lane_ty, 1), "")
    all_ones_vec = builder.vector_splat(count, int_constant(lane_ty, -1), "")
    if opcode == llvm.Opcode.Add:
        return _build_vector_bitwise_add(builder, va, vb, one_vec)
    if opcode == llvm.Opcode.Sub:
        return _build_vector_bitwise_sub(builder, va, vb, one_vec, all_ones_vec)
    if opcode == llvm.Opcode.Mul:
        return _build_vector_bitwise_mul(builder, va, vb, bits, count)
    if opcode == llvm.Opcode.Xor:
        return builder.xor(va, vb, "")
    if opcode == llvm.Opcode.And:
        return builder.and_(va, vb, "")
    return builder.or_(va, vb, "")



def vectorize_module(
    mod: llvm.Module,
    seed: int,
    opts: VectorizeOptions,
    cfg: FilterConfig,
) -> None:
    rng = random.Random(seed)

    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue

        if opts.vectorize_data:
            vectorize_stack_data(fn, 32)
            if opts.vectorize_i64:
                vectorize_stack_data(fn, 64)

        work = []
        for inst in iter_instructions(fn):
            if inst.opcode not in SUPPORTED_VECTOR_OPS:
                continue
            if inst.type.kind != llvm.TypeKind.Integer:
                continue
            bits = inst.type.int_width
            if bits == 32:
                work.append(inst)
            elif bits == 64 and opts.vectorize_i64:
                work.append(inst)

        for inst in work:
            if not should_transform(rng, cfg):
                continue
            if opts.transform_percent == 0 or rng.randint(0, 99) >= min(opts.transform_percent, 100):
                continue
            if inst.block is None:
                continue

            bits = inst.type.int_width
            count = _vector_lane_count(bits)
            vec_ty = inst.type.vector(count)

            a = inst.get_operand(0)
            b = inst.get_operand(1)
            with inst.block.create_builder() as builder:
                builder.position_before(inst)
                va = _vector_const(builder, a, bits, count) if a.is_constant_int else builder.vector_splat(count, a, "")
                vb = _vector_const(builder, b, bits, count) if b.is_constant_int else builder.vector_splat(count, b, "")
                vr = _build_vector_binop(builder, inst.opcode, va, vb, bits, count, opts)
                result = builder.extract_element(vr, fn.context.types.i32.constant(0, False), "")
            inst.replace_all_uses_with(result)
            inst.erase_from_parent()


# =============================================================================
# --bmi-mutate
# =============================================================================


def _target_features(fn: llvm.Function) -> str:
    attr = fn.get_string_attribute(llvm.AttributeFunctionIndex, "target-features")
    return attr.string_value if attr is not None else ""



def _has_bmi1(fn: llvm.Function) -> bool:
    return "+bmi" in _target_features(fn)



def _has_bmi2(fn: llvm.Function) -> bool:
    return "+bmi2" in _target_features(fn)



def _intrinsic_decl(
    mod: llvm.Module, name: str, tys: list[llvm.Type] | None = None
) -> llvm.Value:
    intrinsic_id = llvm.lookup_intrinsic_id(name)
    if intrinsic_id == 0:
        raise RuntimeError(f"Unknown intrinsic: {name}")
    return mod.get_intrinsic_declaration(intrinsic_id, tys or [])



def contiguous_bits(mask: int, bit_width: int) -> tuple[int, int]:
    if mask == 0:
        return 0, 0
    start = 0
    tmp = mask
    while (tmp & 1) == 0:
        start += 1
        tmp >>= 1
    length = 0
    while tmp & 1:
        length += 1
        tmp >>= 1
    if start + length > bit_width or tmp != 0:
        return 0, 0
    return start, length



def xor_via_andn(builder: llvm.Builder, x: llvm.Value, y: llvm.Value) -> llvm.Value:
    all_ones = int_constant(x.type, -1)
    not_x = builder.xor(x, all_ones, "")
    t1 = builder.and_(not_x, y, "")
    not_y = builder.xor(y, all_ones, "")
    t2 = builder.and_(not_y, x, "")
    return builder.or_(t1, t2, "")



def or_via_andn(builder: llvm.Builder, x: llvm.Value, y: llvm.Value) -> llvm.Value:
    all_ones = int_constant(x.type, -1)
    not_x = builder.xor(x, all_ones, "")
    not_y = builder.xor(y, all_ones, "")
    t = builder.and_(not_x, not_y, "")
    return builder.xor(t, all_ones, "")



def and_via_andn(builder: llvm.Builder, x: llvm.Value, y: llvm.Value) -> llvm.Value:
    all_ones = int_constant(x.type, -1)
    not_y = builder.xor(y, all_ones, "")
    inner = builder.and_(not_y, x, "")
    not_inner = builder.xor(inner, all_ones, "")
    return builder.and_(not_inner, x, "")



def and_const_to_bzhi(
    builder: llvm.Builder, x: llvm.Value, width: int, mod: llvm.Module
) -> llvm.Value:
    ty = x.type
    name = "llvm.x86.bmi.bzhi.64" if ty.int_width == 64 else "llvm.x86.bmi.bzhi.32"
    fn = _intrinsic_decl(mod, name)
    return builder.call(fn.global_value_type, fn, [x, int_constant(ty, width)], "")



def and_const_to_bextr(
    builder: llvm.Builder,
    x: llvm.Value,
    start: int,
    length: int,
    mod: llvm.Module,
) -> llvm.Value:
    ty = x.type
    name = "llvm.x86.bmi.bextr.64" if ty.int_width == 64 else "llvm.x86.bmi.bextr.32"
    fn = _intrinsic_decl(mod, name)
    ctrl = int_constant(ty, start | (length << 8))
    extracted = builder.call(fn.global_value_type, fn, [x, ctrl], "")
    if start == 0:
        return extracted
    return builder.shl(extracted, int_constant(ty, start), "")



def identity_blsi_blsr(
    builder: llvm.Builder, x: llvm.Value
) -> tuple[llvm.Value, set[llvm.Value]]:
    neg_x = builder.sub(int_constant(x.type, 0), x, "")
    blsi = builder.and_(x, neg_x, "")
    dec_x = builder.sub(x, int_constant(x.type, 1), "")
    blsr = builder.and_(x, dec_x, "")
    result = builder.or_(blsi, blsr, "")
    return result, {neg_x, blsi, dec_x, blsr, result}



def identity_pdep_pext(
    builder: llvm.Builder, x: llvm.Value, mask: int, mod: llvm.Module
) -> tuple[llvm.Value, set[llvm.Value]]:
    ty = x.type
    bits = ty.int_width
    not_mask = (~mask) & (0xFFFFFFFF if bits == 32 else 0xFFFFFFFFFFFFFFFF)
    pext_name = "llvm.x86.bmi.pext.64" if bits == 64 else "llvm.x86.bmi.pext.32"
    pdep_name = "llvm.x86.bmi.pdep.64" if bits == 64 else "llvm.x86.bmi.pdep.32"
    pext_fn = _intrinsic_decl(mod, pext_name)
    pdep_fn = _intrinsic_decl(mod, pdep_name)
    m = int_constant(ty, mask)
    nm = int_constant(ty, not_mask)
    e1 = builder.call(pext_fn.global_value_type, pext_fn, [x, m], "")
    d1 = builder.call(pdep_fn.global_value_type, pdep_fn, [e1, m], "")
    e2 = builder.call(pext_fn.global_value_type, pext_fn, [x, nm], "")
    d2 = builder.call(pdep_fn.global_value_type, pdep_fn, [e2, nm], "")
    result = builder.or_(d1, d2, "")
    return result, {e1, d1, e2, d2, result}



def bmi_mutate_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue
        bmi1 = _has_bmi1(fn)
        bmi2 = _has_bmi2(fn)
        if not bmi1 and not bmi2:
            continue

        fn_seed = seed ^ hash(fn.name)
        rng = random.Random(fn_seed)
        candidates: list[tuple[llvm.Value, str, int, int]] = []
        for inst in iter_instructions(fn):
            if inst.type.kind != llvm.TypeKind.Integer or inst.type.int_width not in {32, 64}:
                continue
            if inst.opcode in {llvm.Opcode.Xor, llvm.Opcode.Or, llvm.Opcode.And}:
                if inst.opcode == llvm.Opcode.Xor:
                    op1 = inst.get_operand(1)
                    if op1.is_constant_int and op1.const_sext_value == -1:
                        continue
                    if bmi1:
                        candidates.append((inst, "xor", 0, 0))
                elif inst.opcode == llvm.Opcode.Or and bmi1:
                    candidates.append((inst, "or", 0, 0))
                elif inst.opcode == llvm.Opcode.And:
                    op1 = inst.get_operand(1)
                    if op1.is_constant_int:
                        start, length = contiguous_bits(op1.const_zext_value, inst.type.int_width)
                        if length > 0 and length < inst.type.int_width and (bmi1 or bmi2):
                            candidates.append((inst, "andconst", start, length))
                            continue
                    if bmi1:
                        candidates.append((inst, "and", 0, 0))
            elif len(list(inst.uses)) > 0:
                candidates.append((inst, "identity", 0, 0))

        for inst, kind, mask_start, mask_len in candidates:
            if inst.block is None or not should_transform(rng, cfg):
                continue

            replacement = None
            if kind != "identity":
                with inst.block.create_builder() as builder:
                    builder.position_before(inst)
                    lhs = inst.get_operand(0)
                    rhs = inst.get_operand(1)
                    if kind == "xor":
                        replacement = xor_via_andn(builder, lhs, rhs)
                    elif kind == "or":
                        replacement = or_via_andn(builder, lhs, rhs)
                    elif kind == "and":
                        replacement = and_via_andn(builder, lhs, rhs)
                    elif kind == "andconst":
                        if mask_start == 0 and bmi2:
                            replacement = and_const_to_bzhi(builder, lhs, mask_len, mod)
                        elif bmi1:
                            replacement = and_const_to_bextr(builder, lhs, mask_start, mask_len, mod)
                if replacement is not None:
                    inst.replace_all_uses_with(replacement)
                    inst.erase_from_parent()
            else:
                next_inst = inst.next_instruction
                if next_inst is None:
                    continue
                with inst.block.create_builder() as builder:
                    builder.position_before(next_inst)
                    if bmi2 and rng.randint(0, 1) == 1:
                        mask = rng.randint(1, 0xFFFFFFFE if inst.type.int_width == 32 else 0xFFFFFFFFFFFFFFFE)
                        replacement, created = identity_pdep_pext(builder, inst, mask, mod)
                    else:
                        replacement, created = identity_blsi_blsr(builder, inst)
                replace_all_uses_with_if(inst, replacement, lambda user: user not in created)


# =============================================================================
# --outline-functions
# =============================================================================


_outlined_counter = 0



def is_outline_candidate(bb: llvm.BasicBlock) -> bool:
    fn = bb.function
    if bb == fn.entry_block:
        return False
    if len(list(bb.predecessors)) != 1:
        return False
    if bb.first_instruction is not None and bb.first_instruction.opcode == llvm.Opcode.PHI:
        return False
    if bb.first_instruction is not None and bb.first_instruction.opcode == llvm.Opcode.LandingPad:
        return False
    for inst in bb.instructions:
        if inst.opcode in {llvm.Opcode.Invoke, llvm.Opcode.Resume}:
            return False
    if len(list(bb.instructions)) <= 3:
        return False
    term = bb.terminator
    return term is not None and term.opcode == llvm.Opcode.Br and not term.is_conditional



def compute_live_in(bb: llvm.BasicBlock) -> list[llvm.Value]:
    live_in: list[llvm.Value] = []
    seen = set()
    for inst in bb.instructions:
        for op in inst.operands:
            if op.is_constant or op.value_is_basic_block:
                continue
            if op.is_instruction and op.block == bb:
                continue
            if op not in seen:
                seen.add(op)
                live_in.append(op)
    return live_in



def compute_live_out(bb: llvm.BasicBlock) -> list[llvm.Value]:
    live_out: list[llvm.Value] = []
    for inst in bb.instructions:
        if inst.is_terminator:
            continue
        for user in inst.users:
            if user.is_instruction and user.block != bb:
                live_out.append(inst)
                break
    return live_out



def outline_block(bb: llvm.BasicBlock, live_in: list[llvm.Value], live_out: list[llvm.Value]) -> None:
    global _outlined_counter
    fn = bb.function
    mod = fn.module
    ctx = fn.context

    struct_ty = None
    if not live_out:
        ret_ty = ctx.types.void
    elif len(live_out) == 1:
        ret_ty = live_out[0].type
    else:
        struct_ty = ctx.types.struct([v.type for v in live_out])
        ret_ty = struct_ty

    outlined_ty = ctx.types.function(ret_ty, [v.type for v in live_in])
    outlined = mod.add_function(f"{fn.name}.outlined.{_outlined_counter}", outlined_ty)
    _outlined_counter += 1
    outlined.linkage = llvm.Linkage.Internal
    g_entry = outlined.append_basic_block("entry")

    vmap: dict[llvm.Value, llvm.Value] = {}
    for i, value in enumerate(live_in):
        vmap[value] = outlined.get_param(i)

    orig_insts = [inst for inst in bb.instructions if not inst.is_terminator]
    cloned: list[llvm.Value] = []
    with g_entry.create_builder() as builder:
        for orig in orig_insts:
            clone = orig.instruction_clone()
            builder.insert_into_builder_with_name(clone, "")
            vmap[orig] = clone
            cloned.append(clone)

        for clone in cloned:
            for i in range(clone.num_operands):
                op = clone.get_operand(i)
                if op in vmap:
                    clone.set_operand(i, vmap[op])

        if not live_out:
            builder.ret_void()
        elif len(live_out) == 1:
            builder.ret(vmap.get(live_out[0], live_out[0]))
        else:
            assert struct_ty is not None
            agg = struct_ty.undef()
            for i, value in enumerate(live_out):
                agg = builder.insert_value(agg, vmap.get(value, value), i, "")
            builder.ret(agg)

    term = bb.terminator
    with bb.create_builder() as builder:
        builder.position_before(term)
        call = builder.call(outlined_ty, outlined, live_in, "")
        if len(live_out) == 1:
            replace_all_uses_with_if(live_out[0], call, lambda user: user.block.function == fn)
        elif len(live_out) > 1:
            for i, value in enumerate(live_out):
                extracted = builder.extract_value(call, i, "")
                replace_all_uses_with_if(value, extracted, lambda user: user.block.function == fn)

    for inst in reversed(orig_insts):
        inst.erase_from_parent()



def outline_functions_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)
    funcs = list(mod.functions)
    for fn in funcs:
        if should_skip_function(fn, cfg):
            continue
        candidates = [bb for bb in fn.basic_blocks if is_outline_candidate(bb)]
        outlined = 0
        for bb in candidates:
            if outlined >= 3:
                break
            if not should_transform(rng, cfg):
                continue
            if rng.randint(1, 100) > 30:
                continue
            outline_block(bb, compute_live_in(bb), compute_live_out(bb))
            outlined += 1


# =============================================================================
# --loop-to-recursion
# =============================================================================


@dataclass(slots=True)
class ExitPhiInfo:
    phi: llvm.Value
    value_from_loop: llvm.Value



def _has_eh(bb: llvm.BasicBlock) -> bool:
    return any(inst.opcode in {llvm.Opcode.Invoke, llvm.Opcode.LandingPad} for inst in bb.instructions)



def _header_phis(header: llvm.BasicBlock, latch: llvm.BasicBlock, preheader: llvm.BasicBlock) -> list[llvm.Value] | None:
    phis = []
    for inst in header.instructions:
        if inst.opcode != llvm.Opcode.PHI:
            break
        if inst.num_incoming != 2:
            return None
        incoming_blocks = {inst.get_incoming_block(i) for i in range(inst.num_incoming)}
        if incoming_blocks != {latch, preheader}:
            return None
        phis.append(inst)
    return phis



def _find_simple_loop_candidate(fn: llvm.Function):
    for header in fn.basic_blocks:
        preds = list(header.predecessors)
        if len(preds) != 2:
            continue
        for latch in preds:
            term = latch.terminator
            if term is None or term.opcode != llvm.Opcode.Br or not term.is_conditional:
                continue
            succs = list(term.successors)
            if header not in succs:
                continue
            exit_bb = succs[0] if succs[1] == header else succs[1]
            preheader = preds[0] if preds[1] == latch else preds[1]
            if _has_eh(header) or _has_eh(latch):
                continue

            blocks = {header} if latch == header else {header, latch}
            exit_preds = [pred for pred in exit_bb.predecessors if pred in blocks]
            if any(pred not in blocks for pred in exit_bb.predecessors):
                continue

            # Match the public C++ pass more closely: only handle loops that
            # exit through a single loop block. In practice this means either a
            # single-block loop (header == latch) or a canonical 2-block loop
            # where the header unconditionally transfers to the latch/body and
            # only the latch exits the loop.
            if len(exit_preds) != 1:
                continue
            exiting_block = exit_preds[0]
            if latch != header:
                header_term = header.terminator
                if (
                    header_term is None
                    or header_term.opcode != llvm.Opcode.Br
                    or header_term.is_conditional
                    or header_term.get_successor(0) != latch
                    or exiting_block != latch
                ):
                    continue

            phis = _header_phis(header, latch, preheader)
            if phis is None:
                continue
            return header, latch, preheader, exit_bb, phis, exiting_block
    return None



def _clone_loop_blocks_into_helper(
    helper: llvm.Function,
    loop_blocks: list[llvm.BasicBlock],
    header: llvm.BasicBlock,
    exit_bb: llvm.BasicBlock,
    phis: list[llvm.Value],
    live_ins: list[llvm.Value],
) -> tuple[dict[llvm.BasicBlock, llvm.BasicBlock], llvm.BasicBlock, dict[llvm.Value, llvm.Value]]:
    vmap: dict[llvm.Value, llvm.Value] = {}
    for i, phi in enumerate(phis):
        vmap[phi] = helper.get_param(i)
    for i, val in enumerate(live_ins):
        vmap[val] = helper.get_param(len(phis) + i)

    new_blocks: dict[llvm.BasicBlock, llvm.BasicBlock] = {}
    for bb in loop_blocks:
        new_blocks[bb] = helper.append_basic_block(bb.name or "loop")
        vmap[bb.as_value()] = new_blocks[bb].as_value()
    exit_in_helper = helper.append_basic_block("exit")
    vmap[exit_bb.as_value()] = exit_in_helper.as_value()

    for old_bb in loop_blocks:
        new_bb = new_blocks[old_bb]
        with new_bb.create_builder() as builder:
            for inst in old_bb.instructions:
                if old_bb == header and inst.opcode == llvm.Opcode.PHI:
                    continue
                clone = inst.instruction_clone()
                builder.insert_into_builder_with_name(clone, "")
                vmap[inst] = clone

    for new_bb in new_blocks.values():
        for inst in list(new_bb.instructions):
            for i in range(inst.num_operands):
                op = inst.get_operand(i)
                if op in vmap:
                    inst.set_operand(i, vmap[op])

    return new_blocks, exit_in_helper, vmap



def loop_to_recursion_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)

    for fn in list(mod.functions):
        if should_skip_function(fn, cfg):
            continue
        if not should_transform(rng, cfg):
            continue

        candidate = _find_simple_loop_candidate(fn)
        if candidate is None:
            continue
        header, latch, preheader, exit_bb, phis, exiting_block = candidate
        loop_blocks = [header] if header == latch else [header, latch]
        loop_block_set = set(loop_blocks)

        init_vals = [phi.get_incoming_value_for_block(preheader) for phi in phis]
        next_vals = [phi.get_incoming_value_for_block(latch) for phi in phis]

        exit_phis: list[ExitPhiInfo] = []
        all_exit_phis = list(exit_bb.phis)
        for phi in all_exit_phis:
            loop_incoming_blocks = [
                phi.get_incoming_block(i)
                for i in range(phi.num_incoming)
                if phi.get_incoming_block(i) in loop_block_set
            ]
            if set(loop_incoming_blocks) != {exiting_block}:
                exit_phis = []
                break
            found = phi.get_incoming_value_for_block(exiting_block)
            if found is None:
                exit_phis = []
                break
            exit_phis.append(ExitPhiInfo(phi, found))
        if len(exit_phis) != len(all_exit_phis):
            continue

        captured_exit_phis = {ep.phi for ep in exit_phis}
        invalid_external_use = False
        for bb in loop_blocks:
            for inst in bb.instructions:
                for user in inst.users:
                    if not user.is_instruction:
                        invalid_external_use = True
                        break
                    if user.block in loop_block_set:
                        continue
                    if user not in captured_exit_phis:
                        invalid_external_use = True
                        break
                if invalid_external_use:
                    break
            if invalid_external_use:
                break
        if invalid_external_use:
            continue

        loop_defs = set()
        for bb in loop_blocks:
            for inst in bb.instructions:
                loop_defs.add(inst)
        for phi in phis:
            loop_defs.add(phi)

        live_ins: list[llvm.Value] = []
        live_in_seen = set()
        for bb in loop_blocks:
            for inst in bb.instructions:
                for op in inst.operands:
                    if op.is_constant or op.value_is_basic_block or op in loop_defs:
                        continue
                    if op not in live_in_seen:
                        live_in_seen.add(op)
                        live_ins.append(op)

        if not exit_phis:
            ret_ty = fn.context.types.void
            struct_ty = None
        elif len(exit_phis) == 1:
            ret_ty = exit_phis[0].phi.type
            struct_ty = None
        else:
            struct_ty = fn.context.types.struct([ep.phi.type for ep in exit_phis])
            ret_ty = struct_ty

        helper_ty = fn.context.types.function(ret_ty, [phi.type for phi in phis] + [v.type for v in live_ins])
        helper = mod.add_function(f"{fn.name}.recur", helper_ty)
        helper.linkage = llvm.Linkage.Internal

        new_blocks, exit_in_helper, vmap = _clone_loop_blocks_into_helper(
            helper, loop_blocks, header, exit_bb, phis, live_ins
        )

        with exit_in_helper.create_builder() as builder:
            if not exit_phis:
                builder.ret_void()
            elif len(exit_phis) == 1:
                builder.ret(vmap.get(exit_phis[0].value_from_loop, exit_phis[0].value_from_loop))
            else:
                assert struct_ty is not None
                agg = struct_ty.undef()
                for i, ep in enumerate(exit_phis):
                    agg = builder.insert_value(agg, vmap.get(ep.value_from_loop, ep.value_from_loop), i, "")
                builder.ret(agg)

        new_latch = new_blocks[latch]
        old_latch_term = latch.terminator
        new_latch_term = new_latch.terminator
        cond = new_latch_term.condition
        succ0 = old_latch_term.get_successor(0)
        back_edge_on_true = succ0 == header
        new_latch_term.erase_from_parent()

        recurse_bb = helper.append_basic_block("recurse")
        recurse_args = [vmap.get(v, v) for v in next_vals]
        recurse_args.extend(helper.get_param(len(phis) + i) for i in range(len(live_ins)))
        with recurse_bb.create_builder() as builder:
            tail_call = builder.call(helper_ty, helper, list(recurse_args), "")
            tail_call.set_tail_call_kind(llvm.TailCallKind.MustTail)
            if ret_ty == fn.context.types.void:
                builder.ret_void()
            else:
                builder.ret(tail_call)

        with new_latch.create_builder() as builder:
            builder.position_at_end(new_latch)
            if back_edge_on_true:
                builder.cond_br(cond, recurse_bb, exit_in_helper)
            else:
                builder.cond_br(cond, exit_in_helper, recurse_bb)

        pre_term = preheader.terminator
        with preheader.create_builder() as builder:
            builder.position_before(pre_term)
            init_call_args = list(init_vals) + list(live_ins)
            call_result = builder.call(helper_ty, helper, init_call_args, "")
            if not exit_phis:
                pass
            elif len(exit_phis) == 1:
                replace_all_uses_with_if(
                    exit_phis[0].phi,
                    call_result,
                    lambda user: user.block.function == fn,
                )
            else:
                for i, ep in enumerate(exit_phis):
                    extracted = builder.extract_value(call_result, i, "")
                    replace_all_uses_with_if(ep.phi, extracted, lambda user: user.block.function == fn)
            builder.br(exit_bb)
            pre_term.erase_from_parent()

        for ep in exit_phis:
            ep.phi.erase_from_parent()

        for bb in loop_blocks:
            for inst in reversed(list(bb.instructions)):
                inst.erase_from_parent()
        for bb in loop_blocks:
            bb.erase_from_parent()
        break


# =============================================================================
# --code-clone
# =============================================================================


def calls_self(fn: llvm.Function) -> bool:
    for inst in iter_instructions(fn):
        if _is_call_like(inst):
            try:
                if inst.called_value == fn:
                    return True
            except llvm.LLVMError:
                pass
    return False


def _collect_direct_call_sites(fn: llvm.Function) -> tuple[list[llvm.Value], bool]:
    call_sites: list[llvm.Value] = []
    address_taken = False

    for user in fn.users:
        if not user.is_instruction:
            address_taken = True
            break
        if not _is_call_like(user):
            address_taken = True
            break
        try:
            if user.called_value != fn:
                address_taken = True
                break
        except llvm.LLVMError:
            address_taken = True
            break
        call_sites.append(user)

    return call_sites, address_taken


def diversify_clone(fn: llvm.Function, rng: random.Random) -> None:
    binaries: list[llvm.Value] = []
    for inst in iter_instructions(fn):
        if inst.opcode in {llvm.Opcode.Add, llvm.Opcode.Xor}:
            binaries.append(inst)

    for inst in binaries:
        if rng.randint(1, 100) > 20:
            continue
        if inst.type.kind != llvm.TypeKind.Integer:
            continue

        a = inst.get_operand(0)
        b = inst.get_operand(1)
        k = int_constant(inst.type, rng.randint(1, 0xFFFF))

        with inst.block.create_builder() as builder:
            builder.position_before(inst)
            replacement: llvm.Value | None = None
            if inst.opcode == llvm.Opcode.Add:
                total = builder.add(a, b, "")
                total_k = builder.add(total, k, "")
                replacement = builder.sub(total_k, k, "")
            elif inst.opcode == llvm.Opcode.Xor:
                x1 = builder.xor(a, b, "")
                x2 = builder.xor(x1, k, "")
                replacement = builder.xor(x2, k, "")

        if replacement is not None:
            inst.replace_all_uses_with(replacement)
            inst.erase_from_parent()


def clone_functions_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    from llvm_c_test.echo import FunCloner

    rng = random.Random(seed)
    candidates: list[llvm.Function] = []

    for fn in mod.functions:
        if fn.is_declaration:
            continue
        if fn.linkage not in {llvm.Linkage.Internal, llvm.Linkage.Private}:
            continue
        call_sites, address_taken = _collect_direct_call_sites(fn)
        if address_taken or not call_sites:
            continue
        if count_instructions(fn) <= 5:
            continue
        if calls_self(fn):
            continue
        if should_skip_function(fn, cfg):
            continue
        candidates.append(fn)

    cloned = 0
    for fn in candidates:
        if cloned >= 20:
            break
        if not should_transform(rng, cfg):
            continue
        if rng.randint(1, 100) > 30:
            continue

        call_sites, _ = _collect_direct_call_sites(fn)
        clone_count = rng.randint(2, 3)
        variants: list[llvm.Function] = [fn]

        for i in range(clone_count):
            clone_name = f"{fn.name}.clone.{cloned}.{i}"
            clone = mod.add_function(clone_name, fn.global_value_type)
            clone.linkage = llvm.Linkage.Internal
            clone.calling_conv = fn.calling_conv
            for idx in range(llvm.AttributeFunctionIndex, fn.param_count + 1):
                for attr in fn.get_attributes(idx):
                    clone.add_attribute(idx, attr)
            FunCloner(fn, clone, mod).clone_bbs(fn)
            diversify_clone(clone, rng)
            variants.append(clone)

        for call_site in call_sites:
            picked = variants[rng.randrange(len(variants))]
            call_site.set_called_operand(picked)

        cloned += 1


# =============================================================================
# --schedule-instructions
# =============================================================================


def is_memory_op(inst: llvm.Value) -> bool:
    return inst.opcode in MEMORY_OPCODES


def schedule_basic_block(bb: llvm.BasicBlock, rng: random.Random) -> None:
    work = [
        inst
        for inst in bb.instructions
        if inst.opcode != llvm.Opcode.PHI and not inst.is_terminator
    ]
    if len(work) < 3:
        return

    pinned: list[llvm.Value] = []
    schedulable: list[llvm.Value] = []
    in_alloca_prefix = bb == bb.function.entry_block
    for inst in work:
        if inst.opcode == llvm.Opcode.LandingPad:
            pinned.append(inst)
            continue
        if in_alloca_prefix and inst.opcode == llvm.Opcode.Alloca:
            pinned.append(inst)
            continue
        in_alloca_prefix = False
        schedulable.append(inst)

    if len(schedulable) < 3:
        return

    idx = {inst: i for i, inst in enumerate(schedulable)}
    n = len(schedulable)
    preds: list[set[int]] = [set() for _ in range(n)]
    in_degree = [0] * n

    last_mem: int | None = None
    for i, inst in enumerate(schedulable):
        if is_memory_op(inst):
            if last_mem is not None and last_mem not in preds[i]:
                preds[i].add(last_mem)
                in_degree[i] += 1
            last_mem = i

    for i, inst in enumerate(schedulable):
        for operand in inst.operands:
            if operand in idx and idx[operand] != i and idx[operand] not in preds[i]:
                preds[i].add(idx[operand])
                in_degree[i] += 1

    ready = [i for i in range(n) if in_degree[i] == 0]
    order: list[llvm.Value] = []

    while ready:
        rng.shuffle(ready)
        pick = ready.pop()
        order.append(schedulable[pick])
        for j in range(n):
            if pick in preds[j]:
                preds[j].remove(pick)
                in_degree[j] -= 1
                if in_degree[j] == 0:
                    ready.append(j)

    if len(order) != n:
        return

    term = bb.terminator
    for inst in order:
        inst.move_before(term)


def schedule_instructions_module(mod: llvm.Module, seed: int, cfg: FilterConfig) -> None:
    rng = random.Random(seed)
    for fn in mod.functions:
        if should_skip_function(fn, cfg):
            continue
        for bb in fn.basic_blocks:
            if not should_transform(rng, cfg):
                continue
            if rng.randint(0, 1) == 0:
                continue
            schedule_basic_block(bb, rng)


# =============================================================================
# Pipeline driver
# =============================================================================


def apply_pipeline(mod: llvm.Module, options: PipelineOptions) -> None:
    cfg = build_filter_config(options)

    def verify_after(pass_name: str) -> None:
        if options.verify_each and not mod.verify():
            raise RuntimeError(
                f"ollvm-obf: module verification failed after {pass_name}: "
                f"{mod.get_verification_error()}"
            )

    if options.string_encrypt:
        encrypt_strings_module(mod, mix_seed(options.seed, STRING_ENCRYPT_SALT))
        verify_after("string-encrypt")

    if options.code_clone:
        clone_functions_module(mod, mix_seed(options.seed, CODE_CLONE_SALT), cfg)
        verify_after("code-clone")

    if options.substitute:
        substitute_module(mod, mix_seed(options.seed, SUBSTITUTE_SALT), cfg)
        verify_after("substitute")

    if options.if_convert:
        if_convert_module(mod, mix_seed(options.seed, IF_CONVERT_SALT), cfg)
        verify_after("if-convert")

    if options.loop_to_recursion:
        loop_to_recursion_module(mod, mix_seed(options.seed, LOOP_TO_RECURSION_SALT), cfg)
        verify_after("loop-to-recursion")

    if options.flatten:
        flatten_module(mod, mix_seed(options.seed, FLATTEN_SALT), cfg)
        verify_after("flatten")

    if options.opaque_predicates:
        insert_opaque_predicates_module(
            mod, mix_seed(options.seed, OPAQUE_PREDICATES_SALT), cfg
        )
        verify_after("opaque-predicates")

    if options.bogus_control_flow:
        insert_bogus_control_flow_module(
            mod, mix_seed(options.seed, BOGUS_CONTROL_FLOW_SALT), cfg
        )
        verify_after("bogus-control-flow")

    if options.bmi_mutate:
        bmi_mutate_module(mod, mix_seed(options.seed, BMI_MUTATE_SALT), cfg)
        verify_after("bmi-mutate")

    if options.const_unfold:
        unfold_constants_module(mod, mix_seed(options.seed, CONST_UNFOLD_SALT), cfg)
        verify_after("const-unfold")

    if options.schedule_instructions:
        schedule_instructions_module(mod, mix_seed(options.seed, SCHEDULE_SALT), cfg)
        verify_after("schedule-instructions")

    if options.outline_functions:
        outline_functions_module(mod, mix_seed(options.seed, OUTLINE_SALT), cfg)
        verify_after("outline-functions")

    if options.arith_encode:
        encode_allocas_module(mod, mix_seed(options.seed, ARITH_ENCODE_SALT), cfg)
        verify_after("arith-encode")

    if options.stack_randomize:
        randomize_stack_module(mod, mix_seed(options.seed, STACK_RANDOMIZE_SALT), cfg)
        verify_after("stack-randomize")

    if options.vectorize:
        vectorize_module(
            mod,
            mix_seed(options.seed, VECTORIZE_SALT),
            VectorizeOptions(
                vectorize_data=options.vectorize_data,
                vectorize_bitwise=options.vectorize_bitwise,
                vectorize_i64=options.vectorize_i64,
                transform_percent=options.vectorize_percent,
            ),
            cfg,
        )
        verify_after("vectorize")

    if options.reg_pressure:
        extend_register_pressure_module(mod, mix_seed(options.seed, REG_PRESSURE_SALT), cfg)
        verify_after("reg-pressure")

    if not mod.verify():
        raise RuntimeError(
            "ollvm-obf: module verification failed after obfuscation: "
            + mod.get_verification_error()
        )


# =============================================================================
# CLI helpers
# =============================================================================


def read_module(ctx: llvm.Context, input_path: str | None) -> llvm.Module:
    if input_path is None:
        ir_text = sys.stdin.read()
        manager = ctx.parse_ir(ir_text)
    elif input_path.endswith(".bc"):
        bitcode = Path(input_path).read_bytes()
        manager = ctx.parse_bitcode_from_bytes(bitcode)
    else:
        manager = ctx.parse_ir(Path(input_path).read_text(encoding="utf-8"))
    return manager.__enter__()


def write_module(mod: llvm.Module, output_path: str | None) -> None:
    if output_path and output_path.endswith(".bc"):
        data = mod.write_bitcode_to_memory_buffer()
        Path(output_path).write_bytes(data)
        return

    text = mod.to_string()
    if output_path:
        Path(output_path).write_text(text, encoding="utf-8")
    else:
        print(text, end="")


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="OLLVM-style obfuscation tool")
    parser.add_argument("input", nargs="?", help="Input .ll/.bc file (default: stdin)")
    parser.add_argument("-o", "--output", help="Output filename")

    parser.add_argument("--code-clone", action="store_true")
    parser.add_argument("--substitute", action="store_true")
    parser.add_argument("--if-convert", action="store_true")
    parser.add_argument("--flatten", action="store_true")
    parser.add_argument("--opaque-predicates", action="store_true")
    parser.add_argument("--bogus-control-flow", action="store_true")
    parser.add_argument("--const-unfold", action="store_true")
    parser.add_argument("--schedule-instructions", action="store_true")

    parser.add_argument("--string-encrypt", action="store_true")
    parser.add_argument("--bmi-mutate", action="store_true")
    parser.add_argument("--outline-functions", action="store_true")
    parser.add_argument("--stack-randomize", action="store_true")
    parser.add_argument("--arith-encode", action="store_true")
    parser.add_argument("--loop-to-recursion", action="store_true")
    parser.add_argument("--reg-pressure", action="store_true")
    parser.add_argument("--vectorize", action="store_true")
    parser.add_argument("--vectorize-data", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--vectorize-bitwise", action="store_true")
    parser.add_argument("--vectorize-i64", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--vectorize-percent", type=int, default=40)

    parser.add_argument("--seed", type=lambda x: int(x, 0), default=0xB16B00B5)
    parser.add_argument("--verify-each", action="store_true")
    parser.add_argument("--min-instructions", type=int, default=0)
    parser.add_argument("--transform-percent", type=int, default=100)
    parser.add_argument("--skip-inline-asm", action="store_true")

    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)

    requested_unsupported = [
        flag.replace("_", "-")
        for flag, message in UNSUPPORTED_FLAGS.items()
        if getattr(args, flag)
    ]
    if requested_unsupported:
        print(
            "ollvm-obf: unsupported flags in Python port: "
            + ", ".join(f"--{flag}" for flag in requested_unsupported),
            file=sys.stderr,
        )
        return 2

    options = PipelineOptions(
        code_clone=args.code_clone,
        substitute=args.substitute,
        if_convert=args.if_convert,
        flatten=args.flatten,
        opaque_predicates=args.opaque_predicates,
        bogus_control_flow=args.bogus_control_flow,
        const_unfold=args.const_unfold,
        schedule_instructions=args.schedule_instructions,
        string_encrypt=args.string_encrypt,
        bmi_mutate=args.bmi_mutate,
        outline_functions=args.outline_functions,
        stack_randomize=args.stack_randomize,
        arith_encode=args.arith_encode,
        loop_to_recursion=args.loop_to_recursion,
        reg_pressure=args.reg_pressure,
        vectorize=args.vectorize,
        vectorize_data=args.vectorize_data,
        vectorize_bitwise=args.vectorize_bitwise,
        vectorize_i64=args.vectorize_i64,
        vectorize_percent=args.vectorize_percent,
        seed=args.seed,
        verify_each=args.verify_each,
        min_instructions=args.min_instructions,
        transform_percent=args.transform_percent,
        skip_inline_asm=args.skip_inline_asm,
    )

    with llvm.create_context() as ctx:
        manager = None
        mod = None
        try:
            if args.input is None:
                manager = ctx.parse_ir(sys.stdin.read())
            elif args.input.endswith(".bc"):
                manager = ctx.parse_bitcode_from_bytes(Path(args.input).read_bytes())
            else:
                manager = ctx.parse_ir(Path(args.input).read_text(encoding="utf-8"))

            with manager as mod:
                apply_pipeline(mod, options)
                write_module(mod, args.output)
            return 0
        except (llvm.LLVMError, RuntimeError) as exc:
            print(str(exc), file=sys.stderr)
            return 1


if __name__ == "__main__":
    raise SystemExit(main())
