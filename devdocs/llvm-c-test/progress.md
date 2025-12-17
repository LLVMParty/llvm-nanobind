# llvm-c-test Python Port Progress

**Last Updated:** December 17, 2025 (Phase 5 COMPLETE - Echo command working!)

## Quick Summary

✅ **Phase 1 Complete** - All 8 foundation commands working (targets, calc, module operations)  
✅ **Phase 2 Complete** - All 6 metadata/attribute test commands working  
✅ **Phase 3 Complete** - All 4 diagnostic & debug info commands working  
✅ **Phase 4 Complete** - All 3 platform-specific commands working (disassembly, object files)
✅ **Phase 5 Complete** - Echo command (module cloning) working!

**Progress:** 22/22 commands (100%) • All lit tests passing!

---

## Status Overview

| Phase | Commands | Status |
|-------|----------|--------|
| Phase 1: Foundation | 8/8 | ✅ Complete |
| Phase 2: Metadata & Attributes | 6/6 | ✅ Complete |
| Phase 3: Complex (Debug Info) | 4/4 | ✅ Complete |
| Phase 4: Platform-Specific | 3/3 | ✅ Complete |
| Phase 5: Echo (Module Cloning) | 1/1 | ✅ Complete |
| **Total** | **22/22 (100%)** | **All Lit Tests Passing** |

---

## Phase 5: Echo Command - COMPLETE ✅

### Final Session: December 17, 2025

Successfully completed the `--echo` command implementation, the most complex command in the llvm-c-test suite.

### Work Completed

**Python Implementation (`llvm_c_test/echo.py`):**
- ~1400 lines of Python code
- `TypeCloner` class - Clones all LLVM type kinds
- `clone_constant()` / `clone_constant_impl()` - Clone all constant types
- `clone_inline_asm()` - Clone inline assembly values
- `FunCloner` class with full instruction cloning:
  - All 60+ instruction opcodes supported
  - Operand bundle cloning
  - Exception handling (landingpad, catchswitch, etc.)
  - Atomic operations with sync scope IDs
  - Fast-math flags preservation
- `declare_symbols()` / `clone_symbols()` - Global cloning
- Proper cleanup order to avoid use-after-free crashes

**C++ Bindings Added:**
- Value equality operators (`__eq__`, `__ne__`, `__hash__`) for Value, Type, BasicBlock, NamedMDNode
- `add_clause()` - Add clause to landing pad
- `add_handler()` - Add handler to catch switch
- `get_handlers()` - Get all handlers from catch switch
- `get_operand_bundle_at_index()` - Get operand bundle at index
- `get_indices()` - Get indices for extractvalue/insertvalue
- `first_param()` / `last_param()` - Function parameter iteration
- `next_function()` / `prev_function()` - Function iteration
- `first_function()` / `last_function()` - Module function iteration
- `invoke_with_operand_bundles()` - Build invoke with operand bundles
- `call_with_operand_bundles()` - Build call with operand bundles
- `callbr()` - Build callbr instruction
- `catch_switch()` - Build catch switch
- `cleanup_ret()` - Build cleanup ret
- `OperandBundle` class with tag, num_args, get_arg_at_index
- `create_operand_bundle()` - Create operand bundle
- `get_undef_mask_elem()` - Get undef mask element value
- `get_inline_asm()` - Create inline assembly value

**Memory Safety Fix:**
- Fixed critical bug where Module destructor would crash if context was already destroyed
- Module wrapper now checks validity token before calling `LLVMDisposeModule`
- Prevents interpreter crash, prints warning about potential memory leak
- Added comprehensive test suite in `test_memory_safety.py`
- Documented issue and fix in `devdocs/memory-model-issues.md`

### Test Results

All 23 lit tests passing:
```
PASS: llvm-c-test :: ARM/disassemble.test
PASS: llvm-c-test :: X86/disassemble.test
PASS: llvm-c-test :: add_named_metadata_operand.ll
PASS: llvm-c-test :: atomics.ll
PASS: llvm-c-test :: calc.test
PASS: llvm-c-test :: callsite_attributes.ll
PASS: llvm-c-test :: debug_info_new_format.ll
PASS: llvm-c-test :: di-type-get-name.ll
PASS: llvm-c-test :: echo.ll
PASS: llvm-c-test :: empty.ll
PASS: llvm-c-test :: float_ops.ll
PASS: llvm-c-test :: freeze.ll
PASS: llvm-c-test :: function_attributes.ll
PASS: llvm-c-test :: functions.ll
PASS: llvm-c-test :: get-di-tag.ll
PASS: llvm-c-test :: globals.ll
PASS: llvm-c-test :: invalid-bitcode.test
PASS: llvm-c-test :: invoke.ll
PASS: llvm-c-test :: is_a_value_as_metadata.ll
PASS: llvm-c-test :: memops.ll
PASS: llvm-c-test :: objectfile.ll
PASS: llvm-c-test :: replace_md_operand.ll
PASS: llvm-c-test :: set_metadata.ll

Total Discovered Tests: 23
  Passed: 23 (100.00%)
```

### Phase 6: Attribute and Metadata Bindings - COMPLETE ✅

**December 17, 2025**

Added the remaining bindings for complete attribute and metadata copying:

**Attribute Bindings Added:**
- `get_last_enum_attribute_kind()` - Get highest attribute kind number
- `create_enum_attribute(ctx, kind_id, val)` - Create enum attribute
- `get_enum_attribute_at_index(func, idx, kind_id)` - Get function attribute
- `add_attribute_at_index(func, idx, attr)` - Add attribute to function
- `get_callsite_enum_attribute(call_inst, idx, kind_id)` - Get call site attribute
- `add_callsite_attribute(call_inst, idx, attr)` - Add attribute to call site
- `Attribute` wrapper class with `kind` and `value` properties

**Metadata Bindings Added:**
- `global_copy_all_metadata(value)` - Get all metadata from global/function
- `global_set_metadata(global_val, kind, md)` - Set metadata on global
- `instruction_get_all_metadata_other_than_debug_loc(instr)` - Get instruction metadata
- `ValueMetadataEntries` wrapper class with `get_kind()` and `get_metadata()` methods

**Echo.py Updated:**
- `clone_attrs()` now fully implements attribute copying for calls/invokes
- Global metadata copying implemented in `clone_symbols()`
- Function metadata copying implemented in `clone_symbols()`
- Instruction metadata copying implemented in `clone_instruction()`

---

## Future Work

### Potential Enhancements

1. **Improve memory model** - Consider Option 1 from memory-model-issues.md (context tracks modules)

### Documentation Created

- `devdocs/memory-model-issues.md` - Documents module/context lifetime issues and fix
- `test_memory_safety.py` - Comprehensive memory safety test suite

---

## All Commands Implemented

| Command | Module | Status |
|---------|--------|--------|
| `--targets-list` | `targets.py` | ✅ |
| `--calc` | `calc.py` | ✅ |
| `--module-dump` | `module_ops.py` | ✅ |
| `--lazy-module-dump` | `module_ops.py` | ✅ |
| `--new-module-dump` | `module_ops.py` | ✅ |
| `--lazy-new-module-dump` | `module_ops.py` | ✅ |
| `--module-list-functions` | `module_ops.py` | ✅ |
| `--module-list-globals` | `module_ops.py` | ✅ |
| `--test-function-attributes` | `attributes.py` | ✅ |
| `--test-callsite-attributes` | `attributes.py` | ✅ |
| `--add-named-metadata-operand` | `metadata.py` | ✅ |
| `--set-metadata` | `metadata.py` | ✅ |
| `--replace-md-operand` | `metadata.py` | ✅ |
| `--is-a-value-as-metadata` | `metadata.py` | ✅ |
| `--test-dibuilder` | `debuginfo.py` | ✅ |
| `--get-di-tag` | `debuginfo.py` | ✅ |
| `--di-type-get-name` | `debuginfo.py` | ✅ |
| `--test-diagnostic-handler` | `diagnostic.py` | ✅ |
| `--disassemble` | `disassemble.py` | ✅ |
| `--object-list-sections` | `object_file.py` | ✅ |
| `--object-list-symbols` | `object_file.py` | ✅ |
| `--echo` | `echo.py` | ✅ |

---

## Running the Tests

```bash
# Run all lit tests
CMAKE_PREFIX_PATH=$(brew --prefix llvm) uv run python run_llvm_c_tests.py

# Run with verbose output
CMAKE_PREFIX_PATH=$(brew --prefix llvm) uv run python run_llvm_c_tests.py -v

# Run memory safety tests
CMAKE_PREFIX_PATH=$(brew --prefix llvm) uv run python test_memory_safety.py

# Test echo command directly
echo 'define i32 @main() { ret i32 0 }' | $(brew --prefix llvm)/bin/llvm-as | \
  CMAKE_PREFIX_PATH=$(brew --prefix llvm) uv run python -m llvm_c_test --echo
```
