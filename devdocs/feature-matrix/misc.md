# Miscellaneous Headers Feature Matrix

Implementation status for other LLVM-C headers.

## Legend

| Status | Meaning |
|--------|---------|
| ✅ | Implemented |
| ❌ | Not implemented |
| 🚫 | Intentionally skipped |

---


## Analysis.h

**Header:** `llvm-c/Analysis.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMVerifyModule` | ✅ | |
| `LLVMVerifyFunction` | ❌ | |
| `LLVMViewFunctionCFG` | ❌ | |
| `LLVMViewFunctionCFGOnly` | ❌ | |

**Summary:** 1/4 (25.0%)


## BitReader.h

**Header:** `llvm-c/BitReader.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMParseBitcode` | ❌ | |
| `LLVMParseBitcode2` | ❌ | |
| `LLVMParseBitcodeInContext` | ❌ | |
| `LLVMParseBitcodeInContext2` | ✅ | |
| `LLVMGetBitcodeModuleInContext` | ❌ | |
| `LLVMGetBitcodeModuleInContext2` | ✅ | |
| `LLVMGetBitcodeModule` | ❌ | |
| `LLVMGetBitcodeModule2` | ✅ | |

**Summary:** 3/8 (37.5%)


## BitWriter.h

**Header:** `llvm-c/BitWriter.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMWriteBitcodeToFile` | ❌ | |
| `LLVMWriteBitcodeToFD` | ❌ | |
| `LLVMWriteBitcodeToFileHandle` | ❌ | |
| `LLVMWriteBitcodeToMemoryBuffer` | ❌ | |

**Summary:** 0/4 (0.0%)


## IRReader.h

**Header:** `llvm-c/IRReader.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMParseIRInContext` | ✅ | |

**Summary:** 1/1 (100.0%)


## Disassembler.h

**Header:** `llvm-c/Disassembler.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMCreateDisasm` | ❌ | |
| `LLVMCreateDisasmCPU` | ❌ | |
| `LLVMCreateDisasmCPUFeatures` | ✅ | |
| `LLVMSetDisasmOptions` | ❌ | |
| `LLVMDisasmDispose` | ✅ | |
| `LLVMDisasmInstruction` | ✅ | |

**Summary:** 3/6 (50.0%)


## Object.h

**Header:** `llvm-c/Object.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMCreateBinary` | ✅ | |
| `LLVMDisposeBinary` | ✅ | |
| `LLVMBinaryCopyMemoryBuffer` | ❌ | |
| `LLVMBinaryGetType` | ✅ | |
| `LLVMMachOUniversalBinaryCopyObjectForArch` | ❌ | |
| `LLVMObjectFileIsSectionIteratorAtEnd` | ✅ | |
| `LLVMDisposeSectionIterator` | ✅ | |
| `LLVMMoveToNextSection` | ✅ | |
| `LLVMMoveToContainingSection` | ✅ | |
| `LLVMDisposeSymbolIterator` | ✅ | |
| `LLVMMoveToNextSymbol` | ✅ | |
| `LLVMGetSectionName` | ✅ | |
| `LLVMGetSectionSize` | ✅ | |
| `LLVMGetSectionContents` | ✅ | |
| `LLVMGetSectionAddress` | ✅ | |
| `LLVMGetSectionContainsSymbol` | ✅ | |
| `LLVMDisposeRelocationIterator` | ✅ | |
| `LLVMIsRelocationIteratorAtEnd` | ✅ | |
| `LLVMMoveToNextRelocation` | ✅ | |
| `LLVMGetSymbolName` | ✅ | |
| `LLVMGetSymbolAddress` | ✅ | |
| `LLVMGetSymbolSize` | ✅ | |
| `LLVMGetRelocationOffset` | ✅ | |
| `LLVMGetRelocationType` | ✅ | |
| `LLVMGetRelocationTypeName` | ✅ | |
| `LLVMCreateObjectFile` | ❌ | |
| `LLVMDisposeObjectFile` | ❌ | |
| `LLVMGetSections` | ❌ | |
| `LLVMIsSectionIteratorAtEnd` | ❌ | |
| `LLVMGetSymbols` | ❌ | |
| `LLVMIsSymbolIteratorAtEnd` | ❌ | |

**Summary:** 23/31 (74.2%)


## PassBuilder.h

**Header:** `llvm-c/Transforms/PassBuilder.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMRunPasses` | ❌ | |
| `LLVMRunPassesOnFunction` | ❌ | |
| `LLVMCreatePassBuilderOptions` | ❌ | |
| `LLVMPassBuilderOptionsSetForgetAllSCEVInLoopUnroll` | ❌ | |
| `LLVMPassBuilderOptionsSetLicmMssaNoAccForPromotionCap` | ❌ | |

**Summary:** 0/5 (0.0%)


## Linker.h

**Header:** `llvm-c/Linker.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMLinkModules2` | ❌ | |

**Summary:** 0/1 (0.0%)


## Error.h

**Header:** `llvm-c/Error.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetErrorTypeId` | ❌ | |
| `LLVMConsumeError` | ❌ | |
| `LLVMCantFail` | ❌ | |
| `LLVMGetErrorMessage` | ❌ | |
| `LLVMDisposeErrorMessage` | ❌ | |
| `LLVMGetStringErrorTypeId` | ❌ | |
| `LLVMCreateStringError` | ❌ | |

**Summary:** 0/7 (0.0%)


## ErrorHandling.h

**Header:** `llvm-c/ErrorHandling.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMInstallFatalErrorHandler` | ❌ | |
| `LLVMResetFatalErrorHandler` | ❌ | |
| `LLVMEnablePrettyStackTrace` | ❌ | |

**Summary:** 0/3 (0.0%)


## Support.h

**Header:** `llvm-c/Support.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMLoadLibraryPermanently` | ❌ | |
| `LLVMParseCommandLineOptions` | ❌ | |
| `LLVMSearchForAddressOfSymbol` | ❌ | |
| `LLVMAddSymbol` | ❌ | |

**Summary:** 0/4 (0.0%)


## Comdat.h

**Header:** `llvm-c/Comdat.h`

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetOrInsertComdat` | ❌ | |
| `LLVMGetComdat` | ❌ | |
| `LLVMSetComdat` | ❌ | |
| `LLVMGetComdatSelectionKind` | ❌ | |
| `LLVMSetComdatSelectionKind` | ❌ | |

**Summary:** 0/5 (0.0%)

---

## Overall Summary

| Header | Total | Implemented | Coverage |
|--------|-------|-------------|----------|
| Analysis.h | 4 | 1 | 25.0% |
| BitReader.h | 8 | 3 | 37.5% |
| BitWriter.h | 4 | 0 | 0.0% |
| IRReader.h | 1 | 1 | 100.0% |
| Disassembler.h | 6 | 3 | 50.0% |
| Object.h | 31 | 23 | 74.2% |
| PassBuilder.h | 5 | 0 | 0.0% |
| Linker.h | 1 | 0 | 0.0% |
| Error.h | 7 | 0 | 0.0% |
| ErrorHandling.h | 3 | 0 | 0.0% |
| Support.h | 4 | 0 | 0.0% |
| Comdat.h | 5 | 0 | 0.0% |
| **Total** | **79** | **31** | **39.2%** |
