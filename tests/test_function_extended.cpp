/**
 * Test: test_function_extended
 * Tests for extended Function APIs including verification and intrinsics
 *
 * This test demonstrates:
 * - Function verification
 * - Intrinsic ID queries
 * - Personality functions
 * - GC names
 *
 * LLVM-C APIs tested:
 * - LLVMVerifyFunction
 * - LLVMGetIntrinsicID
 * - LLVMHasPersonalityFn, LLVMGetPersonalityFn, LLVMSetPersonalityFn
 * - LLVMGetGC, LLVMSetGC
 * - LLVMGetIntrinsicDeclaration
 * - LLVMLookupIntrinsicID
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

int main() {
  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("test_function_extended", ctx);
  LLVMSetTarget(mod, "x86_64-unknown-linux-gnu");

  printf("; Test: test_function_extended\n");
  printf("; Tests extended Function APIs\n");
  printf(";\n");

  LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
  LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);
  LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

  // ==========================================================================
  // Test 1: Function verification - valid function
  // ==========================================================================
  printf("; Test 1: Function verification (valid function)\n");

  LLVMTypeRef valid_ty = LLVMFunctionType(i32, NULL, 0, 0);
  LLVMValueRef valid_fn = LLVMAddFunction(mod, "valid_function", valid_ty);

  LLVMBasicBlockRef valid_entry = LLVMAppendBasicBlockInContext(ctx, valid_fn, "entry");
  LLVMPositionBuilderAtEnd(builder, valid_entry);
  LLVMBuildRet(builder, LLVMConstInt(i32, 42, 0));

  LLVMBool valid_failed = LLVMVerifyFunction(valid_fn, LLVMReturnStatusAction);
  printf(";   valid_function verification passed: %s\n", valid_failed ? "no" : "yes");

  // ==========================================================================
  // Test 2: Function verification - invalid function (no terminator)
  // ==========================================================================
  printf(";\n; Test 2: Function verification (invalid function)\n");

  LLVMTypeRef invalid_ty = LLVMFunctionType(i32, NULL, 0, 0);
  LLVMValueRef invalid_fn = LLVMAddFunction(mod, "invalid_function", invalid_ty);

  LLVMBasicBlockRef invalid_entry = LLVMAppendBasicBlockInContext(ctx, invalid_fn, "entry");
  LLVMPositionBuilderAtEnd(builder, invalid_entry);
  // Deliberately not adding a terminator - this makes the function invalid
  // We add an alloca just to have something in the block
  LLVMBuildAlloca(builder, i32, "x");

  LLVMBool invalid_failed = LLVMVerifyFunction(invalid_fn, LLVMReturnStatusAction);
  printf(";   invalid_function verification failed (expected): %s\n", 
         invalid_failed ? "yes" : "no");

  // Now fix it by adding a return
  LLVMBuildRet(builder, LLVMConstInt(i32, 0, 0));
  invalid_failed = LLVMVerifyFunction(invalid_fn, LLVMReturnStatusAction);
  printf(";   After adding return, verification passed: %s\n",
         invalid_failed ? "no" : "yes");

  // ==========================================================================
  // Test 3: Intrinsic IDs
  // ==========================================================================
  printf(";\n; Test 3: Intrinsic IDs\n");

  // User functions have intrinsic ID 0
  unsigned valid_id = LLVMGetIntrinsicID(valid_fn);
  printf(";   valid_function intrinsic ID: %u (0 = not intrinsic)\n", valid_id);
  printf(";   valid_function is_intrinsic: %s\n", valid_id != 0 ? "yes" : "no");

  // Look up an intrinsic ID by name
  unsigned memcpy_id = LLVMLookupIntrinsicID("llvm.memcpy", 11);
  printf(";   llvm.memcpy intrinsic ID: %u\n", memcpy_id);

  // Get intrinsic declaration
  if (memcpy_id != 0) {
    LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);
    LLVMTypeRef param_types[] = {ptr, ptr, i64};
    LLVMValueRef memcpy_decl = LLVMGetIntrinsicDeclaration(mod, memcpy_id, 
                                                            param_types, 3);
    unsigned memcpy_decl_id = LLVMGetIntrinsicID(memcpy_decl);
    printf(";   memcpy declaration is_intrinsic: %s\n", 
           memcpy_decl_id != 0 ? "yes" : "no");
    
    size_t len;
    const char *name = LLVMGetValueName2(memcpy_decl, &len);
    printf(";   memcpy declaration name: %s\n", name);
  }

  // ==========================================================================
  // Test 4: Personality function
  // ==========================================================================
  printf(";\n; Test 4: Personality function\n");

  // Create a personality function (like __gxx_personality_v0)
  LLVMTypeRef personality_params[] = {};
  LLVMTypeRef personality_ty = LLVMFunctionType(i32, personality_params, 0, 1);
  LLVMValueRef personality_fn = LLVMAddFunction(mod, "__gxx_personality_v0", personality_ty);

  // Create a function that uses the personality
  LLVMTypeRef with_personality_ty = LLVMFunctionType(void_ty, NULL, 0, 0);
  LLVMValueRef with_personality_fn = LLVMAddFunction(mod, "with_personality", with_personality_ty);

  printf(";   Before setting personality:\n");
  printf(";     has_personality_fn: %s\n", 
         LLVMHasPersonalityFn(with_personality_fn) ? "yes" : "no");

  LLVMSetPersonalityFn(with_personality_fn, personality_fn);

  printf(";   After setting personality:\n");
  printf(";     has_personality_fn: %s\n",
         LLVMHasPersonalityFn(with_personality_fn) ? "yes" : "no");

  LLVMValueRef got_personality = LLVMGetPersonalityFn(with_personality_fn);
  size_t plen;
  printf(";     personality fn name: %s\n", LLVMGetValueName2(got_personality, &plen));

  // Add entry block to with_personality function
  LLVMBasicBlockRef wp_entry = LLVMAppendBasicBlockInContext(ctx, with_personality_fn, "entry");
  LLVMPositionBuilderAtEnd(builder, wp_entry);
  LLVMBuildRetVoid(builder);

  // ==========================================================================
  // Test 5: GC name
  // ==========================================================================
  printf(";\n; Test 5: GC name\n");

  LLVMTypeRef gc_fn_ty = LLVMFunctionType(void_ty, NULL, 0, 0);
  LLVMValueRef gc_fn = LLVMAddFunction(mod, "gc_function", gc_fn_ty);

  printf(";   Before setting GC:\n");
  const char *gc_before = LLVMGetGC(gc_fn);
  printf(";     GC name: %s\n", gc_before ? gc_before : "(none)");

  LLVMSetGC(gc_fn, "statepoint-example");

  printf(";   After setting GC:\n");
  const char *gc_after = LLVMGetGC(gc_fn);
  printf(";     GC name: %s\n", gc_after ? gc_after : "(none)");

  // Add entry block
  LLVMBasicBlockRef gc_entry = LLVMAppendBasicBlockInContext(ctx, gc_fn, "entry");
  LLVMPositionBuilderAtEnd(builder, gc_entry);
  LLVMBuildRetVoid(builder);

  // ==========================================================================
  // Print module
  // ==========================================================================
  printf(";\n; Module IR:\n");
  char *ir = LLVMPrintModuleToString(mod);
  printf("%s", ir);
  LLVMDisposeMessage(ir);

  LLVMDisposeBuilder(builder);
  LLVMDisposeModule(mod);
  LLVMContextDispose(ctx);

  return 0;
}
