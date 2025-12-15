/**
 * Test: test_types
 * Tests LLVM Type creation and inspection
 *
 * LLVM-C APIs covered:
 * - LLVMInt1TypeInContext(), LLVMInt8TypeInContext(), LLVMInt16TypeInContext()
 * - LLVMInt32TypeInContext(), LLVMInt64TypeInContext(), LLVMInt128TypeInContext()
 * - LLVMIntTypeInContext()
 * - LLVMHalfTypeInContext(), LLVMFloatTypeInContext(), LLVMDoubleTypeInContext()
 * - LLVMVoidTypeInContext()
 * - LLVMPointerTypeInContext()
 * - LLVMArrayType2()
 * - LLVMVectorType()
 * - LLVMFunctionType()
 * - LLVMStructTypeInContext(), LLVMStructCreateNamed(), LLVMStructSetBody()
 * - LLVMPrintTypeToString(), LLVMGetTypeKind()
 * - LLVMGetIntTypeWidth()
 * - LLVMTypeIsSized()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

const char* type_kind_name(LLVMTypeKind kind) {
    switch (kind) {
        case LLVMVoidTypeKind: return "void";
        case LLVMHalfTypeKind: return "half";
        case LLVMFloatTypeKind: return "float";
        case LLVMDoubleTypeKind: return "double";
        case LLVMX86_FP80TypeKind: return "x86_fp80";
        case LLVMFP128TypeKind: return "fp128";
        case LLVMPPC_FP128TypeKind: return "ppc_fp128";
        case LLVMLabelTypeKind: return "label";
        case LLVMIntegerTypeKind: return "integer";
        case LLVMFunctionTypeKind: return "function";
        case LLVMStructTypeKind: return "struct";
        case LLVMArrayTypeKind: return "array";
        case LLVMPointerTypeKind: return "pointer";
        case LLVMVectorTypeKind: return "vector";
        case LLVMMetadataTypeKind: return "metadata";
        case LLVMTokenTypeKind: return "token";
        case LLVMScalableVectorTypeKind: return "scalable_vector";
        case LLVMBFloatTypeKind: return "bfloat";
        case LLVMX86_AMXTypeKind: return "x86_amx";
        case LLVMTargetExtTypeKind: return "target_ext";
        default: return "unknown";
    }
}

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_types", ctx);

    // Integer types
    LLVMTypeRef i1 = LLVMInt1TypeInContext(ctx);
    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx);
    LLVMTypeRef i16 = LLVMInt16TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef i128 = LLVMInt128TypeInContext(ctx);
    LLVMTypeRef i256 = LLVMIntTypeInContext(ctx, 256);

    // Floating point types
    LLVMTypeRef f16 = LLVMHalfTypeInContext(ctx);
    LLVMTypeRef bf16 = LLVMBFloatTypeInContext(ctx);
    LLVMTypeRef f32 = LLVMFloatTypeInContext(ctx);
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx);

    // Void type
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);

    // Pointer type (opaque pointer)
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);

    // Array type
    LLVMTypeRef arr_i32_10 = LLVMArrayType2(i32, 10);

    // Vector type
    LLVMTypeRef vec_i32_4 = LLVMVectorType(i32, 4);

    // Function type: i32(i32, i32)
    LLVMTypeRef func_params[] = {i32, i32};
    LLVMTypeRef func_ty = LLVMFunctionType(i32, func_params, 2, 0);

    // Function type with varargs: i32(i32, ...)
    LLVMTypeRef vararg_params[] = {i32};
    LLVMTypeRef vararg_func_ty = LLVMFunctionType(i32, vararg_params, 1, 1);

    // Anonymous struct type: {i32, f64}
    LLVMTypeRef struct_elems[] = {i32, f64};
    LLVMTypeRef anon_struct = LLVMStructTypeInContext(ctx, struct_elems, 2, 0);

    // Packed anonymous struct type: <{i8, i32}>
    LLVMTypeRef packed_elems[] = {i8, i32};
    LLVMTypeRef packed_struct = LLVMStructTypeInContext(ctx, packed_elems, 2, 1);

    // Named struct type
    LLVMTypeRef named_struct = LLVMStructCreateNamed(ctx, "MyStruct");
    LLVMTypeRef named_elems[] = {i32, ptr, f64};
    LLVMStructSetBody(named_struct, named_elems, 3, 0);

    // Opaque struct (no body set)
    LLVMTypeRef opaque_struct = LLVMStructCreateNamed(ctx, "OpaqueStruct");

    // Add global variables to make types visible in output
    LLVMAddGlobal(mod, i32, "global_i32");
    LLVMAddGlobal(mod, arr_i32_10, "global_arr");
    LLVMAddGlobal(mod, vec_i32_4, "global_vec");
    LLVMAddGlobal(mod, anon_struct, "global_anon_struct");
    LLVMAddGlobal(mod, packed_struct, "global_packed_struct");
    LLVMAddGlobal(mod, named_struct, "global_named_struct");

    // Add a function declaration to show function type
    LLVMAddFunction(mod, "example_func", func_ty);
    LLVMAddFunction(mod, "example_vararg_func", vararg_func_ty);

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
    printf("; Test: test_types\n");
    printf(";\n");
    printf("; Integer types:\n");
    printf(";   i1 width: %u, kind: %s\n", LLVMGetIntTypeWidth(i1), type_kind_name(LLVMGetTypeKind(i1)));
    printf(";   i8 width: %u, kind: %s\n", LLVMGetIntTypeWidth(i8), type_kind_name(LLVMGetTypeKind(i8)));
    printf(";   i16 width: %u, kind: %s\n", LLVMGetIntTypeWidth(i16), type_kind_name(LLVMGetTypeKind(i16)));
    printf(";   i32 width: %u, kind: %s\n", LLVMGetIntTypeWidth(i32), type_kind_name(LLVMGetTypeKind(i32)));
    printf(";   i64 width: %u, kind: %s\n", LLVMGetIntTypeWidth(i64), type_kind_name(LLVMGetTypeKind(i64)));
    printf(";   i128 width: %u, kind: %s\n", LLVMGetIntTypeWidth(i128), type_kind_name(LLVMGetTypeKind(i128)));
    printf(";   i256 width: %u, kind: %s\n", LLVMGetIntTypeWidth(i256), type_kind_name(LLVMGetTypeKind(i256)));
    printf(";\n");
    printf("; Floating point types:\n");
    printf(";   half kind: %s\n", type_kind_name(LLVMGetTypeKind(f16)));
    printf(";   bfloat kind: %s\n", type_kind_name(LLVMGetTypeKind(bf16)));
    printf(";   float kind: %s\n", type_kind_name(LLVMGetTypeKind(f32)));
    printf(";   double kind: %s\n", type_kind_name(LLVMGetTypeKind(f64)));
    printf(";\n");
    printf("; Other types:\n");
    printf(";   void kind: %s, sized: %s\n", type_kind_name(LLVMGetTypeKind(void_ty)), LLVMTypeIsSized(void_ty) ? "yes" : "no");
    printf(";   pointer kind: %s, sized: %s\n", type_kind_name(LLVMGetTypeKind(ptr)), LLVMTypeIsSized(ptr) ? "yes" : "no");
    printf(";   array kind: %s, sized: %s\n", type_kind_name(LLVMGetTypeKind(arr_i32_10)), LLVMTypeIsSized(arr_i32_10) ? "yes" : "no");
    printf(";   vector kind: %s, sized: %s\n", type_kind_name(LLVMGetTypeKind(vec_i32_4)), LLVMTypeIsSized(vec_i32_4) ? "yes" : "no");
    printf(";   function kind: %s, sized: %s\n", type_kind_name(LLVMGetTypeKind(func_ty)), LLVMTypeIsSized(func_ty) ? "yes" : "no");
    printf(";\n");
    printf("; Struct types:\n");
    printf(";   anon_struct kind: %s, packed: %s\n", type_kind_name(LLVMGetTypeKind(anon_struct)), LLVMIsPackedStruct(anon_struct) ? "yes" : "no");
    printf(";   packed_struct kind: %s, packed: %s\n", type_kind_name(LLVMGetTypeKind(packed_struct)), LLVMIsPackedStruct(packed_struct) ? "yes" : "no");

    const char *named_struct_name = LLVMGetStructName(named_struct);
    printf(";   named_struct name: %s, opaque: %s\n", named_struct_name, LLVMIsOpaqueStruct(named_struct) ? "yes" : "no");

    const char *opaque_struct_name = LLVMGetStructName(opaque_struct);
    printf(";   opaque_struct name: %s, opaque: %s\n", opaque_struct_name, LLVMIsOpaqueStruct(opaque_struct) ? "yes" : "no");

    // Print type strings
    printf(";\n");
    printf("; Type strings:\n");
    char *i32_str = LLVMPrintTypeToString(i32);
    printf(";   i32: %s\n", i32_str);
    LLVMDisposeMessage(i32_str);

    char *arr_str = LLVMPrintTypeToString(arr_i32_10);
    printf(";   [10 x i32]: %s\n", arr_str);
    LLVMDisposeMessage(arr_str);

    char *func_str = LLVMPrintTypeToString(func_ty);
    printf(";   func type: %s\n", func_str);
    LLVMDisposeMessage(func_str);

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
