# DebugInfo.h Feature Matrix

LLVM-C Debug Info API implementation status.

**Header:** `llvm-c/DebugInfo.h`

## Legend

| Status | Meaning |
|--------|---------|
| ✅ | Implemented |
| ❌ | Not implemented |
| 🚫 | Intentionally skipped |

## Functions

| Function | Status | Notes |
|----------|--------|-------|
| `LLVMDebugMetadataVersion` | ❌ | |
| `LLVMGetModuleDebugMetadataVersion` | ❌ | |
| `LLVMStripModuleDebugInfo` | ❌ | |
| `LLVMCreateDIBuilder` | ✅ | |
| `LLVMDisposeDIBuilder` | ✅ | |
| `LLVMDIBuilderFinalize` | ✅ | |
| `LLVMDIBuilderFinalizeSubprogram` | ❌ | |
| `LLVMDIBuilderCreateCompileUnit` | ✅ | |
| `LLVMDIBuilderCreateFile` | ✅ | |
| `LLVMDIBuilderCreateModule` | ✅ | |
| `LLVMDIBuilderCreateNameSpace` | ✅ | |
| `LLVMDIBuilderCreateFunction` | ✅ | |
| `LLVMDIBuilderCreateLexicalBlock` | ✅ | |
| `LLVMDIBuilderCreateLexicalBlockFile` | ❌ | |
| `LLVMDIBuilderCreateImportedModuleFromNamespace` | ❌ | |
| `LLVMDIBuilderCreateImportedModuleFromAlias` | ✅ | |
| `LLVMDIBuilderCreateImportedModuleFromModule` | ✅ | |
| `LLVMDIBuilderCreateImportedDeclaration` | ❌ | |
| `LLVMDIBuilderCreateDebugLocation` | ✅ | |
| `LLVMDILocationGetLine` | ❌ | |
| `LLVMDILocationGetColumn` | ❌ | |
| `LLVMDILocationGetScope` | ❌ | |
| `LLVMDILocationGetInlinedAt` | ❌ | |
| `LLVMDIScopeGetFile` | ❌ | |
| `LLVMDIFileGetDirectory` | ❌ | |
| `LLVMDIFileGetFilename` | ❌ | |
| `LLVMDIFileGetSource` | ❌ | |
| `LLVMDIBuilderGetOrCreateTypeArray` | ❌ | |
| `LLVMDIBuilderCreateSubroutineType` | ✅ | |
| `LLVMDIBuilderCreateMacro` | ✅ | |
| `LLVMDIBuilderCreateTempMacroFile` | ✅ | |
| `LLVMDIBuilderCreateEnumerator` | ✅ | |
| `LLVMDIBuilderCreateEnumeratorOfArbitraryPrecision` | ✅ | |
| `LLVMDIBuilderCreateEnumerationType` | ✅ | |
| `LLVMDIBuilderCreateUnionType` | ❌ | |
| `LLVMDIBuilderCreateArrayType` | ❌ | |
| `LLVMDIBuilderCreateSetType` | ✅ | |
| `LLVMDIBuilderCreateSubrangeType` | ✅ | |
| `LLVMDIBuilderCreateDynamicArrayType` | ✅ | |
| `LLVMReplaceArrays` | ✅ | |
| `LLVMDIBuilderCreateVectorType` | ✅ | |
| `LLVMDIBuilderCreateUnspecifiedType` | ❌ | |
| `LLVMDIBuilderCreateBasicType` | ✅ | |
| `LLVMDIBuilderCreatePointerType` | ✅ | |
| `LLVMDIBuilderCreateStructType` | ✅ | |
| `LLVMDIBuilderCreateMemberType` | ❌ | |
| `LLVMDIBuilderCreateStaticMemberType` | ❌ | |
| `LLVMDIBuilderCreateMemberPointerType` | ❌ | |
| `LLVMDIBuilderCreateObjCIVar` | ✅ | |
| `LLVMDIBuilderCreateObjCProperty` | ✅ | |
| `LLVMDIBuilderCreateObjectPointerType` | ❌ | |
| `LLVMDIBuilderCreateQualifiedType` | ❌ | |
| `LLVMDIBuilderCreateReferenceType` | ❌ | |
| `LLVMDIBuilderCreateTypedef` | ✅ | |
| `LLVMDIBuilderCreateInheritance` | ✅ | |
| `LLVMDIBuilderCreateForwardDecl` | ✅ | |
| `LLVMDIBuilderCreateReplaceableCompositeType` | ✅ | |
| `LLVMDIBuilderCreateBitFieldMemberType` | ❌ | |
| `LLVMDIBuilderCreateClassType` | ❌ | |
| `LLVMDIBuilderCreateArtificialType` | ❌ | |
| `LLVMDITypeGetName` | ✅ | |
| `LLVMDITypeGetSizeInBits` | ❌ | |
| `LLVMDITypeGetOffsetInBits` | ❌ | |
| `LLVMDITypeGetAlignInBits` | ❌ | |
| `LLVMDITypeGetLine` | ❌ | |
| `LLVMDITypeGetFlags` | ❌ | |
| `LLVMDIBuilderGetOrCreateSubrange` | ✅ | |
| `LLVMDIBuilderGetOrCreateArray` | ✅ | |
| `LLVMDIBuilderCreateExpression` | ✅ | |
| `LLVMDIBuilderCreateConstantValueExpression` | ✅ | |
| `LLVMDIBuilderCreateGlobalVariableExpression` | ✅ | |
| `LLVMGetDINodeTag` | ✅ | |
| `LLVMDIVariableGetFile` | ❌ | |
| `LLVMDIVariableGetScope` | ❌ | |
| `LLVMDIVariableGetLine` | ❌ | |
| `LLVMTemporaryMDNode` | ❌ | |
| `LLVMDisposeTemporaryMDNode` | ❌ | |
| `LLVMDIBuilderCreateTempGlobalVariableFwdDecl` | ❌ | |
| `LLVMDIBuilderInsertDeclareRecordBefore` | ❌ | |
| `LLVMDIBuilderInsertDeclareRecordAtEnd` | ✅ | |
| `LLVMDIBuilderInsertDbgValueRecordBefore` | ❌ | |
| `LLVMDIBuilderInsertDbgValueRecordAtEnd` | ✅ | |
| `LLVMDIBuilderCreateAutoVariable` | ✅ | |
| `LLVMDIBuilderCreateParameterVariable` | ✅ | |
| `LLVMGetSubprogram` | ❌ | |
| `LLVMSetSubprogram` | ✅ | |
| `LLVMDISubprogramGetLine` | ❌ | |
| `LLVMDISubprogramReplaceType` | ✅ | |
| `LLVMInstructionGetDebugLoc` | ❌ | |
| `LLVMInstructionSetDebugLoc` | ❌ | |
| `LLVMDIBuilderInsertLabelBefore` | ✅ | |
| `LLVMDIBuilderInsertLabelAtEnd` | ✅ | |
| `LLVMGetMetadataKind` | ❌ | |

## Summary

- **Total:** 93
- **Implemented:** 47
- **Not Implemented:** 46
- **Coverage:** 50.5%

