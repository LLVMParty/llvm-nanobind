# Core.h Feature Matrix

LLVM-C Core API implementation status.

**Total Functions:** ~640  
**Header:** `llvm-c/Core.h`

## Legend

| Status | Meaning |
|--------|---------|
| ✅ | Implemented |
| ❌ | Not implemented |
| 🚫 | Intentionally skipped |

## Summary


| Section | Total | ✅ | 🚫 | ❌ |
|---------|-------|-----|-----|-----|
| Types and Enumerations | 4 | 0 | 3 | 1 |
| Contexts | 27 | 13 | 0 | 14 |
| Modules | 54 | 36 | 4 | 14 |
| Types | 5 | 4 | 0 | 1 |
| Integer Types | 15 | 8 | 7 | 0 |
| Floating Point Types | 14 | 7 | 7 | 0 |
| Function Types | 5 | 5 | 0 | 0 |
| Structure Types | 11 | 8 | 1 | 2 |
| Sequential Types | 16 | 11 | 0 | 5 |
| Other Types | 14 | 11 | 3 | 0 |
| General APIs | 17 | 9 | 0 | 8 |
| Usage | 4 | 4 | 0 | 0 |
| User value | 4 | 2 | 0 | 2 |
| Constants | 6 | 6 | 0 | 0 |
| Scalar constants | 9 | 4 | 0 | 5 |
| Composite Constants | 15 | 9 | 2 | 4 |
| Constant Expressions | 29 | 3 | 0 | 26 |
| Global Values | 15 | 11 | 0 | 4 |
| Values with alignment | 6 | 4 | 0 | 2 |
| Global Variables | 19 | 16 | 0 | 3 |
| Global Aliases | 8 | 8 | 0 | 0 |
| Function values | 30 | 18 | 0 | 12 |
| Function Parameters | 9 | 8 | 0 | 1 |
| IFuncs | 10 | 8 | 0 | 2 |
| Metadata | 12 | 4 | 2 | 6 |
| Operand Bundles | 4 | 4 | 0 | 0 |
| Basic Block | 24 | 19 | 1 | 4 |
| Instructions | 19 | 15 | 0 | 4 |
| Call Sites and Invocations | 26 | 15 | 0 | 11 |
| Terminators | 7 | 4 | 0 | 3 |
| Allocas | 1 | 1 | 0 | 0 |
| GEPs | 5 | 2 | 0 | 3 |
| PHI Nodes | 4 | 4 | 0 | 0 |
| InsertValue | 2 | 2 | 0 | 0 |
| Instruction Builders | 173 | 125 | 5 | 43 |
| Module Providers | 1 | 0 | 1 | 0 |
| Memory Buffers | 7 | 5 | 0 | 2 |
| Pass Managers | 6 | 0 | 6 | 0 |
| Threading | 3 | 0 | 2 | 1 |
| **Total** | **640** | **413** | **44** | **183** |

---


## Types and Enumerations

*Lines 54-558*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMShutdown` | 🚫 | 🚫 Unsafe for embedding |
| `LLVMGetVersion` | ❌ |  |
| `LLVMCreateMessage` | 🚫 | 🚫 Internal use only |
| `LLVMDisposeMessage` | 🚫 | 🚫 Internal use only |

## Contexts

*Lines 559-757*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMContextCreate` | ✅ | → `Context()` |
| `LLVMGetGlobalContext` | ✅ |  |
| `LLVMContextSetDiagnosticHandler` | ✅ |  |
| `LLVMContextGetDiagnosticContext` | ❌ |  |
| `LLVMContextSetYieldCallback` | ❌ |  |
| `LLVMContextShouldDiscardValueNames` | ✅ |  |
| `LLVMContextSetDiscardValueNames` | ✅ |  |
| `LLVMContextDispose` | ✅ | → `Context destructor` |
| `LLVMGetDiagInfoDescription` | ✅ |  |
| `LLVMGetMDKindIDInContext` | ❌ |  |
| `LLVMGetMDKindID` | ✅ |  |
| `LLVMGetSyncScopeID` | ❌ |  |
| `LLVMGetEnumAttributeKindForName` | ❌ |  |
| `LLVMGetLastEnumAttributeKind` | ✅ |  |
| `LLVMCreateEnumAttribute` | ✅ |  |
| `LLVMGetEnumAttributeKind` | ✅ |  |
| `LLVMGetEnumAttributeValue` | ✅ |  |
| `LLVMCreateTypeAttribute` | ❌ |  |
| `LLVMGetTypeAttributeValue` | ❌ |  |
| `LLVMCreateConstantRangeAttribute` | ❌ |  |
| `LLVMCreateStringAttribute` | ❌ |  |
| `LLVMGetStringAttributeKind` | ❌ |  |
| `LLVMGetStringAttributeValue` | ❌ |  |
| `LLVMIsEnumAttribute` | ❌ |  |
| `LLVMIsStringAttribute` | ❌ |  |
| `LLVMIsTypeAttribute` | ❌ |  |
| `LLVMGetTypeByName2` | ✅ |  |

## Modules

*Lines 758-1265*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMModuleCreateWithName` | 🚫 | 🚫 Uses global context |
| `LLVMModuleCreateWithNameInContext` | ✅ | → `Context.create_module()` |
| `LLVMCloneModule` | ✅ | → `Module.clone()` |
| `LLVMDisposeModule` | ✅ | → `Module destructor` |
| `LLVMIsNewDbgInfoFormat` | ✅ |  |
| `LLVMSetIsNewDbgInfoFormat` | ✅ |  |
| `LLVMGetModuleIdentifier` | ✅ | → `Module.name` |
| `LLVMSetModuleIdentifier` | ✅ | → `Module.name setter` |
| `LLVMGetSourceFileName` | ✅ | → `Module.source_filename` |
| `LLVMSetSourceFileName` | ✅ | → `Module.source_filename setter` |
| `LLVMGetDataLayoutStr` | ✅ | → `Module.data_layout` |
| `LLVMGetDataLayout` | 🚫 | 🚫 Deprecated |
| `LLVMSetDataLayout` | ✅ | → `Module.data_layout setter` |
| `LLVMGetTarget` | ✅ | → `Module.target` |
| `LLVMSetTarget` | ✅ | → `Module.target setter` |
| `LLVMCopyModuleFlagsMetadata` | ❌ |  |
| `LLVMDisposeModuleFlagsMetadata` | ❌ |  |
| `LLVMModuleFlagEntriesGetFlagBehavior` | ❌ |  |
| `LLVMModuleFlagEntriesGetKey` | ❌ |  |
| `LLVMGetModuleFlag` | ❌ |  |
| `LLVMAddModuleFlag` | ❌ |  |
| `LLVMDumpModule` | ❌ |  |
| `LLVMPrintModuleToFile` | ❌ |  |
| `LLVMPrintModuleToString` | ✅ | → `str(Module)` |
| `LLVMGetModuleInlineAsm` | ✅ |  |
| `LLVMSetModuleInlineAsm2` | ✅ |  |
| `LLVMAppendModuleInlineAsm` | ❌ |  |
| `LLVMGetInlineAsm` | ✅ |  |
| `LLVMGetInlineAsmAsmString` | ✅ |  |
| `LLVMGetInlineAsmFunctionType` | ✅ |  |
| `LLVMGetInlineAsmHasSideEffects` | ✅ |  |
| `LLVMGetInlineAsmCanUnwind` | ✅ |  |
| `LLVMGetModuleContext` | ✅ |  |
| `LLVMGetTypeByName` | 🚫 | 🚫 Uses global context |
| `LLVMGetFirstNamedMetadata` | ✅ |  |
| `LLVMGetLastNamedMetadata` | ✅ |  |
| `LLVMGetNamedMetadata` | ✅ |  |
| `LLVMGetOrInsertNamedMetadata` | ✅ |  |
| `LLVMGetNamedMetadataName` | ✅ |  |
| `LLVMGetNamedMetadataNumOperands` | ✅ |  |
| `LLVMGetNamedMetadataOperands` | ✅ |  |
| `LLVMAddNamedMetadataOperand` | ✅ |  |
| `LLVMGetDebugLocDirectory` | ❌ |  |
| `LLVMGetDebugLocFilename` | ❌ |  |
| `LLVMGetDebugLocLine` | ❌ |  |
| `LLVMGetDebugLocColumn` | ❌ |  |
| `LLVMAddFunction` | ✅ | → `Module.add_function()` |
| `LLVMGetNamedFunction` | ✅ | → `Module.get_function()` |
| `LLVMGetNamedFunctionWithLength` | ❌ |  |
| `LLVMGetFirstFunction` | ✅ | → `Module.functions iterator` |
| `LLVMGetLastFunction` | ✅ | → `Module.functions iterator` |
| `LLVMGetNextFunction` | ✅ | → `Module.functions iterator` |
| `LLVMGetPreviousFunction` | ✅ | → `Module.functions iterator` |
| `LLVMSetModuleInlineAsm` | 🚫 | 🚫 Deprecated |

## Types

*Lines 1266-1332*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetTypeKind` | ✅ |  |
| `LLVMTypeIsSized` | ✅ |  |
| `LLVMGetTypeContext` | ✅ |  |
| `LLVMDumpType` | ❌ |  |
| `LLVMPrintTypeToString` | ✅ |  |

## Integer Types

*Lines 1333-1368*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMInt1TypeInContext` | ✅ |  |
| `LLVMInt8TypeInContext` | ✅ |  |
| `LLVMInt16TypeInContext` | ✅ |  |
| `LLVMInt32TypeInContext` | ✅ |  |
| `LLVMInt64TypeInContext` | ✅ |  |
| `LLVMInt128TypeInContext` | ✅ |  |
| `LLVMIntTypeInContext` | ✅ |  |
| `LLVMInt1Type` | 🚫 | 🚫 Uses global context |
| `LLVMInt8Type` | 🚫 | 🚫 Uses global context |
| `LLVMInt16Type` | 🚫 | 🚫 Uses global context |
| `LLVMInt32Type` | 🚫 | 🚫 Uses global context |
| `LLVMInt64Type` | 🚫 | 🚫 Uses global context |
| `LLVMInt128Type` | 🚫 | 🚫 Uses global context |
| `LLVMIntType` | 🚫 | 🚫 Uses global context |
| `LLVMGetIntTypeWidth` | ✅ |  |

## Floating Point Types

*Lines 1369-1427*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMHalfTypeInContext` | ✅ |  |
| `LLVMBFloatTypeInContext` | ✅ |  |
| `LLVMFloatTypeInContext` | ✅ |  |
| `LLVMDoubleTypeInContext` | ✅ |  |
| `LLVMX86FP80TypeInContext` | ✅ |  |
| `LLVMFP128TypeInContext` | ✅ |  |
| `LLVMPPCFP128TypeInContext` | ✅ |  |
| `LLVMHalfType` | 🚫 | 🚫 Uses global context |
| `LLVMBFloatType` | 🚫 | 🚫 Uses global context |
| `LLVMFloatType` | 🚫 | 🚫 Uses global context |
| `LLVMDoubleType` | 🚫 | 🚫 Uses global context |
| `LLVMX86FP80Type` | 🚫 | 🚫 Uses global context |
| `LLVMFP128Type` | 🚫 | 🚫 Uses global context |
| `LLVMPPCFP128Type` | 🚫 | 🚫 Uses global context |

## Function Types

*Lines 1428-1475*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMFunctionType` | ✅ |  |
| `LLVMIsFunctionVarArg` | ✅ |  |
| `LLVMGetReturnType` | ✅ |  |
| `LLVMCountParamTypes` | ✅ |  |
| `LLVMGetParamTypes` | ✅ |  |

## Structure Types

*Lines 1476-1583*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMStructTypeInContext` | ✅ |  |
| `LLVMStructType` | 🚫 | 🚫 Uses global context |
| `LLVMStructCreateNamed` | ✅ |  |
| `LLVMGetStructName` | ✅ |  |
| `LLVMStructSetBody` | ✅ |  |
| `LLVMCountStructElementTypes` | ✅ |  |
| `LLVMGetStructElementTypes` | ❌ |  |
| `LLVMStructGetTypeAtIndex` | ✅ |  |
| `LLVMIsPackedStruct` | ✅ |  |
| `LLVMIsOpaqueStruct` | ✅ |  |
| `LLVMIsLiteralStruct` | ❌ |  |

## Sequential Types

*Lines 1584-1762*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetElementType` | ✅ |  |
| `LLVMGetSubtypes` | ❌ |  |
| `LLVMGetNumContainedTypes` | ❌ |  |
| `LLVMArrayType` | ❌ |  |
| `LLVMArrayType2` | ✅ |  |
| `LLVMGetArrayLength` | ❌ |  |
| `LLVMGetArrayLength2` | ✅ |  |
| `LLVMPointerType` | ❌ |  |
| `LLVMPointerTypeIsOpaque` | ✅ |  |
| `LLVMPointerTypeInContext` | ✅ |  |
| `LLVMGetPointerAddressSpace` | ✅ |  |
| `LLVMVectorType` | ✅ |  |
| `LLVMScalableVectorType` | ✅ |  |
| `LLVMGetVectorSize` | ✅ |  |
| `LLVMGetConstantPtrAuthPointer` | ✅ |  |
| `LLVMGetConstantPtrAuthKey` | ✅ |  |

## Other Types

*Lines 1763-1853*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMVoidTypeInContext` | ✅ |  |
| `LLVMLabelTypeInContext` | ✅ |  |
| `LLVMX86AMXTypeInContext` | ✅ |  |
| `LLVMTokenTypeInContext` | ✅ |  |
| `LLVMMetadataTypeInContext` | ✅ |  |
| `LLVMVoidType` | 🚫 | 🚫 Uses global context |
| `LLVMLabelType` | 🚫 | 🚫 Uses global context |
| `LLVMX86AMXType` | 🚫 | 🚫 Uses global context |
| `LLVMTargetExtTypeInContext` | ✅ |  |
| `LLVMGetTargetExtTypeName` | ✅ |  |
| `LLVMGetTargetExtTypeNumTypeParams` | ✅ |  |
| `LLVMGetTargetExtTypeTypeParam` | ✅ |  |
| `LLVMGetTargetExtTypeNumIntParams` | ✅ |  |
| `LLVMGetTargetExtTypeIntParam` | ✅ |  |

## General APIs

*Lines 1972-2091*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMTypeOf` | ✅ |  |
| `LLVMGetValueKind` | ✅ |  |
| `LLVMGetValueName2` | ✅ |  |
| `LLVMSetValueName2` | ✅ |  |
| `LLVMDumpValue` | ❌ |  |
| `LLVMPrintValueToString` | ✅ |  |
| `LLVMGetValueContext` | ❌ |  |
| `LLVMPrintDbgRecordToString` | ❌ |  |
| `LLVMReplaceAllUsesWith` | ❌ |  |
| `LLVMIsConstant` | ✅ |  |
| `LLVMIsUndef` | ✅ |  |
| `LLVMIsPoison` | ✅ |  |
| `LLVMIsAMDNode` | ❌ |  |
| `LLVMIsAValueAsMetadata` | ✅ |  |
| `LLVMIsAMDString` | ❌ |  |
| `LLVMGetValueName` | ❌ |  |
| `LLVMSetValueName` | ❌ |  |

## Usage

*Lines 2092-2144*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetFirstUse` | ✅ |  |
| `LLVMGetNextUse` | ✅ |  |
| `LLVMGetUser` | ✅ |  |
| `LLVMGetUsedValue` | ✅ |  |

## User value

*Lines 2145-2187*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetOperand` | ✅ |  |
| `LLVMGetOperandUse` | ❌ |  |
| `LLVMSetOperand` | ❌ |  |
| `LLVMGetNumOperands` | ✅ |  |

## Constants

*Lines 2188-2243*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMConstNull` | ✅ |  |
| `LLVMConstAllOnes` | ✅ |  |
| `LLVMGetUndef` | ✅ |  |
| `LLVMGetPoison` | ✅ |  |
| `LLVMIsNull` | ✅ |  |
| `LLVMConstPointerNull` | ✅ |  |

## Scalar constants

*Lines 2244-2353*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMConstInt` | ✅ |  |
| `LLVMConstIntOfArbitraryPrecision` | ✅ |  |
| `LLVMConstIntOfString` | ❌ |  |
| `LLVMConstIntOfStringAndSize` | ❌ |  |
| `LLVMConstReal` | ✅ |  |
| `LLVMConstRealOfString` | ❌ |  |
| `LLVMConstRealOfStringAndSize` | ❌ |  |
| `LLVMConstIntGetSExtValue` | ✅ |  |
| `LLVMConstRealGetDouble` | ❌ |  |

## Composite Constants

*Lines 2354-2523*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMConstStringInContext` | ❌ |  |
| `LLVMConstStringInContext2` | ✅ |  |
| `LLVMConstString` | 🚫 | 🚫 Uses global context |
| `LLVMIsConstantString` | ❌ |  |
| `LLVMGetAsString` | ❌ |  |
| `LLVMGetRawDataValues` | ✅ |  |
| `LLVMConstStructInContext` | ✅ |  |
| `LLVMConstStruct` | 🚫 | 🚫 Uses global context |
| `LLVMConstArray` | ❌ |  |
| `LLVMConstArray2` | ✅ |  |
| `LLVMConstDataArray` | ✅ |  |
| `LLVMConstNamedStruct` | ✅ |  |
| `LLVMGetAggregateElement` | ✅ |  |
| `LLVMConstVector` | ✅ |  |
| `LLVMConstantPtrAuth` | ✅ |  |

## Constant Expressions

*Lines 2524-2617*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetConstOpcode` | ✅ |  |
| `LLVMAlignOf` | ❌ |  |
| `LLVMSizeOf` | ❌ |  |
| `LLVMConstNeg` | ❌ |  |
| `LLVMConstNSWNeg` | ❌ |  |
| `LLVMConstNot` | ❌ |  |
| `LLVMConstAdd` | ❌ |  |
| `LLVMConstNSWAdd` | ❌ |  |
| `LLVMConstNUWAdd` | ❌ |  |
| `LLVMConstSub` | ❌ |  |
| `LLVMConstNSWSub` | ❌ |  |
| `LLVMConstNUWSub` | ❌ |  |
| `LLVMConstXor` | ❌ |  |
| `LLVMConstGEP2` | ❌ |  |
| `LLVMConstInBoundsGEP2` | ❌ |  |
| `LLVMConstGEPWithNoWrapFlags` | ✅ |  |
| `LLVMConstTrunc` | ❌ |  |
| `LLVMConstPtrToInt` | ❌ |  |
| `LLVMConstIntToPtr` | ❌ |  |
| `LLVMConstBitCast` | ✅ |  |
| `LLVMConstAddrSpaceCast` | ❌ |  |
| `LLVMConstTruncOrBitCast` | ❌ |  |
| `LLVMConstPointerCast` | ❌ |  |
| `LLVMConstExtractElement` | ❌ |  |
| `LLVMConstInsertElement` | ❌ |  |
| `LLVMConstShuffleVector` | ❌ |  |
| `LLVMBlockAddress` | ❌ |  |
| `LLVMGetBlockAddressFunction` | ❌ |  |
| `LLVMConstInlineAsm` | ❌ |  |

## Global Values

*Lines 2618-2658*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetGlobalParent` | ✅ |  |
| `LLVMIsDeclaration` | ✅ |  |
| `LLVMGetLinkage` | ✅ |  |
| `LLVMSetLinkage` | ✅ |  |
| `LLVMGetSection` | ✅ |  |
| `LLVMSetSection` | ✅ |  |
| `LLVMGetVisibility` | ✅ |  |
| `LLVMSetVisibility` | ✅ |  |
| `LLVMGetDLLStorageClass` | ❌ |  |
| `LLVMSetDLLStorageClass` | ❌ |  |
| `LLVMGetUnnamedAddress` | ✅ |  |
| `LLVMSetUnnamedAddress` | ✅ |  |
| `LLVMGlobalGetValueType` | ✅ |  |
| `LLVMHasUnnamedAddr` | ❌ |  |
| `LLVMSetUnnamedAddr` | ❌ |  |

## Values with alignment

*Lines 2659-2744*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetAlignment` | ✅ |  |
| `LLVMSetAlignment` | ✅ |  |
| `LLVMGlobalSetMetadata` | ✅ |  |
| `LLVMGlobalEraseMetadata` | ❌ |  |
| `LLVMGlobalClearMetadata` | ❌ |  |
| `LLVMValueMetadataEntriesGetMetadata` | ✅ |  |

## Global Variables

*Lines 2745-2788*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMAddGlobal` | ✅ |  |
| `LLVMAddGlobalInAddressSpace` | ✅ |  |
| `LLVMGetNamedGlobal` | ✅ |  |
| `LLVMGetNamedGlobalWithLength` | ❌ |  |
| `LLVMGetFirstGlobal` | ✅ |  |
| `LLVMGetLastGlobal` | ✅ |  |
| `LLVMGetNextGlobal` | ✅ |  |
| `LLVMGetPreviousGlobal` | ✅ |  |
| `LLVMDeleteGlobal` | ✅ |  |
| `LLVMGetInitializer` | ✅ |  |
| `LLVMSetInitializer` | ✅ |  |
| `LLVMIsThreadLocal` | ✅ |  |
| `LLVMSetThreadLocal` | ✅ |  |
| `LLVMIsGlobalConstant` | ✅ |  |
| `LLVMSetGlobalConstant` | ✅ |  |
| `LLVMGetThreadLocalMode` | ❌ |  |
| `LLVMSetThreadLocalMode` | ❌ |  |
| `LLVMIsExternallyInitialized` | ✅ |  |
| `LLVMSetExternallyInitialized` | ✅ |  |

## Global Aliases

*Lines 2789-2862*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMAddAlias2` | ✅ |  |
| `LLVMGetNamedGlobalAlias` | ✅ |  |
| `LLVMGetFirstGlobalAlias` | ✅ |  |
| `LLVMGetLastGlobalAlias` | ✅ |  |
| `LLVMGetNextGlobalAlias` | ✅ |  |
| `LLVMGetPreviousGlobalAlias` | ✅ |  |
| `LLVMAliasGetAliasee` | ✅ |  |
| `LLVMAliasSetAliasee` | ✅ |  |

## Function values

*Lines 2863-3077*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMDeleteFunction` | ✅ |  |
| `LLVMHasPersonalityFn` | ✅ |  |
| `LLVMGetPersonalityFn` | ✅ |  |
| `LLVMSetPersonalityFn` | ✅ |  |
| `LLVMLookupIntrinsicID` | ❌ |  |
| `LLVMGetIntrinsicID` | ✅ |  |
| `LLVMGetIntrinsicDeclaration` | ✅ |  |
| `LLVMIntrinsicGetType` | ❌ |  |
| `LLVMIntrinsicGetName` | ❌ |  |
| `LLVMIntrinsicCopyOverloadedName` | ❌ |  |
| `LLVMIntrinsicCopyOverloadedName2` | ❌ |  |
| `LLVMIntrinsicIsOverloaded` | ✅ |  |
| `LLVMGetFunctionCallConv` | ✅ |  |
| `LLVMSetFunctionCallConv` | ✅ |  |
| `LLVMGetGC` | ❌ |  |
| `LLVMSetGC` | ❌ |  |
| `LLVMGetPrefixData` | ✅ |  |
| `LLVMHasPrefixData` | ✅ |  |
| `LLVMSetPrefixData` | ✅ |  |
| `LLVMGetPrologueData` | ✅ |  |
| `LLVMHasPrologueData` | ✅ |  |
| `LLVMSetPrologueData` | ✅ |  |
| `LLVMAddAttributeAtIndex` | ✅ |  |
| `LLVMGetAttributeCountAtIndex` | ✅ |  |
| `LLVMGetAttributesAtIndex` | ❌ |  |
| `LLVMGetEnumAttributeAtIndex` | ✅ |  |
| `LLVMGetStringAttributeAtIndex` | ❌ |  |
| `LLVMRemoveEnumAttributeAtIndex` | ❌ |  |
| `LLVMRemoveStringAttributeAtIndex` | ❌ |  |
| `LLVMAddTargetDependentFunctionAttr` | ❌ |  |

## Function Parameters

*Lines 3078-3170*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMCountParams` | ✅ |  |
| `LLVMGetParams` | ✅ |  |
| `LLVMGetParam` | ✅ |  |
| `LLVMGetParamParent` | ✅ |  |
| `LLVMGetFirstParam` | ✅ |  |
| `LLVMGetLastParam` | ✅ |  |
| `LLVMGetNextParam` | ✅ |  |
| `LLVMGetPreviousParam` | ✅ |  |
| `LLVMSetParamAlignment` | ❌ |  |

## IFuncs

*Lines 3171-3281*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMAddGlobalIFunc` | ✅ |  |
| `LLVMGetNamedGlobalIFunc` | ✅ |  |
| `LLVMGetFirstGlobalIFunc` | ✅ |  |
| `LLVMGetLastGlobalIFunc` | ✅ |  |
| `LLVMGetNextGlobalIFunc` | ✅ |  |
| `LLVMGetPreviousGlobalIFunc` | ✅ |  |
| `LLVMGetGlobalIFuncResolver` | ✅ |  |
| `LLVMSetGlobalIFuncResolver` | ✅ |  |
| `LLVMEraseGlobalIFunc` | ❌ |  |
| `LLVMRemoveGlobalIFunc` | ❌ |  |

## Metadata

*Lines 3282-3371*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMMDStringInContext2` | ✅ |  |
| `LLVMMDNodeInContext2` | ✅ |  |
| `LLVMMetadataAsValue` | ✅ |  |
| `LLVMValueAsMetadata` | ✅ |  |
| `LLVMGetMDString` | ❌ |  |
| `LLVMGetMDNodeNumOperands` | ❌ |  |
| `LLVMGetMDNodeOperands` | ❌ |  |
| `LLVMReplaceMDNodeOperandWith` | ❌ |  |
| `LLVMMDStringInContext` | ❌ |  |
| `LLVMMDString` | 🚫 | 🚫 Uses global context |
| `LLVMMDNodeInContext` | ❌ |  |
| `LLVMMDNode` | 🚫 | 🚫 Uses global context |

## Operand Bundles

*Lines 3372-3441*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMCreateOperandBundle` | ✅ |  |
| `LLVMDisposeOperandBundle` | ✅ |  |
| `LLVMGetOperandBundleTag` | ✅ |  |
| `LLVMGetNumOperandBundleArgs` | ✅ |  |

## Basic Block

*Lines 3442-3670*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMBasicBlockAsValue` | ✅ |  |
| `LLVMValueIsBasicBlock` | ✅ |  |
| `LLVMValueAsBasicBlock` | ✅ |  |
| `LLVMGetBasicBlockName` | ✅ |  |
| `LLVMGetBasicBlockParent` | ✅ |  |
| `LLVMGetBasicBlockTerminator` | ✅ |  |
| `LLVMCountBasicBlocks` | ✅ |  |
| `LLVMGetBasicBlocks` | ❌ |  |
| `LLVMGetFirstBasicBlock` | ✅ |  |
| `LLVMGetLastBasicBlock` | ✅ |  |
| `LLVMGetNextBasicBlock` | ✅ |  |
| `LLVMGetPreviousBasicBlock` | ✅ |  |
| `LLVMGetEntryBasicBlock` | ✅ |  |
| `LLVMAppendExistingBasicBlock` | ✅ |  |
| `LLVMCreateBasicBlockInContext` | ✅ |  |
| `LLVMAppendBasicBlockInContext` | ✅ |  |
| `LLVMAppendBasicBlock` | 🚫 | 🚫 Uses global context |
| `LLVMInsertBasicBlockInContext` | ❌ |  |
| `LLVMDeleteBasicBlock` | ❌ |  |
| `LLVMRemoveBasicBlockFromParent` | ❌ |  |
| `LLVMMoveBasicBlockBefore` | ✅ |  |
| `LLVMMoveBasicBlockAfter` | ✅ |  |
| `LLVMGetFirstInstruction` | ✅ |  |
| `LLVMGetLastInstruction` | ✅ |  |

## Instructions

*Lines 3671-3866*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMHasMetadata` | ❌ |  |
| `LLVMGetMetadata` | ❌ |  |
| `LLVMSetMetadata` | ✅ |  |
| `LLVMGetInstructionParent` | ✅ |  |
| `LLVMGetNextInstruction` | ✅ |  |
| `LLVMGetPreviousInstruction` | ✅ |  |
| `LLVMInstructionRemoveFromParent` | ✅ |  |
| `LLVMInstructionEraseFromParent` | ❌ |  |
| `LLVMDeleteInstruction` | ✅ |  |
| `LLVMGetInstructionOpcode` | ✅ |  |
| `LLVMGetICmpPredicate` | ✅ |  |
| `LLVMGetICmpSameSign` | ✅ |  |
| `LLVMSetICmpSameSign` | ✅ |  |
| `LLVMGetFCmpPredicate` | ✅ |  |
| `LLVMInstructionClone` | ❌ |  |
| `LLVMIsATerminatorInst` | ✅ |  |
| `LLVMGetFirstDbgRecord` | ✅ |  |
| `LLVMGetLastDbgRecord` | ✅ |  |
| `LLVMGetNextDbgRecord` | ✅ |  |

## Call Sites and Invocations

*Lines 3867-4070*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetNumArgOperands` | ✅ |  |
| `LLVMSetInstructionCallConv` | ❌ |  |
| `LLVMGetInstructionCallConv` | ❌ |  |
| `LLVMSetInstrParamAlignment` | ❌ |  |
| `LLVMAddCallSiteAttribute` | ✅ |  |
| `LLVMGetCallSiteAttributeCount` | ✅ |  |
| `LLVMGetCallSiteAttributes` | ❌ |  |
| `LLVMGetCallSiteEnumAttribute` | ✅ |  |
| `LLVMGetCallSiteStringAttribute` | ❌ |  |
| `LLVMRemoveCallSiteEnumAttribute` | ❌ |  |
| `LLVMRemoveCallSiteStringAttribute` | ❌ |  |
| `LLVMGetCalledFunctionType` | ✅ |  |
| `LLVMGetCalledValue` | ✅ |  |
| `LLVMGetNumOperandBundles` | ✅ |  |
| `LLVMGetOperandBundleAtIndex` | ✅ |  |
| `LLVMIsTailCall` | ❌ |  |
| `LLVMSetTailCall` | ❌ |  |
| `LLVMGetTailCallKind` | ✅ |  |
| `LLVMSetTailCallKind` | ✅ |  |
| `LLVMGetNormalDest` | ✅ |  |
| `LLVMGetUnwindDest` | ✅ |  |
| `LLVMSetNormalDest` | ❌ |  |
| `LLVMSetUnwindDest` | ❌ |  |
| `LLVMGetCallBrDefaultDest` | ✅ |  |
| `LLVMGetCallBrNumIndirectDests` | ✅ |  |
| `LLVMGetCallBrIndirectDest` | ✅ |  |

## Terminators

*Lines 4071-4141*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetNumSuccessors` | ✅ |  |
| `LLVMGetSuccessor` | ✅ |  |
| `LLVMSetSuccessor` | ❌ |  |
| `LLVMIsConditional` | ✅ |  |
| `LLVMGetCondition` | ✅ |  |
| `LLVMSetCondition` | ❌ |  |
| `LLVMGetSwitchDefaultDest` | ❌ |  |

## Allocas

*Lines 4142-4159*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetAllocatedType` | ✅ |  |

## GEPs

*Lines 4160-4202*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMIsInBounds` | ❌ |  |
| `LLVMSetIsInBounds` | ❌ |  |
| `LLVMGetGEPSourceElementType` | ✅ |  |
| `LLVMGEPGetNoWrapFlags` | ✅ |  |
| `LLVMGEPSetNoWrapFlags` | ❌ |  |

## PHI Nodes

*Lines 4203-4240*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMAddIncoming` | ✅ |  |
| `LLVMCountIncoming` | ✅ |  |
| `LLVMGetIncomingValue` | ✅ |  |
| `LLVMGetIncomingBlock` | ✅ |  |

## InsertValue

*Lines 4242-4273*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMGetNumIndices` | ✅ |  |
| `LLVMGetIndices` | ✅ |  |

## Instruction Builders

*Lines 4274-4929*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMCreateBuilderInContext` | ✅ | → `Context.create_builder()` |
| `LLVMCreateBuilder` | 🚫 | 🚫 Uses global context |
| `LLVMPositionBuilder` | ❌ |  |
| `LLVMPositionBuilderBeforeDbgRecords` | ✅ |  |
| `LLVMPositionBuilderBefore` | ✅ |  |
| `LLVMPositionBuilderAtEnd` | ✅ |  |
| `LLVMGetInsertBlock` | ✅ |  |
| `LLVMClearInsertionPosition` | ❌ |  |
| `LLVMInsertIntoBuilder` | ❌ |  |
| `LLVMInsertIntoBuilderWithName` | ✅ |  |
| `LLVMDisposeBuilder` | ✅ | → `Builder destructor` |
| `LLVMGetCurrentDebugLocation2` | ❌ |  |
| `LLVMSetCurrentDebugLocation2` | ❌ |  |
| `LLVMSetInstDebugLocation` | ❌ |  |
| `LLVMAddMetadataToInst` | ✅ |  |
| `LLVMBuilderSetDefaultFPMathTag` | ❌ |  |
| `LLVMGetBuilderContext` | ❌ |  |
| `LLVMSetCurrentDebugLocation` | ❌ |  |
| `LLVMGetCurrentDebugLocation` | ❌ |  |
| `LLVMBuildRetVoid` | ✅ |  |
| `LLVMBuildRet` | ✅ |  |
| `LLVMBuildAggregateRet` | ❌ |  |
| `LLVMBuildBr` | ✅ |  |
| `LLVMBuildCondBr` | ✅ |  |
| `LLVMBuildSwitch` | ✅ |  |
| `LLVMBuildIndirectBr` | ❌ |  |
| `LLVMBuildCallBr` | ✅ |  |
| `LLVMBuildInvoke2` | ❌ |  |
| `LLVMBuildInvokeWithOperandBundles` | ✅ |  |
| `LLVMBuildUnreachable` | ✅ |  |
| `LLVMBuildResume` | ✅ |  |
| `LLVMBuildLandingPad` | 🚫 | 🚫 Deprecated |
| `LLVMBuildCleanupRet` | ✅ |  |
| `LLVMBuildCatchRet` | ✅ |  |
| `LLVMBuildCatchPad` | ✅ |  |
| `LLVMBuildCleanupPad` | ✅ |  |
| `LLVMBuildCatchSwitch` | ✅ |  |
| `LLVMAddCase` | ✅ |  |
| `LLVMAddDestination` | ❌ |  |
| `LLVMGetNumClauses` | ✅ |  |
| `LLVMGetClause` | ✅ |  |
| `LLVMAddClause` | ✅ |  |
| `LLVMIsCleanup` | ✅ |  |
| `LLVMSetCleanup` | ✅ |  |
| `LLVMAddHandler` | ✅ |  |
| `LLVMGetNumHandlers` | ✅ |  |
| `LLVMGetHandlers` | ✅ |  |
| `LLVMGetArgOperand` | ✅ |  |
| `LLVMSetArgOperand` | ❌ |  |
| `LLVMGetParentCatchSwitch` | ✅ |  |
| `LLVMSetParentCatchSwitch` | ❌ |  |
| `LLVMBuildAdd` | ✅ |  |
| `LLVMBuildNSWAdd` | ✅ |  |
| `LLVMBuildNUWAdd` | ✅ |  |
| `LLVMBuildFAdd` | ✅ |  |
| `LLVMBuildSub` | ✅ |  |
| `LLVMBuildNSWSub` | ✅ |  |
| `LLVMBuildNUWSub` | ✅ |  |
| `LLVMBuildFSub` | ✅ |  |
| `LLVMBuildMul` | ✅ |  |
| `LLVMBuildNSWMul` | ✅ |  |
| `LLVMBuildNUWMul` | ✅ |  |
| `LLVMBuildFMul` | ✅ |  |
| `LLVMBuildUDiv` | ✅ |  |
| `LLVMBuildExactUDiv` | ❌ |  |
| `LLVMBuildSDiv` | ✅ |  |
| `LLVMBuildExactSDiv` | ✅ |  |
| `LLVMBuildFDiv` | ✅ |  |
| `LLVMBuildURem` | ✅ |  |
| `LLVMBuildSRem` | ✅ |  |
| `LLVMBuildFRem` | ✅ |  |
| `LLVMBuildShl` | ✅ |  |
| `LLVMBuildLShr` | ✅ |  |
| `LLVMBuildAShr` | ✅ |  |
| `LLVMBuildAnd` | ✅ |  |
| `LLVMBuildOr` | ✅ |  |
| `LLVMBuildXor` | ✅ |  |
| `LLVMBuildBinOp` | ✅ |  |
| `LLVMBuildNeg` | ✅ |  |
| `LLVMBuildNSWNeg` | ✅ |  |
| `LLVMBuildFNeg` | ✅ |  |
| `LLVMBuildNot` | ✅ |  |
| `LLVMGetNUW` | ✅ |  |
| `LLVMSetNUW` | ✅ |  |
| `LLVMGetNSW` | ✅ |  |
| `LLVMSetNSW` | ✅ |  |
| `LLVMGetExact` | ✅ |  |
| `LLVMSetExact` | ✅ |  |
| `LLVMGetNNeg` | ✅ |  |
| `LLVMSetNNeg` | ✅ |  |
| `LLVMGetFastMathFlags` | ✅ |  |
| `LLVMSetFastMathFlags` | ✅ |  |
| `LLVMCanValueUseFastMathFlags` | ✅ |  |
| `LLVMGetIsDisjoint` | ✅ |  |
| `LLVMSetIsDisjoint` | ✅ |  |
| `LLVMBuildMalloc` | ❌ |  |
| `LLVMBuildArrayMalloc` | ❌ |  |
| `LLVMBuildMemSet` | ❌ |  |
| `LLVMBuildMemCpy` | ❌ |  |
| `LLVMBuildMemMove` | ❌ |  |
| `LLVMBuildAlloca` | ✅ |  |
| `LLVMBuildArrayAlloca` | ✅ |  |
| `LLVMBuildFree` | ❌ |  |
| `LLVMBuildLoad2` | ✅ |  |
| `LLVMBuildStore` | ✅ |  |
| `LLVMBuildGEP2` | ✅ |  |
| `LLVMBuildInBoundsGEP2` | ✅ |  |
| `LLVMBuildGEPWithNoWrapFlags` | ✅ |  |
| `LLVMBuildStructGEP2` | ✅ |  |
| `LLVMBuildGlobalString` | 🚫 | 🚫 Deprecated |
| `LLVMBuildGlobalStringPtr` | 🚫 | 🚫 Deprecated |
| `LLVMGetVolatile` | ✅ |  |
| `LLVMSetVolatile` | ✅ |  |
| `LLVMGetWeak` | ✅ |  |
| `LLVMSetWeak` | ✅ |  |
| `LLVMGetOrdering` | ✅ |  |
| `LLVMSetOrdering` | ✅ |  |
| `LLVMGetAtomicRMWBinOp` | ✅ |  |
| `LLVMSetAtomicRMWBinOp` | ❌ |  |
| `LLVMBuildTrunc` | ✅ |  |
| `LLVMBuildZExt` | ✅ |  |
| `LLVMBuildSExt` | ✅ |  |
| `LLVMBuildFPToUI` | ✅ |  |
| `LLVMBuildFPToSI` | ✅ |  |
| `LLVMBuildUIToFP` | ✅ |  |
| `LLVMBuildSIToFP` | ✅ |  |
| `LLVMBuildFPTrunc` | ✅ |  |
| `LLVMBuildFPExt` | ✅ |  |
| `LLVMBuildPtrToInt` | ✅ |  |
| `LLVMBuildIntToPtr` | ✅ |  |
| `LLVMBuildBitCast` | ✅ |  |
| `LLVMBuildAddrSpaceCast` | ❌ |  |
| `LLVMBuildZExtOrBitCast` | ❌ |  |
| `LLVMBuildSExtOrBitCast` | ❌ |  |
| `LLVMBuildTruncOrBitCast` | ❌ |  |
| `LLVMBuildCast` | ❌ |  |
| `LLVMBuildPointerCast` | ❌ |  |
| `LLVMBuildIntCast2` | ✅ |  |
| `LLVMBuildFPCast` | ❌ |  |
| `LLVMBuildIntCast` | 🚫 | 🚫 Deprecated |
| `LLVMGetCastOpcode` | ❌ |  |
| `LLVMBuildICmp` | ✅ |  |
| `LLVMBuildFCmp` | ✅ |  |
| `LLVMBuildPhi` | ✅ |  |
| `LLVMBuildCall2` | ✅ |  |
| `LLVMBuildCallWithOperandBundles` | ✅ |  |
| `LLVMBuildSelect` | ✅ |  |
| `LLVMBuildVAArg` | ❌ |  |
| `LLVMBuildExtractElement` | ✅ |  |
| `LLVMBuildInsertElement` | ✅ |  |
| `LLVMBuildShuffleVector` | ✅ |  |
| `LLVMBuildExtractValue` | ✅ |  |
| `LLVMBuildInsertValue` | ✅ |  |
| `LLVMBuildFreeze` | ✅ |  |
| `LLVMBuildIsNull` | ❌ |  |
| `LLVMBuildIsNotNull` | ❌ |  |
| `LLVMBuildPtrDiff2` | ❌ |  |
| `LLVMBuildFence` | ❌ |  |
| `LLVMBuildFenceSyncScope` | ✅ |  |
| `LLVMBuildAtomicRMW` | ❌ |  |
| `LLVMBuildAtomicRMWSyncScope` | ✅ |  |
| `LLVMBuildAtomicCmpXchg` | ❌ |  |
| `LLVMBuildAtomicCmpXchgSyncScope` | ✅ |  |
| `LLVMGetNumMaskElements` | ✅ |  |
| `LLVMGetUndefMaskElem` | ✅ |  |
| `LLVMGetMaskValue` | ✅ |  |
| `LLVMIsAtomicSingleThread` | ❌ |  |
| `LLVMSetAtomicSingleThread` | ❌ |  |
| `LLVMIsAtomic` | ✅ |  |
| `LLVMGetAtomicSyncScopeID` | ✅ |  |
| `LLVMSetAtomicSyncScopeID` | ✅ |  |
| `LLVMSetCmpXchgSuccessOrdering` | ❌ |  |
| `LLVMSetCmpXchgFailureOrdering` | ❌ |  |

## Module Providers

*Lines 4930-4951*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMDisposeModuleProvider` | 🚫 | 🚫 Legacy PM - use PassBuilder |

## Memory Buffers

*Lines 4952-4974*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMCreateMemoryBufferWithContentsOfFile` | ✅ |  |
| `LLVMCreateMemoryBufferWithSTDIN` | ❌ |  |
| `LLVMCreateMemoryBufferWithMemoryRange` | ❌ |  |
| `LLVMCreateMemoryBufferWithMemoryRangeCopy` | ✅ |  |
| `LLVMGetBufferStart` | ✅ |  |
| `LLVMGetBufferSize` | ✅ |  |
| `LLVMDisposeMemoryBuffer` | ✅ |  |

## Pass Managers

*Lines 4975-5029*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMCreatePassManager` | 🚫 | 🚫 Legacy PM - use PassBuilder |
| `LLVMRunPassManager` | 🚫 | 🚫 Legacy PM - use PassBuilder |
| `LLVMInitializeFunctionPassManager` | 🚫 | 🚫 Legacy PM - use PassBuilder |
| `LLVMRunFunctionPassManager` | 🚫 | 🚫 Legacy PM - use PassBuilder |
| `LLVMFinalizeFunctionPassManager` | 🚫 | 🚫 Legacy PM - use PassBuilder |
| `LLVMDisposePassManager` | 🚫 | 🚫 Legacy PM - use PassBuilder |

## Threading

*Lines 5030-5065*

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMStartMultithreaded` | 🚫 | 🚫 Deprecated |
| `LLVMStopMultithreaded` | 🚫 | 🚫 Deprecated |
| `LLVMIsMultithreaded` | ❌ |  |
