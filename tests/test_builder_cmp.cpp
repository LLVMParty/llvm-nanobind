/**
 * Test: test_builder_cmp
 * Tests LLVM Builder comparison operations
 *
 * LLVM-C APIs covered:
 * - LLVMBuildICmp() with all LLVMIntPredicate values
 * - LLVMBuildFCmp() with LLVMRealPredicate values
 * - LLVMBuildSelect()
 * - LLVMGetICmpPredicate(), LLVMGetFCmpPredicate()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

const char* int_pred_name(LLVMIntPredicate pred) {
    switch (pred) {
        case LLVMIntEQ: return "eq";
        case LLVMIntNE: return "ne";
        case LLVMIntUGT: return "ugt";
        case LLVMIntUGE: return "uge";
        case LLVMIntULT: return "ult";
        case LLVMIntULE: return "ule";
        case LLVMIntSGT: return "sgt";
        case LLVMIntSGE: return "sge";
        case LLVMIntSLT: return "slt";
        case LLVMIntSLE: return "sle";
        default: return "unknown";
    }
}

const char* real_pred_name(LLVMRealPredicate pred) {
    switch (pred) {
        case LLVMRealPredicateFalse: return "false";
        case LLVMRealOEQ: return "oeq";
        case LLVMRealOGT: return "ogt";
        case LLVMRealOGE: return "oge";
        case LLVMRealOLT: return "olt";
        case LLVMRealOLE: return "ole";
        case LLVMRealONE: return "one";
        case LLVMRealORD: return "ord";
        case LLVMRealUNO: return "uno";
        case LLVMRealUEQ: return "ueq";
        case LLVMRealUGT: return "ugt";
        case LLVMRealUGE: return "uge";
        case LLVMRealULT: return "ult";
        case LLVMRealULE: return "ule";
        case LLVMRealUNE: return "une";
        case LLVMRealPredicateTrue: return "true";
        default: return "unknown";
    }
}

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_builder_cmp", ctx);

    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    // ==========================================
    // Function 1: Integer comparisons
    // ==========================================
    LLVMTypeRef icmp_params[] = {i32, i32};
    LLVMTypeRef icmp_ty = LLVMFunctionType(void_ty, icmp_params, 2, 0);
    LLVMValueRef icmp_func = LLVMAddFunction(mod, "int_comparisons", icmp_ty);
    LLVMValueRef a = LLVMGetParam(icmp_func, 0);
    LLVMValueRef b = LLVMGetParam(icmp_func, 1);
    LLVMSetValueName2(a, "a", 1);
    LLVMSetValueName2(b, "b", 1);

    LLVMBasicBlockRef icmp_entry = LLVMAppendBasicBlockInContext(ctx, icmp_func, "entry");
    LLVMPositionBuilderAtEnd(builder, icmp_entry);

    // All integer predicates
    LLVMValueRef icmp_eq = LLVMBuildICmp(builder, LLVMIntEQ, a, b, "eq");
    LLVMValueRef icmp_ne = LLVMBuildICmp(builder, LLVMIntNE, a, b, "ne");
    LLVMValueRef icmp_ugt = LLVMBuildICmp(builder, LLVMIntUGT, a, b, "ugt");
    LLVMValueRef icmp_uge = LLVMBuildICmp(builder, LLVMIntUGE, a, b, "uge");
    LLVMValueRef icmp_ult = LLVMBuildICmp(builder, LLVMIntULT, a, b, "ult");
    LLVMValueRef icmp_ule = LLVMBuildICmp(builder, LLVMIntULE, a, b, "ule");
    LLVMValueRef icmp_sgt = LLVMBuildICmp(builder, LLVMIntSGT, a, b, "sgt");
    LLVMValueRef icmp_sge = LLVMBuildICmp(builder, LLVMIntSGE, a, b, "sge");
    LLVMValueRef icmp_slt = LLVMBuildICmp(builder, LLVMIntSLT, a, b, "slt");
    LLVMValueRef icmp_sle = LLVMBuildICmp(builder, LLVMIntSLE, a, b, "sle");

    LLVMBuildRetVoid(builder);

    // ==========================================
    // Function 2: Float comparisons
    // ==========================================
    LLVMTypeRef fcmp_params[] = {f64, f64};
    LLVMTypeRef fcmp_ty = LLVMFunctionType(void_ty, fcmp_params, 2, 0);
    LLVMValueRef fcmp_func = LLVMAddFunction(mod, "float_comparisons", fcmp_ty);
    LLVMValueRef x = LLVMGetParam(fcmp_func, 0);
    LLVMValueRef y = LLVMGetParam(fcmp_func, 1);
    LLVMSetValueName2(x, "x", 1);
    LLVMSetValueName2(y, "y", 1);

    LLVMBasicBlockRef fcmp_entry = LLVMAppendBasicBlockInContext(ctx, fcmp_func, "entry");
    LLVMPositionBuilderAtEnd(builder, fcmp_entry);

    // Ordered comparisons (false if either is NaN)
    LLVMValueRef fcmp_oeq = LLVMBuildFCmp(builder, LLVMRealOEQ, x, y, "oeq");
    LLVMValueRef fcmp_ogt = LLVMBuildFCmp(builder, LLVMRealOGT, x, y, "ogt");
    LLVMValueRef fcmp_oge = LLVMBuildFCmp(builder, LLVMRealOGE, x, y, "oge");
    LLVMValueRef fcmp_olt = LLVMBuildFCmp(builder, LLVMRealOLT, x, y, "olt");
    LLVMValueRef fcmp_ole = LLVMBuildFCmp(builder, LLVMRealOLE, x, y, "ole");
    LLVMValueRef fcmp_one = LLVMBuildFCmp(builder, LLVMRealONE, x, y, "one");
    LLVMValueRef fcmp_ord = LLVMBuildFCmp(builder, LLVMRealORD, x, y, "ord");

    // Unordered comparisons (true if either is NaN)
    LLVMValueRef fcmp_uno = LLVMBuildFCmp(builder, LLVMRealUNO, x, y, "uno");
    LLVMValueRef fcmp_ueq = LLVMBuildFCmp(builder, LLVMRealUEQ, x, y, "ueq");
    LLVMValueRef fcmp_ugt = LLVMBuildFCmp(builder, LLVMRealUGT, x, y, "ugt");
    LLVMValueRef fcmp_uge = LLVMBuildFCmp(builder, LLVMRealUGE, x, y, "uge");
    LLVMValueRef fcmp_ult = LLVMBuildFCmp(builder, LLVMRealULT, x, y, "ult");
    LLVMValueRef fcmp_ule = LLVMBuildFCmp(builder, LLVMRealULE, x, y, "ule");
    LLVMValueRef fcmp_une = LLVMBuildFCmp(builder, LLVMRealUNE, x, y, "une");

    // Always true/false
    LLVMValueRef fcmp_true = LLVMBuildFCmp(builder, LLVMRealPredicateTrue, x, y, "always_true");
    LLVMValueRef fcmp_false = LLVMBuildFCmp(builder, LLVMRealPredicateFalse, x, y, "always_false");

    LLVMBuildRetVoid(builder);

    // ==========================================
    // Function 3: Select instruction
    // ==========================================
    LLVMTypeRef sel_params[] = {i1, i32, i32};
    LLVMTypeRef sel_ty = LLVMFunctionType(i32, sel_params, 3, 0);
    LLVMValueRef sel_func = LLVMAddFunction(mod, "select_example", sel_ty);
    LLVMValueRef cond = LLVMGetParam(sel_func, 0);
    LLVMValueRef true_val = LLVMGetParam(sel_func, 1);
    LLVMValueRef false_val = LLVMGetParam(sel_func, 2);
    LLVMSetValueName2(cond, "cond", 4);
    LLVMSetValueName2(true_val, "true_val", 8);
    LLVMSetValueName2(false_val, "false_val", 9);

    LLVMBasicBlockRef sel_entry = LLVMAppendBasicBlockInContext(ctx, sel_func, "entry");
    LLVMPositionBuilderAtEnd(builder, sel_entry);

    LLVMValueRef selected = LLVMBuildSelect(builder, cond, true_val, false_val, "selected");
    LLVMBuildRet(builder, selected);

    // ==========================================
    // Function 4: Select with comparison
    // ==========================================
    LLVMTypeRef max_params[] = {i32, i32};
    LLVMTypeRef max_ty = LLVMFunctionType(i32, max_params, 2, 0);
    LLVMValueRef max_func = LLVMAddFunction(mod, "max", max_ty);
    LLVMValueRef m_a = LLVMGetParam(max_func, 0);
    LLVMValueRef m_b = LLVMGetParam(max_func, 1);
    LLVMSetValueName2(m_a, "a", 1);
    LLVMSetValueName2(m_b, "b", 1);

    LLVMBasicBlockRef max_entry = LLVMAppendBasicBlockInContext(ctx, max_func, "entry");
    LLVMPositionBuilderAtEnd(builder, max_entry);

    LLVMValueRef cmp_gt = LLVMBuildICmp(builder, LLVMIntSGT, m_a, m_b, "a_gt_b");
    LLVMValueRef max_result = LLVMBuildSelect(builder, cmp_gt, m_a, m_b, "max");
    LLVMBuildRet(builder, max_result);

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
    printf("; Test: test_builder_cmp\n");
    printf(";\n");
    printf("; Integer comparison predicates:\n");
    printf(";   eq, ne (equality)\n");
    printf(";   ugt, uge, ult, ule (unsigned)\n");
    printf(";   sgt, sge, slt, sle (signed)\n");
    printf(";\n");
    printf("; Float comparison predicates:\n");
    printf(";   Ordered: oeq, ogt, oge, olt, ole, one, ord\n");
    printf(";   Unordered: uno, ueq, ugt, uge, ult, ule, une\n");
    printf(";   Constant: true, false\n");
    printf(";\n");
    printf("; Predicate extraction:\n");
    printf(";   icmp_eq predicate: %s\n", int_pred_name(LLVMGetICmpPredicate(icmp_eq)));
    printf(";   icmp_slt predicate: %s\n", int_pred_name(LLVMGetICmpPredicate(icmp_slt)));
    printf(";   fcmp_oeq predicate: %s\n", real_pred_name(LLVMGetFCmpPredicate(fcmp_oeq)));
    printf(";   fcmp_uno predicate: %s\n", real_pred_name(LLVMGetFCmpPredicate(fcmp_uno)));
    printf(";\n");
    printf("; Select instruction: cond ? true_val : false_val\n");
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
