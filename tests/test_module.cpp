/**
 * Test: test_module
 * Tests LLVM Module creation and properties
 *
 * LLVM-C APIs covered:
 * - LLVMModuleCreateWithNameInContext()
 * - LLVMGetModuleIdentifier() / LLVMSetModuleIdentifier()
 * - LLVMGetSourceFileName() / LLVMSetSourceFileName()
 * - LLVMGetDataLayoutStr() / LLVMSetDataLayout()
 * - LLVMGetTarget() / LLVMSetTarget()
 * - LLVMPrintModuleToString()
 * - LLVMCloneModule()
 * - LLVMDisposeModule()
 */

#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <cstdio>
#include <cstring>

int main() {
    LLVMContextRef ctx = LLVMContextCreate();

    // Create module
    LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_module", ctx);

    // Get initial module identifier
    size_t id_len;
    const char *initial_id = LLVMGetModuleIdentifier(mod, &id_len);

    // Set new module identifier
    LLVMSetModuleIdentifier(mod, "renamed_module", strlen("renamed_module"));
    const char *new_id = LLVMGetModuleIdentifier(mod, &id_len);

    // Get/set source filename
    size_t src_len;
    const char *initial_src = LLVMGetSourceFileName(mod, &src_len);
    LLVMSetSourceFileName(mod, "test_source.c", strlen("test_source.c"));
    const char *new_src = LLVMGetSourceFileName(mod, &src_len);

    // Get/set data layout
    const char *initial_layout = LLVMGetDataLayoutStr(mod);
    LLVMSetDataLayout(mod, "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128");
    const char *new_layout = LLVMGetDataLayoutStr(mod);

    // Get/set target triple
    const char *initial_target = LLVMGetTarget(mod);
    LLVMSetTarget(mod, "x86_64-unknown-linux-gnu");
    const char *new_target = LLVMGetTarget(mod);

    // Clone module
    LLVMModuleRef cloned = LLVMCloneModule(mod);
    const char *cloned_id = LLVMGetModuleIdentifier(cloned, &id_len);

    // Verify module
    char *error = nullptr;
    if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &error)) {
        fprintf(stderr, "; Verification failed: %s\n", error);
        LLVMDisposeMessage(error);
        LLVMDisposeModule(cloned);
        LLVMDisposeModule(mod);
        LLVMContextDispose(ctx);
        return 1;
    }
    LLVMDisposeMessage(error);

    // Print diagnostic comments
    printf("; Test: test_module\n");
    printf("; Initial module ID: %s\n", initial_id);
    printf("; New module ID: %s\n", new_id);
    printf("; Initial source filename: %s\n", initial_src);
    printf("; New source filename: %s\n", new_src);
    printf("; Initial data layout: %s\n", initial_layout[0] ? initial_layout : "(empty)");
    printf("; New data layout: %s\n", new_layout);
    printf("; Initial target: %s\n", initial_target[0] ? initial_target : "(empty)");
    printf("; New target: %s\n", new_target);
    printf("; Cloned module ID: %s\n", cloned_id);
    printf("\n");

    // Print module IR
    char *ir = LLVMPrintModuleToString(mod);
    printf("%s", ir);
    LLVMDisposeMessage(ir);

    // Cleanup
    LLVMDisposeModule(cloned);
    LLVMDisposeModule(mod);
    LLVMContextDispose(ctx);

    return 0;
}
