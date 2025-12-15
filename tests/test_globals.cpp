/**
 * Test: test_globals
 * Tests LLVM global variable operations
 *
 * LLVM-C APIs covered:
 * - LLVMAddGlobal(), LLVMAddGlobalInAddressSpace()
 * - LLVMGetNamedGlobal()
 * - LLVMSetInitializer(), LLVMGetInitializer()
 * - LLVMSetGlobalConstant(), LLVMIsGlobalConstant()
 * - LLVMSetLinkage(), LLVMGetLinkage()
 * - LLVMSetVisibility(), LLVMGetVisibility()
 * - LLVMSetAlignment(), LLVMGetAlignment()
 * - LLVMSetSection(), LLVMGetSection()
 * - LLVMGetFirstGlobal(), LLVMGetNextGlobal(), LLVMGetLastGlobal()
 * - LLVMDeleteGlobal()
 * - LLVMSetThreadLocal(), LLVMIsThreadLocal()
 * - LLVMSetExternallyInitialized(), LLVMIsExternallyInitialized()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

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

const char* visibility_name(LLVMVisibility vis) {
    switch (vis) {
        case LLVMDefaultVisibility: return "default";
        case LLVMHiddenVisibility: return "hidden";
        case LLVMProtectedVisibility: return "protected";
        default: return "unknown";
    }
}

int main() {
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_globals", ctx);

    LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx);
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);

    // ==========================================
    // Basic global variable
    // ==========================================
    LLVMValueRef global_counter = LLVMAddGlobal(mod, i32, "counter");
    LLVMSetInitializer(global_counter, LLVMConstInt(i32, 0, 0));

    // ==========================================
    // Constant global
    // ==========================================
    LLVMValueRef global_const = LLVMAddGlobal(mod, i32, "magic_number");
    LLVMSetInitializer(global_const, LLVMConstInt(i32, 42, 0));
    LLVMSetGlobalConstant(global_const, 1);

    // ==========================================
    // Global with alignment
    // ==========================================
    LLVMValueRef global_aligned = LLVMAddGlobal(mod, i64, "aligned_var");
    LLVMSetInitializer(global_aligned, LLVMConstInt(i64, 0, 0));
    LLVMSetAlignment(global_aligned, 16);

    // ==========================================
    // Global with linkage
    // ==========================================
    LLVMValueRef global_internal = LLVMAddGlobal(mod, i32, "internal_var");
    LLVMSetInitializer(global_internal, LLVMConstInt(i32, 100, 0));
    LLVMSetLinkage(global_internal, LLVMInternalLinkage);

    LLVMValueRef global_private = LLVMAddGlobal(mod, i32, "private_var");
    LLVMSetInitializer(global_private, LLVMConstInt(i32, 200, 0));
    LLVMSetLinkage(global_private, LLVMPrivateLinkage);

    LLVMValueRef global_weak = LLVMAddGlobal(mod, i32, "weak_var");
    LLVMSetInitializer(global_weak, LLVMConstInt(i32, 300, 0));
    LLVMSetLinkage(global_weak, LLVMWeakAnyLinkage);

    // ==========================================
    // Global with visibility
    // ==========================================
    LLVMValueRef global_hidden = LLVMAddGlobal(mod, i32, "hidden_var");
    LLVMSetInitializer(global_hidden, LLVMConstInt(i32, 0, 0));
    LLVMSetVisibility(global_hidden, LLVMHiddenVisibility);

    // ==========================================
    // Global with section
    // ==========================================
    LLVMValueRef global_section = LLVMAddGlobal(mod, i32, "section_var");
    LLVMSetInitializer(global_section, LLVMConstInt(i32, 0, 0));
    LLVMSetSection(global_section, ".mydata");

    // ==========================================
    // Thread-local global
    // ==========================================
    LLVMValueRef global_tls = LLVMAddGlobal(mod, i32, "tls_var");
    LLVMSetInitializer(global_tls, LLVMConstInt(i32, 0, 0));
    LLVMSetThreadLocal(global_tls, 1);

    // ==========================================
    // Externally initialized global (no initializer)
    // ==========================================
    LLVMValueRef global_extern = LLVMAddGlobal(mod, i32, "extern_var");
    LLVMSetExternallyInitialized(global_extern, 1);

    // ==========================================
    // Global in address space
    // ==========================================
    LLVMValueRef global_addrspace = LLVMAddGlobalInAddressSpace(mod, i32, "addrspace_var", 1);
    LLVMSetInitializer(global_addrspace, LLVMConstInt(i32, 0, 0));

    // ==========================================
    // Global to be deleted
    // ==========================================
    LLVMValueRef global_delete = LLVMAddGlobal(mod, i32, "to_be_deleted");
    LLVMSetInitializer(global_delete, LLVMConstInt(i32, 999, 0));

    // Count globals before deletion
    int count_before = 0;
    for (LLVMValueRef g = LLVMGetFirstGlobal(mod); g; g = LLVMGetNextGlobal(g)) {
        count_before++;
    }

    // Delete the global
    LLVMDeleteGlobal(global_delete);

    // Count globals after deletion
    int count_after = 0;
    for (LLVMValueRef g = LLVMGetFirstGlobal(mod); g; g = LLVMGetNextGlobal(g)) {
        count_after++;
    }

    // Get global by name
    LLVMValueRef found_counter = LLVMGetNamedGlobal(mod, "counter");
    LLVMValueRef found_nonexist = LLVMGetNamedGlobal(mod, "nonexistent");

    // Get initializer
    LLVMValueRef init = LLVMGetInitializer(global_const);

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
    printf("; Test: test_globals\n");
    printf(";\n");
    printf("; Global variable properties:\n");
    printf(";\n");
    printf("; counter:\n");
    printf(";   is constant: %s\n", LLVMIsGlobalConstant(global_counter) ? "yes" : "no");
    printf(";   linkage: %s\n", linkage_name(LLVMGetLinkage(global_counter)));
    printf(";\n");
    printf("; magic_number:\n");
    printf(";   is constant: %s\n", LLVMIsGlobalConstant(global_const) ? "yes" : "no");
    printf(";   has initializer: %s\n", init != nullptr ? "yes" : "no");
    printf(";   initializer value: %llu\n", LLVMConstIntGetZExtValue(init));
    printf(";\n");
    printf("; aligned_var:\n");
    printf(";   alignment: %u\n", LLVMGetAlignment(global_aligned));
    printf(";\n");
    printf("; internal_var:\n");
    printf(";   linkage: %s\n", linkage_name(LLVMGetLinkage(global_internal)));
    printf(";\n");
    printf("; hidden_var:\n");
    printf(";   visibility: %s\n", visibility_name(LLVMGetVisibility(global_hidden)));
    printf(";\n");
    printf("; section_var:\n");
    printf(";   section: %s\n", LLVMGetSection(global_section));
    printf(";\n");
    printf("; tls_var:\n");
    printf(";   is thread local: %s\n", LLVMIsThreadLocal(global_tls) ? "yes" : "no");
    printf(";\n");
    printf("; extern_var:\n");
    printf(";   is externally initialized: %s\n", LLVMIsExternallyInitialized(global_extern) ? "yes" : "no");
    printf(";\n");
    printf("; Lookup tests:\n");
    printf(";   found 'counter': %s\n", found_counter != nullptr ? "yes" : "no");
    printf(";   found 'nonexistent': %s\n", found_nonexist != nullptr ? "yes" : "no");
    printf(";\n");
    printf("; Global counts:\n");
    printf(";   before deletion: %d\n", count_before);
    printf(";   after deletion: %d\n", count_after);
    printf(";\n");
    printf("; All globals:\n");
    for (LLVMValueRef g = LLVMGetFirstGlobal(mod); g; g = LLVMGetNextGlobal(g)) {
        size_t len;
        const char *name = LLVMGetValueName2(g, &len);
        printf(";   - %s\n", name);
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
