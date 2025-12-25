#!/usr/bin/env -S uv run
"""
Test: test_passbuilder
Tests for the new pass manager / PassBuilder API

This is the Python equivalent of tests/test_passbuilder.cpp.
Output should match the C++ golden master test.

LLVM Python APIs tested:
- llvm.PassBuilderOptions()
- options.set_verify_each(), set_debug_logging(), etc.
- llvm.run_passes()
"""

import sys
import llvm


def build_optimizable_module(mod, ctx):
    """Build optimizable code into an existing module."""
    mod.target_triple = "x86_64-unknown-linux-gnu"

    i32 = ctx.types.i32

    # Create: int add_inline(int x, int y) { return x + y; }
    add_ty = ctx.types.function(i32, [i32, i32])
    add_fn = mod.add_function("add_inline", add_ty)
    add_fn.linkage = llvm.Linkage.Internal

    add_entry = add_fn.append_basic_block("entry")
    with add_entry.create_builder() as builder:
        sum_val = builder.add(add_fn.get_param(0), add_fn.get_param(1), "sum")
        builder.ret(sum_val)

    # Create: int compute(int n) {
    #   int result = 0;
    #   for (int i = 0; i < n; i++) {
    #     result = add_inline(result, i);
    #   }
    #   return result;
    # }
    compute_ty = ctx.types.function(i32, [i32])
    compute_fn = mod.add_function("compute", compute_ty)

    entry = compute_fn.append_basic_block("entry")
    loop = compute_fn.append_basic_block("loop")
    loop_body = compute_fn.append_basic_block("loop_body")
    exit_bb = compute_fn.append_basic_block("exit")

    with entry.create_builder() as builder:
        # Entry: initialize loop variables
        result_ptr = builder.alloca(i32, "result_ptr")
        i_ptr = builder.alloca(i32, "i_ptr")
        builder.store(i32.constant(0), result_ptr)
        builder.store(i32.constant(0), i_ptr)
        builder.br(loop)

        # Loop
        builder.position_at_end(loop)
        i_val = builder.load(i32, i_ptr, "i")
        n = compute_fn.get_param(0)
        cond = builder.icmp(llvm.IntPredicate.SLT, i_val, n, "cond")
        builder.cond_br(cond, loop_body, exit_bb)

        # Loop body
        builder.position_at_end(loop_body)
        result_val = builder.load(i32, result_ptr, "result")
        i_val = builder.load(i32, i_ptr, "i2")

        # Call add_inline
        new_result = builder.call(add_ty, add_fn, [result_val, i_val], "new_result")
        builder.store(new_result, result_ptr)

        # Increment i
        new_i = builder.add(i_val, i32.constant(1), "new_i")
        builder.store(new_i, i_ptr)
        builder.br(loop)

        # Exit
        builder.position_at_end(exit_bb)
        final_result = builder.load(i32, result_ptr, "final_result")
        builder.ret(final_result)


def count_instructions(mod):
    """Count instructions in a module."""
    count = 0
    for fn in mod.functions:
        for bb in fn.basic_blocks:
            for _ in bb.instructions:
                count += 1
    return count


def count_functions(mod):
    """Count functions in a module."""
    return len(mod.functions)


def main():
    # Initialize all targets (needed for pass manager)
    llvm.initialize_all_target_infos()
    llvm.initialize_all_targets()
    llvm.initialize_all_target_mcs()
    llvm.initialize_all_asm_printers()
    llvm.initialize_all_asm_parsers()

    # Get default target for running passes
    triple = llvm.get_default_target_triple()
    target = llvm.get_target_from_triple(triple)
    if target is None:
        print("; ERROR: Failed to get target", file=sys.stderr)
        return 1
    tm = llvm.create_target_machine(
        target, triple, "generic", "",
        llvm.CodeGenOptLevel.Default,
        llvm.RelocMode.Default,
        llvm.CodeModel.Default
    )

    print("; Test: test_passbuilder")
    print("; Tests optimization pass running via PassBuilder API")
    print(";")

    with llvm.create_context() as ctx:
        # ==========================================================================
        # Test 1: Create PassBuilderOptions with various settings
        # ==========================================================================
        print("; Test 1: PassBuilderOptions configuration")

        opts = llvm.PassBuilderOptions()
        
        # Configure options
        opts.set_verify_each(False)
        opts.set_debug_logging(False)
        opts.set_loop_interleaving(True)
        opts.set_loop_vectorization(True)
        opts.set_slp_vectorization(True)
        opts.set_loop_unrolling(True)
        opts.set_merge_functions(True)
        opts.set_inliner_threshold(250)

        print(";   PassBuilderOptions created: yes")
        print(";   Options configured: yes")

        # ==========================================================================
        # Test 2: Optimization at O0 (no optimization)
        # ==========================================================================
        print(";")
        print("; Test 2: O0 optimization (no optimization)")

        with ctx.create_module("optimize_me") as mod_o0:
            build_optimizable_module(mod_o0, ctx)
            instr_before_o0 = count_instructions(mod_o0)
            funcs_before_o0 = count_functions(mod_o0)

            llvm.run_passes(mod_o0, "default<O0>", target_machine=tm, options=opts)

            instr_after_o0 = count_instructions(mod_o0)
            funcs_after_o0 = count_functions(mod_o0)

            print(f";   Instructions before: {instr_before_o0}, after: {instr_after_o0}")
            print(f";   Functions before: {funcs_before_o0}, after: {funcs_after_o0}")
            print(";   O0 passes ran successfully: yes")

        # ==========================================================================
        # Test 3: Optimization at O2
        # ==========================================================================
        print(";")
        print("; Test 3: O2 optimization")

        with ctx.create_module("optimize_me") as mod_o2:
            build_optimizable_module(mod_o2, ctx)
            instr_before_o2 = count_instructions(mod_o2)
            funcs_before_o2 = count_functions(mod_o2)

            llvm.run_passes(mod_o2, "default<O2>", target_machine=tm, options=opts)

            instr_after_o2 = count_instructions(mod_o2)
            funcs_after_o2 = count_functions(mod_o2)

            print(f";   Instructions before: {instr_before_o2}, after: {instr_after_o2}")
            print(f";   Functions before: {funcs_before_o2}, after: {funcs_after_o2}")
            print(";   O2 passes ran successfully: yes")

            # At O2, the add_inline function should be inlined and possibly removed
            add_fn = mod_o2.get_function("add_inline")
            print(f";   add_inline function removed: {'no' if add_fn else 'yes'}")

            # Verify the optimized module is still valid
            if not mod_o2.verify():
                print(f"; ERROR: Optimized module verification failed: {mod_o2.get_verification_error()}",
                      file=sys.stderr)
            else:
                print(";   Optimized module verified: yes")

        # ==========================================================================
        # Test 4: Custom pass pipeline
        # ==========================================================================
        print(";")
        print("; Test 4: Custom pass pipeline")

        with ctx.create_module("optimize_me") as mod_custom:
            build_optimizable_module(mod_custom, ctx)
            instr_before_custom = count_instructions(mod_custom)

            # Run just instcombine and simplifycfg
            llvm.run_passes(mod_custom, "instcombine,simplifycfg", target_machine=tm, options=opts)

            instr_after_custom = count_instructions(mod_custom)
            print(f";   Instructions before: {instr_before_custom}, after: {instr_after_custom}")
            print(";   Custom passes ran successfully: yes")

            # Print the optimized module
            print(";")
            print("; Optimized module (after instcombine,simplifycfg):")
            print(mod_custom.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
