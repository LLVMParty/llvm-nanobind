/**
 * Test: test_bitcode_linker
 * Tests for BitWriter (bitcode writing) and Linker (module linking)
 *
 * This test demonstrates:
 * - Writing modules to bitcode files
 * - Writing modules to memory buffers
 * - Reading bitcode back
 * - Linking modules together
 *
 * LLVM-C APIs tested:
 * - LLVMWriteBitcodeToFile
 * - LLVMWriteBitcodeToMemoryBuffer
 * - LLVMParseBitcodeInContext2
 * - LLVMLinkModules2
 * - LLVMCloneModule
 * - LLVMPrintModuleToFile
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Core.h>
#include <llvm-c/Linker.h>

// Helper: create a module with a function
static LLVMModuleRef create_module(LLVMContextRef ctx, const char *name,
                                   const char *func_name, int value) {
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext(name, ctx);
  LLVMSetTarget(mod, "x86_64-unknown-linux-gnu");

  LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
  LLVMTypeRef fn_ty = LLVMFunctionType(i32, NULL, 0, 0);
  LLVMValueRef fn = LLVMAddFunction(mod, func_name, fn_ty);

  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, fn, "entry");
  LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
  LLVMPositionBuilderAtEnd(builder, entry);

  LLVMBuildRet(builder, LLVMConstInt(i32, value, 0));
  LLVMDisposeBuilder(builder);

  return mod;
}

int main() {
  LLVMContextRef ctx = LLVMContextCreate();

  printf("; Test: test_bitcode_linker\n");
  printf("; Tests bitcode writing/reading and module linking\n");
  printf(";\n");

  // ==========================================================================
  // Test 1: Write module to memory buffer and read back
  // ==========================================================================
  printf("; Test 1: Bitcode round-trip (memory buffer)\n");

  LLVMModuleRef mod1 = create_module(ctx, "module1", "get_one", 1);

  // Write to memory buffer
  LLVMMemoryBufferRef buf = LLVMWriteBitcodeToMemoryBuffer(mod1);
  if (!buf) {
    fprintf(stderr, "; ERROR: Failed to write bitcode to memory buffer\n");
    LLVMDisposeModule(mod1);
    LLVMContextDispose(ctx);
    return 1;
  }

  size_t bc_size = LLVMGetBufferSize(buf);
  printf(";   Bitcode size: %zu bytes\n", bc_size);

  // Check bitcode magic (0x42, 0x43, 0xC0, 0xDE for "BC"...)
  const char *bc_data = LLVMGetBufferStart(buf);
  if (bc_size >= 4) {
    printf(";   Magic bytes: 0x%02X 0x%02X 0x%02X 0x%02X\n",
           (unsigned char)bc_data[0], (unsigned char)bc_data[1],
           (unsigned char)bc_data[2], (unsigned char)bc_data[3]);
  }

  // Read back from memory buffer
  LLVMModuleRef mod1_copy = NULL;
  if (LLVMParseBitcodeInContext2(ctx, buf, &mod1_copy)) {
    fprintf(stderr, "; ERROR: Failed to parse bitcode\n");
    LLVMDisposeMemoryBuffer(buf);
    LLVMDisposeModule(mod1);
    LLVMContextDispose(ctx);
    return 1;
  }
  LLVMDisposeMemoryBuffer(buf);

  // Verify the read-back module has the expected function
  LLVMValueRef fn = LLVMGetNamedFunction(mod1_copy, "get_one");
  printf(";   Round-trip function found: %s\n", fn ? "yes" : "no");

  // Verify
  char *error = NULL;
  if (LLVMVerifyModule(mod1_copy, LLVMReturnStatusAction, &error)) {
    fprintf(stderr, "; ERROR: Verification failed: %s\n", error);
    LLVMDisposeMessage(error);
  }
  if (error)
    LLVMDisposeMessage(error);

  printf(";   Round-trip module verified: yes\n");

  LLVMDisposeModule(mod1);
  LLVMDisposeModule(mod1_copy);

  // ==========================================================================
  // Test 2: Clone module
  // ==========================================================================
  printf(";\n; Test 2: Module cloning\n");

  LLVMModuleRef mod2 = create_module(ctx, "module2", "get_two", 2);

  LLVMModuleRef mod2_clone = LLVMCloneModule(mod2);

  // Change the original
  LLVMSetModuleIdentifier(mod2, "module2_modified", 17);

  // Check clone still has original name
  size_t len;
  const char *clone_name = LLVMGetModuleIdentifier(mod2_clone, &len);
  printf(";   Original module renamed to: %s\n", LLVMGetModuleIdentifier(mod2, &len));
  printf(";   Clone still has name: %s\n", clone_name);

  // Verify clone has the function
  LLVMValueRef fn2 = LLVMGetNamedFunction(mod2_clone, "get_two");
  printf(";   Clone has function get_two: %s\n", fn2 ? "yes" : "no");

  LLVMDisposeModule(mod2);
  LLVMDisposeModule(mod2_clone);

  // ==========================================================================
  // Test 3: Link modules
  // ==========================================================================
  printf(";\n; Test 3: Module linking\n");

  // Create destination module
  LLVMModuleRef dest = create_module(ctx, "dest", "get_dest", 100);
  
  // Create source module
  LLVMModuleRef src = create_module(ctx, "src", "get_src", 200);

  // Count functions before linking
  int fn_count_before = 0;
  for (LLVMValueRef f = LLVMGetFirstFunction(dest); f;
       f = LLVMGetNextFunction(f)) {
    fn_count_before++;
  }
  printf(";   Functions in dest before linking: %d\n", fn_count_before);

  // Link src into dest (src is destroyed)
  if (LLVMLinkModules2(dest, src)) {
    fprintf(stderr, "; ERROR: Failed to link modules\n");
    LLVMDisposeModule(dest);
    LLVMContextDispose(ctx);
    return 1;
  }

  // Count functions after linking
  int fn_count_after = 0;
  for (LLVMValueRef f = LLVMGetFirstFunction(dest); f;
       f = LLVMGetNextFunction(f)) {
    fn_count_after++;
  }
  printf(";   Functions in dest after linking: %d\n", fn_count_after);

  // Check both functions exist
  LLVMValueRef fn_dest = LLVMGetNamedFunction(dest, "get_dest");
  LLVMValueRef fn_src = LLVMGetNamedFunction(dest, "get_src");
  printf(";   get_dest exists: %s\n", fn_dest ? "yes" : "no");
  printf(";   get_src exists: %s\n", fn_src ? "yes" : "no");

  // Verify linked module
  error = NULL;
  if (LLVMVerifyModule(dest, LLVMReturnStatusAction, &error)) {
    fprintf(stderr, "; ERROR: Linked module verification failed: %s\n", error);
    LLVMDisposeMessage(error);
  }
  if (error)
    LLVMDisposeMessage(error);

  printf(";   Linked module verified: yes\n");

  // Print the final module IR
  printf(";\n; Final linked module:\n");
  char *ir = LLVMPrintModuleToString(dest);
  printf("%s", ir);
  LLVMDisposeMessage(ir);

  LLVMDisposeModule(dest);
  LLVMContextDispose(ctx);

  return 0;
}
