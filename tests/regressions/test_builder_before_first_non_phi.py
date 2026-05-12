"""
Regression tests for BasicBlock.create_builder(first_non_phi=True).

This helper should avoid manual Optional checks for first_non_phi by
positioning before the first non-PHI instruction, or at block end when
no non-PHI instruction exists.
"""

import llvm


def test_insert_before_first_non_phi_in_phi_block():
    with llvm.create_context() as ctx:
        ir = """
declare void @hook()

define void @f() {
entry:
  br label %merge

merge:
  %x = phi i32 [ 0, %entry ]
  ret void
}
"""
        with ctx.parse_ir(ir) as mod:
            fn = mod.get_function("f")
            assert fn is not None
            merge = None
            for bb in fn.basic_blocks:
                if bb.name == "merge":
                    merge = bb
                    break
            assert merge is not None

            hook = mod.get_function("hook")
            assert hook is not None

            with merge.create_builder(first_non_phi=True) as b:
                b.call(hook, [])

            opcodes = [inst.opcode for inst in merge.instructions]
            assert opcodes[0] == llvm.Opcode.PHI
            assert opcodes[1] == llvm.Opcode.Call
            assert opcodes[2] == llvm.Opcode.Ret

            assert mod.verify(), mod.verification_error


def test_insert_at_end_when_no_non_phi_exists():
    with llvm.create_context() as ctx:
        with ctx.create_module("m") as mod:
            fn_ty = ctx.types.function(ctx.types.void, [])
            fn = mod.add_function("f", fn_ty)
            bb = fn.append_basic_block("entry")

            assert bb.first_non_phi is None

            with bb.create_builder(first_non_phi=True) as b:
                b.ret_void()

            assert bb.first_non_phi is not None
            assert bb.first_instruction is not None
            assert bb.first_instruction.opcode == llvm.Opcode.Ret

            assert mod.verify(), mod.verification_error


if __name__ == "__main__":
    test_insert_before_first_non_phi_in_phi_block()
    print("test_insert_before_first_non_phi_in_phi_block: PASSED")
    test_insert_at_end_when_no_non_phi_exists()
    print("test_insert_at_end_when_no_non_phi_exists: PASSED")
