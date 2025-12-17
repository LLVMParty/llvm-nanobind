#!/usr/bin/env -S uv run
"""
Test: test_builder_arithmetic
Tests LLVM Builder arithmetic instruction creation

Python equivalent of tests/test_builder_arithmetic.cpp
Must produce identical output to the C++ version.
"""

import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_builder_arithmetic") as mod:
            i32 = ctx.int32_type()
            i64 = ctx.int64_type()
            f64 = ctx.double_type()

            # Integer arithmetic function: i32 int_arith(i32, i32)
            int_func_ty = ctx.function_type(i32, [i32, i32])
            int_func = mod.add_function("int_arith", int_func_ty)

            a = int_func.get_param(0)
            b = int_func.get_param(1)
            a.name = "a"
            b.name = "b"

            with ctx.create_builder() as builder:
                int_entry = int_func.append_basic_block("entry", ctx)
                builder.position_at_end(int_entry)

                # Basic arithmetic
                add = builder.add(a, b, "add")
                sub = builder.sub(a, b, "sub")
                mul = builder.mul(a, b, "mul")
                sdiv = builder.sdiv(a, b, "sdiv")
                udiv = builder.udiv(a, b, "udiv")
                srem = builder.srem(a, b, "srem")
                urem = builder.urem(a, b, "urem")

                # With overflow flags
                nsw_add = builder.nsw_add(a, b, "nsw_add")
                nuw_add = builder.nuw_add(a, b, "nuw_add")
                nsw_sub = builder.nsw_sub(a, b, "nsw_sub")
                nuw_sub = builder.nuw_sub(a, b, "nuw_sub")
                nsw_mul = builder.nsw_mul(a, b, "nsw_mul")
                nuw_mul = builder.nuw_mul(a, b, "nuw_mul")
                exact_sdiv = builder.exact_sdiv(a, b, "exact_sdiv")

                # Bitwise operations
                and_op = builder.and_(a, b, "and")
                or_op = builder.or_(a, b, "or")
                xor_op = builder.xor_(a, b, "xor")

                # Shift operations
                shl = builder.shl(a, b, "shl")
                lshr = builder.lshr(a, b, "lshr")
                ashr = builder.ashr(a, b, "ashr")

                # Unary operations
                neg = builder.neg(a, "neg")
                nsw_neg = builder.nsw_neg(a, "nsw_neg")
                not_op = builder.not_(a, "not")

                # Return something to make function complete
                builder.ret(add)

            # Floating point arithmetic function: f64 float_arith(f64, f64)
            with ctx.create_builder() as builder:
                fp_func_ty = ctx.function_type(f64, [f64, f64])
                fp_func = mod.add_function("float_arith", fp_func_ty)

                x = fp_func.get_param(0)
                y = fp_func.get_param(1)
                x.name = "x"
                y.name = "y"

                fp_entry = fp_func.append_basic_block("entry", ctx)
                builder.position_at_end(fp_entry)

                # Floating point operations
                fadd = builder.fadd(x, y, "fadd")
                fsub = builder.fsub(x, y, "fsub")
                fmul = builder.fmul(x, y, "fmul")
                fdiv = builder.fdiv(x, y, "fdiv")
                frem = builder.frem(x, y, "frem")
                fneg = builder.fneg(x, "fneg")

                builder.ret(fadd)

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_builder_arithmetic")
            print(";")
            print("; Integer operations demonstrated:")
            print(";   add, sub, mul, sdiv, udiv, srem, urem")
            print(
                ";   nsw_add, nuw_add, nsw_sub, nuw_sub, nsw_mul, nuw_mul, exact_sdiv"
            )
            print(";   and, or, xor, shl, lshr, ashr")
            print(";   neg, nsw_neg, not")
            print(";")
            print("; Floating point operations demonstrated:")
            print(";   fadd, fsub, fmul, fdiv, frem, fneg")
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
