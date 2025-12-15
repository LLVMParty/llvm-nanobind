/**
 * Test: test_builder_casts
 * Tests LLVM Builder cast operations
 *
 * LLVM-C APIs covered:
 * - LLVMBuildTrunc(), LLVMBuildZExt(), LLVMBuildSExt()
 * - LLVMBuildFPToUI(), LLVMBuildFPToSI()
 * - LLVMBuildUIToFP(), LLVMBuildSIToFP()
 * - LLVMBuildFPTrunc(), LLVMBuildFPExt()
 * - LLVMBuildPtrToInt(), LLVMBuildIntToPtr()
 * - LLVMBuildBitCast()
 * - LLVMBuildIntCast2()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_builder_casts", ctx);

    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx);
    LLVMTypeRef i16 = LLVMInt16TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef f32 = LLVMFloatTypeInContext(ctx);
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

    // ==========================================
    // Function 1: Integer casts
    // ==========================================
    LLVMTypeRef int_cast_params[] = {i64};
    LLVMTypeRef int_cast_ty = LLVMFunctionType(i8, int_cast_params, 1, 0);
    LLVMValueRef int_cast_func = LLVMAddFunction(mod, "integer_casts", int_cast_ty);
    LLVMValueRef i64_val = LLVMGetParam(int_cast_func, 0);
    LLVMSetValueName2(i64_val, "val", 3);

    LLVMBasicBlockRef int_entry = LLVMAppendBasicBlockInContext(ctx, int_cast_func, "entry");
    LLVMPositionBuilderAtEnd(builder, int_entry);

    // Truncate i64 -> i32 -> i16 -> i8
    LLVMValueRef trunc_32 = LLVMBuildTrunc(builder, i64_val, i32, "trunc_32");
    LLVMValueRef trunc_16 = LLVMBuildTrunc(builder, trunc_32, i16, "trunc_16");
    LLVMValueRef trunc_8 = LLVMBuildTrunc(builder, trunc_16, i8, "trunc_8");

    // Zero extend i8 -> i16 -> i32 -> i64
    LLVMValueRef zext_16 = LLVMBuildZExt(builder, trunc_8, i16, "zext_16");
    LLVMValueRef zext_32 = LLVMBuildZExt(builder, zext_16, i32, "zext_32");
    LLVMValueRef zext_64 = LLVMBuildZExt(builder, zext_32, i64, "zext_64");

    // Sign extend i8 -> i16 -> i32 -> i64
    LLVMValueRef sext_16 = LLVMBuildSExt(builder, trunc_8, i16, "sext_16");
    LLVMValueRef sext_32 = LLVMBuildSExt(builder, sext_16, i32, "sext_32");
    LLVMValueRef sext_64 = LLVMBuildSExt(builder, sext_32, i64, "sext_64");

    // IntCast2 (auto-selects trunc/zext/sext)
    LLVMValueRef intcast_unsigned = LLVMBuildIntCast2(builder, i64_val, i32, 0, "intcast_unsigned");
    LLVMValueRef intcast_signed = LLVMBuildIntCast2(builder, trunc_8, i32, 1, "intcast_signed");

    LLVMBuildRet(builder, trunc_8);

    // ==========================================
    // Function 2: Float casts
    // ==========================================
    LLVMTypeRef fp_cast_params[] = {f64};
    LLVMTypeRef fp_cast_ty = LLVMFunctionType(f32, fp_cast_params, 1, 0);
    LLVMValueRef fp_cast_func = LLVMAddFunction(mod, "float_casts", fp_cast_ty);
    LLVMValueRef f64_val = LLVMGetParam(fp_cast_func, 0);
    LLVMSetValueName2(f64_val, "val", 3);

    LLVMBasicBlockRef fp_entry = LLVMAppendBasicBlockInContext(ctx, fp_cast_func, "entry");
    LLVMPositionBuilderAtEnd(builder, fp_entry);

    // FP truncate f64 -> f32
    LLVMValueRef fptrunc = LLVMBuildFPTrunc(builder, f64_val, f32, "fptrunc");

    // FP extend f32 -> f64
    LLVMValueRef fpext = LLVMBuildFPExt(builder, fptrunc, f64, "fpext");

    LLVMBuildRet(builder, fptrunc);

    // ==========================================
    // Function 3: Int <-> Float casts
    // ==========================================
    LLVMTypeRef mixed_params[] = {i32, f64};
    LLVMTypeRef mixed_ty = LLVMFunctionType(void_ty, mixed_params, 2, 0);
    LLVMValueRef mixed_func = LLVMAddFunction(mod, "int_float_casts", mixed_ty);
    LLVMValueRef int_param = LLVMGetParam(mixed_func, 0);
    LLVMValueRef fp_param = LLVMGetParam(mixed_func, 1);
    LLVMSetValueName2(int_param, "i", 1);
    LLVMSetValueName2(fp_param, "f", 1);

    LLVMBasicBlockRef mixed_entry = LLVMAppendBasicBlockInContext(ctx, mixed_func, "entry");
    LLVMPositionBuilderAtEnd(builder, mixed_entry);

    // Int -> Float
    LLVMValueRef uitofp = LLVMBuildUIToFP(builder, int_param, f64, "uitofp");
    LLVMValueRef sitofp = LLVMBuildSIToFP(builder, int_param, f64, "sitofp");

    // Float -> Int
    LLVMValueRef fptoui = LLVMBuildFPToUI(builder, fp_param, i32, "fptoui");
    LLVMValueRef fptosi = LLVMBuildFPToSI(builder, fp_param, i32, "fptosi");

    LLVMBuildRetVoid(builder);

    // ==========================================
    // Function 4: Pointer casts
    // ==========================================
    LLVMTypeRef ptr_params[] = {ptr, i64};
    LLVMTypeRef ptr_ty = LLVMFunctionType(void_ty, ptr_params, 2, 0);
    LLVMValueRef ptr_func = LLVMAddFunction(mod, "pointer_casts", ptr_ty);
    LLVMValueRef ptr_param = LLVMGetParam(ptr_func, 0);
    LLVMValueRef int_for_ptr = LLVMGetParam(ptr_func, 1);
    LLVMSetValueName2(ptr_param, "p", 1);
    LLVMSetValueName2(int_for_ptr, "addr", 4);

    LLVMBasicBlockRef ptr_entry = LLVMAppendBasicBlockInContext(ctx, ptr_func, "entry");
    LLVMPositionBuilderAtEnd(builder, ptr_entry);

    // Pointer -> Int
    LLVMValueRef ptrtoint = LLVMBuildPtrToInt(builder, ptr_param, i64, "ptrtoint");

    // Int -> Pointer
    LLVMValueRef inttoptr = LLVMBuildIntToPtr(builder, int_for_ptr, ptr, "inttoptr");

    LLVMBuildRetVoid(builder);

    // ==========================================
    // Function 5: Bitcast
    // ==========================================
    LLVMTypeRef vec_i32 = LLVMVectorType(i32, 4);
    LLVMTypeRef vec_f32 = LLVMVectorType(f32, 4);

    LLVMTypeRef bitcast_params[] = {vec_i32};
    LLVMTypeRef bitcast_ty = LLVMFunctionType(vec_f32, bitcast_params, 1, 0);
    LLVMValueRef bitcast_func = LLVMAddFunction(mod, "bitcast_example", bitcast_ty);
    LLVMValueRef vec_param = LLVMGetParam(bitcast_func, 0);
    LLVMSetValueName2(vec_param, "v", 1);

    LLVMBasicBlockRef bitcast_entry = LLVMAppendBasicBlockInContext(ctx, bitcast_func, "entry");
    LLVMPositionBuilderAtEnd(builder, bitcast_entry);

    LLVMValueRef bitcast = LLVMBuildBitCast(builder, vec_param, vec_f32, "bitcast");
    LLVMBuildRet(builder, bitcast);

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
    printf("; Test: test_builder_casts\n");
    printf(";\n");
    printf("; Cast operations demonstrated:\n");
    printf(";   Integer: trunc, zext, sext, intcast2\n");
    printf(";   Float: fptrunc, fpext\n");
    printf(";   Int<->Float: uitofp, sitofp, fptoui, fptosi\n");
    printf(";   Pointer: ptrtoint, inttoptr\n");
    printf(";   Reinterpret: bitcast\n");
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
