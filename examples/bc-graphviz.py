import argparse
import llvm


def main():
    parser = argparse.ArgumentParser(description="LLVM Bitcode GraphViz")
    parser.add_argument("ir_file", type=str, help="Path to the LLVM bitcode file")
    args = parser.parse_args()
    ctx = llvm.global_context()
    with open(args.ir_file, "r", encoding="utf-8") as f:
        ir_text = f.read()
    with ctx.parse_ir(ir_text) as mod:
        for func in mod.functions:
            if func.is_declaration:
                continue
            print(func.name)
            graph = []
            for block in func.basic_blocks:
                graph.append(f'  "{block.name}";')
                terminator = block.terminator
                print(f"  Basic Block: {block.name}, Terminator: {terminator}")
                assert terminator is not None # TODO: should this be nullable
                match terminator.get_instruction_opcode():
                    # TODO: Copilot autocomplets this as `llvm.Instruction.Branch`
                    case llvm.Opcode.Br:
                        print("branch")
                        if terminator.is_conditional():
                            succ_true = terminator.get_successor(0)
                            dest_true = terminator.get_operand(1).value_as_basic_block()
                            #assert succ_true == dest_true # TODO: this should work
                            succ_false = terminator.get_successor(1)
                            dest_false = terminator.get_operand(2).value_as_basic_block()
                            #assert succ_false == dest_false
                            graph.append(f"  '{block.name}' -> '{dest_true.name}' [label='true'];")
                            graph.append(f"  '{block.name}' -> '{dest_false.name}' [label='false'];")
                        else:
                            succ = terminator.get_successor(0)
                            dest = terminator.get_operand(0).value_as_basic_block()
                            assert succ == dest
                            graph.append(f'  "{block.name}" -> "{dest.name}";')
                    case llvm.Opcode.Ret:
                        print("ret")
                    case llvm.Opcode.Switch:
                        print("switch")
                        # TODO: terminator.successors iterator
                        for i in range(terminator.get_num_successors()): # TODO: should be property
                            succ = terminator.get_successor(i)
                            graph.append(f'  "{block.name}" -> "{succ.name}";')
                    case unsupported:
                        raise NotImplementedError(f"Unsupported terminator opcode: {terminator.get_instruction_opcode()}")
            print("digraph G {")
            for line in graph:
                print(line)
            print("}")


if __name__ == "__main__":
    main()
