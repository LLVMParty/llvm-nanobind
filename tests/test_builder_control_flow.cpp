/**
 * Test: test_builder_control_flow
 * Tests LLVM Builder control flow instructions
 *
 * LLVM-C APIs covered:
 * - LLVMBuildRetVoid(), LLVMBuildRet()
 * - LLVMBuildBr(), LLVMBuildCondBr()
 * - LLVMBuildSwitch(), LLVMAddCase()
 * - LLVMBuildCall2()
 * - LLVMBuildUnreachable()
 * - LLVMGetInsertBlock()
 * - LLVMIsConditional(), LLVMGetCondition()
 * - LLVMGetNumSuccessors(), LLVMGetSuccessor()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_builder_control_flow", ctx);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    // ==========================================
    // Function 1: void return
    // ==========================================
    LLVMTypeRef void_func_ty = LLVMFunctionType(void_ty, nullptr, 0, 0);
    LLVMValueRef void_func = LLVMAddFunction(mod, "return_void", void_func_ty);
    LLVMBasicBlockRef void_entry = LLVMAppendBasicBlockInContext(ctx, void_func, "entry");
    LLVMPositionBuilderAtEnd(builder, void_entry);
    LLVMBuildRetVoid(builder);

    // ==========================================
    // Function 2: value return
    // ==========================================
    LLVMTypeRef ret_params[] = {i32};
    LLVMTypeRef ret_func_ty = LLVMFunctionType(i32, ret_params, 1, 0);
    LLVMValueRef ret_func = LLVMAddFunction(mod, "return_value", ret_func_ty);
    LLVMValueRef ret_param = LLVMGetParam(ret_func, 0);
    LLVMSetValueName2(ret_param, "x", 1);

    LLVMBasicBlockRef ret_entry = LLVMAppendBasicBlockInContext(ctx, ret_func, "entry");
    LLVMPositionBuilderAtEnd(builder, ret_entry);
    LLVMBuildRet(builder, ret_param);

    // ==========================================
    // Function 3: unconditional branch
    // ==========================================
    LLVMValueRef br_func = LLVMAddFunction(mod, "unconditional_branch", void_func_ty);
    LLVMBasicBlockRef br_entry = LLVMAppendBasicBlockInContext(ctx, br_func, "entry");
    LLVMBasicBlockRef br_target = LLVMAppendBasicBlockInContext(ctx, br_func, "target");

    LLVMPositionBuilderAtEnd(builder, br_entry);
    LLVMValueRef br_inst = LLVMBuildBr(builder, br_target);

    LLVMPositionBuilderAtEnd(builder, br_target);
    LLVMBuildRetVoid(builder);

    // ==========================================
    // Function 4: conditional branch
    // ==========================================
    LLVMTypeRef cond_params[] = {i1};
    LLVMTypeRef cond_func_ty = LLVMFunctionType(i32, cond_params, 1, 0);
    LLVMValueRef cond_func = LLVMAddFunction(mod, "conditional_branch", cond_func_ty);
    LLVMValueRef cond_param = LLVMGetParam(cond_func, 0);
    LLVMSetValueName2(cond_param, "cond", 4);

    LLVMBasicBlockRef cond_entry = LLVMAppendBasicBlockInContext(ctx, cond_func, "entry");
    LLVMBasicBlockRef cond_true = LLVMAppendBasicBlockInContext(ctx, cond_func, "if_true");
    LLVMBasicBlockRef cond_false = LLVMAppendBasicBlockInContext(ctx, cond_func, "if_false");

    LLVMPositionBuilderAtEnd(builder, cond_entry);
    LLVMValueRef cond_br = LLVMBuildCondBr(builder, cond_param, cond_true, cond_false);

    LLVMPositionBuilderAtEnd(builder, cond_true);
    LLVMBuildRet(builder, LLVMConstInt(i32, 1, 0));

    LLVMPositionBuilderAtEnd(builder, cond_false);
    LLVMBuildRet(builder, LLVMConstInt(i32, 0, 0));

    // ==========================================
    // Function 5: switch statement
    // ==========================================
    LLVMTypeRef switch_params[] = {i32};
    LLVMTypeRef switch_func_ty = LLVMFunctionType(i32, switch_params, 1, 0);
    LLVMValueRef switch_func = LLVMAddFunction(mod, "switch_example", switch_func_ty);
    LLVMValueRef switch_param = LLVMGetParam(switch_func, 0);
    LLVMSetValueName2(switch_param, "val", 3);

    LLVMBasicBlockRef switch_entry = LLVMAppendBasicBlockInContext(ctx, switch_func, "entry");
    LLVMBasicBlockRef case_0 = LLVMAppendBasicBlockInContext(ctx, switch_func, "case_0");
    LLVMBasicBlockRef case_1 = LLVMAppendBasicBlockInContext(ctx, switch_func, "case_1");
    LLVMBasicBlockRef case_2 = LLVMAppendBasicBlockInContext(ctx, switch_func, "case_2");
    LLVMBasicBlockRef default_case = LLVMAppendBasicBlockInContext(ctx, switch_func, "default");

    LLVMPositionBuilderAtEnd(builder, switch_entry);
    LLVMValueRef switch_inst = LLVMBuildSwitch(builder, switch_param, default_case, 3);
    LLVMAddCase(switch_inst, LLVMConstInt(i32, 0, 0), case_0);
    LLVMAddCase(switch_inst, LLVMConstInt(i32, 1, 0), case_1);
    LLVMAddCase(switch_inst, LLVMConstInt(i32, 2, 0), case_2);

    LLVMPositionBuilderAtEnd(builder, case_0);
    LLVMBuildRet(builder, LLVMConstInt(i32, 100, 0));

    LLVMPositionBuilderAtEnd(builder, case_1);
    LLVMBuildRet(builder, LLVMConstInt(i32, 200, 0));

    LLVMPositionBuilderAtEnd(builder, case_2);
    LLVMBuildRet(builder, LLVMConstInt(i32, 300, 0));

    LLVMPositionBuilderAtEnd(builder, default_case);
    LLVMBuildRet(builder, LLVMConstInt(i32, -1, 1));

    // ==========================================
    // Function 6: function call
    // ==========================================
    LLVMValueRef call_func = LLVMAddFunction(mod, "call_example", ret_func_ty);
    LLVMValueRef call_param = LLVMGetParam(call_func, 0);
    LLVMSetValueName2(call_param, "n", 1);

    LLVMBasicBlockRef call_entry = LLVMAppendBasicBlockInContext(ctx, call_func, "entry");
    LLVMPositionBuilderAtEnd(builder, call_entry);

    LLVMValueRef args[] = {call_param};
    LLVMValueRef call_result = LLVMBuildCall2(builder, ret_func_ty, ret_func, args, 1, "result");
    LLVMBuildRet(builder, call_result);

    // ==========================================
    // Function 7: unreachable
    // ==========================================
    LLVMValueRef unreach_func = LLVMAddFunction(mod, "unreachable_example", void_func_ty);
    LLVMBasicBlockRef unreach_entry = LLVMAppendBasicBlockInContext(ctx, unreach_func, "entry");
    LLVMPositionBuilderAtEnd(builder, unreach_entry);
    LLVMBuildUnreachable(builder);

    // Check insert block
    LLVMBasicBlockRef current_block = LLVMGetInsertBlock(builder);

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
    printf("; Test: test_builder_control_flow\n");
    printf(";\n");
    printf("; Control flow operations demonstrated:\n");
    printf(";   ret void, ret value\n");
    printf(";   br (unconditional)\n");
    printf(";   br (conditional)\n");
    printf(";   switch with 3 cases + default\n");
    printf(";   call\n");
    printf(";   unreachable\n");
    printf(";\n");
    printf("; Branch analysis:\n");
    printf(";   unconditional br is conditional: %s\n", LLVMIsConditional(br_inst) ? "yes" : "no");
    printf(";   conditional br is conditional: %s\n", LLVMIsConditional(cond_br) ? "yes" : "no");
    printf(";   unconditional br num successors: %u\n", LLVMGetNumSuccessors(br_inst));
    printf(";   conditional br num successors: %u\n", LLVMGetNumSuccessors(cond_br));
    printf(";\n");
    printf("; Current insert block: %s\n", LLVMGetBasicBlockName(current_block));
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
