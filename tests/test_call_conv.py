#!/usr/bin/env -S uv run
"""
Test: test_call_conv
Tests calling convention enum values and instruction call conv get/set
via Python bindings.

This is the Python equivalent of tests/test_call_conv.cpp.
Output should match the C++ golden master test.

LLVM APIs covered (via Python bindings):
- CallConv enum (all values including new: GHC, HiPE, etc.)
- instruction_call_conv property (get/set)
- calling_conv property on functions
"""

import sys
import llvm


def call_conv_name(cc: llvm.CallConv) -> str:
    mapping = {
        llvm.CallConv.C: "ccc",
        llvm.CallConv.Fast: "fastcc",
        llvm.CallConv.Cold: "coldcc",
        llvm.CallConv.X86Stdcall: "x86_stdcallcc",
        llvm.CallConv.X86Fastcall: "x86_fastcallcc",
        llvm.CallConv.GHC: "ghccc",
        llvm.CallConv.HiPE: "cc11",
        llvm.CallConv.PreserveMost: "preserve_mostcc",
        llvm.CallConv.PreserveAll: "preserve_allcc",
        llvm.CallConv.Swift: "swiftcc",
        llvm.CallConv.CXX_FAST_TLS: "cxx_fast_tlscc",
        llvm.CallConv.X86ThisCall: "x86_thiscallcc",
        llvm.CallConv.X86_64_SysV: "x86_64_sysvcc",
        llvm.CallConv.Win64: "win64cc",
        llvm.CallConv.X86VectorCall: "x86_vectorcallcc",
        llvm.CallConv.X86RegCall: "x86_regcallcc",
    }
    return mapping.get(cc, "unknown")


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_call_conv") as mod:
            i32 = ctx.types.i32
            void_ty = ctx.types.void

            # ==========================================
            # Test 1: CallConv enum values
            # ==========================================
            convs = [
                (llvm.CallConv.C, "C"),
                (llvm.CallConv.Fast, "Fast"),
                (llvm.CallConv.Cold, "Cold"),
                (llvm.CallConv.X86Stdcall, "X86Stdcall"),
                (llvm.CallConv.X86Fastcall, "X86Fastcall"),
                (llvm.CallConv.GHC, "GHC"),
                (llvm.CallConv.HiPE, "HiPE"),
                (llvm.CallConv.PreserveMost, "PreserveMost"),
                (llvm.CallConv.PreserveAll, "PreserveAll"),
                (llvm.CallConv.Swift, "Swift"),
                (llvm.CallConv.CXX_FAST_TLS, "CXX_FAST_TLS"),
                (llvm.CallConv.X86ThisCall, "X86ThisCall"),
                (llvm.CallConv.X86_64_SysV, "X86_64_SysV"),
                (llvm.CallConv.Win64, "Win64"),
                (llvm.CallConv.X86VectorCall, "X86VectorCall"),
                (llvm.CallConv.X86RegCall, "X86RegCall"),
            ]

            # ==========================================
            # Test 2: Declare a callee and callers with different CCs
            # ==========================================

            # Callee: i32 callee(i32)
            callee_ty = ctx.types.function(i32, [i32])
            callee = mod.add_function("callee", callee_ty)

            # Caller: i32 caller_default(i32) - default CC call
            caller_default = mod.add_function("caller_default", callee_ty)
            cd_param = caller_default.get_param(0)
            cd_param.name = "x"
            cd_entry = caller_default.append_basic_block("entry")

            with cd_entry.create_builder() as builder:
                cd_call = builder.call(callee_ty, callee, [cd_param], "result")
                builder.ret(cd_call)

                # Read default call conv
                default_cc = cd_call.instruction_call_conv

                # Caller: i32 caller_fastcc(i32) - fastcc call
                caller_fastcc = mod.add_function("caller_fastcc", callee_ty)
                cf_param = caller_fastcc.get_param(0)
                cf_param.name = "x"
                cf_entry = caller_fastcc.append_basic_block("entry")
                builder.position_at_end(cf_entry)
                cf_call = builder.call(callee_ty, callee, [cf_param], "result")
                cf_call.instruction_call_conv = llvm.CallConv.Fast
                builder.ret(cf_call)
                fast_cc = cf_call.instruction_call_conv

                # Caller: i32 caller_coldcc(i32) - coldcc call
                caller_coldcc = mod.add_function("caller_coldcc", callee_ty)
                cc_param = caller_coldcc.get_param(0)
                cc_param.name = "x"
                cc_entry = caller_coldcc.append_basic_block("entry")
                builder.position_at_end(cc_entry)
                cc_call = builder.call(callee_ty, callee, [cc_param], "result")
                cc_call.instruction_call_conv = llvm.CallConv.Cold
                builder.ret(cc_call)
                cold_cc = cc_call.instruction_call_conv

                # Caller: i32 caller_ghccc(i32) - ghccc call
                caller_ghccc = mod.add_function("caller_ghccc", callee_ty)
                cg_param = caller_ghccc.get_param(0)
                cg_param.name = "x"
                cg_entry = caller_ghccc.append_basic_block("entry")
                builder.position_at_end(cg_entry)
                cg_call = builder.call(callee_ty, callee, [cg_param], "result")
                cg_call.instruction_call_conv = llvm.CallConv.GHC
                builder.ret(cg_call)
                ghc_cc = cg_call.instruction_call_conv

                # Caller: i32 caller_swiftcc(i32) - swiftcc call
                caller_swiftcc = mod.add_function("caller_swiftcc", callee_ty)
                cs_param = caller_swiftcc.get_param(0)
                cs_param.name = "x"
                cs_entry = caller_swiftcc.append_basic_block("entry")
                builder.position_at_end(cs_entry)
                cs_call = builder.call(callee_ty, callee, [cs_param], "result")
                cs_call.instruction_call_conv = llvm.CallConv.Swift
                builder.ret(cs_call)
                swift_cc = cs_call.instruction_call_conv

            # Verify module
            if not mod.verify():
                print(
                    f"; Verification failed: {mod.get_verification_error()}",
                    file=sys.stderr,
                )
                return 1

            # Print diagnostic comments
            print("; Test: test_call_conv")
            print(";")

            print("; CallConv enum values:")
            for cc_val, name in convs:
                print(f";   {name} = {cc_val.value} ({call_conv_name(cc_val)})")

            print(";")
            print("; Instruction call conv tests:")
            print(
                f";   default call conv: {default_cc.value} ({call_conv_name(default_cc)})"
            )
            print(
                f";   after set fastcc: {fast_cc.value} ({call_conv_name(fast_cc)})"
            )
            print(
                f";   after set coldcc: {cold_cc.value} ({call_conv_name(cold_cc)})"
            )
            print(
                f";   after set ghccc: {ghc_cc.value} ({call_conv_name(ghc_cc)})"
            )
            print(
                f";   after set swiftcc: {swift_cc.value} ({call_conv_name(swift_cc)})"
            )

            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    sys.exit(main())
