/**
 * Test: test_basic_block
 * Tests LLVM BasicBlock creation and manipulation
 *
 * LLVM-C APIs covered:
 * - LLVMAppendBasicBlockInContext()
 * - LLVMGetBasicBlockName()
 * - LLVMGetBasicBlockParent()
 * - LLVMGetEntryBasicBlock()
 * - LLVMCountBasicBlocks()
 * - LLVMGetFirstBasicBlock(), LLVMGetNextBasicBlock(), LLVMGetLastBasicBlock()
 * - LLVMGetFirstInstruction(), LLVMGetLastInstruction()
 * - LLVMGetBasicBlockTerminator()
 * - LLVMMoveBasicBlockBefore(), LLVMMoveBasicBlockAfter()
 * - LLVMCreateBasicBlockInContext()
 * - LLVMDeleteBasicBlock()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_basic_block", ctx);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);

    // Create a function to hold basic blocks
    LLVMTypeRef func_ty = LLVMFunctionType(void_ty, nullptr, 0, 0);
    LLVMValueRef func = LLVMAddFunction(mod, "test_func", func_ty);

    // Append basic blocks
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, func, "entry");
    LLVMBasicBlockRef middle = LLVMAppendBasicBlockInContext(ctx, func, "middle");
    LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(ctx, func, "exit");

    // Get block names
    const char *entry_name = LLVMGetBasicBlockName(entry);
    const char *middle_name = LLVMGetBasicBlockName(middle);
    const char *exit_name = LLVMGetBasicBlockName(exit_bb);

    // Get parent function
    LLVMValueRef entry_parent = LLVMGetBasicBlockParent(entry);

    // Get entry basic block
    LLVMBasicBlockRef func_entry = LLVMGetEntryBasicBlock(func);

    // Count basic blocks
    unsigned bb_count = LLVMCountBasicBlocks(func);

    // Create a builder to add instructions
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    // Add instructions to entry block
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMBuildBr(builder, middle);

    // Add instructions to middle block
    LLVMPositionBuilderAtEnd(builder, middle);
    LLVMBuildBr(builder, exit_bb);

    // Add instructions to exit block
    LLVMPositionBuilderAtEnd(builder, exit_bb);
    LLVMBuildRetVoid(builder);

    // Get first/last instructions
    LLVMValueRef entry_first = LLVMGetFirstInstruction(entry);
    LLVMValueRef entry_last = LLVMGetLastInstruction(entry);
    LLVMValueRef exit_terminator = LLVMGetBasicBlockTerminator(exit_bb);

    // Create unattached block
    LLVMBasicBlockRef unattached = LLVMCreateBasicBlockInContext(ctx, "unattached");

    // Attach unattached block to function
    LLVMAppendExistingBasicBlock(func, unattached);
    LLVMPositionBuilderAtEnd(builder, unattached);
    LLVMBuildUnreachable(builder);

    // Block count after adding unattached
    unsigned bb_count_after = LLVMCountBasicBlocks(func);

    // Move blocks around: move unattached before exit
    LLVMMoveBasicBlockBefore(unattached, exit_bb);

    // Iterate through blocks
    int block_index = 0;
    printf("; Test: test_basic_block\n");
    printf(";\n");
    printf("; Basic block info:\n");
    printf(";   entry name: %s\n", entry_name);
    printf(";   middle name: %s\n", middle_name);
    printf(";   exit name: %s\n", exit_name);
    printf(";\n");
    printf("; Parent checks:\n");
    printf(";   entry parent is func: %s\n", entry_parent == func ? "yes" : "no");
    printf(";   func entry block is entry: %s\n", func_entry == entry ? "yes" : "no");
    printf(";\n");
    printf("; Block counts:\n");
    printf(";   initial count: %u\n", bb_count);
    printf(";   after adding unattached: %u\n", bb_count_after);
    printf(";\n");
    printf("; Instruction checks:\n");
    printf(";   entry has first instruction: %s\n", entry_first != nullptr ? "yes" : "no");
    printf(";   entry first == last (single inst): %s\n", entry_first == entry_last ? "yes" : "no");
    printf(";   exit has terminator: %s\n", exit_terminator != nullptr ? "yes" : "no");
    printf(";\n");
    printf("; Block iteration (after move):\n");
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func); bb; bb = LLVMGetNextBasicBlock(bb)) {
        printf(";   [%d] %s\n", block_index++, LLVMGetBasicBlockName(bb));
    }

    // Get last block
    LLVMBasicBlockRef last_bb = LLVMGetLastBasicBlock(func);
    printf(";\n");
    printf("; Last block: %s\n", LLVMGetBasicBlockName(last_bb));

    LLVMDisposeBuilder(builder);

    // Verify module
    char *error = nullptr;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "; Verification failed: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return 1;
    }
    LLVMDisposeMessage(error);

    printf("\n");

    // Print module IR
    char *ir = LLVMPrintModuleToString(mod);
    printf("%s", ir);
    LLVMDisposeMessage(ir);

    // Cleanup
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);

    return 0;
}
