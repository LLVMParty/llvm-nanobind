#!/usr/bin/env -S uv run
"""
Test: test_builder_control_flow
Tests LLVM Builder control flow instructions

Python equivalent of tests/test_builder_control_flow.cpp
Must produce identical output to the C++ version.
"""

import llvm


def main():
    with llvm.create_context() as ctx:
        with ctx.create_module("test_builder_control_flow") as mod:
            i32 = ctx.int32_type()
            i1 = ctx.int1_type()
            void_ty = ctx.void_type()

            with ctx.create_builder() as builder:
                # ==========================================
                # Function 1: void return
                # ==========================================
                void_func_ty = ctx.function_type(void_ty, [])
                void_func = mod.add_function("return_void", void_func_ty)
                void_entry = void_func.append_basic_block("entry", ctx)
                builder.position_at_end(void_entry)
                builder.ret_void()

                # ==========================================
                # Function 2: value return
                # ==========================================
                ret_func_ty = ctx.function_type(i32, [i32])
                ret_func = mod.add_function("return_value", ret_func_ty)
                ret_param = ret_func.get_param(0)
                ret_param.name = "x"

                ret_entry = ret_func.append_basic_block("entry", ctx)
                builder.position_at_end(ret_entry)
                builder.ret(ret_param)

                # ==========================================
                # Function 3: unconditional branch
                # ==========================================
                br_func = mod.add_function("unconditional_branch", void_func_ty)
                br_entry = br_func.append_basic_block("entry", ctx)
                br_target = br_func.append_basic_block("target", ctx)

                builder.position_at_end(br_entry)
                br_inst = builder.br(br_target)

                builder.position_at_end(br_target)
                builder.ret_void()

                # ==========================================
                # Function 4: conditional branch
                # ==========================================
                cond_func_ty = ctx.function_type(i32, [i1])
                cond_func = mod.add_function("conditional_branch", cond_func_ty)
                cond_param = cond_func.get_param(0)
                cond_param.name = "cond"

                cond_entry = cond_func.append_basic_block("entry", ctx)
                cond_true = cond_func.append_basic_block("if_true", ctx)
                cond_false = cond_func.append_basic_block("if_false", ctx)

                builder.position_at_end(cond_entry)
                cond_br = builder.cond_br(cond_param, cond_true, cond_false)

                builder.position_at_end(cond_true)
                builder.ret(llvm.const_int(i32, 1))

                builder.position_at_end(cond_false)
                builder.ret(llvm.const_int(i32, 0))

                # ==========================================
                # Function 5: switch statement
                # ==========================================
                switch_func_ty = ctx.function_type(i32, [i32])
                switch_func = mod.add_function("switch_example", switch_func_ty)
                switch_param = switch_func.get_param(0)
                switch_param.name = "val"

                switch_entry = switch_func.append_basic_block("entry", ctx)
                case_0 = switch_func.append_basic_block("case_0", ctx)
                case_1 = switch_func.append_basic_block("case_1", ctx)
                case_2 = switch_func.append_basic_block("case_2", ctx)
                default_case = switch_func.append_basic_block("default", ctx)

                builder.position_at_end(switch_entry)
                switch_inst = builder.switch_(switch_param, default_case, 3)
                switch_inst.add_case(llvm.const_int(i32, 0), case_0)
                switch_inst.add_case(llvm.const_int(i32, 1), case_1)
                switch_inst.add_case(llvm.const_int(i32, 2), case_2)

                builder.position_at_end(case_0)
                builder.ret(llvm.const_int(i32, 100))

                builder.position_at_end(case_1)
                builder.ret(llvm.const_int(i32, 200))

                builder.position_at_end(case_2)
                builder.ret(llvm.const_int(i32, 300))

                builder.position_at_end(default_case)
                builder.ret(llvm.const_int(i32, -1, sign_extend=True))

                # ==========================================
                # Function 6: function call
                # ==========================================
                call_func = mod.add_function("call_example", ret_func_ty)
                call_param = call_func.get_param(0)
                call_param.name = "n"

                call_entry = call_func.append_basic_block("entry", ctx)
                builder.position_at_end(call_entry)

                args = [call_param]
                call_result = builder.call(ret_func_ty, ret_func, args, "result")
                builder.ret(call_result)

                # ==========================================
                # Function 7: unreachable
                # ==========================================
                unreach_func = mod.add_function("unreachable_example", void_func_ty)
                unreach_entry = unreach_func.append_basic_block("entry", ctx)
                builder.position_at_end(unreach_entry)
                builder.unreachable()

                # Check insert block
                current_block = builder.insert_block
                assert current_block is not None

            # Verify module
            if not mod.verify():
                print(f"; Verification failed: {mod.get_verification_error()}")
                return 1

            # Print diagnostic comments
            print("; Test: test_builder_control_flow")
            print(";")
            print("; Control flow operations demonstrated:")
            print(";   ret void, ret value")
            print(";   br (unconditional)")
            print(";   br (conditional)")
            print(";   switch with 3 cases + default")
            print(";   call")
            print(";   unreachable")
            print(";")
            print("; Branch analysis:")
            print(
                f";   unconditional br is conditional: {'yes' if br_inst.is_conditional() else 'no'}"
            )
            print(
                f";   conditional br is conditional: {'yes' if cond_br.is_conditional() else 'no'}"
            )
            print(
                f";   unconditional br num successors: {br_inst.get_num_successors()}"
            )
            print(f";   conditional br num successors: {cond_br.get_num_successors()}")
            print(";")
            print(f"; Current insert block: {current_block.name}")
            print()

            # Print module IR
            print(mod.to_string(), end="")

    return 0


if __name__ == "__main__":
    exit(main())
