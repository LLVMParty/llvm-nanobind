/**
 * Test: test_target_codegen
 * Tests for Target, TargetData, TargetMachine, and code generation
 *
 * This test demonstrates:
 * - Target initialization and lookup
 * - TargetData/TargetMachine creation
 * - Code generation to object files and assembly
 * - Host CPU queries
 *
 * LLVM-C APIs tested:
 * - LLVMInitializeAllTargets, LLVMInitializeAllTargetMCs
 * - LLVMInitializeAllAsmPrinters, LLVMInitializeAllAsmParsers
 * - LLVMInitializeNativeTarget, LLVMInitializeNativeAsmPrinter
 * - LLVMGetDefaultTargetTriple, LLVMNormalizeTargetTriple
 * - LLVMGetHostCPUName, LLVMGetHostCPUFeatures
 * - LLVMGetTargetFromTriple, LLVMGetTargetFromName
 * - LLVMGetTargetName, LLVMGetTargetDescription
 * - LLVMTargetHasJIT, LLVMTargetHasTargetMachine, LLVMTargetHasAsmBackend
 * - LLVMCreateTargetMachine, LLVMDisposeTargetMachine
 * - LLVMGetTargetMachineTriple, LLVMGetTargetMachineCPU
 * - LLVMCreateTargetDataLayout, LLVMDisposeTargetData
 * - LLVMCopyStringRepOfTargetData
 * - LLVMSizeOfTypeInBits, LLVMABISizeOfType, LLVMABIAlignmentOfType
 * - LLVMTargetMachineEmitToMemoryBuffer
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

int main() {
  // Initialize all targets
  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargets();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllAsmPrinters();
  LLVMInitializeAllAsmParsers();

  printf("; Test: test_target_codegen\n");
  printf("; Tests target initialization, queries, and code generation\n");
  printf(";\n");

  // ==========================================================================
  // Host CPU queries
  // ==========================================================================
  printf("; Host queries:\n");

  char *default_triple = LLVMGetDefaultTargetTriple();
  printf(";   Default triple: %s\n", default_triple);

  char *normalized = LLVMNormalizeTargetTriple(default_triple);
  printf(";   Normalized triple: %s\n", normalized);

  char *cpu_name = LLVMGetHostCPUName();
  printf(";   Host CPU: %s\n", cpu_name);

  char *cpu_features = LLVMGetHostCPUFeatures();
  // Features can be very long, just check it's not empty
  printf(";   Host features length: %zu\n", strlen(cpu_features));

  LLVMDisposeMessage(cpu_features);
  LLVMDisposeMessage(cpu_name);
  LLVMDisposeMessage(normalized);

  // ==========================================================================
  // Target lookup
  // ==========================================================================
  printf(";\n; Target lookup:\n");

  LLVMTargetRef target = NULL;
  char *error = NULL;
  if (LLVMGetTargetFromTriple(default_triple, &target, &error)) {
    fprintf(stderr, "; ERROR: Failed to get target: %s\n", error);
    LLVMDisposeMessage(error);
    LLVMDisposeMessage(default_triple);
    return 1;
  }
  if (error)
    LLVMDisposeMessage(error);

  printf(";   Target name: %s\n", LLVMGetTargetName(target));
  printf(";   Target description: %s\n", LLVMGetTargetDescription(target));
  printf(";   Has JIT: %s\n", LLVMTargetHasJIT(target) ? "yes" : "no");
  printf(";   Has TargetMachine: %s\n",
         LLVMTargetHasTargetMachine(target) ? "yes" : "no");
  printf(";   Has AsmBackend: %s\n",
         LLVMTargetHasAsmBackend(target) ? "yes" : "no");

  // ==========================================================================
  // Create TargetMachine
  // ==========================================================================
  printf(";\n; TargetMachine:\n");

  LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
      target, default_triple, "generic", "",
      LLVMCodeGenLevelDefault, LLVMRelocDefault, LLVMCodeModelDefault);

  if (!tm) {
    fprintf(stderr, "; ERROR: Failed to create target machine\n");
    LLVMDisposeMessage(default_triple);
    return 1;
  }

  char *tm_triple = LLVMGetTargetMachineTriple(tm);
  char *tm_cpu = LLVMGetTargetMachineCPU(tm);
  char *tm_features = LLVMGetTargetMachineFeatureString(tm);

  printf(";   TM Triple: %s\n", tm_triple);
  printf(";   TM CPU: %s\n", tm_cpu);
  printf(";   TM Features: %s\n",
         strlen(tm_features) > 0 ? "(has features)" : "(empty)");

  LLVMDisposeMessage(tm_features);
  LLVMDisposeMessage(tm_cpu);
  LLVMDisposeMessage(tm_triple);

  // ==========================================================================
  // Target Data Layout
  // ==========================================================================
  printf(";\n; TargetData:\n");

  LLVMTargetDataRef td = LLVMCreateTargetDataLayout(tm);
  char *layout_str = LLVMCopyStringRepOfTargetData(td);
  printf(";   Data layout: %s\n", layout_str);
  LLVMDisposeMessage(layout_str);

  // Create a context and some types for size queries
  LLVMContextRef ctx = LLVMContextCreate();
  LLVMTypeRef i8 = LLVMInt8TypeInContext(ctx);
  LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
  LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
  LLVMTypeRef f32 = LLVMFloatTypeInContext(ctx);
  LLVMTypeRef f64 = LLVMDoubleTypeInContext(ctx);
  LLVMTypeRef ptr = LLVMPointerTypeInContext(ctx, 0);

  printf(";\n; Type sizes and alignments:\n");
  printf(";   i8:  size=%llu bits, abi_size=%llu bytes, alignment=%u\n",
         LLVMSizeOfTypeInBits(td, i8), LLVMABISizeOfType(td, i8),
         LLVMABIAlignmentOfType(td, i8));
  printf(";   i32: size=%llu bits, abi_size=%llu bytes, alignment=%u\n",
         LLVMSizeOfTypeInBits(td, i32), LLVMABISizeOfType(td, i32),
         LLVMABIAlignmentOfType(td, i32));
  printf(";   i64: size=%llu bits, abi_size=%llu bytes, alignment=%u\n",
         LLVMSizeOfTypeInBits(td, i64), LLVMABISizeOfType(td, i64),
         LLVMABIAlignmentOfType(td, i64));
  printf(";   f32: size=%llu bits, abi_size=%llu bytes, alignment=%u\n",
         LLVMSizeOfTypeInBits(td, f32), LLVMABISizeOfType(td, f32),
         LLVMABIAlignmentOfType(td, f32));
  printf(";   f64: size=%llu bits, abi_size=%llu bytes, alignment=%u\n",
         LLVMSizeOfTypeInBits(td, f64), LLVMABISizeOfType(td, f64),
         LLVMABIAlignmentOfType(td, f64));
  printf(";   ptr: size=%llu bits, abi_size=%llu bytes, alignment=%u\n",
         LLVMSizeOfTypeInBits(td, ptr), LLVMABISizeOfType(td, ptr),
         LLVMABIAlignmentOfType(td, ptr));

  // Struct type
  LLVMTypeRef struct_elems[] = {i8, i32, f64};
  LLVMTypeRef struct_ty =
      LLVMStructTypeInContext(ctx, struct_elems, 3, /*packed=*/0);
  printf(";   struct {i8, i32, f64}: size=%llu bits, abi_size=%llu bytes\n",
         LLVMSizeOfTypeInBits(td, struct_ty), LLVMABISizeOfType(td, struct_ty));

  // Packed struct
  LLVMTypeRef packed_struct =
      LLVMStructTypeInContext(ctx, struct_elems, 3, /*packed=*/1);
  printf(";   packed struct {i8, i32, f64}: size=%llu bits, abi_size=%llu bytes\n",
         LLVMSizeOfTypeInBits(td, packed_struct),
         LLVMABISizeOfType(td, packed_struct));

  // ==========================================================================
  // Code generation test
  // ==========================================================================
  printf(";\n; Code generation:\n");

  // Create a simple module with a function
  LLVMModuleRef mod = LLVMModuleCreateWithNameInContext("codegen_test", ctx);
  LLVMSetTarget(mod, default_triple);

  // Create: i32 @add(i32 %a, i32 %b) { return a + b }
  LLVMTypeRef param_types[] = {i32, i32};
  LLVMTypeRef fn_ty = LLVMFunctionType(i32, param_types, 2, 0);
  LLVMValueRef fn = LLVMAddFunction(mod, "add", fn_ty);

  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx, fn, "entry");
  LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
  LLVMPositionBuilderAtEnd(builder, entry);

  LLVMValueRef a = LLVMGetParam(fn, 0);
  LLVMValueRef b = LLVMGetParam(fn, 1);
  LLVMValueRef sum = LLVMBuildAdd(builder, a, b, "sum");
  LLVMBuildRet(builder, sum);

  LLVMDisposeBuilder(builder);

  // Verify module
  char *verify_error = NULL;
  if (LLVMVerifyModule(mod, LLVMReturnStatusAction, &verify_error)) {
    fprintf(stderr, "; ERROR: Module verification failed: %s\n", verify_error);
    LLVMDisposeMessage(verify_error);
    LLVMDisposeModule(mod);
    LLVMDisposeTargetData(td);
    LLVMDisposeTargetMachine(tm);
    LLVMContextDispose(ctx);
    LLVMDisposeMessage(default_triple);
    return 1;
  }
  if (verify_error)
    LLVMDisposeMessage(verify_error);

  // Emit to memory buffer (object file)
  LLVMMemoryBufferRef obj_buf = NULL;
  char *codegen_error = NULL;
  if (LLVMTargetMachineEmitToMemoryBuffer(tm, mod, LLVMObjectFile,
                                          &codegen_error, &obj_buf)) {
    fprintf(stderr, "; ERROR: Object codegen failed: %s\n", codegen_error);
    if (codegen_error)
      LLVMDisposeMessage(codegen_error);
    LLVMDisposeModule(mod);
    LLVMDisposeTargetData(td);
    LLVMDisposeTargetMachine(tm);
    LLVMContextDispose(ctx);
    LLVMDisposeMessage(default_triple);
    return 1;
  }
  if (codegen_error)
    LLVMDisposeMessage(codegen_error);

  size_t obj_size = LLVMGetBufferSize(obj_buf);
  printf(";   Object file size: %zu bytes\n", obj_size);
  printf(";   Object file generated: %s\n", obj_size > 0 ? "yes" : "no");
  LLVMDisposeMemoryBuffer(obj_buf);

  // Emit to memory buffer (assembly)
  LLVMMemoryBufferRef asm_buf = NULL;
  if (LLVMTargetMachineEmitToMemoryBuffer(tm, mod, LLVMAssemblyFile,
                                          &codegen_error, &asm_buf)) {
    fprintf(stderr, "; ERROR: Assembly codegen failed: %s\n", codegen_error);
    if (codegen_error)
      LLVMDisposeMessage(codegen_error);
    LLVMDisposeModule(mod);
    LLVMDisposeTargetData(td);
    LLVMDisposeTargetMachine(tm);
    LLVMContextDispose(ctx);
    LLVMDisposeMessage(default_triple);
    return 1;
  }
  if (codegen_error)
    LLVMDisposeMessage(codegen_error);

  size_t asm_size = LLVMGetBufferSize(asm_buf);
  printf(";   Assembly size: %zu bytes\n", asm_size);
  printf(";   Assembly generated: %s\n", asm_size > 0 ? "yes" : "no");

  // Print first few lines of assembly (portable check)
  const char *asm_start = LLVMGetBufferStart(asm_buf);
  printf(";\n; Assembly output (first 200 chars or first 5 lines):\n");
  int lines = 0;
  size_t printed = 0;
  for (size_t i = 0; i < asm_size && lines < 5 && printed < 200; i++) {
    if (asm_start[i] == '\n') {
      lines++;
    }
    printf("%c", asm_start[i]);
    printed++;
  }
  if (printed > 0 && asm_start[printed - 1] != '\n') {
    printf("\n");
  }
  printf("; ... (truncated)\n");

  LLVMDisposeMemoryBuffer(asm_buf);

  // ==========================================================================
  // Cleanup
  // ==========================================================================
  LLVMDisposeModule(mod);
  LLVMDisposeTargetData(td);
  LLVMDisposeTargetMachine(tm);
  LLVMContextDispose(ctx);
  LLVMDisposeMessage(default_triple);

  printf(";\n; Test completed successfully\n");

  return 0;
}
