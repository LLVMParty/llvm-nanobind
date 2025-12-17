# llvm-c-test Python Port Plan

## Overview

### Goal

Port the LLVM-C test suite (`llvm-c-test`) from C/C++ to Python using the llvm-nanobind bindings. The Python port will be CLI-compatible with the original `llvm-c-test`, allowing it to be used as a drop-in replacement for the existing lit test infrastructure.

### Motivation

1. **Validate binding completeness**: Porting llvm-c-test exercises nearly all LLVM-C APIs
2. **Test binding correctness**: Golden-master comparison ensures identical behavior
3. **Living documentation**: Python port serves as comprehensive API usage examples
4. **Continuous testing**: Any binding regression will be caught by lit tests

### Scope

- Port all 22 llvm-c-test commands to Python
- Extend llvm-nanobind bindings as needed (~200+ new API bindings)
- Maintain byte-identical output with the C version
- Integrate with existing lit test framework

---

## CLI Design

### Command Line Interface

The Python port will mirror the exact CLI of `llvm-c-test`:

```bash
# C version
./build/llvm-c-test --module-dump < input.bc
./build/llvm-c-test --calc < calc.test
./build/llvm-c-test --targets-list

# Python version (drop-in replacement)
python -m llvm_c_test --module-dump < input.bc
python -m llvm_c_test --calc < calc.test
python -m llvm_c_test --targets-list
```

### Lit Integration

A wrapper script will allow substituting the Python version:

```python
#!/usr/bin/env python3
# llvm-c-test-py wrapper script
import sys
from llvm_c_test import main
sys.exit(main())
```

The lit configuration can then substitute:

```python
# lit.cfg.py
if os.environ.get("USE_PYTHON_LLVM_C_TEST"):
    config.substitutions.append(("llvm-c-test", "python -m llvm_c_test"))
else:
    config.substitutions.append(("llvm-c-test", llvm_c_test_exe))
```

---

## Architecture

### Directory Structure

```
llvm-nanobind/
├── llvm_c_test/                    # Python port package
│   ├── __init__.py
│   ├── __main__.py                 # CLI entry point
│   ├── main.py                     # Command dispatcher
│   ├── helpers.py                  # tokenize_stdin, utility functions
│   │
│   │  # Phase 1: Foundation
│   ├── targets.py                  # --targets-list
│   ├── calc.py                     # --calc
│   ├── module_ops.py               # --module-dump, --module-list-{functions,globals}
│   │
│   │  # Phase 2: Metadata & Attributes
│   ├── attributes.py               # --test-{function,callsite}-attributes
│   ├── metadata.py                 # --{add-named-metadata-operand,set-metadata,...}
│   │
│   │  # Phase 3: Complex Commands
│   ├── echo.py                     # --echo (module cloning)
│   ├── debuginfo.py                # --test-dibuilder, --get-di-tag, --di-type-get-name
│   ├── diagnostic.py               # --test-diagnostic-handler
│   │
│   │  # Phase 4: Platform-Specific
│   ├── disassemble.py              # --disassemble
│   └── object_file.py              # --object-list-{sections,symbols}
│
├── devdocs/llvm-c-test/
│   ├── plan.md                     # This file
│   └── progress.md                 # Implementation progress
│
└── tests/llvm-c-test-py/           # Optional: Python-specific tests
    └── ...
```

### Command to Module Mapping

| Command | Module | Source Reference |
|---------|--------|------------------|
| `--targets-list` | `targets.py` | `targets.c` |
| `--calc` | `calc.py` | `calc.c`, `helpers.c` |
| `--module-dump` | `module_ops.py` | `module.c` |
| `--lazy-module-dump` | `module_ops.py` | `module.c` |
| `--new-module-dump` | `module_ops.py` | `module.c` |
| `--lazy-new-module-dump` | `module_ops.py` | `module.c` |
| `--module-list-functions` | `module_ops.py` | `module.c` |
| `--module-list-globals` | `module_ops.py` | `module.c` |
| `--test-function-attributes` | `attributes.py` | `attributes.c` |
| `--test-callsite-attributes` | `attributes.py` | `attributes.c` |
| `--add-named-metadata-operand` | `metadata.py` | `metadata.c` |
| `--set-metadata` | `metadata.py` | `metadata.c` |
| `--replace-md-operand` | `metadata.py` | `metadata.c` |
| `--is-a-value-as-metadata` | `metadata.py` | `metadata.c` |
| `--echo` | `echo.py` | `echo.cpp` |
| `--test-dibuilder` | `debuginfo.py` | `debuginfo.c` |
| `--get-di-tag` | `debuginfo.py` | `debuginfo.c` |
| `--di-type-get-name` | `debuginfo.py` | `debuginfo.c` |
| `--test-diagnostic-handler` | `diagnostic.py` | `diagnostic.c` |
| `--disassemble` | `disassemble.py` | `disassemble.c` |
| `--object-list-sections` | `object_file.py` | `object.c` |
| `--object-list-symbols` | `object_file.py` | `object.c` |

---

## Phase 1: Foundation & Simple Commands

### Commands to Implement

1. `--targets-list` - List all registered LLVM targets
2. `--calc` - RPN calculator that generates IR
3. `--module-dump` - Parse bitcode and print IR
4. `--lazy-module-dump` - Lazy load and print bitcode
5. `--new-module-dump` - Parse bitcode (new API) and print IR
6. `--lazy-new-module-dump` - Lazy load (new API) and print IR
7. `--module-list-functions` - List functions with basic block/instruction counts
8. `--module-list-globals` - List global variables

### Required Binding Extensions

#### Support.h (Memory Buffer)

| API | Description |
|-----|-------------|
| `LLVMCreateMemoryBufferWithSTDIN` | Read stdin into memory buffer |
| `LLVMCreateMemoryBufferWithContentsOfFile` | Read file into memory buffer |
| `LLVMDisposeMemoryBuffer` | Free memory buffer |
| `LLVMGetBufferStart` | Get buffer data pointer |
| `LLVMGetBufferSize` | Get buffer size |

#### BitReader.h

| API | Description |
|-----|-------------|
| `LLVMParseBitcodeInContext` | Parse bitcode (legacy, with error message) |
| `LLVMParseBitcodeInContext2` | Parse bitcode (new, uses diagnostic handler) |
| `LLVMGetBitcodeModuleInContext` | Lazy load bitcode (legacy) |
| `LLVMGetBitcodeModuleInContext2` | Lazy load bitcode (new) |
| `LLVMGetBitcodeModule2` | Lazy load from global context |

#### Target.h

| API | Description |
|-----|-------------|
| `LLVMInitializeAllTargetInfos` | Initialize all target info |
| `LLVMInitializeAllTargets` | Initialize all targets |
| `LLVMInitializeAllTargetMCs` | Initialize all target MCs |
| `LLVMInitializeAllAsmPrinters` | Initialize all asm printers |
| `LLVMInitializeAllAsmParsers` | Initialize all asm parsers |
| `LLVMInitializeAllDisassemblers` | Initialize all disassemblers |
| `LLVMGetFirstTarget` | Get first registered target |
| `LLVMGetNextTarget` | Get next target in list |
| `LLVMGetTargetName` | Get target name |
| `LLVMGetTargetDescription` | Get target description |
| `LLVMTargetHasJIT` | Check if target has JIT |
| `LLVMTargetHasTargetMachine` | Check if target has target machine |
| `LLVMTargetHasAsmBackend` | Check if target has asm backend |

#### Core.h (Additional APIs)

| API | Description |
|-----|-------------|
| `LLVMGetFirstInstruction` | Get first instruction in basic block |
| `LLVMGetLastInstruction` | Get last instruction in basic block |
| `LLVMGetNextInstruction` | Get next instruction |
| `LLVMGetPreviousInstruction` | Get previous instruction |
| `LLVMIsACallInst` | Check if value is call instruction |
| `LLVMGetNumOperands` | Get number of operands |
| `LLVMGetOperand` | Get operand by index |
| `LLVMIsDeclaration` | Check if global is declaration |
| `LLVMBuildBinOp` | Build binary operation by opcode |
| `LLVMContextSetDiagnosticHandler` | Set diagnostic handler callback |
| `LLVMGetDiagInfoDescription` | Get diagnostic description |
| `LLVMGetDiagInfoSeverity` | Get diagnostic severity |

---

## Phase 2: Metadata & Attributes

### Commands to Implement

1. `--test-function-attributes` - Test function attribute enumeration
2. `--test-callsite-attributes` - Test callsite attribute enumeration
3. `--add-named-metadata-operand` - Test adding named metadata
4. `--set-metadata` - Test setting instruction metadata
5. `--replace-md-operand` - Test replacing metadata operands
6. `--is-a-value-as-metadata` - Test ValueAsMetadata

### Required Binding Extensions

#### Core.h (Attributes)

| API | Description |
|-----|-------------|
| `LLVMAttributeReturnIndex` | Constant for return index |
| `LLVMAttributeFunctionIndex` | Constant for function index |
| `LLVMGetAttributeCountAtIndex` | Count attributes at index |
| `LLVMGetAttributesAtIndex` | Get all attributes at index |
| `LLVMGetEnumAttributeAtIndex` | Get enum attribute at index |
| `LLVMAddAttributeAtIndex` | Add attribute at index |
| `LLVMGetCallSiteAttributeCount` | Count callsite attributes |
| `LLVMGetCallSiteAttributes` | Get all callsite attributes |
| `LLVMGetCallSiteEnumAttribute` | Get callsite enum attribute |
| `LLVMAddCallSiteAttribute` | Add callsite attribute |
| `LLVMGetLastEnumAttributeKind` | Get last enum attribute kind |
| `LLVMCreateEnumAttribute` | Create enum attribute |
| `LLVMGetEnumAttributeValue` | Get enum attribute value |

#### Core.h (Metadata)

| API | Description |
|-----|-------------|
| `LLVMMDNode` | Create metadata node (global context) |
| `LLVMMDNodeInContext` | Create metadata node (explicit context) |
| `LLVMMDNodeInContext2` | Create metadata node from LLVMMetadataRef |
| `LLVMMDStringInContext` | Create metadata string (explicit context) |
| `LLVMMDStringInContext2` | Create metadata string (returns LLVMMetadataRef) |
| `LLVMGetMDString` | Get string from metadata |
| `LLVMSetMetadata` | Set instruction metadata |
| `LLVMGetMetadata` | Get instruction metadata |
| `LLVMGetMDKindID` | Get metadata kind ID |
| `LLVMGetMDKindIDInContext` | Get metadata kind ID (explicit context) |
| `LLVMAddNamedMetadataOperand` | Add operand to named metadata |
| `LLVMGetNamedMetadataNumOperands` | Get operand count |
| `LLVMGetNamedMetadataOperands` | Get all operands |
| `LLVMMetadataAsValue` | Convert metadata to value |
| `LLVMValueAsMetadata` | Convert value to metadata |
| `LLVMReplaceMDNodeOperandWith` | Replace metadata operand |
| `LLVMIsAValueAsMetadata` | Check if value is ValueAsMetadata |
| `LLVMDeleteInstruction` | Delete instruction |
| `LLVMGetModuleContext` | Get module's context |

#### ErrorHandling.h

| API | Description |
|-----|-------------|
| `LLVMEnablePrettyStackTrace` | Enable pretty stack traces |

---

## Phase 3: Complex Commands (Echo & Debug Info)

### Commands to Implement

1. ✅ `--test-dibuilder` - Comprehensive debug info builder test **COMPLETE**
2. ✅ `--get-di-tag` - Get DWARF tag from debug info node **COMPLETE**
3. ✅ `--di-type-get-name` - Get type name from debug info **COMPLETE**
4. ✅ `--test-diagnostic-handler` - Test diagnostic handler callbacks **COMPLETE**

**Status**: 4/4 commands complete (100%). The `--echo` command has been moved to Phase 5.

### 3.1 Echo Command Requirements

The `--echo` command is the most comprehensive test, requiring cloning of:
- All type kinds (20+ types)
- All constant kinds (15+ kinds)
- All instruction types (30+ opcodes)
- Global variables, aliases, IFuncs
- Named metadata
- Attributes and operand bundles
- Inline assembly

#### Core.h (Type Cloning)

| API | Description |
|-----|-------------|
| `LLVMGetTypeKind` | Get type kind enum |
| `LLVMHalfTypeInContext` | Create half type |
| `LLVMBFloatTypeInContext` | Create bfloat type |
| `LLVMX86FP80TypeInContext` | Create x86 fp80 type |
| `LLVMFP128TypeInContext` | Create fp128 type |
| `LLVMPPCFP128TypeInContext` | Create ppc fp128 type |
| `LLVMLabelTypeInContext` | Create label type |
| `LLVMMetadataTypeInContext` | Create metadata type |
| `LLVMX86AMXTypeInContext` | Create x86 AMX type |
| `LLVMTokenTypeInContext` | Create token type |
| `LLVMScalableVectorType` | Create scalable vector type |
| `LLVMTargetExtTypeInContext` | Create target extension type |
| `LLVMGetTargetExtTypeName` | Get target ext type name |
| `LLVMGetTargetExtTypeNumTypeParams` | Get type param count |
| `LLVMGetTargetExtTypeNumIntParams` | Get int param count |
| `LLVMGetTargetExtTypeTypeParam` | Get type param at index |
| `LLVMGetTargetExtTypeIntParam` | Get int param at index |
| `LLVMGetTypeByName2` | Get named type from context |
| `LLVMStructGetTypeAtIndex` | Get struct element type |
| `LLVMPointerTypeIsOpaque` | Check if pointer is opaque |
| `LLVMPointerType` | Create typed pointer (legacy) |
| `LLVMCountParamTypes` | Count function param types |
| `LLVMGetParamTypes` | Get function param types |
| `LLVMGetReturnType` | Get function return type |
| `LLVMGetElementType` | Get element type |
| `LLVMGetArrayLength2` | Get array length |
| `LLVMGetVectorSize` | Get vector size |

#### Core.h (Constant Cloning)

| API | Description |
|-----|-------------|
| `LLVMIsAConstant` | Check if value is constant |
| `LLVMIsAGlobalValue` | Check if constant is global |
| `LLVMIsAFunction` | Check if value is function |
| `LLVMIsAGlobalVariable` | Check if value is global var |
| `LLVMIsAGlobalAlias` | Check if value is alias |
| `LLVMIsAConstantInt` | Check if constant is int |
| `LLVMIsAConstantFP` | Check if constant is FP |
| `LLVMIsAConstantAggregateZero` | Check if zeroinitializer |
| `LLVMIsAConstantDataArray` | Check if constant data array |
| `LLVMIsAConstantArray` | Check if constant array |
| `LLVMIsAConstantStruct` | Check if constant struct |
| `LLVMIsAConstantPointerNull` | Check if null pointer |
| `LLVMIsAConstantVector` | Check if constant vector |
| `LLVMIsAConstantDataVector` | Check if constant data vector |
| `LLVMIsAConstantExpr` | Check if constant expression |
| `LLVMIsAConstantPtrAuth` | Check if pointer auth constant |
| `LLVMGetIntrinsicID` | Get intrinsic ID |
| `LLVMIntrinsicIsOverloaded` | Check if intrinsic overloaded |
| `LLVMGetIntrinsicDeclaration` | Get intrinsic declaration |
| `LLVMGetRawDataValues` | Get raw constant data |
| `LLVMConstDataArray` | Create constant data array |
| `LLVMGetAggregateElement` | Get aggregate element |
| `LLVMGetConstOpcode` | Get constant expr opcode |
| `LLVMConstBitCast` | Create const bitcast |
| `LLVMConstGEPWithNoWrapFlags` | Create const GEP with flags |
| `LLVMGEPGetNoWrapFlags` | Get GEP no-wrap flags |
| `LLVMGetGEPSourceElementType` | Get GEP source element type |
| `LLVMGetNumIndices` | Get number of indices |
| `LLVMConstantPtrAuth` | Create pointer auth constant |
| `LLVMGetConstantPtrAuthPointer` | Get ptr auth pointer |
| `LLVMGetConstantPtrAuthKey` | Get ptr auth key |
| `LLVMGetConstantPtrAuthDiscriminator` | Get ptr auth discriminator |
| `LLVMGetConstantPtrAuthAddrDiscriminator` | Get ptr auth addr discriminator |

#### Core.h (Instruction Cloning)

| API | Description |
|-----|-------------|
| `LLVMGetInstructionOpcode` | Get instruction opcode |
| `LLVMInstructionRemoveFromParent` | Remove instruction from BB |
| `LLVMInsertIntoBuilderWithName` | Insert instruction at builder |
| `LLVMGetNUW` | Get no unsigned wrap flag |
| `LLVMSetNUW` | Set no unsigned wrap flag |
| `LLVMGetNSW` | Get no signed wrap flag |
| `LLVMSetNSW` | Set no signed wrap flag |
| `LLVMGetExact` | Get exact flag |
| `LLVMSetExact` | Set exact flag |
| `LLVMGetNNeg` | Get non-negative flag |
| `LLVMSetNNeg` | Set non-negative flag |
| `LLVMGetIsDisjoint` | Get disjoint flag |
| `LLVMSetIsDisjoint` | Set disjoint flag |
| `LLVMCanValueUseFastMathFlags` | Check if can use FMF |
| `LLVMGetFastMathFlags` | Get fast math flags |
| `LLVMSetFastMathFlags` | Set fast math flags |
| `LLVMGetICmpSameSign` | Get icmp same sign |
| `LLVMSetICmpSameSign` | Set icmp same sign |
| `LLVMGetOrdering` | Get atomic ordering |
| `LLVMSetOrdering` | Set atomic ordering |
| `LLVMIsAtomic` | Check if atomic |
| `LLVMGetAtomicSyncScopeID` | Get sync scope ID |
| `LLVMSetAtomicSyncScopeID` | Set sync scope ID |
| `LLVMGetAtomicRMWBinOp` | Get atomicrmw operation |
| `LLVMGetCmpXchgSuccessOrdering` | Get cmpxchg success ordering |
| `LLVMGetCmpXchgFailureOrdering` | Get cmpxchg failure ordering |
| `LLVMGetWeak` | Get cmpxchg weak flag |
| `LLVMSetWeak` | Set cmpxchg weak flag |
| `LLVMGetVolatile` | Get volatile flag |
| `LLVMSetVolatile` | Set volatile flag |
| `LLVMGetAllocatedType` | Get alloca allocated type |
| `LLVMGetTailCallKind` | Get tail call kind |
| `LLVMSetTailCallKind` | Set tail call kind |
| `LLVMGetCalledFunctionType` | Get called function type |
| `LLVMGetCalledValue` | Get called value |
| `LLVMGetNumArgOperands` | Get arg operand count |
| `LLVMGetArgOperand` | Get arg operand |

#### Core.h (Builder Instructions - Additional)

| API | Description |
|-----|-------------|
| `LLVMBuildInvokeWithOperandBundles` | Build invoke with bundles |
| `LLVMBuildCallWithOperandBundles` | Build call with bundles |
| `LLVMBuildCallBr` | Build callbr |
| `LLVMBuildResume` | Build resume |
| `LLVMBuildLandingPad` | Build landing pad |
| `LLVMAddClause` | Add landing pad clause |
| `LLVMSetCleanup` | Set landing pad cleanup |
| `LLVMIsCleanup` | Get landing pad cleanup |
| `LLVMGetNumClauses` | Get clause count |
| `LLVMGetClause` | Get clause at index |
| `LLVMBuildCatchSwitch` | Build catch switch |
| `LLVMBuildCatchPad` | Build catch pad |
| `LLVMBuildCleanupPad` | Build cleanup pad |
| `LLVMBuildCatchRet` | Build catch ret |
| `LLVMBuildCleanupRet` | Build cleanup ret |
| `LLVMAddHandler` | Add catch switch handler |
| `LLVMGetNumHandlers` | Get handler count |
| `LLVMGetHandlers` | Get all handlers |
| `LLVMGetParentCatchSwitch` | Get parent catch switch |
| `LLVMBuildExtractValue` | Build extractvalue |
| `LLVMBuildInsertValue` | Build insertvalue |
| `LLVMBuildExtractElement` | Build extractelement |
| `LLVMBuildInsertElement` | Build insertelement |
| `LLVMBuildShuffleVector` | Build shufflevector |
| `LLVMGetNumMaskElements` | Get shuffle mask size |
| `LLVMGetMaskValue` | Get shuffle mask value |
| `LLVMGetUndefMaskElem` | Get undef mask element value |
| `LLVMBuildFreeze` | Build freeze |
| `LLVMBuildFenceSyncScope` | Build fence with sync scope |
| `LLVMBuildAtomicRMWSyncScope` | Build atomicrmw with scope |
| `LLVMBuildAtomicCmpXchgSyncScope` | Build cmpxchg with scope |
| `LLVMBuildGEPWithNoWrapFlags` | Build GEP with no-wrap flags |
| `LLVMGetIndices` | Get extractvalue/insertvalue indices |
| `LLVMGetNormalDest` | Get invoke normal dest |
| `LLVMGetUnwindDest` | Get invoke/cleanupret unwind dest |
| `LLVMGetSuccessor` | Get terminator successor |
| `LLVMGetCallBrDefaultDest` | Get callbr default dest |
| `LLVMGetCallBrNumIndirectDests` | Get callbr indirect dest count |
| `LLVMGetCallBrIndirectDest` | Get callbr indirect dest |

#### Core.h (Operand Bundles)

| API | Description |
|-----|-------------|
| `LLVMGetNumOperandBundles` | Get bundle count |
| `LLVMGetOperandBundleAtIndex` | Get bundle at index |
| `LLVMCreateOperandBundle` | Create operand bundle |
| `LLVMDisposeOperandBundle` | Dispose operand bundle |
| `LLVMGetOperandBundleTag` | Get bundle tag |
| `LLVMGetNumOperandBundleArgs` | Get bundle arg count |
| `LLVMGetOperandBundleArgAtIndex` | Get bundle arg |

#### Core.h (Inline Assembly)

| API | Description |
|-----|-------------|
| `LLVMIsAInlineAsm` | Check if inline asm |
| `LLVMGetInlineAsm` | Create inline asm |
| `LLVMGetInlineAsmAsmString` | Get asm string |
| `LLVMGetInlineAsmConstraintString` | Get constraint string |
| `LLVMGetInlineAsmDialect` | Get asm dialect |
| `LLVMGetInlineAsmFunctionType` | Get asm function type |
| `LLVMGetInlineAsmHasSideEffects` | Get has side effects |
| `LLVMGetInlineAsmNeedsAlignedStack` | Get needs aligned stack |
| `LLVMGetInlineAsmCanUnwind` | Get can unwind |

#### Core.h (Global Variable Cloning)

| API | Description |
|-----|-------------|
| `LLVMGlobalGetValueType` | Get global value type |
| `LLVMGetFirstGlobalAlias` | Get first alias |
| `LLVMGetLastGlobalAlias` | Get last alias |
| `LLVMGetNextGlobalAlias` | Get next alias |
| `LLVMGetPreviousGlobalAlias` | Get previous alias |
| `LLVMGetNamedGlobalAlias` | Get alias by name |
| `LLVMAddAlias2` | Add alias |
| `LLVMAliasGetAliasee` | Get aliasee |
| `LLVMAliasSetAliasee` | Set aliasee |
| `LLVMGetFirstGlobalIFunc` | Get first ifunc |
| `LLVMGetLastGlobalIFunc` | Get last ifunc |
| `LLVMGetNextGlobalIFunc` | Get next ifunc |
| `LLVMGetPreviousGlobalIFunc` | Get previous ifunc |
| `LLVMGetNamedGlobalIFunc` | Get ifunc by name |
| `LLVMAddGlobalIFunc` | Add ifunc |
| `LLVMGetGlobalIFuncResolver` | Get ifunc resolver |
| `LLVMSetGlobalIFuncResolver` | Set ifunc resolver |
| `LLVMGlobalCopyAllMetadata` | Copy global metadata |
| `LLVMGlobalSetMetadata` | Set global metadata |
| `LLVMGetUnnamedAddress` | Get unnamed address |
| `LLVMSetUnnamedAddress` | Set unnamed address |
| `LLVMHasPersonalityFn` | Check has personality |
| `LLVMGetPersonalityFn` | Get personality fn |
| `LLVMSetPersonalityFn` | Set personality fn |
| `LLVMHasPrefixData` | Check has prefix data |
| `LLVMGetPrefixData` | Get prefix data |
| `LLVMSetPrefixData` | Set prefix data |
| `LLVMHasPrologueData` | Check has prologue data |
| `LLVMGetPrologueData` | Get prologue data |
| `LLVMSetPrologueData` | Set prologue data |

#### Core.h (Named Metadata)

| API | Description |
|-----|-------------|
| `LLVMGetFirstNamedMetadata` | Get first named MD |
| `LLVMGetLastNamedMetadata` | Get last named MD |
| `LLVMGetNextNamedMetadata` | Get next named MD |
| `LLVMGetPreviousNamedMetadata` | Get previous named MD |
| `LLVMGetNamedMetadataName` | Get named MD name |
| `LLVMGetNamedMetadata` | Get named MD by name |
| `LLVMGetOrInsertNamedMetadata` | Get or create named MD |

#### Core.h (Module Properties)

| API | Description |
|-----|-------------|
| `LLVMGetModuleInlineAsm` | Get module inline asm |
| `LLVMSetModuleInlineAsm2` | Set module inline asm |
| `LLVMSetModuleDataLayout` | Set module data layout |
| `LLVMGetModuleDataLayout` | Get module data layout |

#### Core.h (Parameter Iteration)

| API | Description |
|-----|-------------|
| `LLVMGetFirstParam` | Get first parameter |
| `LLVMGetLastParam` | Get last parameter |
| `LLVMGetNextParam` | Get next parameter |
| `LLVMGetPreviousParam` | Get previous parameter |

#### Core.h (BasicBlock Operations)

| API | Description |
|-----|-------------|
| `LLVMValueIsBasicBlock` | Check if value is BB |
| `LLVMMoveBasicBlockBefore` | Move BB before another |
| `LLVMMoveBasicBlockAfter` | Move BB after another |

#### Core.h (Instruction Metadata)

| API | Description |
|-----|-------------|
| `LLVMInstructionGetAllMetadataOtherThanDebugLoc` | Get all instruction metadata |
| `LLVMValueMetadataEntriesGetKind` | Get metadata entry kind |
| `LLVMValueMetadataEntriesGetMetadata` | Get metadata entry metadata |
| `LLVMDisposeValueMetadataEntries` | Dispose metadata entries |
| `LLVMAddMetadataToInst` | Add builder metadata to instruction |

### 3.2 Debug Info Requirements

#### DebugInfo.h

| API | Description |
|-----|-------------|
| `LLVMCreateDIBuilder` | Create debug info builder |
| `LLVMDisposeDIBuilder` | Dispose debug info builder |
| `LLVMDIBuilderFinalize` | Finalize debug info |
| `LLVMDIBuilderCreateFile` | Create file metadata |
| `LLVMDIBuilderCreateCompileUnit` | Create compile unit |
| `LLVMDIBuilderCreateModule` | Create module metadata |
| `LLVMDIBuilderCreateNameSpace` | Create namespace |
| `LLVMDIBuilderCreateFunction` | Create function metadata |
| `LLVMDIBuilderCreateSubroutineType` | Create subroutine type |
| `LLVMDIBuilderCreateLexicalBlock` | Create lexical block |
| `LLVMSetSubprogram` | Set function subprogram |
| `LLVMDIBuilderCreateBasicType` | Create basic type |
| `LLVMDIBuilderCreateStructType` | Create struct type |
| `LLVMDIBuilderCreatePointerType` | Create pointer type |
| `LLVMDIBuilderCreateVectorType` | Create vector type |
| `LLVMDIBuilderCreateArrayType` | Create array type |
| `LLVMDIBuilderCreateTypedef` | Create typedef |
| `LLVMDIBuilderCreateForwardDecl` | Create forward declaration |
| `LLVMDIBuilderCreateEnumerationType` | Create enum type |
| `LLVMDIBuilderCreateEnumerator` | Create enumerator |
| `LLVMDIBuilderCreateEnumeratorOfArbitraryPrecision` | Create large enumerator |
| `LLVMDIBuilderCreateSubrangeType` | Create subrange type |
| `LLVMDIBuilderCreateSetType` | Create set type |
| `LLVMDIBuilderCreateDynamicArrayType` | Create dynamic array type |
| `LLVMDIBuilderCreateReplaceableCompositeType` | Create replaceable type |
| `LLVMDIBuilderCreateObjCProperty` | Create ObjC property |
| `LLVMDIBuilderCreateObjCIVar` | Create ObjC ivar |
| `LLVMDIBuilderCreateInheritance` | Create inheritance |
| `LLVMDIBuilderCreateGlobalVariableExpression` | Create global var expr |
| `LLVMDIBuilderCreateParameterVariable` | Create parameter var |
| `LLVMDIBuilderCreateAutoVariable` | Create auto var |
| `LLVMDIBuilderCreateLabel` | Create label |
| `LLVMDIBuilderInsertLabelAtEnd` | Insert label at end |
| `LLVMDIBuilderInsertLabelBefore` | Insert label before |
| `LLVMDIBuilderCreateDebugLocation` | Create debug location |
| `LLVMDIBuilderCreateExpression` | Create expression |
| `LLVMDIBuilderCreateConstantValueExpression` | Create const value expr |
| `LLVMDIBuilderInsertDeclareRecordAtEnd` | Insert declare at end |
| `LLVMDIBuilderInsertDbgValueRecordAtEnd` | Insert dbg value at end |
| `LLVMDIBuilderCreateTempMacroFile` | Create temp macro file |
| `LLVMDIBuilderCreateMacro` | Create macro |
| `LLVMDIBuilderGetOrCreateSubrange` | Get or create subrange |
| `LLVMDIBuilderGetOrCreateArray` | Get or create array |
| `LLVMReplaceArrays` | Replace arrays in type |
| `LLVMDIBuilderCreateImportedModuleFromModule` | Create imported module |
| `LLVMDIBuilderCreateImportedModuleFromAlias` | Create imported from alias |
| `LLVMMetadataReplaceAllUsesWith` | Replace metadata uses |
| `LLVMDISubprogramReplaceType` | Replace subprogram type |
| `LLVMSetIsNewDbgInfoFormat` | Set new debug format |
| `LLVMIsNewDbgInfoFormat` | Check new debug format |
| `LLVMGetFirstDbgRecord` | Get first debug record |
| `LLVMGetLastDbgRecord` | Get last debug record |
| `LLVMGetNextDbgRecord` | Get next debug record |
| `LLVMGetPreviousDbgRecord` | Get previous debug record |
| `LLVMGetDINodeTag` | Get DI node DWARF tag |
| `LLVMDITypeGetName` | Get DI type name |
| `LLVMPositionBuilderBeforeInstrAndDbgRecords` | Position before instr and dbg |
| `LLVMPositionBuilderBeforeDbgRecords` | Position before dbg records |

### 3.3 Diagnostic Handler Requirements

| API | Description |
|-----|-------------|
| `LLVMContextSetDiagnosticHandler` | Set diagnostic callback |
| `LLVMContextGetDiagnosticHandler` | Get diagnostic callback |
| `LLVMContextGetDiagnosticContext` | Get diagnostic context |
| `LLVMGetDiagInfoSeverity` | Get diagnostic severity |
| `LLVMGetDiagInfoDescription` | Get diagnostic description |

---

## Phase 4: Platform-Specific ✅ COMPLETE

### Commands Implemented

1. ✅ `--disassemble` - Disassemble hex bytes
2. ✅ `--object-list-sections` - List sections in object file
3. ✅ `--object-list-symbols` - List symbols in object file

**Status**: All 3 commands complete. Disassembly works for x86, ARM, and other architectures. Object file parsing handles Mach-O, ELF, COFF formats.

### 4.1 Disassemble Command Requirements

#### Disassembler.h

| API | Description |
|-----|-------------|
| `LLVMCreateDisasm` | Create disassembler (basic) |
| `LLVMCreateDisasmCPU` | Create disassembler with CPU |
| `LLVMCreateDisasmCPUFeatures` | Create disassembler with features |
| `LLVMDisasmInstruction` | Disassemble single instruction |
| `LLVMSetDisasmOptions` | Set disassembler options |
| `LLVMDisasmDispose` | Dispose disassembler |

### 4.2 Object File Command Requirements

#### Object.h

| API | Description |
|-----|-------------|
| `LLVMCreateBinary` | Create binary from buffer |
| `LLVMDisposeBinary` | Dispose binary |
| `LLVMObjectFileCopySectionIterator` | Get section iterator |
| `LLVMObjectFileIsSectionIteratorAtEnd` | Check iterator at end |
| `LLVMMoveToNextSection` | Move to next section |
| `LLVMDisposeSectionIterator` | Dispose section iterator |
| `LLVMGetSectionName` | Get section name |
| `LLVMGetSectionAddress` | Get section address |
| `LLVMGetSectionSize` | Get section size |
| `LLVMObjectFileCopySymbolIterator` | Get symbol iterator |
| `LLVMObjectFileIsSymbolIteratorAtEnd` | Check iterator at end |
| `LLVMMoveToNextSymbol` | Move to next symbol |
| `LLVMDisposeSymbolIterator` | Dispose symbol iterator |
| `LLVMGetSymbolName` | Get symbol name |
| `LLVMGetSymbolAddress` | Get symbol address |
| `LLVMGetSymbolSize` | Get symbol size |
| `LLVMMoveToContainingSection` | Move to symbol's section |

---

## Testing Strategy

### Golden-Master Testing

The Python port is validated by comparing its output to the C `llvm-c-test`:

```bash
# C version produces golden output
./build/llvm-c-test --module-list-globals < input.bc > expected.txt

# Python version must match exactly
python -m llvm_c_test --module-list-globals < input.bc > actual.txt

# Comparison
diff expected.txt actual.txt
```

### Lit Integration

The existing lit test suite can be used with minimal modifications:

```python
# lit.cfg.py modification
if os.environ.get("USE_PYTHON_LLVM_C_TEST"):
    config.substitutions.append(("llvm-c-test", "python -m llvm_c_test"))
else:
    config.substitutions.append(("llvm-c-test", llvm_c_test_exe))
```

Running tests:

```bash
# Run with C version
uv run python run_llvm_c_tests.py

# Run with Python version
USE_PYTHON_LLVM_C_TEST=1 uv run python run_llvm_c_tests.py
```

### Test Categories

| Category | Test Files | Commands Covered |
|----------|------------|------------------|
| Echo Tests | `atomics.ll`, `echo.ll`, `float_ops.ll`, `freeze.ll`, `invoke.ll`, `memops.ll` | `--echo` |
| Module Tests | `functions.ll`, `globals.ll`, `empty.ll` | `--module-dump`, `--module-list-*` |
| Calc Tests | `calc.test` | `--calc` |
| Attribute Tests | `function_attributes.ll`, `callsite_attributes.ll` | `--test-*-attributes` |
| Metadata Tests | `add_named_metadata_operand.ll`, `set_metadata.ll`, `replace_md_operand.ll`, `is_a_value_as_metadata.ll` | `--*-metadata*` |
| Debug Info Tests | `debug_info_new_format.ll`, `get-di-tag.ll`, `di-type-get-name.ll` | `--test-dibuilder`, `--get-di-tag`, `--di-type-get-name` |
| Error Tests | `invalid-bitcode.test` | Error handling |
| Disassembly Tests | `X86/disassemble.test`, `ARM/disassemble.test` | `--disassemble` |
| Object Tests | `objectfile.ll` | `--object-list-*` |

---

## Implementation Order

### Recommended Sequence

1. **Infrastructure First**
   - Create package structure (`llvm_c_test/`)
   - Implement CLI dispatcher (`main.py`, `__main__.py`)
   - Implement helper functions (`helpers.py`)

2. **Phase 1 Commands** (Foundation)
   - `--targets-list` (no bitcode input)
   - `--calc` (generates IR, no parsing)
   - `--module-dump` (requires BitReader)
   - `--module-list-functions` (requires instruction iteration)
   - `--module-list-globals` (straightforward)

3. **Phase 2 Commands** (Metadata & Attributes)
   - `--add-named-metadata-operand`
   - `--set-metadata`
   - `--replace-md-operand`
   - `--is-a-value-as-metadata`
   - `--test-function-attributes`
   - `--test-callsite-attributes`

4. **Phase 3 Commands** (Complex) ✅ COMPLETE
   - `--test-dibuilder` (large but self-contained)
   - `--get-di-tag`
   - `--di-type-get-name`
   - `--test-diagnostic-handler`

5. **Phase 4 Commands** (Platform-Specific) ✅ COMPLETE
   - `--disassemble`
   - `--object-list-sections`
   - `--object-list-symbols`

6. **Phase 5 Commands** (Echo - Module Cloning)
   - `--echo` (most complex, requires ~150 additional APIs)

### Binding Implementation Strategy

For each command:

1. Identify all LLVM-C APIs used (see this document)
2. Add missing APIs to `src/llvm-nanobind.cpp`
3. Implement Python command
4. Verify against C version output
5. Run corresponding lit tests

---

## Summary Statistics

| Phase | Commands | New Bindings (approx) | Status |
|-------|----------|----------------------|--------|
| Phase 1 | 8 | 30 | ✅ Complete |
| Phase 2 | 6 | 9 | ✅ Complete |
| Phase 3 | 4 | 69 | ✅ Complete |
| Phase 4 | 3 | 20 | ✅ Complete |
| Phase 5 | 1 | ~150 | Not Started |
| **Total** | **22** | **128/~305 (42%)** | **21/22 (95%)** |

The project has progressed significantly with 21 of 22 commands complete. Phase 5 (`--echo`) remains as the final and most complex command, requiring extensive type/constant/instruction cloning APIs. The lit test infrastructure provides immediate feedback on correctness, and all implemented commands produce byte-identical output to the C version.

---

## Phase 5: Echo Command (Module Cloning)

### Command

1. `--echo` - Complete module cloning via C API

The `--echo` command is the most comprehensive test in the llvm-c-test suite. It reads LLVM IR, clones it entirely using the C API, and outputs the cloned module. This exercises nearly every LLVM-C API for types, constants, instructions, and metadata.

### Why Deferred to Phase 5

The `--echo` command requires ~150 additional API bindings across:
- **Type cloning (27 APIs)**: All type kinds including target extension types
- **Constant cloning (34 APIs)**: All constant kinds, constant expressions, pointer auth
- **Instruction cloning (40 APIs)**: All instruction opcodes with flags
- **Global cloning (30 APIs)**: Aliases, IFuncs, metadata
- **Operand bundles (7 APIs)**: Bundle creation and inspection
- **Inline assembly (9 APIs)**: Inline asm inspection and creation
- **Named metadata (7 APIs)**: Named metadata iteration

Given the scope, it's more efficient to complete Phases 3-4 first, then tackle `--echo` as a dedicated phase.

### Required Bindings

See Phase 3 sections 3.1 (Echo Command Bindings) for the complete API list.
