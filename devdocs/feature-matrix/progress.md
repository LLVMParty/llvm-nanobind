# Feature Matrix Progress

## Current Status

**Phase:** Implementation Complete - Maintenance Mode

**Overall Coverage:** ~80% of LLVM-C API

All high-priority items are implemented. Remaining items are low-priority, deprecated, or have better alternatives.

---

## Summary Statistics (Updated December 2024)

| Header | Total | ✅ Impl | 🚫 Skip | ❌ TODO | Coverage |
|--------|-------|---------|---------|---------|----------|
| Core.h | 640 | **472** | 45 | 123 | **74%** |
| DebugInfo.h | 99 | **~75** | 0 | ~24 | **~76%** |
| Target.h | 22 | **22** | 0 | 0 | **100%** |
| TargetMachine.h | 29 | **14** | 9 | 6 | **79%** |
| Object.h | 31 | 23 | 0 | 8 | 74% |
| Analysis.h | 4 | **2** | 2 | 0 | **100%** |
| BitReader.h | 8 | 3 | 5 | 0 | 37.5% |
| BitWriter.h | 4 | **2** | 2 | 0 | **100%** |
| IRReader.h | 1 | 1 | 0 | 0 | 100% |
| PassBuilder.h | 15 | **15** | 0 | 0 | **100%** |
| Disassembler.h | 6 | **4** | 0 | 2 | **67%** |
| Linker.h | 1 | **1** | 0 | 0 | **100%** |
| Misc | 20 | 0 | 7 | 13 | 0% |
| **Total** | **~880** | **~634** | **~71** | **~175** | **~80%** |

---

## Recently Implemented (December 2024)

### Session 1 - Core Workflows

#### BitWriter.h - COMPLETE ✅
- `LLVMWriteBitcodeToFile` → `mod.write_bitcode_to_file(path)`
- `LLVMWriteBitcodeToMemoryBuffer` → `mod.write_bitcode_to_memory_buffer()`

#### PassBuilder.h - COMPLETE ✅
All 15 functions implemented including all optimization options.

#### Target.h - COMPLETE ✅
All 22 functions including host queries, initialization, and target data.

#### TargetMachine.h - 79%
Core code generation fully working. Only TargetMachineOptions builder API missing (not needed).

#### Linker.h - COMPLETE ✅
- `LLVMLinkModules2` → `mod.link_module(src_mod)`

#### Analysis.h - COMPLETE ✅
- Module and function verification implemented.

### Session 2 - Additional APIs

#### Module Flags
- `LLVMAddModuleFlag` → `mod.add_module_flag(behavior, key, val)`
- `LLVMGetModuleFlag` → `mod.get_module_flag(key)`
- `LLVMModuleFlagBehavior` enum → `llvm.ModuleFlagBehavior`

#### String Attributes
- `LLVMCreateStringAttribute` → `ctx.create_string_attribute(key, val)`
- `LLVMGetStringAttributeKind` → `attr.string_kind`
- `LLVMGetStringAttributeValue` → `attr.string_value`
- `LLVMIsEnumAttribute` → `attr.is_enum_attribute`
- `LLVMIsStringAttribute` → `attr.is_string_attribute`
- `LLVMIsTypeAttribute` → `attr.is_type_attribute`

#### Type Queries
- `LLVMIsLiteralStruct` → `ty.is_literal_struct`

#### Value Manipulation
- `LLVMSetOperand` → `val.set_operand(index, new_val)`

#### Constant Creation from Strings
- `LLVMConstIntOfStringAndSize` → `ty.constant_from_string(text, radix)`
- `LLVMConstRealOfStringAndSize` → `ty.real_constant_from_string(text)`

#### Disassembler Options
- `LLVMSetDisasmOptions` → `disasm.set_options(options)`
- Disassembler option constants (`DisasmOption_*`)

#### Builder Instructions
- `LLVMBuildAddrSpaceCast` → `b.addr_space_cast(val, ty, name)`
- `LLVMBuildFence` → `b.fence(ordering, single_thread, name)`

### Session 3 - Low Priority Items (December 2024)

#### Global IFunc Management
- `LLVMEraseGlobalIFunc` → `val.erase_from_parent_ifunc()`
- `LLVMRemoveGlobalIFunc` → `val.remove_from_parent_ifunc()`

#### DLL Storage Class
- `LLVMGetDLLStorageClass` → `val.dll_storage_class` (getter)
- `LLVMSetDLLStorageClass` → `val.dll_storage_class = ...` (setter)
- `LLVMDLLStorageClass` enum → `llvm.DLLStorageClass`

#### Sync Scope
- `LLVMGetSyncScopeID` → `ctx.get_sync_scope_id(name)`

### Session 4 - Medium Priority Items (December 2024)

#### Type Attributes
- `LLVMCreateTypeAttribute` → `ctx.create_type_attribute(kind_id, type)`
- `LLVMGetTypeAttributeValue` → `attr.type_value`

#### Metadata Kind ID
- `LLVMGetMDKindIDInContext` → `ctx.get_md_kind_id(name)`

#### Attribute Management
- `LLVMGetAttributesAtIndex` → `fn.get_attributes(idx)`
- `LLVMGetStringAttributeAtIndex` → `fn.get_string_attribute(idx, key)`
- `LLVMRemoveEnumAttributeAtIndex` → `fn.remove_enum_attribute(idx, kind)`
- `LLVMRemoveStringAttributeAtIndex` → `fn.remove_string_attribute(idx, key)`
- `LLVMAddTargetDependentFunctionAttr` → `fn.add_target_attribute(key, value)`

#### Builder Position Control
- `LLVMPositionBuilder` → `b.position_at(bb, inst)`
- `LLVMClearInsertionPosition` → `b.clear_insertion_position()`

#### Additional Builder Instructions
- `LLVMBuildExactUDiv` → `b.exact_udiv(lhs, rhs, name)`
- `LLVMBuildIndirectBr` → `b.indirect_br(addr, num_dests)`
- `LLVMAddDestination` → `ibr.add_destination(bb)`
- `LLVMBuildAtomicRMW` → `b.atomic_rmw(op, ptr, val, ordering, single_thread)`
- `LLVMBuildAtomicCmpXchg` → `b.atomic_cmpxchg(ptr, cmp, new, succ_ord, fail_ord, single_thread)`

#### Convenience Cast Builders
- `LLVMBuildZExtOrBitCast` → `b.zext_or_bitcast(val, ty, name)`
- `LLVMBuildSExtOrBitCast` → `b.sext_or_bitcast(val, ty, name)`
- `LLVMBuildTruncOrBitCast` → `b.trunc_or_bitcast(val, ty, name)`
- `LLVMBuildCast` → `b.cast(op, val, ty, name)`
- `LLVMBuildPointerCast` → `b.pointer_cast(val, ty, name)`
- `LLVMBuildFPCast` → `b.fp_cast(val, ty, name)`

### Session 5 - DebugInfo.h Extensions (December 2024)

#### DIBuilder Type Creation - NEW ✅
- `LLVMDIBuilderFinalizeSubprogram` → `dib.finalize_subprogram(subprogram)`
- `LLVMDIBuilderCreateMemberType` → `dib.create_member_type(scope, name, file, line, size, align, offset, flags, type)`
- `LLVMDIBuilderCreateUnionType` → `dib.create_union_type(scope, name, file, line, size, align, flags, elements, runtime_lang, unique_id)`
- `LLVMDIBuilderCreateArrayType` → `dib.create_array_type(size, align, elem_type, subscripts)`
- `LLVMDIBuilderCreateQualifiedType` → `dib.create_qualified_type(tag, type)` (const/volatile)
- `LLVMDIBuilderCreateReferenceType` → `dib.create_reference_type(tag, type)`
- `LLVMDIBuilderCreateNullPtrType` → `dib.create_null_ptr_type()`
- `LLVMDIBuilderCreateBitFieldMemberType` → `dib.create_bit_field_member_type(...)`
- `LLVMDIBuilderCreateArtificialType` → `dib.create_artificial_type(type)`
- `LLVMDIBuilderGetOrCreateTypeArray` → `dib.get_or_create_type_array(types)`
- `LLVMDIBuilderCreateLexicalBlockFile` → `dib.create_lexical_block_file(scope, file, discriminator)`
- `LLVMDIBuilderCreateImportedDeclaration` → `dib.create_imported_declaration(scope, decl, file, line, name, elements)`
- `LLVMDIBuilderCreateImportedModuleFromNamespace` → `dib.create_imported_module_from_namespace(scope, ns, file, line)`

#### DILocation Accessors - NEW ✅
- `LLVMDILocationGetLine` → `llvm.di_location_get_line(loc)`
- `LLVMDILocationGetColumn` → `llvm.di_location_get_column(loc)`
- `LLVMDILocationGetScope` → `llvm.di_location_get_scope(loc)`
- `LLVMDILocationGetInlinedAt` → `llvm.di_location_get_inlined_at(loc)`

#### Debug Metadata Version - NEW ✅
- `LLVMDebugMetadataVersion` → `llvm.debug_metadata_version()`
- `LLVMGetModuleDebugMetadataVersion` → `llvm.get_module_debug_metadata_version(mod)`
- `LLVMStripModuleDebugInfo` → `llvm.strip_module_debug_info(mod)`

#### DI File/Scope/Variable Accessors - NEW ✅
- `LLVMDIScopeGetFile` → `llvm.di_scope_get_file(scope)`
- `LLVMDIFileGetDirectory` → `llvm.di_file_get_directory(file)`
- `LLVMDIFileGetFilename` → `llvm.di_file_get_filename(file)`
- `LLVMDIFileGetSource` → `llvm.di_file_get_source(file)`
- `LLVMDISubprogramGetLine` → `llvm.di_subprogram_get_line(subprogram)`
- `LLVMDIVariableGetFile` → `llvm.di_variable_get_file(variable)`
- `LLVMDIVariableGetScope` → `llvm.di_variable_get_scope(variable)`
- `LLVMDIVariableGetLine` → `llvm.di_variable_get_line(variable)`

#### Core.h Extensions - NEW ✅
- `LLVMBlockAddress` → `llvm.block_address(fn, bb)` (for computed goto)
- `LLVMGetOperandUse` → `val.get_operand_use(index)` (use-def chain access)

---

## Remaining TODO - Detailed Breakdown

### Medium Priority - Most Now Implemented

#### Type Attributes (Core.h) - COMPLETE ✅
- `LLVMCreateTypeAttribute` → `ctx.create_type_attribute(kind_id, type)`
- `LLVMGetTypeAttributeValue` → `attr.type_value`
- `LLVMCreateConstantRangeAttribute` - 🚫 Skip (rare use case, complex API)

#### Metadata Kind ID (Core.h) - COMPLETE ✅
- `LLVMGetMDKindIDInContext` → `ctx.get_md_kind_id(name)`

#### Module Flag Iteration (Core.h) - 🚫 SKIP
- Can iterate via IR parsing if needed, complex API with limited value

#### Attribute Management (Core.h) - COMPLETE ✅
All implemented (see Session 4 above).

#### Builder Position Control (Core.h) - COMPLETE ✅
- `LLVMPositionBuilder` → `b.position_at(bb, inst)`
- `LLVMClearInsertionPosition` → `b.clear_insertion_position()`
- FP math tag methods - 🚫 Skip (very specialized, rarely needed)

#### Additional Builder Instructions (Core.h) - COMPLETE ✅
All main instructions implemented. `LLVMBuildNUWNeg` deprecated in LLVM 21.

#### Convenience Cast Builders (Core.h) - COMPLETE ✅
All implemented (see Session 4 above).
- `LLVMGetCastOpcode` - ❌ TODO (utility function, medium value)

#### Value/Use Access (Core.h) - COMPLETE ✅
- `LLVMGetOperandUse` → `val.get_operand_use(index)`
- `LLVMBlockAddress` → `llvm.block_address(fn, bb)`

#### Intrinsics (Core.h) - PARTIAL
- `LLVMIntrinsicGetType` - ❌ TODO (medium value)

#### Memory Buffer (Core.h)
- `LLVMCreateMemoryBufferWithSTDIN` - 🚫 Skip (Python has better stdin handling)
- `LLVMCreateMemoryBufferWithMemoryRange` - ❌ TODO (zero-copy buffer, low value)

#### Metadata (Core.h) - MOSTLY COMPLETE ✅
- `LLVMGetMDKindIDInContext` → `ctx.get_md_kind_id(name)` ✅
- `LLVMReplaceMDNodeOperandWith` - ❌ TODO (medium value)

---

### Low Priority - Now Implemented ✅

#### Global IFunc (Core.h) - COMPLETE ✅
| Function | Python API | Notes |
|----------|------------|-------|
| `LLVMEraseGlobalIFunc` | `val.erase_from_parent_ifunc()` | Safe, deletes IFunc |
| `LLVMRemoveGlobalIFunc` | `val.remove_from_parent_ifunc()` | Advanced, keeps IFunc alive (use with care) |

#### DLL Storage Class (Core.h) - COMPLETE ✅
| Function | Python API | Notes |
|----------|------------|-------|
| `LLVMGetDLLStorageClass` | `val.dll_storage_class` | Windows-specific property |
| `LLVMSetDLLStorageClass` | `val.dll_storage_class = ...` | Windows-specific property |
| `LLVMDLLStorageClass` enum | `llvm.DLLStorageClass` | Default, DLLImport, DLLExport |

#### Sync Scope (Core.h) - COMPLETE ✅
| Function | Python API | Notes |
|----------|------------|-------|
| `LLVMGetSyncScopeID` | `ctx.get_sync_scope_id(name)` | Maps scope name to ID |

#### X86-Specific Types (Core.h)
| Function | Python API | Notes |
|----------|------------|-------|
| `LLVMX86MMXTypeInContext` | 🚫 | Removed in LLVM 21, deprecated |
| `LLVMX86AMXTypeInContext` | `ctx.types.x86_amx` | Already implemented |

#### Arbitrary Precision Constants (Core.h)
| Function | Description | Notes |
|----------|-------------|-------|
| `LLVMConstIntOfArbitraryPrecision` | Arbitrary precision int | Use `constant_from_string` instead |
| `LLVMConstVector` | Create constant vector | Can use array + bitcast |

---

### Intentionally Skipped (🚫)

#### Deprecated Functions
| Function | Replacement |
|----------|-------------|
| `LLVMArrayType` | Use `LLVMArrayType2` (already bound) |
| `LLVMConstArray` | Use `LLVMConstArray2` (already bound) |
| `LLVMWriteBitcodeToFileHandle` | Use `LLVMWriteBitcodeToFile` |
| `LLVMInsertIntoBuilder` | Use specific build methods |

#### Const Expression Builders
These are deprecated - use builder instructions instead:
| Function | Alternative |
|----------|-------------|
| `LLVMConstNeg`, `LLVMConstNot`, etc. | Build in function |
| `LLVMConstAdd`, `LLVMConstSub`, etc. | Build in function |
| `LLVMConstGEP2`, `LLVMConstInBoundsGEP2` | Use `builder.gep()` |
| `LLVMConstTrunc`, `LLVMConstZExt`, etc. | Use builder casts |
| `LLVMConstBitCast`, `LLVMConstAddrSpaceCast` | Use builder casts |
| `LLVMConstICmp`, `LLVMConstFCmp` | Use `builder.icmp/fcmp` |
| `LLVMConstSelect` | Use `builder.select` |

#### Dump Functions
Use Python's `print()` instead:
| Function | Alternative |
|----------|-------------|
| `LLVMDumpModule` | `print(mod)` |
| `LLVMDumpType` | `print(ty)` |
| `LLVMDumpValue` | `print(val)` |

#### Redundant Functions
| Function | Alternative |
|----------|-------------|
| `LLVMGetNamedFunctionWithLength` | Use `mod.get_function(name)` |
| `LLVMAppendModuleInlineAsm` | Use `mod.inline_asm += "..."` |
| `LLVMGetLastEnumAttributeKind` | Not useful in Python |

#### Debugging-Only Functions
| Function | Notes |
|----------|-------|
| `LLVMViewFunctionCFG` | Requires graphviz, debugging only |
| `LLVMViewFunctionCFGOnly` | Requires graphviz, debugging only |

#### Low-Level File APIs
| Function | Notes |
|----------|-------|
| `LLVMWriteBitcodeToFD` | Use file path or memory buffer |

---

### Advanced Features - Not Yet Implemented

#### Comdat.h (Windows/COFF Linking)
| Function | Description | Priority |
|----------|-------------|----------|
| `LLVMGetOrInsertComdat` | Get/create COMDAT | Low |
| `LLVMGetComdat` | Get global's COMDAT | Low |
| `LLVMSetComdat` | Set global's COMDAT | Low |
| `LLVMGetComdatSelectionKind` | Get selection kind | Low |
| `LLVMSetComdatSelectionKind` | Set selection kind | Low |

**Use Case:** COMDAT sections for deduplication in Windows COFF object files.

#### ErrorHandling.h (Fatal Error Handling)
| Function | Description | Priority |
|----------|-------------|----------|
| `LLVMInstallFatalErrorHandler` | Install custom handler | Low |
| `LLVMResetFatalErrorHandler` | Reset to default | Low |
| `LLVMEnablePrettyStackTrace` | Enable stack traces | Low |

**Use Case:** Custom handling of LLVM fatal errors (normally crashes).

#### Support.h (Runtime Symbol Resolution)
| Function | Description | Priority |
|----------|-------------|----------|
| `LLVMLoadLibraryPermanently` | Load shared library | Low |
| `LLVMParseCommandLineOptions` | Parse LLVM cl options | Low |
| `LLVMSearchForAddressOfSymbol` | Find symbol address | Low |
| `LLVMAddSymbol` | Add symbol to table | Low |

**Use Case:** JIT symbol resolution and dynamic library loading.

#### TargetMachineOptions (Builder API)
| Function | Description | Priority |
|----------|-------------|----------|
| `LLVMCreateTargetMachineOptions` | Create options builder | Low |
| `LLVMDisposeTargetMachineOptions` | Dispose options | Low |
| `LLVMTargetMachineOptionsSetCPU` | Set CPU | Low |
| `LLVMTargetMachineOptionsSetFeatures` | Set features | Low |
| `LLVMTargetMachineOptionsSetABI` | Set ABI | Low |
| `LLVMTargetMachineOptionsSetRelocationModel` | Set reloc model | Low |
| `LLVMTargetMachineOptionsSetCodeGenOptLevel` | Set opt level | Low |
| `LLVMCreateTargetMachineWithOptions` | Create TM with options | Low |

**Use Case:** Alternative builder pattern for target machine creation. Current `create_target_machine()` is sufficient.

#### ORC JIT APIs (Orc.h, LLJIT.h)
Not yet tracked in detail. These provide modern JIT compilation:
- ORC lazy compilation
- LLJIT high-level interface
- Object linking layer
- Symbol resolution

**Use Case:** JIT compilation. Consider implementing if JIT is needed.

#### DebugInfo.h Remaining (~49 functions)
Many DI creation functions for various DWARF constructs:
- Composite types (arrays, vectors)
- Derived types
- Subranges
- Template parameters
- Imported entities
- etc.

**Use Case:** Complete debug info for all language constructs.

---

## Implementation Notes

### Core Workflows - All Supported ✅

1. **IR Creation** - Full support for modules, functions, types, instructions
2. **Bitcode I/O** - Read and write bitcode files
3. **Optimization** - Full PassBuilder support with all options
4. **Code Generation** - Emit object files and assembly
5. **Module Linking** - Link modules together
6. **Verification** - Verify modules and functions
7. **Debug Info** - Core DIBuilder support

### Safety Model

All bindings include:
- Lifetime tracking via `ValidityToken`
- Null checks before LLVM-C calls
- Python exceptions instead of crashes
- Automatic resource cleanup

### Testing

- Golden master tests comparing C++ and Python output
- Memory safety tests
- Type checking with `ty check`

---

## Changelog

### December 2024
- Implemented all high-priority items (BitWriter, PassBuilder, Target, Linker)
- Added module flags, string attributes, constant from string
- Added builder instructions (addr_space_cast, fence)
- Added disassembler options
- Updated documentation with detailed remaining items
- Overall coverage: 74%
