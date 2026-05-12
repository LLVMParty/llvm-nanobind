"""
Comprehensive regression matrix for Value accessor validation guards.

This suite verifies guard behavior for every Value accessor currently protected
by value-kind validation in the C++ bindings:

1. Wrong-kind access raises llvm.LLVMAssertionError with actionable text.
2. Correct-kind access works for representative valid values.
"""

import llvm


def assert_llvm_assertion(action, api_name: str, expected_substring: str):
    try:
        action()
        assert False, f"Expected llvm.LLVMAssertionError for {api_name}"
    except llvm.LLVMAssertionError as e:
        msg = str(e)
        msg_lower = msg.lower()
        assert api_name.lower() in msg_lower, (
            f"Expected api name '{api_name}' in: {msg}"
        )
        assert expected_substring.lower() in msg_lower, (
            f"Expected '{expected_substring}' in error message, got: {msg}"
        )


def build_fixture(ctx: llvm.Context, mod: llvm.Module):
    i1 = ctx.types.i1
    i32 = ctx.types.i32
    void = ctx.types.void
    ptr = ctx.types.ptr
    token = ctx.types.token

    refs: dict[str, object] = {}

    # Globals, aliases, and ifuncs.
    g0 = mod.add_global(i32, "g0")
    g1 = mod.add_global(i32, "g1")
    g0.initializer = i32.constant(10, False)
    g1.initializer = i32.constant(20, False)
    refs["g0"] = g0
    refs["g1"] = g1

    alias0 = mod.add_alias(i32, 0, g0, "alias0")
    alias1 = mod.add_alias(i32, 0, g0, "alias1")
    refs["alias0"] = alias0
    refs["alias1"] = alias1

    resolver_ty = ctx.types.function(i32, [], False)
    resolver0 = mod.add_function("resolver0", resolver_ty)
    resolver1 = mod.add_function("resolver1", resolver_ty)
    with resolver0.append_basic_block("entry").create_builder() as b:
        b.ret(i32.constant(0, False))
    with resolver1.append_basic_block("entry").create_builder() as b:
        b.ret(i32.constant(1, False))
    refs["resolver_ty"] = resolver_ty
    refs["resolver0"] = resolver0
    refs["resolver1"] = resolver1

    ifunc0 = mod.add_global_ifunc("ifunc0", resolver_ty, 0, resolver0)
    ifunc1 = mod.add_global_ifunc("ifunc1", resolver_ty, 0, resolver1)
    refs["ifunc0"] = ifunc0
    refs["ifunc1"] = ifunc1

    # Simple function with representative instruction kinds.
    callee_void_ty = ctx.types.function(void, [], False)
    callee_void = mod.add_function("callee_void", callee_void_ty)
    with callee_void.append_basic_block("entry").create_builder() as b:
        b.ret_void()
    refs["callee_void"] = callee_void
    refs["callee_void_ty"] = callee_void_ty

    call_arg_ty = ctx.types.function(void, [i32], False)
    call_arg_callee = mod.add_function("call_arg_callee", call_arg_ty)
    with call_arg_callee.append_basic_block("entry").create_builder() as b:
        b.ret_void()
    refs["call_arg_callee"] = call_arg_callee
    refs["call_arg_ty"] = call_arg_ty

    f_ty = ctx.types.function(void, [i32], False)
    f = mod.add_function("f", f_ty)
    f_arg0 = f.get_param(0)
    f_entry = f.append_basic_block("entry")
    f_exit = f.append_basic_block("exit")

    with f_entry.create_builder() as b:
        add_inst = b.add(f_arg0, i32.constant(1, False), "add")

        bundle = llvm.create_operand_bundle("deopt", [i32.constant(0, False)], ctx)
        call_bundle_inst = b.call_with_operand_bundles(
            callee_void_ty, callee_void, [], [bundle], ""
        )
        call_plain_inst = b.call(callee_void_ty, callee_void, [], "")
        call_arg_inst = b.call(call_arg_ty, call_arg_callee, [f_arg0], "")
        br_inst = b.br(f_exit)

        b.position_at_end(f_exit)
        b.ret_void()

    refs["f"] = f
    refs["f_arg0"] = f_arg0
    refs["f_entry"] = f_entry
    refs["f_exit"] = f_exit
    refs["add_inst"] = add_inst
    refs["call_bundle_inst"] = call_bundle_inst
    refs["call_plain_inst"] = call_plain_inst
    refs["call_arg_inst"] = call_arg_inst
    refs["br_inst"] = br_inst

    # Guard-heavy instruction set for opcode/kind-specific accessors.
    f32 = ctx.types.f32
    guard_fn_ty = ctx.types.function(void, [f32, f32], False)
    guard_fn = mod.add_function("guard_fn", guard_fn_ty)
    guard_entry = guard_fn.append_basic_block("entry")
    guard_f0 = guard_fn.get_param(0)
    guard_f1 = guard_fn.get_param(1)

    with guard_entry.create_builder() as b:
        alloca_inst = b.alloca(i32, "slot")
        store_inst = b.store(i32.constant(0, False), alloca_inst)
        load_inst = b.load(i32, alloca_inst, "ld")
        add_wrap_inst = b.add(load_inst, i32.constant(1, False), "add_wrap")
        sdiv_inst = b.sdiv(add_wrap_inst, i32.constant(1, False), "sdiv")
        lshr_inst = b.lshr(add_wrap_inst, i32.constant(1, False), "lshr")
        or_inst = b.or_(add_wrap_inst, i32.constant(2, False), "or")
        icmp_inst = b.icmp(
            llvm.IntPredicate.EQ, add_wrap_inst, i32.constant(0, False), "icmp"
        )
        zext_inst = b.zext(icmp_inst, i32, "zext")

        fcmp_inst = b.fcmp(llvm.RealPredicate.OEQ, guard_f0, guard_f1, "fcmp")
        fadd_inst = b.fadd(guard_f0, guard_f1, "fadd")

        atomic_rmw_inst = b.atomic_rmw(
            llvm.AtomicRMWBinOp.Xchg,
            alloca_inst,
            i32.constant(1, False),
            llvm.AtomicOrdering.SequentiallyConsistent,
        )
        cmpxchg_inst = b.atomic_cmpxchg(
            alloca_inst,
            i32.constant(1, False),
            i32.constant(2, False),
            llvm.AtomicOrdering.SequentiallyConsistent,
            llvm.AtomicOrdering.Monotonic,
        )
        fence_inst = b.fence(llvm.AtomicOrdering.SequentiallyConsistent)
        gep_inst = b.gep(i32, alloca_inst, [i32.constant(0, False)], "gep0")

        struct_ty = ctx.types.struct([i32, i32])
        struct_const = struct_ty.named_struct_const(
            [i32.constant(10, False), i32.constant(20, False)]
        )
        struct_slot = b.alloca(struct_ty, "struct_slot")
        b.store(struct_const, struct_slot)
        loaded_struct = b.load(struct_ty, struct_slot, "loaded_struct")
        extract_value_inst = b.extract_value(loaded_struct, 0, "ev")
        insert_value_inst = b.insert_value(
            loaded_struct, i32.constant(99, False), 1, "iv"
        )
        b.ret_void()

    refs["guard_fn"] = guard_fn
    refs["alloca_inst"] = alloca_inst
    refs["store_inst"] = store_inst
    refs["load_inst"] = load_inst
    refs["add_wrap_inst"] = add_wrap_inst
    refs["sdiv_inst"] = sdiv_inst
    refs["lshr_inst"] = lshr_inst
    refs["zext_inst"] = zext_inst
    refs["or_inst"] = or_inst
    refs["icmp_inst"] = icmp_inst
    refs["fcmp_inst"] = fcmp_inst
    refs["fadd_inst"] = fadd_inst
    refs["atomic_rmw_inst"] = atomic_rmw_inst
    refs["cmpxchg_inst"] = cmpxchg_inst
    refs["fence_inst"] = fence_inst
    refs["gep_inst"] = gep_inst
    refs["extract_value_inst"] = extract_value_inst
    refs["insert_value_inst"] = insert_value_inst

    # Dedicated ShuffleVector instruction (avoid constant folding).
    vec4_i32 = i32.vector(4)
    shuf_fn_ty = ctx.types.function(vec4_i32, [vec4_i32, vec4_i32], False)
    shuf_fn = mod.add_function("shuf_fn", shuf_fn_ty)
    shuf_entry = shuf_fn.append_basic_block("entry")
    with shuf_entry.create_builder() as b:
        mask = llvm.const_vector(
            [
                i32.constant(0, False),
                i32.constant(5, False),
                i32.constant(2, False),
                i32.constant(7, False),
            ]
        )
        shuffle_inst = b.shuffle_vector(
            shuf_fn.get_param(0), shuf_fn.get_param(1), mask, "shuf"
        )
        b.ret(shuffle_inst)
    refs["shuffle_inst"] = shuffle_inst

    # Argument-only navigation.
    arg_fn_ty = ctx.types.function(void, [i32, i32], False)
    arg_fn = mod.add_function("arg_fn", arg_fn_ty)
    arg0 = arg_fn.get_param(0)
    arg1 = arg_fn.get_param(1)
    refs["arg0"] = arg0
    refs["arg1"] = arg1

    # PHI.
    phi_fn_ty = ctx.types.function(i32, [i1, i32, i32], False)
    phi_fn = mod.add_function("phi_fn", phi_fn_ty)
    phi_entry = phi_fn.append_basic_block("entry")
    phi_left = phi_fn.append_basic_block("left")
    phi_right = phi_fn.append_basic_block("right")
    phi_merge = phi_fn.append_basic_block("merge")
    with phi_entry.create_builder() as b:
        cond_br_inst = b.cond_br(phi_fn.get_param(0), phi_left, phi_right)
        b.position_at_end(phi_left)
        b.br(phi_merge)
        b.position_at_end(phi_right)
        b.br(phi_merge)
        b.position_at_end(phi_merge)
        phi_inst = b.phi(i32, "p")
        phi_inst.add_incoming(phi_fn.get_param(1), phi_left)
        phi_inst.add_incoming(phi_fn.get_param(2), phi_right)
        b.ret(phi_inst)
    refs["phi_inst"] = phi_inst
    refs["cond_br_inst"] = cond_br_inst
    refs["phi_left"] = phi_left
    refs["phi_right"] = phi_right

    # Invoke + landingpad.
    personality_ty = ctx.types.function(i32, [], True)
    personality_fn = mod.add_function("__personality", personality_ty)
    with personality_fn.append_basic_block("entry").create_builder() as b:
        b.ret(i32.constant(0, False))
    refs["personality_fn"] = personality_fn

    may_throw = mod.add_function("may_throw", callee_void_ty)
    with may_throw.append_basic_block("entry").create_builder() as b:
        b.ret_void()

    inv_fn_ty = ctx.types.function(void, [], False)
    inv_fn = mod.add_function("inv_fn", inv_fn_ty)
    inv_fn.set_personality_fn(personality_fn)
    inv_entry = inv_fn.append_basic_block("entry")
    inv_normal = inv_fn.append_basic_block("normal")
    inv_unwind = inv_fn.append_basic_block("unwind")

    with inv_entry.create_builder() as b:
        invoke_inst = b.invoke_with_operand_bundles(
            callee_void_ty, may_throw, [], inv_normal, inv_unwind, [], "inv"
        )
        b.position_at_end(inv_normal)
        b.ret_void()
        b.position_at_end(inv_unwind)
        lp_ty = ctx.types.struct([ptr, i32])
        landing_inst = b.landing_pad(lp_ty, 1, "lp")
        landing_inst.add_clause(ptr.null())
        landing_inst.set_cleanup(True)
        b.ret_void()

    refs["invoke_inst"] = invoke_inst
    refs["landing_inst"] = landing_inst
    refs["inv_normal"] = inv_normal
    refs["inv_unwind"] = inv_unwind

    # CallBr.
    cb_fn_ty = ctx.types.function(void, [i32], False)
    cb_fn = mod.add_function("cb_fn", cb_fn_ty)
    cb_entry = cb_fn.append_basic_block("entry")
    cb_default = cb_fn.append_basic_block("default")
    cb_indirect = cb_fn.append_basic_block("indirect")
    with cb_entry.create_builder() as b:
        inline_asm = llvm.get_inline_asm(
            cb_fn_ty,
            "nop",
            "r",
            True,
            False,
            llvm.InlineAsmDialect.ATT,
            False,
        )
        callbr_inst = b.callbr(
            cb_fn_ty,
            inline_asm,
            cb_default,
            [cb_indirect],
            [cb_fn.get_param(0)],
            [],
            "cb",
        )
        b.position_at_end(cb_default)
        b.ret_void()
        b.position_at_end(cb_indirect)
        b.ret_void()
    refs["callbr_inst"] = callbr_inst
    refs["cb_default"] = cb_default
    refs["cb_indirect"] = cb_indirect
    refs["cb_arg0"] = cb_fn.get_param(0)

    # CatchSwitch/CatchPad/CleanupPad.
    eh_fn_ty = ctx.types.function(void, [], False)
    eh_fn = mod.add_function("eh_fn", eh_fn_ty)
    eh_fn.set_personality_fn(personality_fn)
    cs_entry = eh_fn.append_basic_block("cs_entry")
    cs_pad1 = eh_fn.append_basic_block("cs_pad1")
    cs_pad2 = eh_fn.append_basic_block("cs_pad2")
    cs_extra = eh_fn.append_basic_block("cs_extra")
    cs_cleanup = eh_fn.append_basic_block("cs_cleanup")
    cs_cont = eh_fn.append_basic_block("cs_cont")

    none_token = token.null()
    with cs_entry.create_builder() as b:
        catchswitch_inst = b.catch_switch(none_token, cs_cleanup, 2, "cs")
        catchswitch_inst.add_handler(cs_pad1)
        catchswitch_inst.add_handler(cs_pad2)

    with cs_pad1.create_builder() as b:
        catchpad_inst = b.catch_pad(catchswitch_inst, [i32.constant(5, False)], "cp")
        b.catch_ret(catchpad_inst, cs_cont)

    with cs_pad2.create_builder() as b:
        cp2 = b.catch_pad(catchswitch_inst, [i32.constant(6, False)], "cp2")
        b.catch_ret(cp2, cs_cont)

    with cs_cleanup.create_builder() as b:
        cleanuppad_inst = b.cleanup_pad(none_token, [i32.constant(7, False)], "clp")
        cleanup_ret_inst = b.cleanup_ret(cleanuppad_inst, None)

    with cs_cont.create_builder() as b:
        b.ret_void()

    refs["catchswitch_inst"] = catchswitch_inst
    refs["catchpad_inst"] = catchpad_inst
    refs["cleanuppad_inst"] = cleanuppad_inst
    refs["cleanup_ret_inst"] = cleanup_ret_inst
    refs["cs_extra"] = cs_extra

    # Shared bad value used for wrong-kind checks.
    refs["bad_const"] = i32.constant(999, False)

    return refs


def test_value_accessor_guard_matrix_negative():
    with llvm.create_context() as ctx:
        with ctx.create_module("value_guard_negative") as mod:
            refs = build_fixture(ctx, mod)

            bad = refs["bad_const"]
            add_inst = refs["add_inst"]
            br_inst = refs["br_inst"]
            call_bundle_inst = refs["call_bundle_inst"]
            call_arg_inst = refs["call_arg_inst"]
            phi_inst = refs["phi_inst"]
            shuffle_inst = refs["shuffle_inst"]
            g0 = refs["g0"]
            personality_fn = refs["personality_fn"]
            handler_bb = refs["f_exit"]
            call_target = refs["callee_void"]
            i32 = ctx.types.i32

            negative_cases = [
                ("next_global", lambda: bad.next_global, "global variable"),
                ("prev_global", lambda: bad.prev_global, "global variable"),
                (
                    "next_global_alias",
                    lambda: bad.next_global_alias,
                    "global alias",
                ),
                (
                    "prev_global_alias",
                    lambda: bad.prev_global_alias,
                    "global alias",
                ),
                ("aliasee", lambda: bad.aliasee, "global alias"),
                (
                    "alias_set_aliasee",
                    lambda: bad.alias_set_aliasee(g0),
                    "global alias",
                ),
                (
                    "next_global_ifunc",
                    lambda: bad.next_global_ifunc,
                    "global ifunc value",
                ),
                (
                    "prev_global_ifunc",
                    lambda: bad.prev_global_ifunc,
                    "global ifunc value",
                ),
                (
                    "global_ifunc_resolver",
                    lambda: bad.global_ifunc_resolver,
                    "global ifunc value",
                ),
                (
                    "set_global_ifunc_resolver",
                    lambda: bad.set_global_ifunc_resolver(refs["resolver0"]),
                    "global ifunc value",
                ),
                (
                    "erase_from_parent_ifunc",
                    lambda: bad.erase_from_parent_ifunc(),
                    "global ifunc value",
                ),
                (
                    "remove_from_parent_ifunc",
                    lambda: bad.remove_from_parent_ifunc(),
                    "global ifunc value",
                ),
                (
                    "global_value_type",
                    lambda: bad.global_value_type,
                    "global value",
                ),
                ("unnamed_address", lambda: bad.unnamed_address, "global value"),
                (
                    "set_unnamed_address",
                    lambda: setattr(bad, "unnamed_address", g0.unnamed_address),
                    "global value",
                ),
                (
                    "global_copy_all_metadata",
                    lambda: bad.global_copy_all_metadata(),
                    "global value",
                ),
                (
                    "has_personality_fn",
                    lambda: bad.has_personality_fn,
                    "function value",
                ),
                ("personality_fn", lambda: bad.personality_fn, "function value"),
                (
                    "set_personality_fn",
                    lambda: bad.set_personality_fn(personality_fn),
                    "function value",
                ),
                ("has_prefix_data", lambda: bad.has_prefix_data, "function value"),
                ("prefix_data", lambda: bad.prefix_data, "function value"),
                (
                    "set_prefix_data",
                    lambda: bad.set_prefix_data(ctx.types.ptr.null()),
                    "function value",
                ),
                (
                    "has_prologue_data",
                    lambda: bad.has_prologue_data,
                    "function value",
                ),
                ("prologue_data", lambda: bad.prologue_data, "function value"),
                (
                    "set_prologue_data",
                    lambda: bad.set_prologue_data(ctx.types.ptr.null()),
                    "function value",
                ),
                (
                    "instruction_get_all_metadata_other_than_debug_loc",
                    lambda: bad.instruction_get_all_metadata_other_than_debug_loc(),
                    "instruction value",
                ),
                ("opcode", lambda: bad.opcode, "instruction value"),
                ("opcode_name", lambda: bad.opcode_name, "instruction value"),
                ("next_instruction", lambda: bad.next_instruction, "instruction value"),
                ("prev_instruction", lambda: bad.prev_instruction, "instruction value"),
                ("next_param", lambda: bad.next_param, "function argument value"),
                ("prev_param", lambda: bad.prev_param, "function argument value"),
                ("icmp_predicate", lambda: add_inst.icmp_predicate, "icmp"),
                ("fcmp_predicate", lambda: add_inst.fcmp_predicate, "fcmp"),
                ("nsw", lambda: br_inst.nsw, "overflowing binary"),
                ("nuw", lambda: br_inst.nuw, "overflowing binary"),
                ("exact", lambda: br_inst.exact, "exact-eligible"),
                ("nneg", lambda: br_inst.nneg, "zext"),
                ("set_nsw", lambda: br_inst.set_nsw(True), "overflowing binary"),
                ("set_nuw", lambda: br_inst.set_nuw(True), "overflowing binary"),
                ("set_exact", lambda: br_inst.set_exact(True), "exact-eligible"),
                ("set_nneg", lambda: br_inst.set_nneg(True), "zext"),
                ("is_disjoint", lambda: add_inst.is_disjoint, "or instruction"),
                ("set_is_disjoint", lambda: add_inst.set_is_disjoint(True), "or instruction"),
                ("icmp_same_sign", lambda: add_inst.icmp_same_sign, "icmp instruction"),
                (
                    "set_icmp_same_sign",
                    lambda: add_inst.set_icmp_same_sign(True),
                    "icmp instruction",
                ),
                ("ordering", lambda: add_inst.ordering, "atomic-capable memory"),
                (
                    "set_ordering",
                    lambda: add_inst.set_ordering(llvm.AtomicOrdering.Monotonic),
                    "atomic-capable memory",
                ),
                ("is_atomic", lambda: bad.is_atomic, "instruction value"),
                (
                    "atomic_sync_scope_id",
                    lambda: add_inst.atomic_sync_scope_id,
                    "atomic-capable memory",
                ),
                (
                    "set_atomic_sync_scope_id",
                    lambda: add_inst.set_atomic_sync_scope_id(0),
                    "atomic-capable memory",
                ),
                ("atomic_rmw_bin_op", lambda: add_inst.atomic_rmw_bin_op, "atomicrmw"),
                (
                    "cmpxchg_success_ordering",
                    lambda: add_inst.cmpxchg_success_ordering,
                    "cmpxchg",
                ),
                (
                    "cmpxchg_failure_ordering",
                    lambda: add_inst.cmpxchg_failure_ordering,
                    "cmpxchg",
                ),
                ("weak", lambda: add_inst.weak, "cmpxchg"),
                ("set_weak", lambda: add_inst.set_weak(True), "cmpxchg"),
                ("tail_call_kind", lambda: add_inst.tail_call_kind, "call or invoke"),
                (
                    "set_tail_call_kind",
                    lambda: add_inst.set_tail_call_kind(refs["call_plain_inst"].tail_call_kind),
                    "call or invoke",
                ),
                ("num_incoming", lambda: add_inst.num_incoming, "a PHI instruction"),
                (
                    "get_incoming_value",
                    lambda: add_inst.get_incoming_value(0),
                    "a PHI instruction",
                ),
                (
                    "get_incoming_block",
                    lambda: add_inst.get_incoming_block(0),
                    "a PHI instruction",
                ),
                ("incoming", lambda: add_inst.incoming, "a PHI instruction"),
                (
                    "get_incoming_value",
                    lambda: phi_inst.get_incoming_value(99),
                    "out of range",
                ),
                (
                    "get_incoming_block",
                    lambda: phi_inst.get_incoming_block(99),
                    "out of range",
                ),
                (
                    "num_operand_bundles",
                    lambda: add_inst.num_operand_bundles,
                    "call, invoke, or callbr",
                ),
                (
                    "get_operand_bundle_at_index",
                    lambda: add_inst.get_operand_bundle_at_index(0),
                    "call, invoke, or callbr",
                ),
                (
                    "get_operand_bundle_at_index",
                    lambda: call_bundle_inst.get_operand_bundle_at_index(99),
                    "out of range",
                ),
                (
                    "called_function_type",
                    lambda: add_inst.called_function_type,
                    "call, invoke, or callbr",
                ),
                (
                    "called_value",
                    lambda: add_inst.called_value,
                    "call, invoke, or callbr",
                ),
                (
                    "set_called_operand",
                    lambda: add_inst.set_called_operand(call_target),
                    "call, invoke, or callbr",
                ),
                (
                    "num_arg_operands",
                    lambda: add_inst.num_arg_operands,
                    "call, invoke, callbr, catchpad, or cleanuppad",
                ),
                (
                    "get_arg_operand",
                    lambda: add_inst.get_arg_operand(0),
                    "call, invoke, callbr, catchpad, or cleanuppad",
                ),
                (
                    "get_arg_operand",
                    lambda: call_arg_inst.get_arg_operand(99),
                    "out of range",
                ),
                ("allocated_type", lambda: add_inst.allocated_type, "alloca instruction"),
                (
                    "num_indices",
                    lambda: add_inst.num_indices,
                    "getelementptr, extractvalue, or insertvalue",
                ),
                (
                    "indices",
                    lambda: add_inst.indices,
                    "extractvalue, insertvalue, or constant-expression",
                ),
                (
                    "indices",
                    lambda: refs["gep_inst"].indices,
                    "extractvalue, insertvalue, or constant-expression",
                ),
                (
                    "gep_source_element_type",
                    lambda: add_inst.gep_source_element_type,
                    "GEP value",
                ),
                (
                    "gep_no_wrap_flags",
                    lambda: add_inst.gep_no_wrap_flags,
                    "GEP value",
                ),
                (
                    "num_successors",
                    lambda: add_inst.num_successors,
                    "terminator instruction",
                ),
                (
                    "is_conditional",
                    lambda: add_inst.is_conditional,
                    "br instruction",
                ),
                ("condition", lambda: add_inst.condition, "br instruction"),
                (
                    "get_successor",
                    lambda: add_inst.get_successor(0),
                    "terminator instruction",
                ),
                (
                    "successors",
                    lambda: add_inst.successors,
                    "terminator instruction",
                ),
                (
                    "unwind_dest",
                    lambda: add_inst.unwind_dest,
                    "terminator instruction",
                ),
                ("normal_dest", lambda: br_inst.normal_dest, "an invoke instruction"),
                (
                    "callbr_default_dest",
                    lambda: br_inst.callbr_default_dest,
                    "a callbr instruction",
                ),
                (
                    "callbr_num_indirect_dests",
                    lambda: br_inst.callbr_num_indirect_dests,
                    "a callbr instruction",
                ),
                (
                    "get_callbr_indirect_dest",
                    lambda: br_inst.get_callbr_indirect_dest(0),
                    "a callbr instruction",
                ),
                (
                    "num_clauses",
                    lambda: add_inst.num_clauses,
                    "a landingpad instruction",
                ),
                (
                    "get_clause",
                    lambda: add_inst.get_clause(0),
                    "a landingpad instruction",
                ),
                (
                    "is_cleanup",
                    lambda: add_inst.is_cleanup,
                    "a landingpad instruction",
                ),
                (
                    "set_cleanup",
                    lambda: add_inst.set_cleanup(True),
                    "a landingpad instruction",
                ),
                ("add_clause", lambda: add_inst.add_clause(bad), "landingpad instruction"),
                (
                    "parent_catch_switch",
                    lambda: add_inst.parent_catch_switch,
                    "a catchpad instruction",
                ),
                (
                    "num_handlers",
                    lambda: add_inst.num_handlers,
                    "a catchswitch instruction",
                ),
                (
                    "handlers",
                    lambda: add_inst.handlers,
                    "a catchswitch instruction",
                ),
                (
                    "add_handler",
                    lambda: add_inst.add_handler(handler_bb),
                    "a catchswitch instruction",
                ),
                (
                    "num_mask_elements",
                    lambda: add_inst.num_mask_elements,
                    "a shufflevector instruction",
                ),
                (
                    "get_mask_value",
                    lambda: add_inst.get_mask_value(0),
                    "a shufflevector instruction",
                ),
                (
                    "get_mask_value",
                    lambda: shuffle_inst.get_mask_value(99),
                    "out of range",
                ),
                (
                    "fast_math_flags",
                    lambda: add_inst.fast_math_flags,
                    "supports fast-math flags",
                ),
                (
                    "set_fast_math_flags",
                    lambda: add_inst.set_fast_math_flags(0),
                    "supports fast-math flags",
                ),
                ("const_bitcast", lambda: add_inst.const_bitcast(ctx.types.ptr), "constant value"),
                (
                    "remove_from_parent",
                    lambda: bad.remove_from_parent(),
                    "instruction value",
                ),
                (
                    "delete_instruction",
                    lambda: bad.delete_instruction(),
                    "instruction value",
                ),
                (
                    "erase_from_parent",
                    lambda: bad.erase_from_parent(),
                    "instruction value",
                ),
                (
                    "instruction_clone",
                    lambda: bad.instruction_clone(),
                    "instruction value",
                ),
                (
                    "replace_all_uses_with",
                    lambda: add_inst.replace_all_uses_with(ctx.types.ptr.null()),
                    "identical type",
                ),
                (
                    "get_callsite_attribute_count",
                    lambda: add_inst.get_callsite_attribute_count(0),
                    "call, invoke, or callbr",
                ),
                (
                    "get_callsite_enum_attribute",
                    lambda: add_inst.get_callsite_enum_attribute(0, 1),
                    "call, invoke, or callbr",
                ),
                (
                    "add_callsite_attribute",
                    lambda: add_inst.add_callsite_attribute(
                        0, ctx.create_string_attribute("k", "v")
                    ),
                    "call, invoke, or callbr",
                ),
                (
                    "get_callsite_attribute_count",
                    lambda: refs["call_plain_inst"].get_callsite_attribute_count(-2),
                    "idx >= -1",
                ),
                (
                    "get_callsite_enum_attribute",
                    lambda: refs["call_plain_inst"].get_callsite_enum_attribute(-2, 1),
                    "idx >= -1",
                ),
                (
                    "add_callsite_attribute",
                    lambda: refs["call_plain_inst"].add_callsite_attribute(
                        -2, ctx.create_string_attribute("k2", "v2")
                    ),
                    "idx >= -1",
                ),
                (
                    "get_callsite_attribute_count",
                    lambda: refs["call_plain_inst"].get_callsite_attribute_count(1),
                    "out of range for callsite",
                ),
                (
                    "get_callsite_enum_attribute",
                    lambda: refs["call_plain_inst"].get_callsite_enum_attribute(1, 1),
                    "out of range for callsite",
                ),
                (
                    "add_callsite_attribute",
                    lambda: refs["call_plain_inst"].add_callsite_attribute(
                        1, ctx.create_string_attribute("k3", "v3")
                    ),
                    "out of range for callsite",
                ),
                ("initializer", lambda: bad.initializer, "global variable"),
                (
                    "initializer",
                    lambda: setattr(bad, "initializer", i32.constant(0, False)),
                    "global variable",
                ),
                ("set_constant", lambda: bad.set_constant(True), "global variable"),
                ("is_global_constant", lambda: bad.is_global_constant, "global variable"),
                ("linkage", lambda: bad.linkage, "global value"),
                (
                    "linkage",
                    lambda: setattr(bad, "linkage", g0.linkage),
                    "global value",
                ),
                ("visibility", lambda: bad.visibility, "global value"),
                (
                    "visibility",
                    lambda: setattr(bad, "visibility", g0.visibility),
                    "global value",
                ),
                ("dll_storage_class", lambda: bad.dll_storage_class, "global value"),
                (
                    "dll_storage_class",
                    lambda: setattr(bad, "dll_storage_class", g0.dll_storage_class),
                    "global value",
                ),
                ("comdat", lambda: bad.comdat, "global object"),
                ("set_comdat", lambda: bad.set_comdat(mod.get_or_insert_comdat("x")), "global object"),
                ("section", lambda: bad.section, "global object"),
                ("section", lambda: setattr(bad, "section", ".foo"), "global object"),
                ("set_thread_local", lambda: bad.set_thread_local(True), "global variable"),
                ("is_thread_local", lambda: bad.is_thread_local, "global variable"),
                (
                    "set_externally_initialized",
                    lambda: bad.set_externally_initialized(True),
                    "global variable",
                ),
                (
                    "is_externally_initialized",
                    lambda: bad.is_externally_initialized,
                    "global variable",
                ),
                ("delete_global", lambda: bad.delete_global(), "global variable"),
            ]

            for api_name, action, expected in negative_cases:
                assert_llvm_assertion(action, api_name, expected)


def test_value_accessor_guard_matrix_positive():
    with llvm.create_context() as ctx:
        with ctx.create_module("value_guard_positive") as mod:
            refs = build_fixture(ctx, mod)

            g0 = refs["g0"]
            g1 = refs["g1"]
            alias0 = refs["alias0"]
            alias1 = refs["alias1"]
            ifunc0 = refs["ifunc0"]
            ifunc1 = refs["ifunc1"]
            resolver0 = refs["resolver0"]
            resolver1 = refs["resolver1"]
            f = refs["f"]
            personality_fn = refs["personality_fn"]
            add_inst = refs["add_inst"]
            add_wrap_inst = refs["add_wrap_inst"]
            shuffle_inst = refs["shuffle_inst"]
            call_bundle_inst = refs["call_bundle_inst"]
            call_arg_inst = refs["call_arg_inst"]
            br_inst = refs["br_inst"]
            cond_br_inst = refs["cond_br_inst"]
            arg0 = refs["arg0"]
            arg1 = refs["arg1"]
            phi_inst = refs["phi_inst"]
            invoke_inst = refs["invoke_inst"]
            landing_inst = refs["landing_inst"]
            catchswitch_inst = refs["catchswitch_inst"]
            catchpad_inst = refs["catchpad_inst"]
            cleanuppad_inst = refs["cleanuppad_inst"]
            cleanup_ret_inst = refs["cleanup_ret_inst"]
            callbr_inst = refs["callbr_inst"]
            alloca_inst = refs["alloca_inst"]
            load_inst = refs["load_inst"]
            store_inst = refs["store_inst"]
            sdiv_inst = refs["sdiv_inst"]
            lshr_inst = refs["lshr_inst"]
            zext_inst = refs["zext_inst"]
            or_inst = refs["or_inst"]
            icmp_inst = refs["icmp_inst"]
            fcmp_inst = refs["fcmp_inst"]
            fadd_inst = refs["fadd_inst"]
            atomic_rmw_inst = refs["atomic_rmw_inst"]
            cmpxchg_inst = refs["cmpxchg_inst"]
            fence_inst = refs["fence_inst"]
            gep_inst = refs["gep_inst"]
            extract_value_inst = refs["extract_value_inst"]
            insert_value_inst = refs["insert_value_inst"]
            i32 = ctx.types.i32

            # Global variable/alias/ifunc.
            assert g0.next_global == g1
            assert g1.prev_global == g0
            assert alias0.next_global_alias == alias1
            assert alias1.prev_global_alias == alias0
            assert alias0.aliasee == g0
            alias0.alias_set_aliasee(g1)
            assert alias0.aliasee == g1

            assert ifunc0.next_global_ifunc == ifunc1
            assert ifunc1.prev_global_ifunc == ifunc0
            assert ifunc0.global_ifunc_resolver == resolver0
            ifunc0.set_global_ifunc_resolver(resolver1)
            assert ifunc0.global_ifunc_resolver == resolver1

            ifunc_remove = mod.add_global_ifunc(
                "ifunc_remove", refs["resolver_ty"], 0, resolver0
            )
            ifunc_remove.remove_from_parent_ifunc()
            ifunc_erase = mod.add_global_ifunc("ifunc_erase", refs["resolver_ty"], 0, resolver0)
            ifunc_erase.erase_from_parent_ifunc()
            try:
                _ = ifunc_erase.name
                assert False, "Expected erased ifunc to be invalid"
            except llvm.LLVMMemoryError:
                pass

            # Global value.
            assert g0.global_value_type.kind == llvm.TypeKind.Integer
            ua = g0.unnamed_address
            g0.unnamed_address = ua
            assert len(g0.global_copy_all_metadata()) >= 0

            # Function value.
            assert not f.has_personality_fn
            f.set_personality_fn(personality_fn)
            assert f.has_personality_fn
            assert f.personality_fn == personality_fn
            f.set_prefix_data(ctx.types.ptr.null())
            f.set_prologue_data(ctx.types.ptr.null())
            assert f.has_prefix_data
            assert f.prefix_data is not None
            assert f.has_prologue_data
            assert f.prologue_data is not None

            # Instruction value.
            assert len(add_inst.instruction_get_all_metadata_other_than_debug_loc()) >= 0
            assert add_inst.opcode == llvm.Opcode.Add
            assert add_inst.opcode_name == "add"
            next_inst = add_inst.next_instruction
            assert next_inst is not None
            assert next_inst.prev_instruction == add_inst
            prev_of_br = br_inst.prev_instruction
            assert prev_of_br is not None

            # Opcode-specific predicates/flags.
            assert icmp_inst.icmp_predicate == llvm.IntPredicate.EQ
            assert fcmp_inst.fcmp_predicate == llvm.RealPredicate.OEQ
            add_wrap_inst.set_nsw(True)
            assert add_wrap_inst.nsw
            add_wrap_inst.set_nuw(True)
            assert add_wrap_inst.nuw
            sdiv_inst.set_exact(True)
            assert sdiv_inst.exact
            lshr_inst.set_exact(True)
            assert lshr_inst.exact
            zext_inst.set_nneg(True)
            assert zext_inst.nneg
            or_inst.set_is_disjoint(True)
            assert or_inst.is_disjoint
            icmp_inst.set_icmp_same_sign(True)
            assert icmp_inst.icmp_same_sign

            # Alignment/volatile/ordering/atomic families.
            a = g0.alignment
            g0.alignment = a
            assert load_inst.is_volatile is False
            load_inst.set_volatile(True)
            assert load_inst.is_volatile
            store_inst.set_volatile(True)
            assert store_inst.is_volatile
            assert atomic_rmw_inst.ordering == llvm.AtomicOrdering.SequentiallyConsistent
            atomic_rmw_inst.set_ordering(llvm.AtomicOrdering.Monotonic)
            assert atomic_rmw_inst.ordering == llvm.AtomicOrdering.Monotonic
            assert atomic_rmw_inst.is_atomic
            scope_id = atomic_rmw_inst.atomic_sync_scope_id
            atomic_rmw_inst.set_atomic_sync_scope_id(scope_id)
            _ = atomic_rmw_inst.atomic_rmw_bin_op
            _ = cmpxchg_inst.cmpxchg_success_ordering
            _ = cmpxchg_inst.cmpxchg_failure_ordering
            weak_before = cmpxchg_inst.weak
            cmpxchg_inst.set_weak(not weak_before)
            assert cmpxchg_inst.weak == (not weak_before)
            _ = fence_inst.ordering

            # Argument.
            assert arg0.next_param == arg1
            assert arg1.prev_param == arg0

            # PHI.
            assert phi_inst.num_incoming == 2
            assert phi_inst.get_incoming_block(0) == refs["phi_left"]
            assert phi_inst.get_incoming_value(0) is not None
            assert len(phi_inst.incoming) == 2

            # Call-like + operand bundles.
            assert call_bundle_inst.num_operand_bundles == 1
            ob = call_bundle_inst.get_operand_bundle_at_index(0)
            assert ob.tag == "deopt"
            assert ob.num_args == 1
            assert call_arg_inst.called_function_type.kind == llvm.TypeKind.Function
            assert call_arg_inst.called_value == refs["call_arg_callee"]
            call_arg_inst.set_called_operand(refs["call_arg_callee"])
            _ = call_arg_inst.tail_call_kind
            call_arg_inst.set_tail_call_kind(call_arg_inst.tail_call_kind)

            # Arg-operand instructions.
            assert call_arg_inst.num_arg_operands == 1
            assert callbr_inst.num_arg_operands == 1
            assert catchpad_inst.num_arg_operands == 1
            assert cleanuppad_inst.num_arg_operands == 1
            assert call_arg_inst.get_arg_operand(0) == refs["f_arg0"]
            assert callbr_inst.get_arg_operand(0) == refs["cb_arg0"]
            assert catchpad_inst.get_arg_operand(0).const_zext_value == 5
            assert cleanuppad_inst.get_arg_operand(0).const_zext_value == 7

            # Terminator/invoke/callbr.
            assert br_inst.num_successors == 1
            assert br_inst.get_successor(0) == refs["f_exit"]
            assert len(br_inst.successors) == 1
            assert invoke_inst.normal_dest == refs["inv_normal"]
            assert invoke_inst.unwind_dest == refs["inv_unwind"]
            assert cleanup_ret_inst.unwind_dest is None

            assert callbr_inst.callbr_default_dest == refs["cb_default"]
            assert callbr_inst.callbr_num_indirect_dests == 1
            assert callbr_inst.get_callbr_indirect_dest(0) == refs["cb_indirect"]
            assert not br_inst.is_conditional
            assert cond_br_inst.is_conditional
            assert cond_br_inst.condition is not None

            # LandingPad.
            assert landing_inst.num_clauses == 1
            assert landing_inst.get_clause(0) is not None
            assert landing_inst.is_cleanup
            landing_inst.set_cleanup(False)
            assert not landing_inst.is_cleanup
            landing_inst.set_cleanup(True)
            assert landing_inst.is_cleanup

            # CatchPad/CatchSwitch.
            assert catchpad_inst.parent_catch_switch == catchswitch_inst
            initial_handlers = catchswitch_inst.num_handlers
            assert len(catchswitch_inst.handlers) == initial_handlers
            catchswitch_inst.add_handler(refs["cs_extra"])
            assert catchswitch_inst.num_handlers == initial_handlers + 1

            # ShuffleVector.
            assert shuffle_inst.num_mask_elements == 4
            assert shuffle_inst.get_mask_value(0) == 0

            # Indexed-value families.
            assert alloca_inst.allocated_type.kind == llvm.TypeKind.Integer
            assert gep_inst.num_indices == 1
            assert gep_inst.gep_source_element_type.kind == llvm.TypeKind.Integer
            assert isinstance(gep_inst.gep_no_wrap_flags, int)
            assert extract_value_inst.num_indices == 1
            assert extract_value_inst.indices == [0]
            assert insert_value_inst.num_indices == 1
            assert insert_value_inst.indices == [1]

            # Fast-math flags.
            assert fadd_inst.can_use_fast_math_flags
            fmf = fadd_inst.fast_math_flags
            fadd_inst.set_fast_math_flags(fmf)
            assert fadd_inst.fast_math_flags == fmf

            # const_bitcast/replace/callsite attributes.
            ptr_null = ctx.types.ptr.null()
            assert ptr_null.const_bitcast(ctx.types.ptr) is not None
            add_wrap_inst.replace_all_uses_with(ctx.types.i32.constant(7, False))
            attr = ctx.create_string_attribute("k", "v")
            before = call_arg_inst.get_callsite_attribute_count(llvm.AttributeFunctionIndex)
            call_arg_inst.add_callsite_attribute(llvm.AttributeFunctionIndex, attr)
            after = call_arg_inst.get_callsite_attribute_count(llvm.AttributeFunctionIndex)
            assert after >= before + 1
            _ = call_arg_inst.get_callsite_enum_attribute(llvm.AttributeFunctionIndex, 1)

            # Instruction lifecycle APIs.
            cloned = add_inst.instruction_clone()
            assert cloned.is_instruction

            remove_fn_ty = ctx.types.function(ctx.types.void, [], False)
            remove_fn = mod.add_function("remove_fn", remove_fn_ty)
            remove_bb = remove_fn.append_basic_block("entry")
            with remove_bb.create_builder() as b:
                remove_slot = b.alloca(i32, "remove_slot")
                b.store(i32.constant(1, False), remove_slot)
                remove_lhs = b.load(i32, remove_slot, "remove_lhs")
                removable = b.add(remove_lhs, i32.constant(2, False), "rm")
                b.ret_void()
            removable.remove_from_parent()

            erase_fn_ty = ctx.types.function(ctx.types.void, [], False)
            erase_fn = mod.add_function("erase_fn", erase_fn_ty)
            erase_bb = erase_fn.append_basic_block("entry")
            with erase_bb.create_builder() as b:
                erase_slot = b.alloca(i32, "erase_slot")
                b.store(i32.constant(1, False), erase_slot)
                erase_lhs = b.load(i32, erase_slot, "erase_lhs")
                erasable = b.add(erase_lhs, i32.constant(2, False), "er")
                b.ret_void()
            erasable.erase_from_parent()
            try:
                _ = erasable.name
                assert False, "Expected erased instruction to be invalid"
            except llvm.LLVMMemoryError:
                pass

            delete_fn_ty = ctx.types.function(ctx.types.void, [], False)
            delete_fn = mod.add_function("delete_fn", delete_fn_ty)
            delete_bb = delete_fn.append_basic_block("entry")
            with delete_bb.create_builder() as b:
                delete_slot = b.alloca(i32, "delete_slot")
                b.store(i32.constant(1, False), delete_slot)
                delete_lhs = b.load(i32, delete_slot, "delete_lhs")
                deletable = b.add(delete_lhs, i32.constant(2, False), "del")
                b.ret_void()
            deletable.delete_instruction()
            try:
                _ = deletable.name
                assert False, "Expected deleted instruction to be invalid"
            except llvm.LLVMMemoryError:
                pass

            # Global helper-backed APIs.
            assert g0.initializer is not None
            g0.initializer = ctx.types.i32.constant(123, False)
            g0.set_constant(True)
            assert g0.is_global_constant
            link = g0.linkage
            g0.linkage = link
            vis = g0.visibility
            g0.visibility = vis
            dll = g0.dll_storage_class
            g0.dll_storage_class = dll
            section = g0.section
            g0.section = section
            g0.set_thread_local(True)
            assert g0.is_thread_local
            g0.set_externally_initialized(True)
            assert g0.is_externally_initialized
            c = mod.get_or_insert_comdat("value_guard_positive_comdat")
            g0.set_comdat(c)
            assert g0.comdat is not None
            g_del = mod.add_global(ctx.types.i32, "g_del")
            g_del.delete_global()
            try:
                _ = g_del.name
                assert False, "Expected deleted global to be invalid"
            except llvm.LLVMMemoryError:
                pass


if __name__ == "__main__":
    test_value_accessor_guard_matrix_negative()
    print("test_value_accessor_guard_matrix_negative: PASSED")

    test_value_accessor_guard_matrix_positive()
    print("test_value_accessor_guard_matrix_positive: PASSED")
