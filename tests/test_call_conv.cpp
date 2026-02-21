/**
 * Test: test_call_conv
 * Tests calling convention enum values and instruction call conv get/set
 *
 * LLVM-C APIs covered:
 * - LLVMGetInstructionCallConv()
 * - LLVMSetInstructionCallConv()
 * - LLVMSetFunctionCallConv(), LLVMGetFunctionCallConv()
 * - Various LLVMCallConv enum values
 */

#include <cstdio>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

const char *call_conv_name(unsigned cc) {
  switch (cc) {
  case LLVMCCallConv: return "ccc";
  case LLVMFastCallConv: return "fastcc";
  case LLVMColdCallConv: return "coldcc";
  case LLVMX86StdcallCallConv: return "x86_stdcallcc";
  case LLVMX86FastcallCallConv: return "x86_fastcallcc";
  case LLVMGHCCallConv: return "ghccc";
  case LLVMHiPECallConv: return "cc11";
  case LLVMPreserveMostCallConv: return "preserve_mostcc";
  case LLVMPreserveAllCallConv: return "preserve_allcc";
  case LLVMSwiftCallConv: return "swiftcc";
  case LLVMCXXFASTTLSCallConv: return "cxx_fast_tlscc";
  case LLVMX86ThisCallCallConv: return "x86_thiscallcc";
  case LLVMX8664SysVCallConv: return "x86_64_sysvcc";
  case LLVMWin64CallConv: return "win64cc";
  case LLVMX86VectorCallCallConv: return "x86_vectorcallcc";
  case LLVMX86RegCallCallConv: return "x86_regcallcc";
  default: return "unknown";
  }
}

int main() {
  LLVMContextRef ctx = LLVMContextCreate();
  LLVMModuleRef mod =
      LLVMModuleCreateWithNameInContext("test_call_conv", ctx);

  LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
  LLVMTypeRef void_ty = LLVMVoidTypeInContext(ctx);

  LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

  // ==========================================
  // Test 1: CallConv enum values
  // ==========================================
  struct {
    unsigned value;
    const char *name;
  } convs[] = {
      {LLVMCCallConv, "C"},
      {LLVMFastCallConv, "Fast"},
      {LLVMColdCallConv, "Cold"},
      {LLVMX86StdcallCallConv, "X86Stdcall"},
      {LLVMX86FastcallCallConv, "X86Fastcall"},
      {LLVMGHCCallConv, "GHC"},
      {LLVMHiPECallConv, "HiPE"},
      {LLVMPreserveMostCallConv, "PreserveMost"},
      {LLVMPreserveAllCallConv, "PreserveAll"},
      {LLVMSwiftCallConv, "Swift"},
      {LLVMCXXFASTTLSCallConv, "CXX_FAST_TLS"},
      {LLVMX86ThisCallCallConv, "X86ThisCall"},
      {LLVMX8664SysVCallConv, "X86_64_SysV"},
      {LLVMWin64CallConv, "Win64"},
      {LLVMX86VectorCallCallConv, "X86VectorCall"},
      {LLVMX86RegCallCallConv, "X86RegCall"},
  };

  // ==========================================
  // Test 2: Declare a callee and a caller that uses call with different CCs
  // ==========================================

  // Callee: i32 callee(i32)
  LLVMTypeRef callee_params[] = {i32};
  LLVMTypeRef callee_ty = LLVMFunctionType(i32, callee_params, 1, 0);
  LLVMValueRef callee = LLVMAddFunction(mod, "callee", callee_ty);

  // Caller: i32 caller_default(i32) - default CC call
  LLVMValueRef caller_default =
      LLVMAddFunction(mod, "caller_default", callee_ty);
  LLVMValueRef cd_param = LLVMGetParam(caller_default, 0);
  LLVMSetValueName2(cd_param, "x", 1);
  LLVMBasicBlockRef cd_entry =
      LLVMAppendBasicBlockInContext(ctx, caller_default, "entry");
  LLVMPositionBuilderAtEnd(builder, cd_entry);
  LLVMValueRef cd_args[] = {cd_param};
  LLVMValueRef cd_call =
      LLVMBuildCall2(builder, callee_ty, callee, cd_args, 1, "result");
  LLVMBuildRet(builder, cd_call);

  // Read default call conv
  unsigned default_cc = LLVMGetInstructionCallConv(cd_call);

  // Caller: i32 caller_fastcc(i32) - fastcc call
  LLVMValueRef caller_fastcc =
      LLVMAddFunction(mod, "caller_fastcc", callee_ty);
  LLVMValueRef cf_param = LLVMGetParam(caller_fastcc, 0);
  LLVMSetValueName2(cf_param, "x", 1);
  LLVMBasicBlockRef cf_entry =
      LLVMAppendBasicBlockInContext(ctx, caller_fastcc, "entry");
  LLVMPositionBuilderAtEnd(builder, cf_entry);
  LLVMValueRef cf_args[] = {cf_param};
  LLVMValueRef cf_call =
      LLVMBuildCall2(builder, callee_ty, callee, cf_args, 1, "result");
  LLVMSetInstructionCallConv(cf_call, LLVMFastCallConv);
  LLVMBuildRet(builder, cf_call);

  unsigned fast_cc = LLVMGetInstructionCallConv(cf_call);

  // Caller: i32 caller_coldcc(i32) - coldcc call
  LLVMValueRef caller_coldcc =
      LLVMAddFunction(mod, "caller_coldcc", callee_ty);
  LLVMValueRef cc_param = LLVMGetParam(caller_coldcc, 0);
  LLVMSetValueName2(cc_param, "x", 1);
  LLVMBasicBlockRef cc_entry =
      LLVMAppendBasicBlockInContext(ctx, caller_coldcc, "entry");
  LLVMPositionBuilderAtEnd(builder, cc_entry);
  LLVMValueRef cc_args[] = {cc_param};
  LLVMValueRef cc_call =
      LLVMBuildCall2(builder, callee_ty, callee, cc_args, 1, "result");
  LLVMSetInstructionCallConv(cc_call, LLVMColdCallConv);
  LLVMBuildRet(builder, cc_call);

  unsigned cold_cc = LLVMGetInstructionCallConv(cc_call);

  // Caller: i32 caller_ghccc(i32) - ghccc call
  LLVMValueRef caller_ghccc =
      LLVMAddFunction(mod, "caller_ghccc", callee_ty);
  LLVMValueRef cg_param = LLVMGetParam(caller_ghccc, 0);
  LLVMSetValueName2(cg_param, "x", 1);
  LLVMBasicBlockRef cg_entry =
      LLVMAppendBasicBlockInContext(ctx, caller_ghccc, "entry");
  LLVMPositionBuilderAtEnd(builder, cg_entry);
  LLVMValueRef cg_args[] = {cg_param};
  LLVMValueRef cg_call =
      LLVMBuildCall2(builder, callee_ty, callee, cg_args, 1, "result");
  LLVMSetInstructionCallConv(cg_call, LLVMGHCCallConv);
  LLVMBuildRet(builder, cg_call);

  unsigned ghc_cc = LLVMGetInstructionCallConv(cg_call);

  // Caller: i32 caller_swiftcc(i32) - swiftcc call
  LLVMValueRef caller_swiftcc =
      LLVMAddFunction(mod, "caller_swiftcc", callee_ty);
  LLVMValueRef cs_param = LLVMGetParam(caller_swiftcc, 0);
  LLVMSetValueName2(cs_param, "x", 1);
  LLVMBasicBlockRef cs_entry =
      LLVMAppendBasicBlockInContext(ctx, caller_swiftcc, "entry");
  LLVMPositionBuilderAtEnd(builder, cs_entry);
  LLVMValueRef cs_args[] = {cs_param};
  LLVMValueRef cs_call =
      LLVMBuildCall2(builder, callee_ty, callee, cs_args, 1, "result");
  LLVMSetInstructionCallConv(cs_call, LLVMSwiftCallConv);
  LLVMBuildRet(builder, cs_call);

  unsigned swift_cc = LLVMGetInstructionCallConv(cs_call);

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
  printf("; Test: test_call_conv\n");
  printf(";\n");

  printf("; CallConv enum values:\n");
  for (size_t i = 0; i < sizeof(convs) / sizeof(convs[0]); i++) {
    printf(";   %s = %u (%s)\n", convs[i].name, convs[i].value,
           call_conv_name(convs[i].value));
  }

  printf(";\n");
  printf("; Instruction call conv tests:\n");
  printf(";   default call conv: %u (%s)\n", default_cc,
         call_conv_name(default_cc));
  printf(";   after set fastcc: %u (%s)\n", fast_cc,
         call_conv_name(fast_cc));
  printf(";   after set coldcc: %u (%s)\n", cold_cc,
         call_conv_name(cold_cc));
  printf(";   after set ghccc: %u (%s)\n", ghc_cc,
         call_conv_name(ghc_cc));
  printf(";   after set swiftcc: %u (%s)\n", swift_cc,
         call_conv_name(swift_cc));

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
