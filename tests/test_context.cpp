/**
 * Test: test_context
 * Tests LLVM Context creation and properties
 *
 * LLVM-C APIs covered:
 * - LLVMContextCreate() / LLVMContextDispose()
 * - LLVMGetGlobalContext()
 * - LLVMContextShouldDiscardValueNames() / LLVMContextSetDiscardValueNames()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>

int main() {
    // Create a new context
    LLVMContextRef ctx = LLVMContextCreate();

    // Get global context for comparison
    LLVMContextRef global_ctx = LLVMGetGlobalContext();

    // Test discard value names property
    LLVMBool discard_before = LLVMContextShouldDiscardValueNames(ctx);
    LLVMContextSetDiscardValueNames(ctx, 1);
    LLVMBool discard_after = LLVMContextShouldDiscardValueNames(ctx);

    // Reset it back for the module we'll create
    LLVMContextSetDiscardValueNames(ctx, 0);

    // Create a minimal module to have valid IR output
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_context", ctx);

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
    printf("; Test: test_context\n");
    printf("; Context created: %s\n", ctx != nullptr ? "yes" : "no");
    printf("; Global context same as created: %s\n", ctx == global_ctx ? "yes" : "no");
    printf("; Discard value names (before): %s\n", discard_before ? "true" : "false");
    printf("; Discard value names (after set to true): %s\n", discard_after ? "true" : "false");
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
