/**
 * Test: test_factorial
 * Integration test: Iterative factorial function
 *
 * This test creates a complete, realistic LLVM function that computes
 * factorial iteratively. It demonstrates comprehensive use of:
 * - Types, functions, basic blocks
 * - Builder operations (alloca, load, store, arithmetic, comparisons)
 * - Control flow (conditional branch, loop)
 * - PHI nodes alternative: using alloca/load/store pattern
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_factorial", ctx);

    // Set target for more realistic output
    LLVMSetTarget(mod, "x86_64-unknown-linux-gnu");

    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    // ==========================================
    // Function: i64 factorial(i64 n)
    // Iterative implementation using alloca/load/store
    // ==========================================
    LLVMTypeRef fact_params[] = {i64};
    LLVMTypeRef fact_ty = LLVMFunctionType(i64, fact_params, 1, 0);
    LLVMValueRef fact_func = LLVMAddFunction(mod, "factorial", fact_ty);

    LLVMValueRef n = LLVMGetParam(fact_func, 0);
    LLVMSetValueName2(n, "n", 1);

    // Basic blocks
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, fact_func, "entry");
    LLVMBasicBlockRef loop_cond = LLVMAppendBasicBlockInContext(ctx, fact_func, "loop_cond");
    LLVMBasicBlockRef loop_body = LLVMAppendBasicBlockInContext(ctx, fact_func, "loop_body");
    LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(ctx, fact_func, "exit");

    // Entry block: initialize result=1, i=1
    LLVMPositionBuilderAtEnd(builder, entry);
    LLVMValueRef result_ptr = LLVMBuildAlloca(builder, i64, "result");
    LLVMValueRef i_ptr = LLVMBuildAlloca(builder, i64, "i");

    LLVMBuildStore(builder, LLVMConstInt(i64, 1, 0), result_ptr);
    LLVMBuildStore(builder, LLVMConstInt(i64, 1, 0), i_ptr);
    LLVMBuildBr(builder, loop_cond);

    // Loop condition: while (i <= n)
    LLVMPositionBuilderAtEnd(builder, loop_cond);
    LLVMValueRef i_val = LLVMBuildLoad2(builder, i64, i_ptr, "i_val");
    LLVMValueRef cmp = LLVMBuildICmp(builder, LLVMIntSLE, i_val, n, "cmp");
    LLVMBuildCondBr(builder, cmp, loop_body, exit_bb);

    // Loop body: result *= i; i++
    LLVMPositionBuilderAtEnd(builder, loop_body);
    LLVMValueRef result_val = LLVMBuildLoad2(builder, i64, result_ptr, "result_val");
    LLVMValueRef i_val2 = LLVMBuildLoad2(builder, i64, i_ptr, "i_val2");

    LLVMValueRef new_result = LLVMBuildMul(builder, result_val, i_val2, "new_result");
    LLVMBuildStore(builder, new_result, result_ptr);

    LLVMValueRef new_i = LLVMBuildAdd(builder, i_val2, LLVMConstInt(i64, 1, 0), "new_i");
    LLVMBuildStore(builder, new_i, i_ptr);

    LLVMBuildBr(builder, loop_cond);

    // Exit: return result
    LLVMPositionBuilderAtEnd(builder, exit_bb);
    LLVMValueRef final_result = LLVMBuildLoad2(builder, i64, result_ptr, "final_result");
    LLVMBuildRet(builder, final_result);

    // ==========================================
    // Function: i64 factorial_recursive(i64 n)
    // Recursive implementation for comparison
    // ==========================================
    LLVMValueRef fact_rec_func = LLVMAddFunction(mod, "factorial_recursive", fact_ty);
    LLVMValueRef n_rec = LLVMGetParam(fact_rec_func, 0);
    LLVMSetValueName2(n_rec, "n", 1);

    LLVMBasicBlockRef rec_entry = LLVMAppendBasicBlockInContext(ctx, fact_rec_func, "entry");
    LLVMBasicBlockRef base_case = LLVMAppendBasicBlockInContext(ctx, fact_rec_func, "base_case");
    LLVMBasicBlockRef recursive = LLVMAppendBasicBlockInContext(ctx, fact_rec_func, "recursive");

    // Entry: if n <= 1 goto base_case else goto recursive
    LLVMPositionBuilderAtEnd(builder, rec_entry);
    LLVMValueRef is_base = LLVMBuildICmp(builder, LLVMIntSLE, n_rec, 
                                          LLVMConstInt(i64, 1, 0), "is_base");
    LLVMBuildCondBr(builder, is_base, base_case, recursive);

    // Base case: return 1
    LLVMPositionBuilderAtEnd(builder, base_case);
    LLVMBuildRet(builder, LLVMConstInt(i64, 1, 0));

    // Recursive: return n * factorial_recursive(n-1)
    LLVMPositionBuilderAtEnd(builder, recursive);
    LLVMValueRef n_minus_1 = LLVMBuildSub(builder, n_rec, LLVMConstInt(i64, 1, 0), "n_minus_1");
    LLVMValueRef rec_args[] = {n_minus_1};
    LLVMValueRef rec_result = LLVMBuildCall2(builder, fact_ty, fact_rec_func, rec_args, 1, "rec_result");
    LLVMValueRef final_rec = LLVMBuildMul(builder, n_rec, rec_result, "final_rec");
    LLVMBuildRet(builder, final_rec);

    // ==========================================
    // Function: i64 main()
    // Calls factorial(5) and returns the result
    // ==========================================
    LLVMTypeRef main_ty = LLVMFunctionType(i64, nullptr, 0, 0);
    LLVMValueRef main_func = LLVMAddFunction(mod, "main", main_ty);

    LLVMBasicBlockRef main_entry = LLVMAppendBasicBlockInContext(ctx, main_func, "entry");
    LLVMPositionBuilderAtEnd(builder, main_entry);

    LLVMValueRef main_args[] = {LLVMConstInt(i64, 5, 0)};
    LLVMValueRef fact_result = LLVMBuildCall2(builder, fact_ty, fact_func, main_args, 1, "fact_result");
    LLVMBuildRet(builder, fact_result);

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
    printf("; Test: test_factorial\n");
    printf("; Integration test: Iterative and recursive factorial\n");
    printf(";\n");
    printf("; factorial(i64 n) -> i64:\n");
    printf(";   Iterative implementation using alloca/load/store\n");
    printf(";   Blocks: entry -> loop_cond -> loop_body -> exit\n");
    printf(";\n");
    printf("; factorial_recursive(i64 n) -> i64:\n");
    printf(";   Recursive implementation\n");
    printf(";   Blocks: entry -> base_case / recursive\n");
    printf(";\n");
    printf("; main() -> i64:\n");
    printf(";   Calls factorial(5), expected result: 120\n");
    printf(";\n");
    printf("; Function info:\n");

    size_t len;
    for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn; fn = LLVMGetNextFunction(fn)) {
        const char *name = LLVMGetValueName2(fn, &len);
        unsigned bb_count = LLVMCountBasicBlocks(fn);
        unsigned param_count = LLVMCountParams(fn);
        printf(";   %s: %u params, %u blocks\n", name, param_count, bb_count);
    }

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
