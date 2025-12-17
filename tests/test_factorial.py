#!/usr/bin/env -S uv run
"""
Test: test_factorial
Integration test: Iterative factorial function

This is the Python equivalent of tests/test_factorial.cpp.
Output should match the C++ golden master test.

This test creates a complete, realistic LLVM function that computes
factorial iteratively. It demonstrates comprehensive use of:
- Types, functions, basic blocks
- Builder operations (alloca, load, store, arithmetic, comparisons)
- Control flow (conditional branch, loop)
- PHI nodes alternative: using alloca/load/store pattern
"""

import sys
import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_factorial") as mod:
            # Set target for more realistic output
            mod.target_triple = "x86_64-unknown-linux-gnu"

            i64 = ctx.int64_type()
            i1 = ctx.int1_type()

            with ctx.create_builder() as builder:
                # ==========================================
                # Function: i64 factorial(i64 n)
                # Iterative implementation using alloca/load/store
                # ==========================================
                fact_ty = ctx.function_type(i64, [i64])
                fact_func = mod.add_function("factorial", fact_ty)

                n = fact_func.get_param(0)
                n.name = "n"

                # Basic blocks
                entry = fact_func.append_basic_block("entry", ctx)
                loop_cond = fact_func.append_basic_block("loop_cond", ctx)
                loop_body = fact_func.append_basic_block("loop_body", ctx)
                exit_bb = fact_func.append_basic_block("exit", ctx)

                # Entry block: initialize result=1, i=1
                builder.position_at_end(entry)
                result_ptr = builder.alloca(i64, "result")
                i_ptr = builder.alloca(i64, "i")

                builder.store(llvm.const_int(i64, 1), result_ptr)
                builder.store(llvm.const_int(i64, 1), i_ptr)
                builder.br(loop_cond)

                # Loop condition: while (i <= n)
                builder.position_at_end(loop_cond)
                i_val = builder.load(i64, i_ptr, "i_val")
                cmp = builder.icmp(llvm.IntPredicate.SLE, i_val, n, "cmp")
                builder.cond_br(cmp, loop_body, exit_bb)

                # Loop body: result *= i; i++
                builder.position_at_end(loop_body)
                result_val = builder.load(i64, result_ptr, "result_val")
                i_val2 = builder.load(i64, i_ptr, "i_val2")

                new_result = builder.mul(result_val, i_val2, "new_result")
                builder.store(new_result, result_ptr)

                new_i = builder.add(i_val2, llvm.const_int(i64, 1), "new_i")
                builder.store(new_i, i_ptr)

                builder.br(loop_cond)

                # Exit: return result
                builder.position_at_end(exit_bb)
                final_result = builder.load(i64, result_ptr, "final_result")
                builder.ret(final_result)

                # ==========================================
                # Function: i64 factorial_recursive(i64 n)
                # Recursive implementation for comparison
                # ==========================================
                fact_rec_func = mod.add_function("factorial_recursive", fact_ty)
                n_rec = fact_rec_func.get_param(0)
                n_rec.name = "n"

                rec_entry = fact_rec_func.append_basic_block("entry", ctx)
                base_case = fact_rec_func.append_basic_block("base_case", ctx)
                recursive = fact_rec_func.append_basic_block("recursive", ctx)

                # Entry: if n <= 1 goto base_case else goto recursive
                builder.position_at_end(rec_entry)
                is_base = builder.icmp(
                    llvm.IntPredicate.SLE, n_rec, llvm.const_int(i64, 1), "is_base"
                )
                builder.cond_br(is_base, base_case, recursive)

                # Base case: return 1
                builder.position_at_end(base_case)
                builder.ret(llvm.const_int(i64, 1))

                # Recursive: return n * factorial_recursive(n-1)
                builder.position_at_end(recursive)
                n_minus_1 = builder.sub(n_rec, llvm.const_int(i64, 1), "n_minus_1")
                rec_result = builder.call(
                    fact_ty, fact_rec_func, [n_minus_1], "rec_result"
                )
                final_rec = builder.mul(n_rec, rec_result, "final_rec")
                builder.ret(final_rec)

                # ==========================================
                # Function: i64 main()
                # Calls factorial(5) and returns the result
                # ==========================================
                main_ty = ctx.function_type(i64, [])
                main_func = mod.add_function("main", main_ty)

                main_entry = main_func.append_basic_block("entry", ctx)
                builder.position_at_end(main_entry)

                fact_result = builder.call(
                    fact_ty, fact_func, [llvm.const_int(i64, 5)], "fact_result"
                )
                builder.ret(fact_result)

            # Verify module (after builder is disposed)
            if not mod.verify():
                print(
                    f"; Verification failed: {mod.get_verification_error()}",
                    file=sys.stderr,
                )
                return 1

            # Print diagnostic comments
            print("; Test: test_factorial")
            print("; Integration test: Iterative and recursive factorial")
            print(";")
            print("; factorial(i64 n) -> i64:")
            print(";   Iterative implementation using alloca/load/store")
            print(";   Blocks: entry -> loop_cond -> loop_body -> exit")
            print(";")
            print("; factorial_recursive(i64 n) -> i64:")
            print(";   Recursive implementation")
            print(";   Blocks: entry -> base_case / recursive")
            print(";")
            print("; main() -> i64:")
            print(";   Calls factorial(5), expected result: 120")
            print(";")
            print("; Function info:")

            # Print function info
            for func_name in ["factorial", "factorial_recursive", "main"]:
                func = mod.get_function(func_name)
                if func:
                    print(
                        f";   {func.name}: {func.param_count} params, {func.basic_block_count} blocks"
                    )

            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
