#!/usr/bin/env -S uv run
"""
Test: test_phi
Tests LLVM PHI node operations

Python equivalent of tests/test_phi.cpp
Must produce identical output to the C++ version.
"""

import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_phi") as mod:
            i1 = ctx.int1_type()
            i32 = ctx.int32_type()

            with ctx.create_builder() as builder:
                # ==========================================
                # Function: simple diamond pattern with PHI
                # i32 diamond(i1 cond, i32 a, i32 b)
                # ==========================================
                diamond_ty = ctx.function_type(i32, [i1, i32, i32])
                diamond_func = mod.add_function("diamond", diamond_ty)

                cond = diamond_func.get_param(0)
                a = diamond_func.get_param(1)
                b = diamond_func.get_param(2)
                cond.name = "cond"
                a.name = "a"
                b.name = "b"

                entry = diamond_func.append_basic_block("entry", ctx)
                if_true = diamond_func.append_basic_block("if_true", ctx)
                if_false = diamond_func.append_basic_block("if_false", ctx)
                merge = diamond_func.append_basic_block("merge", ctx)

                # Entry: conditional branch
                builder.position_at_end(entry)
                builder.cond_br(cond, if_true, if_false)

                # True branch: compute a * 2
                builder.position_at_end(if_true)
                a_doubled = builder.mul(a, llvm.const_int(i32, 2), "a_doubled")
                builder.br(merge)

                # False branch: compute b + 1
                builder.position_at_end(if_false)
                b_inc = builder.add(b, llvm.const_int(i32, 1), "b_inc")
                builder.br(merge)

                # Merge: PHI node to select result
                builder.position_at_end(merge)
                phi = builder.phi(i32, "result")

                # Add incoming values
                phi.add_incoming(a_doubled, if_true)
                phi.add_incoming(b_inc, if_false)

                builder.ret(phi)

                # ==========================================
                # Function: loop with PHI (sum 1 to n)
                # i32 sum_to_n(i32 n)
                # ==========================================
                sum_ty = ctx.function_type(i32, [i32])
                sum_func = mod.add_function("sum_to_n", sum_ty)

                n = sum_func.get_param(0)
                n.name = "n"

                sum_entry = sum_func.append_basic_block("entry", ctx)
                loop = sum_func.append_basic_block("loop", ctx)
                exit_bb = sum_func.append_basic_block("exit", ctx)

                # Entry: branch to loop
                builder.position_at_end(sum_entry)
                builder.br(loop)

                # Loop header with PHIs
                builder.position_at_end(loop)
                i_phi = builder.phi(i32, "i")
                sum_phi = builder.phi(i32, "sum")

                # Loop body: sum += i; i++
                new_sum = builder.add(sum_phi, i_phi, "new_sum")
                new_i = builder.add(i_phi, llvm.const_int(i32, 1), "new_i")

                # Loop condition: i <= n
                loop_cond = builder.icmp(llvm.IntPredicate.SLE, new_i, n, "loop_cond")
                builder.cond_br(loop_cond, loop, exit_bb)

                # Add incoming values to PHIs
                # From entry: i=1, sum=0
                i_phi.add_incoming(llvm.const_int(i32, 1), sum_entry)
                i_phi.add_incoming(new_i, loop)

                sum_phi.add_incoming(llvm.const_int(i32, 0), sum_entry)
                sum_phi.add_incoming(new_sum, loop)

                # Exit: return sum
                builder.position_at_end(exit_bb)
                builder.ret(new_sum)

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_phi")
            print(";")
            print("; Diamond pattern PHI:")
            print(f";   phi incoming count: {phi.count_incoming()}")

            # Get incoming values and blocks
            for i in range(phi.count_incoming()):
                val = phi.get_incoming_value(i)
                blk = phi.get_incoming_block(i)
                print(f";   incoming[{i}]: value={val.name}, block={blk.name}")

            print(";")
            print("; Loop PHIs:")
            print(f";   i_phi incoming count: {i_phi.count_incoming()}")
            print(f";   sum_phi incoming count: {sum_phi.count_incoming()}")

            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
