#!/usr/bin/env -S uv run
"""
Test: test_builder_cmp
Tests LLVM Builder comparison operations

Python equivalent of tests/test_builder_cmp.cpp
Must produce identical output to the C++ version.
"""

import llvm


def int_pred_name(pred):
    names = {
        llvm.IntPredicate.EQ: "eq",
        llvm.IntPredicate.NE: "ne",
        llvm.IntPredicate.UGT: "ugt",
        llvm.IntPredicate.UGE: "uge",
        llvm.IntPredicate.ULT: "ult",
        llvm.IntPredicate.ULE: "ule",
        llvm.IntPredicate.SGT: "sgt",
        llvm.IntPredicate.SGE: "sge",
        llvm.IntPredicate.SLT: "slt",
        llvm.IntPredicate.SLE: "sle",
    }
    return names.get(pred, "unknown")


def real_pred_name(pred):
    names = {
        llvm.RealPredicate.PredicateFalse: "false",
        llvm.RealPredicate.OEQ: "oeq",
        llvm.RealPredicate.OGT: "ogt",
        llvm.RealPredicate.OGE: "oge",
        llvm.RealPredicate.OLT: "olt",
        llvm.RealPredicate.OLE: "ole",
        llvm.RealPredicate.ONE: "one",
        llvm.RealPredicate.ORD: "ord",
        llvm.RealPredicate.UNO: "uno",
        llvm.RealPredicate.UEQ: "ueq",
        llvm.RealPredicate.UGT: "ugt",
        llvm.RealPredicate.UGE: "uge",
        llvm.RealPredicate.ULT: "ult",
        llvm.RealPredicate.ULE: "ule",
        llvm.RealPredicate.UNE: "une",
        llvm.RealPredicate.PredicateTrue: "true",
    }
    return names.get(pred, "unknown")


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_builder_cmp") as mod:
            i1 = ctx.int1_type()
            i32 = ctx.int32_type()
            f64 = ctx.double_type()
            void_ty = ctx.void_type()

            with ctx.create_builder() as builder:
                # ==========================================
                # Function 1: Integer comparisons
                # ==========================================
                icmp_ty = ctx.function_type(void_ty, [i32, i32])
                icmp_func = mod.add_function("int_comparisons", icmp_ty)
                a = icmp_func.get_param(0)
                b = icmp_func.get_param(1)
                a.name = "a"
                b.name = "b"

                icmp_entry = icmp_func.append_basic_block("entry", ctx)
                builder.position_at_end(icmp_entry)

                # All integer predicates
                icmp_eq = builder.icmp(llvm.IntPredicate.EQ, a, b, "eq")
                icmp_ne = builder.icmp(llvm.IntPredicate.NE, a, b, "ne")
                icmp_ugt = builder.icmp(llvm.IntPredicate.UGT, a, b, "ugt")
                icmp_uge = builder.icmp(llvm.IntPredicate.UGE, a, b, "uge")
                icmp_ult = builder.icmp(llvm.IntPredicate.ULT, a, b, "ult")
                icmp_ule = builder.icmp(llvm.IntPredicate.ULE, a, b, "ule")
                icmp_sgt = builder.icmp(llvm.IntPredicate.SGT, a, b, "sgt")
                icmp_sge = builder.icmp(llvm.IntPredicate.SGE, a, b, "sge")
                icmp_slt = builder.icmp(llvm.IntPredicate.SLT, a, b, "slt")
                icmp_sle = builder.icmp(llvm.IntPredicate.SLE, a, b, "sle")

                builder.ret_void()

                # ==========================================
                # Function 2: Float comparisons
                # ==========================================
                fcmp_ty = ctx.function_type(void_ty, [f64, f64])
                fcmp_func = mod.add_function("float_comparisons", fcmp_ty)
                x = fcmp_func.get_param(0)
                y = fcmp_func.get_param(1)
                x.name = "x"
                y.name = "y"

                fcmp_entry = fcmp_func.append_basic_block("entry", ctx)
                builder.position_at_end(fcmp_entry)

                # Ordered comparisons (false if either is NaN)
                fcmp_oeq = builder.fcmp(llvm.RealPredicate.OEQ, x, y, "oeq")
                fcmp_ogt = builder.fcmp(llvm.RealPredicate.OGT, x, y, "ogt")
                fcmp_oge = builder.fcmp(llvm.RealPredicate.OGE, x, y, "oge")
                fcmp_olt = builder.fcmp(llvm.RealPredicate.OLT, x, y, "olt")
                fcmp_ole = builder.fcmp(llvm.RealPredicate.OLE, x, y, "ole")
                fcmp_one = builder.fcmp(llvm.RealPredicate.ONE, x, y, "one")
                fcmp_ord = builder.fcmp(llvm.RealPredicate.ORD, x, y, "ord")

                # Unordered comparisons (true if either is NaN)
                fcmp_uno = builder.fcmp(llvm.RealPredicate.UNO, x, y, "uno")
                fcmp_ueq = builder.fcmp(llvm.RealPredicate.UEQ, x, y, "ueq")
                fcmp_ugt = builder.fcmp(llvm.RealPredicate.UGT, x, y, "ugt")
                fcmp_uge = builder.fcmp(llvm.RealPredicate.UGE, x, y, "uge")
                fcmp_ult = builder.fcmp(llvm.RealPredicate.ULT, x, y, "ult")
                fcmp_ule = builder.fcmp(llvm.RealPredicate.ULE, x, y, "ule")
                fcmp_une = builder.fcmp(llvm.RealPredicate.UNE, x, y, "une")

                # Always true/false
                fcmp_true = builder.fcmp(
                    llvm.RealPredicate.PredicateTrue, x, y, "always_true"
                )
                fcmp_false = builder.fcmp(
                    llvm.RealPredicate.PredicateFalse, x, y, "always_false"
                )

                builder.ret_void()

                # ==========================================
                # Function 3: Select instruction
                # ==========================================
                sel_ty = ctx.function_type(i32, [i1, i32, i32])
                sel_func = mod.add_function("select_example", sel_ty)
                cond = sel_func.get_param(0)
                true_val = sel_func.get_param(1)
                false_val = sel_func.get_param(2)
                cond.name = "cond"
                true_val.name = "true_val"
                false_val.name = "false_val"

                sel_entry = sel_func.append_basic_block("entry", ctx)
                builder.position_at_end(sel_entry)

                selected = builder.select(cond, true_val, false_val, "selected")
                builder.ret(selected)

                # ==========================================
                # Function 4: Select with comparison
                # ==========================================
                max_ty = ctx.function_type(i32, [i32, i32])
                max_func = mod.add_function("max", max_ty)
                m_a = max_func.get_param(0)
                m_b = max_func.get_param(1)
                m_a.name = "a"
                m_b.name = "b"

                max_entry = max_func.append_basic_block("entry", ctx)
                builder.position_at_end(max_entry)

                cmp_gt = builder.icmp(llvm.IntPredicate.SGT, m_a, m_b, "a_gt_b")
                max_result = builder.select(cmp_gt, m_a, m_b, "max")
                builder.ret(max_result)

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_builder_cmp")
            print(";")
            print("; Integer comparison predicates:")
            print(";   eq, ne (equality)")
            print(";   ugt, uge, ult, ule (unsigned)")
            print(";   sgt, sge, slt, sle (signed)")
            print(";")
            print("; Float comparison predicates:")
            print(";   Ordered: oeq, ogt, oge, olt, ole, one, ord")
            print(";   Unordered: uno, ueq, ugt, uge, ult, ule, une")
            print(";   Constant: true, false")
            print(";")
            print("; Predicate extraction:")
            print(
                f";   icmp_eq predicate: {int_pred_name(icmp_eq.get_icmp_predicate())}"
            )
            print(
                f";   icmp_slt predicate: {int_pred_name(icmp_slt.get_icmp_predicate())}"
            )
            print(
                f";   fcmp_oeq predicate: {real_pred_name(fcmp_oeq.get_fcmp_predicate())}"
            )
            print(
                f";   fcmp_uno predicate: {real_pred_name(fcmp_uno.get_fcmp_predicate())}"
            )
            print(";")
            print("; Select instruction: cond ? true_val : false_val")
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
