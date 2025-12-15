/**
 * Test: test_constants
 * Tests LLVM constant value creation
 *
 * LLVM-C APIs covered:
 * - LLVMConstInt(), LLVMConstIntOfArbitraryPrecision()
 * - LLVMConstReal(), LLVMConstRealOfString()
 * - LLVMConstNull(), LLVMConstAllOnes()
 * - LLVMGetUndef(), LLVMGetPoison()
 * - LLVMConstPointerNull()
 * - LLVMConstStringInContext2()
 * - LLVMConstArray2(), LLVMConstStructInContext(), LLVMConstNamedStruct(), LLVMConstVector()
 * - LLVMConstIntGetZExtValue(), LLVMConstIntGetSExtValue()
 * - LLVMIsConstant(), LLVMIsNull(), LLVMIsUndef(), LLVMIsPoison()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>
#include <cstring>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_constants", ctx);

    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef i128 = LLVMInt128TypeInContext(ctx);
    LLVMTypeRef f32 = LLVMFloatTypeInContext(ctx);
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);

    // ==========================================
    // Integer constants
    // ==========================================
    LLVMValueRef const_0 = LLVMConstInt(i32, 0, 0);
    LLVMValueRef const_42 = LLVMConstInt(i32, 42, 0);
    LLVMValueRef const_neg1 = LLVMConstInt(i32, -1, 1);  // Sign extend
    LLVMValueRef const_max_u32 = LLVMConstInt(i32, 0xFFFFFFFF, 0);
    LLVMValueRef const_i64 = LLVMConstInt(i64, 0x123456789ABCDEF0ULL, 0);

    // Arbitrary precision integer (128-bit)
    uint64_t words[] = {0xFFFFFFFFFFFFFFFFULL, 0x0000000000000001ULL};
    LLVMValueRef const_i128 = LLVMConstIntOfArbitraryPrecision(i128, 2, words);

    // ==========================================
    // Floating point constants
    // ==========================================
    LLVMValueRef const_pi = LLVMConstReal(f64, 3.14159265358979323846);
    LLVMValueRef const_e = LLVMConstReal(f64, 2.71828182845904523536);
    LLVMValueRef const_f32 = LLVMConstReal(f32, 1.5f);

    // From string
    LLVMValueRef const_from_str = LLVMConstRealOfString(f64, "1.234567890123456789");

    // ==========================================
    // Special values
    // ==========================================
    LLVMValueRef null_i32 = LLVMConstNull(i32);
    LLVMValueRef null_ptr = LLVMConstPointerNull(ptr);
    LLVMValueRef all_ones = LLVMConstAllOnes(i32);
    LLVMValueRef undef_i32 = LLVMGetUndef(i32);
    LLVMValueRef poison_i32 = LLVMGetPoison(i32);

    // ==========================================
    // String constant
    // ==========================================
    const char *str = "Hello, LLVM!";
    LLVMValueRef const_string = LLVMConstStringInContext2(ctx, str, strlen(str), 0);  // null terminated
    LLVMValueRef const_string_no_null = LLVMConstStringInContext2(ctx, str, strlen(str), 1);  // not null terminated

    // ==========================================
    // Array constant
    // ==========================================
    LLVMValueRef arr_elems[] = {
        LLVMConstInt(i32, 1, 0),
        LLVMConstInt(i32, 2, 0),
        LLVMConstInt(i32, 3, 0),
        LLVMConstInt(i32, 4, 0),
        LLVMConstInt(i32, 5, 0)
    };
    LLVMValueRef const_array = LLVMConstArray2(i32, arr_elems, 5);

    // ==========================================
    // Anonymous struct constant
    // ==========================================
    LLVMValueRef struct_elems[] = {
        LLVMConstInt(i32, 100, 0),
        LLVMConstReal(f64, 3.14),
        LLVMConstInt(i64, 999, 0)
    };
    LLVMValueRef const_struct = LLVMConstStructInContext(ctx, struct_elems, 3, 0);
    LLVMValueRef const_packed_struct = LLVMConstStructInContext(ctx, struct_elems, 3, 1);

    // ==========================================
    // Named struct constant
    // ==========================================
    LLVMTypeRef named_struct_ty = LLVMStructCreateNamed(ctx, "Point");
    LLVMTypeRef point_elems[] = {i32, i32};
    LLVMStructSetBody(named_struct_ty, point_elems, 2, 0);

    LLVMValueRef point_vals[] = {
        LLVMConstInt(i32, 10, 0),
        LLVMConstInt(i32, 20, 0)
    };
    LLVMValueRef const_named_struct = LLVMConstNamedStruct(named_struct_ty, point_vals, 2);

    // ==========================================
    // Vector constant
    // ==========================================
    LLVMValueRef vec_elems[] = {
        LLVMConstInt(i32, 1, 0),
        LLVMConstInt(i32, 2, 0),
        LLVMConstInt(i32, 3, 0),
        LLVMConstInt(i32, 4, 0)
    };
    LLVMValueRef const_vector = LLVMConstVector(vec_elems, 4);

    // ==========================================
    // Add globals to expose constants in output
    // ==========================================
    LLVMValueRef g;

    g = LLVMAddGlobal(mod, i32, "const_42");
    LLVMSetInitializer(g, const_42);
    LLVMSetGlobalConstant(g, 1);

    g = LLVMAddGlobal(mod, i32, "const_neg1");
    LLVMSetInitializer(g, const_neg1);
    LLVMSetGlobalConstant(g, 1);

    g = LLVMAddGlobal(mod, i64, "const_i64");
    LLVMSetInitializer(g, const_i64);
    LLVMSetGlobalConstant(g, 1);

    g = LLVMAddGlobal(mod, i128, "const_i128");
    LLVMSetInitializer(g, const_i128);
    LLVMSetGlobalConstant(g, 1);

    g = LLVMAddGlobal(mod, f64, "const_pi");
    LLVMSetInitializer(g, const_pi);
    LLVMSetGlobalConstant(g, 1);

    g = LLVMAddGlobal(mod, i32, "all_ones");
    LLVMSetInitializer(g, all_ones);
    LLVMSetGlobalConstant(g, 1);

    g = LLVMAddGlobal(mod, i32, "undef_val");
    LLVMSetInitializer(g, undef_i32);

    g = LLVMAddGlobal(mod, i32, "poison_val");
    LLVMSetInitializer(g, poison_i32);

    // Get array type for const_string
    LLVMTypeRef str_arr_ty = LLVMArrayType2(i8, strlen(str) + 1);
    g = LLVMAddGlobal(mod, str_arr_ty, "hello_string");
    LLVMSetInitializer(g, const_string);
    LLVMSetGlobalConstant(g, 1);

    LLVMTypeRef arr_ty = LLVMArrayType2(i32, 5);
    g = LLVMAddGlobal(mod, arr_ty, "const_array");
    LLVMSetInitializer(g, const_array);
    LLVMSetGlobalConstant(g, 1);

    // Struct types for globals
    LLVMTypeRef anon_struct_elems[] = {i32, f64, i64};
    LLVMTypeRef anon_struct_ty = LLVMStructTypeInContext(ctx, anon_struct_elems, 3, 0);
    g = LLVMAddGlobal(mod, anon_struct_ty, "const_struct");
    LLVMSetInitializer(g, const_struct);
    LLVMSetGlobalConstant(g, 1);

    g = LLVMAddGlobal(mod, named_struct_ty, "const_point");
    LLVMSetInitializer(g, const_named_struct);
    LLVMSetGlobalConstant(g, 1);

    LLVMTypeRef vec_ty = LLVMVectorType(i32, 4);
    g = LLVMAddGlobal(mod, vec_ty, "const_vector");
    LLVMSetInitializer(g, const_vector);
    LLVMSetGlobalConstant(g, 1);

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
    printf("; Test: test_constants\n");
    printf(";\n");
    printf("; Integer constants:\n");
    printf(";   const_0 value (zext): %llu\n", LLVMConstIntGetZExtValue(const_0));
    printf(";   const_42 value (zext): %llu\n", LLVMConstIntGetZExtValue(const_42));
    printf(";   const_neg1 value (sext): %lld\n", LLVMConstIntGetSExtValue(const_neg1));
    printf(";   const_max_u32 value (zext): %llu\n", LLVMConstIntGetZExtValue(const_max_u32));
    printf(";\n");
    printf("; Value checks:\n");
    printf(";   const_42 is constant: %s\n", LLVMIsConstant(const_42) ? "yes" : "no");
    printf(";   null_i32 is null: %s\n", LLVMIsNull(null_i32) ? "yes" : "no");
    printf(";   null_ptr is null: %s\n", LLVMIsNull(null_ptr) ? "yes" : "no");
    printf(";   undef_i32 is undef: %s\n", LLVMIsUndef(undef_i32) ? "yes" : "no");
    printf(";   poison_i32 is poison: %s\n", LLVMIsPoison(poison_i32) ? "yes" : "no");
    printf(";   const_42 is undef: %s\n", LLVMIsUndef(const_42) ? "yes" : "no");
    printf(";\n");
    printf("; Aggregate constants:\n");
    printf(";   array with 5 i32 elements\n");
    printf(";   struct with {i32, f64, i64}\n");
    printf(";   named struct Point with {i32, i32}\n");
    printf(";   vector with 4 x i32\n");
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
