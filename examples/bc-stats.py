import argparse
import llvm


def main():
    parser = argparse.ArgumentParser(description="LLVM Bitcode Statistics Tool")
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
            histogram = {}
            for block in func.basic_blocks:
                # TODO: proper iterator for instructions
                inst = block.first_instruction
                while inst is not None:
                    opcode = inst.get_instruction_opcode() # TODO: make property
                    histogram[opcode] = histogram.get(opcode, 0) + 1
                    inst = inst.next_instruction
            print("  Instruction Histogram:")
            for opcode, count in histogram.items():
                print(f"    {opcode}: {count}")


if __name__ == "__main__":
    main()
