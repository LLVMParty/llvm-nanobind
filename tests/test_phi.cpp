/**
 * Test: test_phi
 * Tests LLVM PHI node operations
 *
 * LLVM-C APIs covered:
 * - LLVMBuildPhi()
 * - LLVMAddIncoming()
 * - LLVMCountIncoming(), LLVMGetIncomingValue(), LLVMGetIncomingBlock()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_phi", ctx);

    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    // ==========================================
    // Function: simple diamond pattern with PHI
    // i32 diamond(i1 cond, i32 a, i32 b)
    // ==========================================
    LLVMTypeRef diamond_params[] = {i1, i32, i32};
    LLVMTypeRef diamond_ty = LLVMFunctionType(i32, diamond_params, 3, 0);
    LLVMValueRef diamond_func = LLVMAddFunction(mod, "diamond", diamond_ty);

    LLVMValueRef cond = LLVMGetParam(diamond_func, 0);
    LLVMValueRef a = LLVMGetParam(diamond_func, 1);
    LLVMValueRef b = LLVMGetParam(diamond_func, 2);
    LLVMSetValueName2(cond, "cond", 4);
    LLVMSetValueName2(a, "a", 1);
    LLVMSetValueName2(b, "b", 1);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, diamond_func, "entry");
    LLVMBasicBlockRef if_true = LLVMAppendBasicBlockInContext(ctx, diamond_func, "if_true");
    LLVMBasicBlockRef if_false = LLVMAppendBasicBlockInContext(ctx, diamond_func, "if_false");
    LLVMBasicBlockRef merge = LLVMAppendBasicBlockInContext(ctx, diamond_func, "merge");

    // Entry: conditional branch
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMBuildCondBr(builder, cond, if_true, if_false);

    // True branch: compute a * 2
    LLVMPositionBuilderAtEnd(builder, if_true);
    LLVMValueRef a_doubled = LLVMBuildMul(builder, a, LLVMConstInt(i32, 2, 0), "a_doubled");
    LLVMBuildBr(builder, merge);

    // False branch: compute b + 1
    LLVMPositionBuilderAtEnd(builder, if_false);
    LLVMValueRef b_inc = LLVMBuildAdd(builder, b, LLVMConstInt(i32, 1, 0), "b_inc");
    LLVMBuildBr(builder, merge);

    // Merge: PHI node to select result
    LLVMPositionBuilderAtEnd(builder, merge);
    LLVMValueRef phi = LLVMBuildPhi(builder, i32, "result");

    // Add incoming values
    LLVMValueRef incoming_vals[] = {a_doubled, b_inc};
    LLVMBasicBlockRef incoming_blocks[] = {if_true, if_false};
    LLVMAddIncoming(phi, incoming_vals, incoming_blocks, 2);

    LLVMBuildRet(builder, phi);

    // ==========================================
    // Function: loop with PHI (sum 1 to n)
    // i32 sum_to_n(i32 n)
    // ==========================================
    LLVMTypeRef sum_params[] = {i32};
    LLVMTypeRef sum_ty = LLVMFunctionType(i32, sum_params, 1, 0);
    LLVMValueRef sum_func = LLVMAddFunction(mod, "sum_to_n", sum_ty);

    LLVMValueRef n = LLVMGetParam(sum_func, 0);
    LLVMSetValueName2(n, "n", 1);

    LLVMBasicBlockRef sum_entry = LLVMAppendBasicBlockInContext(ctx, sum_func, "entry");
    LLVMBasicBlockRef loop = LLVMAppendBasicBlockInContext(ctx, sum_func, "loop");
    LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(ctx, sum_func, "exit");

    // Entry: branch to loop
    LLVMPositionBuilderAtEnd(builder, sum_entry);
    LLVMBuildBr(builder, loop);

    // Loop header with PHIs
    LLVMPositionBuilderAtEnd(builder, loop);
    LLVMValueRef i_phi = LLVMBuildPhi(builder, i32, "i");
    LLVMValueRef sum_phi = LLVMBuildPhi(builder, i32, "sum");

    // Loop body: sum += i; i++
    LLVMValueRef new_sum = LLVMBuildAdd(builder, sum_phi, i_phi, "new_sum");
    LLVMValueRef new_i = LLVMBuildAdd(builder, i_phi, LLVMConstInt(i32, 1, 0), "new_i");

    // Loop condition: i <= n
    LLVMValueRef loop_cond = LLVMBuildICmp(builder, LLVMIntSLE, new_i, n, "loop_cond");
    LLVMBuildCondBr(builder, loop_cond, loop, exit_bb);

    // Add incoming values to PHIs
    // From entry: i=1, sum=0
    LLVMValueRef i_incoming_vals[] = {LLVMConstInt(i32, 1, 0), new_i};
    LLVMBasicBlockRef i_incoming_blocks[] = {sum_entry, loop};
    LLVMAddIncoming(i_phi, i_incoming_vals, i_incoming_blocks, 2);

    LLVMValueRef sum_incoming_vals[] = {LLVMConstInt(i32, 0, 0), new_sum};
    LLVMBasicBlockRef sum_incoming_blocks[] = {sum_entry, loop};
    LLVMAddIncoming(sum_phi, sum_incoming_vals, sum_incoming_blocks, 2);

    // Exit: return sum
    LLVMPositionBuilderAtEnd(builder, exit_bb);
    LLVMBuildRet(builder, new_sum);

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

    // Print diagnostic comments
    printf("; Test: test_phi\n");
    printf(";\n");
    printf("; Diamond pattern PHI:\n");
    printf(";   phi incoming count: %u\n", LLVMCountIncoming(phi));

    // Get incoming values and blocks
    for (unsigned i = 0; i < LLVMCountIncoming(phi); i++) {
        LLVMValueRef val = LLVMGetIncomingValue(phi, i);
        LLVMBasicBlockRef blk = LLVMGetIncomingBlock(phi, i);
        size_t len;
        const char *val_name = LLVMGetValueName2(val, &len);
        const char *blk_name = LLVMGetBasicBlockName(blk);
        printf(";   incoming[%u]: value=%s, block=%s\n", i, val_name, blk_name);
    }

    printf(";\n");
    printf("; Loop PHIs:\n");
    printf(";   i_phi incoming count: %u\n", LLVMCountIncoming(i_phi));
    printf(";   sum_phi incoming count: %u\n", LLVMCountIncoming(sum_phi));

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
