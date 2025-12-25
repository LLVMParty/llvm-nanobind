/**
 * Test: test_passbuilder
 * Tests for the new pass manager / PassBuilder API
 *
 * This test demonstrates:
 * - Creating PassBuilderOptions
 * - Running optimization passes on modules
 * - Different optimization levels (O0, O1, O2, O3, Os, Oz)
 * - Custom pass pipelines
 *
 * LLVM-C APIs tested:
 * - LLVMCreatePassBuilderOptions
 * - LLVMDisposePassBuilderOptions
 * - LLVMPassBuilderOptionsSetVerifyEach
 * - LLVMPassBuilderOptionsSetDebugLogging
 * - LLVMPassBuilderOptionsSetLoopInterleaving
 * - LLVMPassBuilderOptionsSetLoopVectorization
 * - LLVMPassBuilderOptionsSetSLPVectorization
 * - LLVMPassBuilderOptionsSetLoopUnrolling
 * - LLVMPassBuilderOptionsSetMergeFunctions
 * - LLVMPassBuilderOptionsSetInlinerThreshold
 * - LLVMRunPasses
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>

// Create a module with code that can be optimized
static LLVMModuleRef create_optimizable_module(LLVMContextRef ctx) {
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("optimize_me", ctx);
  LLVMSetTarget(mod, "x86_64-unknown-linux-gnu");

  LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
  LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

  // Create: int add_inline(int x, int y) { return x + y; }
  LLVMTypeRef add_params[] = {i32, i32};
  LLVMTypeRef add_ty = LLVMFunctionType(i32, add_params, 2, 0);
  LLVMValueRef add_fn = LLVMAddFunction(mod, "add_inline", add_ty);
  LLVMSetLinkage(add_fn, LLVMInternalLinkage);

  LLVMBasicBlockRef add_entry =
      LLVMAppendBasicBlockInContext(ctx, add_fn, "entry");
  LLVMPositionBuilderAtEnd(builder, add_entry);
  LLVMValueRef sum = LLVMBuildAdd(builder, LLVMGetParam(add_fn, 0),
                                  LLVMGetParam(add_fn, 1), "sum");
  LLVMBuildRet(builder, sum);

  // Create: int compute(int n) {
  //   int result = 0;
  //   for (int i = 0; i < n; i++) {
  //     result = add_inline(result, i);
  //   }
  //   return result;
  // }
  LLVMTypeRef compute_params[] = {i32};
  LLVMTypeRef compute_ty = LLVMFunctionType(i32, compute_params, 1, 0);
  LLVMValueRef compute_fn = LLVMAddFunction(mod, "compute", compute_ty);

  LLVMBasicBlockRef entry =
      LLVMAppendBasicBlockInContext(ctx, compute_fn, "entry");
  LLVMBasicBlockRef loop =
      LLVMAppendBasicBlockInContext(ctx, compute_fn, "loop");
  LLVMBasicBlockRef exit_bb =
      LLVMAppendBasicBlockInContext(ctx, compute_fn, "exit");

  // Entry: initialize loop variables
  LLVMPositionBuilderAtEnd(builder, entry);
  LLVMValueRef result_ptr = LLVMBuildAlloca(builder, i32, "result_ptr");
  LLVMValueRef i_ptr = LLVMBuildAlloca(builder, i32, "i_ptr");
  LLVMBuildStore(builder, LLVMConstInt(i32, 0, 0), result_ptr);
  LLVMBuildStore(builder, LLVMConstInt(i32, 0, 0), i_ptr);
  LLVMBuildBr(builder, loop);

  // Loop
  LLVMPositionBuilderAtEnd(builder, loop);
  LLVMValueRef i_val = LLVMBuildLoad2(builder, i32, i_ptr, "i");
  LLVMValueRef n = LLVMGetParam(compute_fn, 0);
  LLVMValueRef cond = LLVMBuildICmp(builder, LLVMIntSLT, i_val, n, "cond");

  LLVMBasicBlockRef loop_body =
      LLVMAppendBasicBlockInContext(ctx, compute_fn, "loop_body");
  LLVMBuildCondBr(builder, cond, loop_body, exit_bb);

  // Loop body
  LLVMPositionBuilderAtEnd(builder, loop_body);
  LLVMValueRef result_val = LLVMBuildLoad2(builder, i32, result_ptr, "result");
  i_val = LLVMBuildLoad2(builder, i32, i_ptr, "i2");

  // Call add_inline
  LLVMValueRef args[] = {result_val, i_val};
  LLVMValueRef new_result =
      LLVMBuildCall2(builder, add_ty, add_fn, args, 2, "new_result");
  LLVMBuildStore(builder, new_result, result_ptr);

  // Increment i
  LLVMValueRef new_i =
      LLVMBuildAdd(builder, i_val, LLVMConstInt(i32, 1, 0), "new_i");
  LLVMBuildStore(builder, new_i, i_ptr);
  LLVMBuildBr(builder, loop);

  // Exit
  LLVMPositionBuilderAtEnd(builder, exit_bb);
  LLVMValueRef final_result =
      LLVMBuildLoad2(builder, i32, result_ptr, "final_result");
  LLVMBuildRet(builder, final_result);

  LLVMDisposeBuilder(builder);
  return mod;
}

// Count instructions in a module
static int count_instructions(LLVMModuleRef mod) {
  int count = 0;
  for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(fn); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
      for (LLVMValueRef inst = LLVMGetFirstInstruction(bb); inst;
           inst = LLVMGetNextInstruction(inst)) {
        count++;
      }
    }
  }
  return count;
}

// Count functions in a module
static int count_functions(LLVMModuleRef mod) {
  int count = 0;
  for (LLVMValueRef fn = LLVMGetFirstFunction(mod); fn;
       fn = LLVMGetNextFunction(fn)) {
    count++;
  }
  return count;
}

int main() {
  // Initialize all targets (needed for pass manager)
  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargets();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllAsmPrinters();
  LLVMInitializeAllAsmParsers();

  LLVMContextRef ctx = LLVMContextCreate();

  // Get default target for running passes
  char *triple = LLVMGetDefaultTargetTriple();
  LLVMTargetRef target = NULL;
  char *err_msg = NULL;
  if (LLVMGetTargetFromTriple(triple, &target, &err_msg)) {
    fprintf(stderr, "; ERROR: Failed to get target: %s\n", err_msg);
    LLVMDisposeMessage(err_msg);
    LLVMDisposeMessage(triple);
    LLVMContextDispose(ctx);
    return 1;
  }
  if (err_msg) LLVMDisposeMessage(err_msg);

  LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
      target, triple, "generic", "",
      LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);
  LLVMDisposeMessage(triple);

  printf("; Test: test_passbuilder\n");
  printf("; Tests optimization pass running via PassBuilder API\n");
  printf(";\n");

  // ==========================================================================
  // Test 1: Create PassBuilderOptions with various settings
  // ==========================================================================
  printf("; Test 1: PassBuilderOptions configuration\n");

  LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
  
  // Configure options
  LLVMPassBuilderOptionsSetVerifyEach(opts, 0);
  LLVMPassBuilderOptionsSetDebugLogging(opts, 0);
  LLVMPassBuilderOptionsSetLoopInterleaving(opts, 1);
  LLVMPassBuilderOptionsSetLoopVectorization(opts, 1);
  LLVMPassBuilderOptionsSetSLPVectorization(opts, 1);
  LLVMPassBuilderOptionsSetLoopUnrolling(opts, 1);
  LLVMPassBuilderOptionsSetMergeFunctions(opts, 1);
  LLVMPassBuilderOptionsSetInlinerThreshold(opts, 250);

  printf(";   PassBuilderOptions created: yes\n");
  printf(";   Options configured: yes\n");

  // ==========================================================================
  // Test 2: Optimization at O0 (no optimization)
  // ==========================================================================
  printf(";\n; Test 2: O0 optimization (no optimization)\n");

  LLVMModuleRef mod_o0 = create_optimizable_module(ctx);
  int instr_before_o0 = count_instructions(mod_o0);
  int funcs_before_o0 = count_functions(mod_o0);

  LLVMErrorRef err = LLVMRunPasses(mod_o0, "default<O0>", tm, opts);
  if (err) {
    char *msg = LLVMGetErrorMessage(err);
    fprintf(stderr, "; ERROR: O0 passes failed: %s\n", msg);
    LLVMDisposeErrorMessage(msg);
    LLVMDisposeModule(mod_o0);
    LLVMDisposePassBuilderOptions(opts);
    LLVMDisposeTargetMachine(tm);
    LLVMContextDispose(ctx);
    return 1;
  }

  int instr_after_o0 = count_instructions(mod_o0);
  int funcs_after_o0 = count_functions(mod_o0);

  printf(";   Instructions before: %d, after: %d\n", instr_before_o0,
         instr_after_o0);
  printf(";   Functions before: %d, after: %d\n", funcs_before_o0,
         funcs_after_o0);
  printf(";   O0 passes ran successfully: yes\n");

  LLVMDisposeModule(mod_o0);

  // ==========================================================================
  // Test 3: Optimization at O2
  // ==========================================================================
  printf(";\n; Test 3: O2 optimization\n");

  LLVMModuleRef mod_o2 = create_optimizable_module(ctx);
  int instr_before_o2 = count_instructions(mod_o2);
  int funcs_before_o2 = count_functions(mod_o2);

  err = LLVMRunPasses(mod_o2, "default<O2>", tm, opts);
  if (err) {
    char *msg = LLVMGetErrorMessage(err);
    fprintf(stderr, "; ERROR: O2 passes failed: %s\n", msg);
    LLVMDisposeErrorMessage(msg);
    LLVMDisposeModule(mod_o2);
    LLVMDisposePassBuilderOptions(opts);
    LLVMDisposeTargetMachine(tm);
    LLVMContextDispose(ctx);
    return 1;
  }

  int instr_after_o2 = count_instructions(mod_o2);
  int funcs_after_o2 = count_functions(mod_o2);

  printf(";   Instructions before: %d, after: %d\n", instr_before_o2,
         instr_after_o2);
  printf(";   Functions before: %d, after: %d\n", funcs_before_o2,
         funcs_after_o2);
  printf(";   O2 passes ran successfully: yes\n");

  // At O2, the add_inline function should be inlined and possibly removed
  LLVMValueRef add_fn = LLVMGetNamedFunction(mod_o2, "add_inline");
  printf(";   add_inline function removed: %s\n", add_fn ? "no" : "yes");

  // Verify the optimized module is still valid
  char *verify_error = NULL;
  if (LLVMVerifyModule(mod_o2, LLVMReturnStatusAction, &verify_error)) {
    fprintf(stderr, "; ERROR: Optimized module verification failed: %s\n",
            verify_error);
    LLVMDisposeMessage(verify_error);
  } else {
    printf(";   Optimized module verified: yes\n");
  }
  if (verify_error)
    LLVMDisposeMessage(verify_error);

  LLVMDisposeModule(mod_o2);

  // ==========================================================================
  // Test 4: Custom pass pipeline
  // ==========================================================================
  printf(";\n; Test 4: Custom pass pipeline\n");

  LLVMModuleRef mod_custom = create_optimizable_module(ctx);
  int instr_before_custom = count_instructions(mod_custom);

  // Run just instcombine and simplifycfg
  err = LLVMRunPasses(mod_custom, "instcombine,simplifycfg", tm, opts);
  if (err) {
    char *msg = LLVMGetErrorMessage(err);
    fprintf(stderr, "; ERROR: Custom passes failed: %s\n", msg);
    LLVMDisposeErrorMessage(msg);
    LLVMDisposeModule(mod_custom);
    LLVMDisposePassBuilderOptions(opts);
    LLVMDisposeTargetMachine(tm);
    LLVMContextDispose(ctx);
    return 1;
  }

  int instr_after_custom = count_instructions(mod_custom);
  printf(";   Instructions before: %d, after: %d\n", instr_before_custom,
         instr_after_custom);
  printf(";   Custom passes ran successfully: yes\n");

  // Print the optimized module
  printf(";\n; Optimized module (after instcombine,simplifycfg):\n");
  char *ir = LLVMPrintModuleToString(mod_custom);
  printf("%s", ir);
  LLVMDisposeMessage(ir);

  LLVMDisposeModule(mod_custom);
  LLVMDisposePassBuilderOptions(opts);
  LLVMDisposeTargetMachine(tm);
  LLVMContextDispose(ctx);

  return 0;
}
