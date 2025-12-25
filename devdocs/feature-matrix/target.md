# Target.h + TargetMachine.h Feature Matrix

LLVM-C Target and Code Generation API implementation status.

## Legend

| Status | Meaning |
|--------|---------|
| ✅ | Implemented |
| ❌ | Not implemented |
| 🚫 | Intentionally skipped |

---

## Target.h

**Header:** `llvm-c/Target.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetModuleDataLayout` | ❌ | |
| `LLVMSetModuleDataLayout` | ❌ | |
| `LLVMCreateTargetData` | ❌ | |
| `LLVMDisposeTargetData` | ❌ | |
| `LLVMAddTargetLibraryInfo` | ❌ | |
| `LLVMCopyStringRepOfTargetData` | ❌ | |
| `LLVMByteOrder` | ❌ | |
| `LLVMPointerSize` | ❌ | |
| `LLVMPointerSizeForAS` | ❌ | |
| `LLVMIntPtrType` | ❌ | |
| `LLVMIntPtrTypeForAS` | ❌ | |
| `LLVMIntPtrTypeInContext` | ❌ | |
| `LLVMIntPtrTypeForASInContext` | ❌ | |
| `LLVMSizeOfTypeInBits` | ❌ | |
| `LLVMStoreSizeOfType` | ❌ | |
| `LLVMABISizeOfType` | ❌ | |
| `LLVMABIAlignmentOfType` | ❌ | |
| `LLVMCallFrameAlignmentOfType` | ❌ | |
| `LLVMPreferredAlignmentOfType` | ❌ | |
| `LLVMPreferredAlignmentOfGlobal` | ❌ | |
| `LLVMElementAtOffset` | ❌ | |
| `LLVMOffsetOfElement` | ❌ | |

**Summary:** 0/22 implemented (0.0%)

---

## TargetMachine.h

**Header:** `llvm-c/TargetMachine.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetFirstTarget` | ✅ | |
| `LLVMGetNextTarget` | ✅ | |
| `LLVMGetTargetFromName` | ❌ | |
| `LLVMGetTargetFromTriple` | ❌ | |
| `LLVMGetTargetName` | ✅ | |
| `LLVMGetTargetDescription` | ✅ | |
| `LLVMTargetHasJIT` | ✅ | |
| `LLVMTargetHasTargetMachine` | ✅ | |
| `LLVMTargetHasAsmBackend` | ✅ | |
| `LLVMCreateTargetMachineOptions` | ❌ | |
| `LLVMCreateTargetMachineWithOptions` | ❌ | |
| `LLVMCreateTargetMachine` | ❌ | |
| `LLVMDisposeTargetMachine` | ❌ | |
| `LLVMGetTargetMachineTarget` | ❌ | |
| `LLVMGetTargetMachineTriple` | ❌ | |
| `LLVMGetTargetMachineCPU` | ❌ | |
| `LLVMGetTargetMachineFeatureString` | ❌ | |
| `LLVMCreateTargetDataLayout` | ❌ | |
| `LLVMSetTargetMachineAsmVerbosity` | ❌ | |
| `LLVMSetTargetMachineFastISel` | ❌ | |
| `LLVMSetTargetMachineGlobalISel` | ❌ | |
| `LLVMSetTargetMachineMachineOutliner` | ❌ | |
| `LLVMTargetMachineEmitToFile` | ❌ | |
| `LLVMTargetMachineEmitToMemoryBuffer` | ❌ | |
| `LLVMGetDefaultTargetTriple` | ❌ | |
| `LLVMNormalizeTargetTriple` | ❌ | |
| `LLVMGetHostCPUName` | ❌ | |
| `LLVMGetHostCPUFeatures` | ❌ | |
| `LLVMAddAnalysisPasses` | ❌ | |

**Summary:** 7/29 implemented (24.1%)

---

## Overall Summary

| Header | Total | Implemented | Coverage |
|--------|-------|-------------|----------|
| Target.h | 22 | 0 | 0.0% |
| TargetMachine.h | 29 | 7 | 24.1% |
| **Total** | **51** | **7** | **13.7%** |

