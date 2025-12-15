/**
 * Test: test_builder_arithmetic
 * Tests LLVM Builder arithmetic instruction creation
 *
 * LLVM-C APIs covered:
 * - LLVMCreateBuilderInContext(), LLVMDisposeBuilder()
 * - LLVMPositionBuilderAtEnd()
 * - LLVMBuildAdd(), LLVMBuildNSWAdd(), LLVMBuildNUWAdd()
 * - LLVMBuildSub(), LLVMBuildNSWSub(), LLVMBuildNUWSub()
 * - LLVMBuildMul(), LLVMBuildNSWMul(), LLVMBuildNUWMul()
 * - LLVMBuildSDiv(), LLVMBuildUDiv(), LLVMBuildExactSDiv()
 * - LLVMBuildSRem(), LLVMBuildURem()
 * - LLVMBuildFAdd(), LLVMBuildFSub(), LLVMBuildFMul(), LLVMBuildFDiv(), LLVMBuildFRem()
 * - LLVMBuildShl(), LLVMBuildLShr(), LLVMBuildAShr()
 * - LLVMBuildAnd(), LLVMBuildOr(), LLVMBuildXor()
 * - LLVMBuildNeg(), LLVMBuildNSWNeg(), LLVMBuildFNeg(), LLVMBuildNot()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_builder_arithmetic", ctx);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx);

    // Integer arithmetic function: i32 int_arith(i32, i32)
    LLVMTypeRef int_params[] = {i32, i32};
    LLVMTypeRef int_func_ty = LLVMFunctionType(i32, int_params, 2, 0);
    LLVMValueRef int_func = LLVMAddFunction(mod, "int_arith", int_func_ty);

    LLVMValueRef a = LLVMGetParam(int_func, 0);
    LLVMValueRef b = LLVMGetParam(int_func, 1);
    LLVMSetValueName2(a, "a", 1);
    LLVMSetValueName2(b, "b", 1);

    LLVMBasicBlockRef int_entry = LLVMAppendBasicBlockInContext(ctx, int_func, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(builder, int_entry);

    // Basic arithmetic
    LLVMValueRef add = LLVMBuildAdd(builder, a, b, "add");
    LLVMValueRef sub = LLVMBuildSub(builder, a, b, "sub");
    LLVMValueRef mul = LLVMBuildMul(builder, a, b, "mul");
    LLVMValueRef sdiv = LLVMBuildSDiv(builder, a, b, "sdiv");
    LLVMValueRef udiv = LLVMBuildUDiv(builder, a, b, "udiv");
    LLVMValueRef srem = LLVMBuildSRem(builder, a, b, "srem");
    LLVMValueRef urem = LLVMBuildURem(builder, a, b, "urem");

    // With overflow flags
    LLVMValueRef nsw_add = LLVMBuildNSWAdd(builder, a, b, "nsw_add");
    LLVMValueRef nuw_add = LLVMBuildNUWAdd(builder, a, b, "nuw_add");
    LLVMValueRef nsw_sub = LLVMBuildNSWSub(builder, a, b, "nsw_sub");
    LLVMValueRef nuw_sub = LLVMBuildNUWSub(builder, a, b, "nuw_sub");
    LLVMValueRef nsw_mul = LLVMBuildNSWMul(builder, a, b, "nsw_mul");
    LLVMValueRef nuw_mul = LLVMBuildNUWMul(builder, a, b, "nuw_mul");
    LLVMValueRef exact_sdiv = LLVMBuildExactSDiv(builder, a, b, "exact_sdiv");

    // Bitwise operations
    LLVMValueRef and_op = LLVMBuildAnd(builder, a, b, "and");
    LLVMValueRef or_op = LLVMBuildOr(builder, a, b, "or");
    LLVMValueRef xor_op = LLVMBuildXor(builder, a, b, "xor");

    // Shift operations
    LLVMValueRef shl = LLVMBuildShl(builder, a, b, "shl");
    LLVMValueRef lshr = LLVMBuildLShr(builder, a, b, "lshr");
    LLVMValueRef ashr = LLVMBuildAShr(builder, a, b, "ashr");

    // Unary operations
    LLVMValueRef neg = LLVMBuildNeg(builder, a, "neg");
    LLVMValueRef nsw_neg = LLVMBuildNSWNeg(builder, a, "nsw_neg");
    LLVMValueRef not_op = LLVMBuildNot(builder, a, "not");

    // Return something to make function complete
    LLVMBuildRet(builder, add);

    // Floating point arithmetic function: f64 float_arith(f64, f64)
    LLVMTypeRef fp_params[] = {f64, f64};
    LLVMTypeRef fp_func_ty = LLVMFunctionType(f64, fp_params, 2, 0);
    LLVMValueRef fp_func = LLVMAddFunction(mod, "float_arith", fp_func_ty);

    LLVMValueRef x = LLVMGetParam(fp_func, 0);
    LLVMValueRef y = LLVMGetParam(fp_func, 1);
    LLVMSetValueName2(x, "x", 1);
    LLVMSetValueName2(y, "y", 1);

    LLVMBasicBlockRef fp_entry = LLVMAppendBasicBlockInContext(ctx, fp_func, "entry");
    LLVMPositionBuilderAtEnd(builder, fp_entry);

    // Floating point operations
    LLVMValueRef fadd = LLVMBuildFAdd(builder, x, y, "fadd");
    LLVMValueRef fsub = LLVMBuildFSub(builder, x, y, "fsub");
    LLVMValueRef fmul = LLVMBuildFMul(builder, x, y, "fmul");
    LLVMValueRef fdiv = LLVMBuildFDiv(builder, x, y, "fdiv");
    LLVMValueRef frem = LLVMBuildFRem(builder, x, y, "frem");
    LLVMValueRef fneg = LLVMBuildFNeg(builder, x, "fneg");

    LLVMBuildRet(builder, fadd);

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
    printf("; Test: test_builder_arithmetic\n");
    printf(";\n");
    printf("; Integer operations demonstrated:\n");
    printf(";   add, sub, mul, sdiv, udiv, srem, urem\n");
    printf(";   nsw_add, nuw_add, nsw_sub, nuw_sub, nsw_mul, nuw_mul, exact_sdiv\n");
    printf(";   and, or, xor, shl, lshr, ashr\n");
    printf(";   neg, nsw_neg, not\n");
    printf(";\n");
    printf("; Floating point operations demonstrated:\n");
    printf(";   fadd, fsub, fmul, fdiv, frem, fneg\n");
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
