/**
 * Test: test_function
 * Tests LLVM Function creation and properties
 *
 * LLVM-C APIs covered:
 * - LLVMAddFunction()
 * - LLVMGetNamedFunction()
 * - LLVMCountParams(), LLVMGetParams(), LLVMGetParam()
 * - LLVMSetValueName2(), LLVMGetValueName2()
 * - LLVMGetFunctionCallConv(), LLVMSetFunctionCallConv()
 * - LLVMGetLinkage(), LLVMSetLinkage()
 * - LLVMGetFirstFunction(), LLVMGetNextFunction(), LLVMGetLastFunction()
 * - LLVMDeleteFunction()
 * - LLVMGetReturnType(), LLVMCountParamTypes()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>
#include <cstring>

const char* linkage_name(LLVMLinkage linkage) {
    switch (linkage) {
        case LLVMExternalLinkage: return "external";
        case LLVMAvailableExternallyLinkage: return "available_externally";
        case LLVMLinkOnceAnyLinkage: return "linkonce";
        case LLVMLinkOnceODRLinkage: return "linkonce_odr";
        case LLVMWeakAnyLinkage: return "weak";
        case LLVMWeakODRLinkage: return "weak_odr";
        case LLVMAppendingLinkage: return "appending";
        case LLVMInternalLinkage: return "internal";
        case LLVMPrivateLinkage: return "private";
        case LLVMExternalWeakLinkage: return "extern_weak";
        case LLVMCommonLinkage: return "common";
        default: return "unknown";
    }
}

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_function", ctx);

    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);

    // Function 1: void foo()
    LLVMTypeRef foo_ty = LLVMFunctionType(void_ty, nullptr, 0, 0);
    LLVMValueRef foo = LLVMAddFunction(mod, "foo", foo_ty);

    // Function 2: i32 bar(i32, i32)
    LLVMTypeRef bar_params[] = {i32, i32};
    LLVMTypeRef bar_ty = LLVMFunctionType(i32, bar_params, 2, 0);
    LLVMValueRef bar = LLVMAddFunction(mod, "bar", bar_ty);

    // Set parameter names
    LLVMValueRef bar_param0 = LLVMGetParam(bar, 0);
    LLVMValueRef bar_param1 = LLVMGetParam(bar, 1);
    LLVMSetValueName2(bar_param0, "x", 1);
    LLVMSetValueName2(bar_param1, "y", 1);

    // Function 3: i64 baz(ptr, i32, i64) with internal linkage
    // Internal linkage requires a body, so we add a simple one
    LLVMTypeRef baz_params[] = {ptr, i32, i64};
    LLVMTypeRef baz_ty = LLVMFunctionType(i64, baz_params, 3, 0);
    LLVMValueRef baz = LLVMAddFunction(mod, "baz", baz_ty);
    LLVMSetLinkage(baz, LLVMInternalLinkage);
    
    // Add a basic block with return to make it a valid definition
    LLVMBasicBlockRef baz_entry = LLVMAppendBasicBlockInContext(ctx, baz, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(builder, baz_entry);
    LLVMBuildRet(builder, LLVMConstInt(i64, 0, 0));
    LLVMDisposeBuilder(builder);

    // Function 4: varargs function - i32 printf(ptr, ...)
    LLVMTypeRef printf_params[] = {ptr};
    LLVMTypeRef printf_ty = LLVMFunctionType(i32, printf_params, 1, 1);
    LLVMValueRef printf_fn = LLVMAddFunction(mod, "printf", printf_ty);

    // Function 5: Function with fastcc calling convention
    LLVMTypeRef fastcc_params[] = {i32};
    LLVMTypeRef fastcc_ty = LLVMFunctionType(i32, fastcc_params, 1, 0);
    LLVMValueRef fastcc_fn = LLVMAddFunction(mod, "fastcc_func", fastcc_ty);
    LLVMSetFunctionCallConv(fastcc_fn, LLVMFastCallConv);

    // Function 6: Will be deleted
    LLVMTypeRef delete_ty = LLVMFunctionType(void_ty, nullptr, 0, 0);
    LLVMValueRef delete_fn = LLVMAddFunction(mod, "to_be_deleted", delete_ty);

    // Get function by name
    LLVMValueRef found_bar = LLVMGetNamedFunction(mod, "bar");

    // Count functions before deletion
    int count_before = 0;
    for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn; fn = LLVMGetNextFunction(fn)) {
        count_before++;
    }

    // Delete the function
    LLVMDeleteFunction(delete_fn);

    // Count functions after deletion
    int count_after = 0;
    for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn; fn = LLVMGetNextFunction(fn)) {
        count_after++;
    }

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
    printf("; Test: test_function\n");
    printf(";\n");

    // foo info
    size_t name_len;
    const char *foo_name = LLVMGetValueName2(foo, &name_len);
    printf("; Function 'foo':\n");
    printf(";   name: %s\n", foo_name);
    printf(";   param count: %u\n", LLVMCountParams(foo));
    printf(";   linkage: %s\n", linkage_name(LLVMGetLinkage(foo)));
    printf(";   calling conv: %u (C=0)\n", LLVMGetFunctionCallConv(foo));

    // bar info
    const char *bar_name = LLVMGetValueName2(bar, &name_len);
    printf(";\n");
    printf("; Function 'bar':\n");
    printf(";   name: %s\n", bar_name);
    printf(";   param count: %u\n", LLVMCountParams(bar));
    printf(";   found by name: %s\n", found_bar == bar ? "yes" : "no");

    // Get param names
    const char *p0_name = LLVMGetValueName2(bar_param0, &name_len);
    const char *p1_name = LLVMGetValueName2(bar_param1, &name_len);
    printf(";   param 0 name: %s\n", p0_name);
    printf(";   param 1 name: %s\n", p1_name);

    // baz info
    printf(";\n");
    printf("; Function 'baz':\n");
    printf(";   param count: %u\n", LLVMCountParams(baz));
    printf(";   linkage: %s\n", linkage_name(LLVMGetLinkage(baz)));

    // printf info
    printf(";\n");
    printf("; Function 'printf':\n");
    printf(";   param count: %u\n", LLVMCountParams(printf_fn));
    printf(";   is vararg: %s\n", LLVMIsFunctionVarArg(printf_ty) ? "yes" : "no");

    // fastcc info
    printf(";\n");
    printf("; Function 'fastcc_func':\n");
    printf(";   calling conv: %u (FastCall=8)\n", LLVMGetFunctionCallConv(fastcc_fn));

    // Function counts
    printf(";\n");
    printf("; Function count before deletion: %d\n", count_before);
    printf("; Function count after deletion: %d\n", count_after);

    // List all functions
    printf(";\n");
    printf("; All functions:\n");
    for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn; fn = LLVMGetNextFunction(fn)) {
        const char *fn_name = LLVMGetValueName2(fn, &name_len);
        printf(";   - %s\n", fn_name);
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
