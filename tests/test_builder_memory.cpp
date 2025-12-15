/**
 * Test: test_builder_memory
 * Tests LLVM Builder memory operations
 *
 * LLVM-C APIs covered:
 * - LLVMBuildAlloca(), LLVMBuildArrayAlloca()
 * - LLVMBuildLoad2(), LLVMBuildStore()
 * - LLVMBuildGEP2(), LLVMBuildInBoundsGEP2(), LLVMBuildStructGEP2()
 * - LLVMGetVolatile(), LLVMSetVolatile()
 * - LLVMGetAlignment(), LLVMSetAlignment()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_builder_memory", ctx);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);

    // Array type for array alloca test
    LLVMTypeRef arr_ty = LLVMArrayType2(i32, 10);

    // Struct type for struct GEP test
    LLVMTypeRef struct_elems[] = {i32, i64, i32};
    LLVMTypeRef struct_ty = LLVMStructTypeInContext(ctx, struct_elems, 3, 0);

    // Function: void memory_ops()
    LLVMTypeRef func_ty = LLVMFunctionType(void_ty, nullptr, 0, 0);
    LLVMValueRef func = LLVMAddFunction(mod, "memory_ops", func_ty);

    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, func, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(builder, entry);

    // Basic alloca
    LLVMValueRef alloca_i32 = LLVMBuildAlloca(builder, i32, "local_i32");

    // Alloca with explicit alignment
    LLVMValueRef alloca_aligned = LLVMBuildAlloca(builder, i64, "local_aligned");
    LLVMSetAlignment(alloca_aligned, 16);

    // Array alloca (dynamic size)
    LLVMValueRef array_size = LLVMConstInt(i32, 5, 0);
    LLVMValueRef array_alloca = LLVMBuildArrayAlloca(builder, i32, array_size, "dynamic_array");

    // Static array alloca
    LLVMValueRef static_array = LLVMBuildAlloca(builder, arr_ty, "static_array");

    // Struct alloca
    LLVMValueRef struct_alloca = LLVMBuildAlloca(builder, struct_ty, "local_struct");

    // Store and load
    LLVMValueRef val = LLVMConstInt(i32, 42, 0);
    LLVMValueRef store = LLVMBuildStore(builder, val, alloca_i32);
    LLVMValueRef load = LLVMBuildLoad2(builder, i32, alloca_i32, "loaded");

    // Volatile load/store
    LLVMValueRef volatile_store = LLVMBuildStore(builder, val, alloca_i32);
    LLVMSetVolatile(volatile_store, 1);
    LLVMValueRef volatile_load = LLVMBuildLoad2(builder, i32, alloca_i32, "volatile_loaded");
    LLVMSetVolatile(volatile_load, 1);

    // Load with alignment
    LLVMValueRef aligned_load = LLVMBuildLoad2(builder, i64, alloca_aligned, "aligned_loaded");
    LLVMSetAlignment(aligned_load, 16);

    // GEP into array (static)
    LLVMValueRef indices[] = {LLVMConstInt(i64, 0, 0), LLVMConstInt(i64, 3, 0)};
    LLVMValueRef gep = LLVMBuildGEP2(builder, arr_ty, static_array, indices, 2, "arr_elem");

    // Inbounds GEP
    LLVMValueRef inbounds_gep = LLVMBuildInBoundsGEP2(builder, arr_ty, static_array, indices, 2, "arr_elem_inbounds");

    // Struct GEP
    LLVMValueRef struct_gep_0 = LLVMBuildStructGEP2(builder, struct_ty, struct_alloca, 0, "field_0");
    LLVMValueRef struct_gep_1 = LLVMBuildStructGEP2(builder, struct_ty, struct_alloca, 1, "field_1");
    LLVMValueRef struct_gep_2 = LLVMBuildStructGEP2(builder, struct_ty, struct_alloca, 2, "field_2");

    // Store to struct fields
    LLVMBuildStore(builder, LLVMConstInt(i32, 100, 0), struct_gep_0);
    LLVMBuildStore(builder, LLVMConstInt(i64, 200, 0), struct_gep_1);
    LLVMBuildStore(builder, LLVMConstInt(i32, 300, 0), struct_gep_2);

    // Load from struct fields
    LLVMValueRef field_0_val = LLVMBuildLoad2(builder, i32, struct_gep_0, "field_0_val");
    LLVMValueRef field_1_val = LLVMBuildLoad2(builder, i64, struct_gep_1, "field_1_val");

    LLVMBuildRetVoid(builder);

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
    printf("; Test: test_builder_memory\n");
    printf(";\n");
    printf("; Memory operations demonstrated:\n");
    printf(";   alloca (i32, i64 with alignment, dynamic array, static array, struct)\n");
    printf(";   store (basic, volatile)\n");
    printf(";   load (basic, volatile, aligned)\n");
    printf(";   GEP (array indexing, inbounds)\n");
    printf(";   struct GEP (field access)\n");
    printf(";\n");
    printf("; Alignment checks:\n");
    printf(";   alloca_aligned alignment: %u\n", LLVMGetAlignment(alloca_aligned));
    printf(";   aligned_load alignment: %u\n", LLVMGetAlignment(aligned_load));
    printf(";\n");
    printf("; Volatile checks:\n");
    printf(";   volatile_store is volatile: %s\n", LLVMGetVolatile(volatile_store) ? "yes" : "no");
    printf(";   volatile_load is volatile: %s\n", LLVMGetVolatile(volatile_load) ? "yes" : "no");
    printf(";   regular store is volatile: %s\n", LLVMGetVolatile(store) ? "yes" : "no");
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
