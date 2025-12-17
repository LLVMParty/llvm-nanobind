#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <atomic>
#include <memory>
#include <stdexcept>

#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Disassembler.h>
#include <llvm-c/Object.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

namespace nb = nanobind;
using namespace nb::literals;

// =============================================================================
// Exceptions
// =============================================================================

struct LLVMException : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct LLVMUseAfterFreeError : LLVMException {
  using LLVMException::LLVMException;
};

struct LLVMInvalidOperationError : LLVMException {
  using LLVMException::LLVMException;
};

struct LLVMVerificationError : LLVMException {
  using LLVMException::LLVMException;
};

// =============================================================================
// Validity Token for Lifetime Tracking
// =============================================================================

struct ValidityToken {
  std::atomic<bool> valid{true};

  void invalidate() { valid = false; }
  bool is_valid() const { return valid.load(); }
};

// =============================================================================
// Base class to prevent copy
// =============================================================================

struct NoMoveCopy {
  NoMoveCopy() = default;
  NoMoveCopy(const NoMoveCopy &) = delete;
  NoMoveCopy &operator=(const NoMoveCopy &) = delete;
};

// =============================================================================
// Forward Declarations
// =============================================================================

struct LLVMContextWrapper;
struct LLVMModuleWrapper;
struct LLVMTypeWrapper;
struct LLVMValueWrapper;
struct LLVMFunctionWrapper;
struct LLVMBasicBlockWrapper;
struct LLVMBuilderWrapper;
struct LLVMModuleManager;
struct LLVMBuilderManager;
struct LLVMNamedMDNodeWrapper;
struct LLVMOperandBundleWrapper;

// =============================================================================
// Operand Bundle Wrapper (for call/invoke instructions with operand bundles)
// =============================================================================

struct LLVMOperandBundleWrapper {
  LLVMOperandBundleRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMOperandBundleWrapper() = default;
  LLVMOperandBundleWrapper(LLVMOperandBundleRef ref,
                           std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}

  ~LLVMOperandBundleWrapper() {
    if (m_ref) {
      LLVMDisposeOperandBundle(m_ref);
      m_ref = nullptr;
    }
  }

  // Move-only to ensure proper ownership
  LLVMOperandBundleWrapper(const LLVMOperandBundleWrapper &) = delete;
  LLVMOperandBundleWrapper &operator=(const LLVMOperandBundleWrapper &) = delete;
  LLVMOperandBundleWrapper(LLVMOperandBundleWrapper &&other) noexcept
      : m_ref(other.m_ref), m_context_token(std::move(other.m_context_token)) {
    other.m_ref = nullptr;
  }
  LLVMOperandBundleWrapper &operator=(LLVMOperandBundleWrapper &&other) noexcept {
    if (this != &other) {
      if (m_ref)
        LLVMDisposeOperandBundle(m_ref);
      m_ref = other.m_ref;
      m_context_token = std::move(other.m_context_token);
      other.m_ref = nullptr;
    }
    return *this;
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("OperandBundle is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError(
          "OperandBundle used after context was destroyed");
  }

  std::string get_tag() const {
    check_valid();
    size_t len = 0;
    const char *tag = LLVMGetOperandBundleTag(m_ref, &len);
    return std::string(tag, len);
  }

  unsigned get_num_args() const {
    check_valid();
    return LLVMGetNumOperandBundleArgs(m_ref);
  }

  LLVMValueWrapper get_arg_at_index(unsigned index) const;
};

// =============================================================================
// Attribute Wrapper (for function/call site attributes)
// =============================================================================

struct LLVMAttributeWrapper {
  LLVMAttributeRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMAttributeWrapper() = default;
  LLVMAttributeWrapper(LLVMAttributeRef ref,
                       std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}

  // Attributes are not owned, so no destructor needed

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Attribute is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError(
          "Attribute used after context was destroyed");
  }

  bool is_valid() const {
    return m_ref != nullptr && m_context_token && m_context_token->is_valid();
  }

  unsigned get_kind() const {
    check_valid();
    return LLVMGetEnumAttributeKind(m_ref);
  }

  uint64_t get_value() const {
    check_valid();
    return LLVMGetEnumAttributeValue(m_ref);
  }
};

// =============================================================================
// Value Metadata Entries Wrapper (for global/instruction metadata copying)
// =============================================================================

struct LLVMValueMetadataEntriesWrapper;

// Forward declaration for metadata wrapper
struct LLVMMetadataWrapper;

struct LLVMValueMetadataEntriesWrapper {
  LLVMValueMetadataEntry *m_entries = nullptr;
  size_t m_count = 0;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMValueMetadataEntriesWrapper() = default;
  LLVMValueMetadataEntriesWrapper(LLVMValueMetadataEntry *entries, size_t count,
                                  std::shared_ptr<ValidityToken> token)
      : m_entries(entries), m_count(count),
        m_context_token(std::move(token)) {}

  ~LLVMValueMetadataEntriesWrapper() {
    if (m_entries) {
      LLVMDisposeValueMetadataEntries(m_entries);
      m_entries = nullptr;
    }
  }

  // Move-only to ensure proper ownership
  LLVMValueMetadataEntriesWrapper(const LLVMValueMetadataEntriesWrapper &) =
      delete;
  LLVMValueMetadataEntriesWrapper &
  operator=(const LLVMValueMetadataEntriesWrapper &) = delete;
  LLVMValueMetadataEntriesWrapper(
      LLVMValueMetadataEntriesWrapper &&other) noexcept
      : m_entries(other.m_entries), m_count(other.m_count),
        m_context_token(std::move(other.m_context_token)) {
    other.m_entries = nullptr;
    other.m_count = 0;
  }
  LLVMValueMetadataEntriesWrapper &
  operator=(LLVMValueMetadataEntriesWrapper &&other) noexcept {
    if (this != &other) {
      if (m_entries)
        LLVMDisposeValueMetadataEntries(m_entries);
      m_entries = other.m_entries;
      m_count = other.m_count;
      m_context_token = std::move(other.m_context_token);
      other.m_entries = nullptr;
      other.m_count = 0;
    }
    return *this;
  }

  void check_valid() const {
    if (!m_entries && m_count > 0)
      throw LLVMUseAfterFreeError("ValueMetadataEntries is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError(
          "ValueMetadataEntries used after context was destroyed");
  }

  size_t size() const { return m_count; }

  unsigned get_kind(unsigned index) const {
    check_valid();
    if (index >= m_count)
      throw std::out_of_range("Index out of range");
    return LLVMValueMetadataEntriesGetKind(m_entries, index);
  }

  // get_metadata is implemented later after LLVMMetadataWrapper is defined
  LLVMMetadataRef get_metadata_ref(unsigned index) const {
    check_valid();
    if (index >= m_count)
      throw std::out_of_range("Index out of range");
    return LLVMValueMetadataEntriesGetMetadata(m_entries, index);
  }
};

// =============================================================================
// Type Wrapper
// =============================================================================

struct LLVMTypeWrapper {
  LLVMTypeRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMTypeWrapper() = default;
  LLVMTypeWrapper(LLVMTypeRef ref, std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}

  bool operator==(const LLVMTypeWrapper &other) const {
    return m_ref == other.m_ref;
  }
  bool operator!=(const LLVMTypeWrapper &other) const {
    return m_ref != other.m_ref;
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Type is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError("Type used after context was destroyed");
  }

  LLVMTypeKind kind() const {
    check_valid();
    return LLVMGetTypeKind(m_ref);
  }

  std::string to_string() const {
    check_valid();
    char *str = LLVMPrintTypeToString(m_ref);
    std::string result(str);
    LLVMDisposeMessage(str);
    return result;
  }

  bool is_void() const { return kind() == LLVMVoidTypeKind; }
  bool is_integer() const { return kind() == LLVMIntegerTypeKind; }
  bool is_float() const {
    auto k = kind();
    return k == LLVMHalfTypeKind || k == LLVMFloatTypeKind ||
           k == LLVMDoubleTypeKind || k == LLVMFP128TypeKind;
  }
  bool is_pointer() const { return kind() == LLVMPointerTypeKind; }
  bool is_function() const { return kind() == LLVMFunctionTypeKind; }
  bool is_struct() const { return kind() == LLVMStructTypeKind; }
  bool is_array() const { return kind() == LLVMArrayTypeKind; }
  bool is_vector() const {
    auto k = kind();
    return k == LLVMVectorTypeKind || k == LLVMScalableVectorTypeKind;
  }

  unsigned get_int_width() const {
    check_valid();
    if (!is_integer())
      throw LLVMInvalidOperationError("Type is not an integer type");
    return LLVMGetIntTypeWidth(m_ref);
  }

  bool is_sized() const {
    check_valid();
    return LLVMTypeIsSized(m_ref);
  }

  bool is_packed_struct() const {
    check_valid();
    if (!is_struct())
      throw LLVMInvalidOperationError("Type is not a struct type");
    return LLVMIsPackedStruct(m_ref);
  }

  bool is_opaque_struct() const {
    check_valid();
    if (!is_struct())
      throw LLVMInvalidOperationError("Type is not a struct type");
    return LLVMIsOpaqueStruct(m_ref);
  }

  std::optional<std::string> get_struct_name() const {
    check_valid();
    if (!is_struct())
      throw LLVMInvalidOperationError("Type is not a struct type");
    const char *name = LLVMGetStructName(m_ref);
    if (!name)
      return std::nullopt;
    return std::string(name);
  }

  bool is_vararg_function() const {
    check_valid();
    if (!is_function())
      throw LLVMInvalidOperationError("Type is not a function type");
    return LLVMIsFunctionVarArg(m_ref);
  }

  // Type introspection for echo command
  LLVMTypeWrapper get_struct_element_type(unsigned index) const {
    check_valid();
    if (!is_struct())
      throw LLVMInvalidOperationError("Type is not a struct type");
    return LLVMTypeWrapper(LLVMStructGetTypeAtIndex(m_ref, index),
                           m_context_token);
  }

  bool is_opaque_pointer() const {
    check_valid();
    if (!is_pointer())
      throw LLVMInvalidOperationError("Type is not a pointer type");
    return LLVMPointerTypeIsOpaque(m_ref);
  }

  LLVMTypeWrapper get_element_type() const {
    check_valid();
    auto k = kind();
    if (k != LLVMPointerTypeKind && k != LLVMVectorTypeKind &&
        k != LLVMScalableVectorTypeKind && k != LLVMArrayTypeKind)
      throw LLVMInvalidOperationError(
          "Type does not have an element type (not pointer/vector/array)");
    return LLVMTypeWrapper(LLVMGetElementType(m_ref), m_context_token);
  }

  uint64_t get_array_length() const {
    check_valid();
    if (!is_array())
      throw LLVMInvalidOperationError("Type is not an array type");
    return LLVMGetArrayLength2(m_ref);
  }

  unsigned get_vector_size() const {
    check_valid();
    if (!is_vector())
      throw LLVMInvalidOperationError("Type is not a vector type");
    return LLVMGetVectorSize(m_ref);
  }

  unsigned get_pointer_address_space() const {
    check_valid();
    if (!is_pointer())
      throw LLVMInvalidOperationError("Type is not a pointer type");
    return LLVMGetPointerAddressSpace(m_ref);
  }

  LLVMTypeWrapper get_return_type() const {
    check_valid();
    if (!is_function())
      throw LLVMInvalidOperationError("Type is not a function type");
    return LLVMTypeWrapper(LLVMGetReturnType(m_ref), m_context_token);
  }

  unsigned count_param_types() const {
    check_valid();
    if (!is_function())
      throw LLVMInvalidOperationError("Type is not a function type");
    return LLVMCountParamTypes(m_ref);
  }

  std::vector<LLVMTypeWrapper> get_param_types() const {
    check_valid();
    if (!is_function())
      throw LLVMInvalidOperationError("Type is not a function type");
    unsigned count = LLVMCountParamTypes(m_ref);
    if (count == 0)
      return {};
    std::vector<LLVMTypeRef> params(count);
    LLVMGetParamTypes(m_ref, params.data());
    std::vector<LLVMTypeWrapper> result;
    result.reserve(count);
    for (auto p : params)
      result.emplace_back(p, m_context_token);
    return result;
  }

  unsigned count_struct_element_types() const {
    check_valid();
    if (!is_struct())
      throw LLVMInvalidOperationError("Type is not a struct type");
    return LLVMCountStructElementTypes(m_ref);
  }

  // Target extension type info
  std::string get_target_ext_type_name() const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMInvalidOperationError("Type is not a target extension type");
    return std::string(LLVMGetTargetExtTypeName(m_ref));
  }

  unsigned get_target_ext_type_num_type_params() const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMInvalidOperationError("Type is not a target extension type");
    return LLVMGetTargetExtTypeNumTypeParams(m_ref);
  }

  unsigned get_target_ext_type_num_int_params() const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMInvalidOperationError("Type is not a target extension type");
    return LLVMGetTargetExtTypeNumIntParams(m_ref);
  }

  LLVMTypeWrapper get_target_ext_type_type_param(unsigned index) const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMInvalidOperationError("Type is not a target extension type");
    return LLVMTypeWrapper(LLVMGetTargetExtTypeTypeParam(m_ref, index),
                           m_context_token);
  }

  unsigned get_target_ext_type_int_param(unsigned index) const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMInvalidOperationError("Type is not a target extension type");
    return LLVMGetTargetExtTypeIntParam(m_ref, index);
  }
};

// =============================================================================
// Named Metadata Node Wrapper
// =============================================================================

struct LLVMNamedMDNodeWrapper {
  LLVMNamedMDNodeRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMNamedMDNodeWrapper() = default;
  LLVMNamedMDNodeWrapper(LLVMNamedMDNodeRef ref,
                         std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}

  bool operator==(const LLVMNamedMDNodeWrapper &other) const {
    return m_ref == other.m_ref;
  }
  bool operator!=(const LLVMNamedMDNodeWrapper &other) const {
    return m_ref != other.m_ref;
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("NamedMDNode is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError(
          "NamedMDNode used after context was destroyed");
  }

  std::string get_name() const {
    check_valid();
    size_t len = 0;
    const char *name = LLVMGetNamedMetadataName(m_ref, &len);
    return std::string(name, len);
  }

  std::optional<LLVMNamedMDNodeWrapper> next() {
    check_valid();
    LLVMNamedMDNodeRef next_md = LLVMGetNextNamedMetadata(m_ref);
    if (!next_md)
      return std::nullopt;
    return LLVMNamedMDNodeWrapper(next_md, m_context_token);
  }

  std::optional<LLVMNamedMDNodeWrapper> prev() {
    check_valid();
    LLVMNamedMDNodeRef prev_md = LLVMGetPreviousNamedMetadata(m_ref);
    if (!prev_md)
      return std::nullopt;
    return LLVMNamedMDNodeWrapper(prev_md, m_context_token);
  }
};

// =============================================================================
// Value Wrapper (base for all LLVM values)
// =============================================================================

struct LLVMValueWrapper {
  LLVMValueRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMValueWrapper() = default;
  LLVMValueWrapper(LLVMValueRef ref, std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}

  bool operator==(const LLVMValueWrapper &other) const {
    return m_ref == other.m_ref;
  }
  bool operator!=(const LLVMValueWrapper &other) const {
    return m_ref != other.m_ref;
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Value is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError("Value used after context was destroyed");
  }

  LLVMTypeWrapper type() const {
    check_valid();
    return LLVMTypeWrapper(LLVMTypeOf(m_ref), m_context_token);
  }

  std::string get_name() const {
    check_valid();
    size_t len;
    const char *name = LLVMGetValueName2(m_ref, &len);
    return std::string(name, len);
  }

  void set_name(const std::string &name) {
    check_valid();
    LLVMSetValueName2(m_ref, name.c_str(), name.size());
  }

  std::string to_string() const {
    check_valid();
    char *str = LLVMPrintValueToString(m_ref);
    std::string result(str);
    LLVMDisposeMessage(str);
    return result;
  }

  LLVMValueKind value_kind() const {
    check_valid();
    return LLVMGetValueKind(m_ref);
  }

  bool is_constant() const {
    check_valid();
    return LLVMIsConstant(m_ref);
  }

  bool is_undef() const {
    check_valid();
    return LLVMIsUndef(m_ref);
  }

  bool is_poison() const {
    check_valid();
    return LLVMIsPoison(m_ref);
  }

  std::optional<LLVMValueWrapper> next_global() const {
    check_valid();
    LLVMValueRef next = LLVMGetNextGlobal(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_global() const {
    check_valid();
    LLVMValueRef prev = LLVMGetPreviousGlobal(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  // Global alias iteration for echo command
  std::optional<LLVMValueWrapper> next_global_alias() const {
    check_valid();
    LLVMValueRef next = LLVMGetNextGlobalAlias(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_global_alias() const {
    check_valid();
    LLVMValueRef prev = LLVMGetPreviousGlobalAlias(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  std::optional<LLVMValueWrapper> alias_get_aliasee() const {
    check_valid();
    LLVMValueRef aliasee = LLVMAliasGetAliasee(m_ref);
    if (!aliasee)
      return std::nullopt;
    return LLVMValueWrapper(aliasee, m_context_token);
  }

  void alias_set_aliasee(const LLVMValueWrapper &aliasee) {
    check_valid();
    aliasee.check_valid();
    LLVMAliasSetAliasee(m_ref, aliasee.m_ref);
  }

  // Global IFunc iteration for echo command
  std::optional<LLVMValueWrapper> next_global_ifunc() const {
    check_valid();
    LLVMValueRef next = LLVMGetNextGlobalIFunc(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_global_ifunc() const {
    check_valid();
    LLVMValueRef prev = LLVMGetPreviousGlobalIFunc(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  std::optional<LLVMValueWrapper> get_global_ifunc_resolver() const {
    check_valid();
    LLVMValueRef resolver = LLVMGetGlobalIFuncResolver(m_ref);
    if (!resolver)
      return std::nullopt;
    return LLVMValueWrapper(resolver, m_context_token);
  }

  void set_global_ifunc_resolver(const LLVMValueWrapper &resolver) {
    check_valid();
    resolver.check_valid();
    LLVMSetGlobalIFuncResolver(m_ref, resolver.m_ref);
  }

  // Global properties for echo command
  LLVMTypeWrapper global_get_value_type() const {
    check_valid();
    return LLVMTypeWrapper(LLVMGlobalGetValueType(m_ref), m_context_token);
  }

  LLVMUnnamedAddr get_unnamed_address() const {
    check_valid();
    return LLVMGetUnnamedAddress(m_ref);
  }

  void set_unnamed_address(LLVMUnnamedAddr unnamed_addr) {
    check_valid();
    LLVMSetUnnamedAddress(m_ref, unnamed_addr);
  }

  bool has_personality_fn() const {
    check_valid();
    return LLVMHasPersonalityFn(m_ref);
  }

  std::optional<LLVMValueWrapper> get_personality_fn() const {
    check_valid();
    LLVMValueRef fn = LLVMGetPersonalityFn(m_ref);
    if (!fn)
      return std::nullopt;
    return LLVMValueWrapper(fn, m_context_token);
  }

  void set_personality_fn(const LLVMValueWrapper &fn) {
    check_valid();
    fn.check_valid();
    LLVMSetPersonalityFn(m_ref, fn.m_ref);
  }

  bool has_prefix_data() const {
    check_valid();
    return LLVMHasPrefixData(m_ref);
  }

  std::optional<LLVMValueWrapper> get_prefix_data() const {
    check_valid();
    LLVMValueRef data = LLVMGetPrefixData(m_ref);
    if (!data)
      return std::nullopt;
    return LLVMValueWrapper(data, m_context_token);
  }

  void set_prefix_data(const LLVMValueWrapper &data) {
    check_valid();
    data.check_valid();
    LLVMSetPrefixData(m_ref, data.m_ref);
  }

  bool has_prologue_data() const {
    check_valid();
    return LLVMHasPrologueData(m_ref);
  }

  std::optional<LLVMValueWrapper> get_prologue_data() const {
    check_valid();
    LLVMValueRef data = LLVMGetPrologueData(m_ref);
    if (!data)
      return std::nullopt;
    return LLVMValueWrapper(data, m_context_token);
  }

  void set_prologue_data(const LLVMValueWrapper &data) {
    check_valid();
    data.check_valid();
    LLVMSetPrologueData(m_ref, data.m_ref);
  }

  // Global/Instruction metadata copying for echo command
  LLVMValueMetadataEntriesWrapper global_copy_all_metadata() const {
    check_valid();
    size_t num_entries = 0;
    LLVMValueMetadataEntry *entries =
        LLVMGlobalCopyAllMetadata(m_ref, &num_entries);
    return LLVMValueMetadataEntriesWrapper(entries, num_entries,
                                           m_context_token);
  }

  LLVMValueMetadataEntriesWrapper instruction_get_all_metadata_other_than_debug_loc() const {
    check_valid();
    size_t num_entries = 0;
    LLVMValueMetadataEntry *entries =
        LLVMInstructionGetAllMetadataOtherThanDebugLoc(m_ref, &num_entries);
    return LLVMValueMetadataEntriesWrapper(entries, num_entries,
                                           m_context_token);
  }

  // Instruction iteration
  std::optional<LLVMValueWrapper> next_instruction() const {
    check_valid();
    LLVMValueRef next = LLVMGetNextInstruction(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_instruction() const {
    check_valid();
    LLVMValueRef prev = LLVMGetPreviousInstruction(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  // Parameter iteration for echo command
  std::optional<LLVMValueWrapper> next_param() const {
    check_valid();
    LLVMValueRef next = LLVMGetNextParam(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_param() const {
    check_valid();
    LLVMValueRef prev = LLVMGetPreviousParam(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  // Instruction/value predicates
  bool is_a_call_inst() const {
    check_valid();
    return LLVMIsACallInst(m_ref) != nullptr;
  }

  bool is_declaration() const {
    check_valid();
    return LLVMIsDeclaration(m_ref);
  }

  // Operand access
  unsigned get_num_operands() const {
    check_valid();
    return LLVMGetNumOperands(m_ref);
  }

  LLVMValueWrapper get_operand(unsigned index) const {
    check_valid();
    LLVMValueRef op = LLVMGetOperand(m_ref, index);
    if (!op)
      throw LLVMInvalidOperationError("Invalid operand index");
    return LLVMValueWrapper(op, m_context_token);
  }

  // Constant type checking for echo command
  bool is_a_global_value() const {
    check_valid();
    return LLVMIsAGlobalValue(m_ref) != nullptr;
  }

  bool is_a_function() const {
    check_valid();
    return LLVMIsAFunction(m_ref) != nullptr;
  }

  bool is_a_global_variable() const {
    check_valid();
    return LLVMIsAGlobalVariable(m_ref) != nullptr;
  }

  bool is_a_global_alias() const {
    check_valid();
    return LLVMIsAGlobalAlias(m_ref) != nullptr;
  }

  bool is_a_constant_int() const {
    check_valid();
    return LLVMIsAConstantInt(m_ref) != nullptr;
  }

  bool is_a_constant_fp() const {
    check_valid();
    return LLVMIsAConstantFP(m_ref) != nullptr;
  }

  bool is_a_constant_aggregate_zero() const {
    check_valid();
    return LLVMIsAConstantAggregateZero(m_ref) != nullptr;
  }

  bool is_a_constant_data_array() const {
    check_valid();
    return LLVMIsAConstantDataArray(m_ref) != nullptr;
  }

  bool is_a_constant_array() const {
    check_valid();
    return LLVMIsAConstantArray(m_ref) != nullptr;
  }

  bool is_a_constant_struct() const {
    check_valid();
    return LLVMIsAConstantStruct(m_ref) != nullptr;
  }

  bool is_a_constant_pointer_null() const {
    check_valid();
    return LLVMIsAConstantPointerNull(m_ref) != nullptr;
  }

  bool is_a_constant_vector() const {
    check_valid();
    return LLVMIsAConstantVector(m_ref) != nullptr;
  }

  bool is_a_constant_data_vector() const {
    check_valid();
    return LLVMIsAConstantDataVector(m_ref) != nullptr;
  }

  bool is_a_constant_expr() const {
    check_valid();
    return LLVMIsAConstantExpr(m_ref) != nullptr;
  }

  bool is_a_constant_ptr_auth() const {
    check_valid();
    return LLVMIsAConstantPtrAuth(m_ref) != nullptr;
  }

  bool is_null() const {
    check_valid();
    return LLVMIsNull(m_ref);
  }

  // Intrinsic support
  unsigned get_intrinsic_id() const {
    check_valid();
    return LLVMGetIntrinsicID(m_ref);
  }

  // Constant data access
  std::pair<size_t, std::string> get_raw_data_values() const {
    check_valid();
    if (!is_a_constant_data_array())
      throw LLVMInvalidOperationError("Value is not a constant data array");
    size_t size;
    const char *data = LLVMGetRawDataValues(m_ref, &size);
    return {size, std::string(data, size)};
  }

  LLVMValueWrapper get_aggregate_element(unsigned index) const {
    check_valid();
    LLVMValueRef elem = LLVMGetAggregateElement(m_ref, index);
    if (!elem)
      throw LLVMInvalidOperationError("Invalid aggregate element index");
    return LLVMValueWrapper(elem, m_context_token);
  }

  // Constant expression support
  LLVMOpcode get_const_opcode() const {
    check_valid();
    if (!is_a_constant_expr())
      throw LLVMInvalidOperationError("Value is not a constant expression");
    return LLVMGetConstOpcode(m_ref);
  }

  LLVMTypeWrapper get_gep_source_element_type() const {
    check_valid();
    return LLVMTypeWrapper(LLVMGetGEPSourceElementType(m_ref), m_context_token);
  }

  unsigned get_num_indices() const {
    check_valid();
    return LLVMGetNumIndices(m_ref);
  }

  unsigned get_gep_no_wrap_flags() const {
    check_valid();
    return LLVMGEPGetNoWrapFlags(m_ref);
  }

  // Pointer auth constant support
  LLVMValueWrapper get_constant_ptr_auth_pointer() const {
    check_valid();
    if (!is_a_constant_ptr_auth())
      throw LLVMInvalidOperationError("Value is not a pointer auth constant");
    return LLVMValueWrapper(LLVMGetConstantPtrAuthPointer(m_ref),
                            m_context_token);
  }

  LLVMValueWrapper get_constant_ptr_auth_key() const {
    check_valid();
    if (!is_a_constant_ptr_auth())
      throw LLVMInvalidOperationError("Value is not a pointer auth constant");
    return LLVMValueWrapper(LLVMGetConstantPtrAuthKey(m_ref), m_context_token);
  }

  LLVMValueWrapper get_constant_ptr_auth_discriminator() const {
    check_valid();
    if (!is_a_constant_ptr_auth())
      throw LLVMInvalidOperationError("Value is not a pointer auth constant");
    return LLVMValueWrapper(LLVMGetConstantPtrAuthDiscriminator(m_ref),
                            m_context_token);
  }

  LLVMValueWrapper get_constant_ptr_auth_addr_discriminator() const {
    check_valid();
    if (!is_a_constant_ptr_auth())
      throw LLVMInvalidOperationError("Value is not a pointer auth constant");
    return LLVMValueWrapper(LLVMGetConstantPtrAuthAddrDiscriminator(m_ref),
                            m_context_token);
  }

  // Instruction properties for echo command
  LLVMOpcode get_instruction_opcode() const {
    check_valid();
    return LLVMGetInstructionOpcode(m_ref);
  }

  LLVMIntPredicate get_icmp_predicate() const {
    check_valid();
    return LLVMGetICmpPredicate(m_ref);
  }

  LLVMRealPredicate get_fcmp_predicate() const {
    check_valid();
    return LLVMGetFCmpPredicate(m_ref);
  }

  // Instruction flags
  bool get_nsw() const {
    check_valid();
    return LLVMGetNSW(m_ref);
  }

  bool get_nuw() const {
    check_valid();
    return LLVMGetNUW(m_ref);
  }

  bool get_exact() const {
    check_valid();
    return LLVMGetExact(m_ref);
  }

  bool get_nneg() const {
    check_valid();
    return LLVMGetNNeg(m_ref);
  }

  // Memory access properties
  unsigned get_alignment() const {
    check_valid();
    return LLVMGetAlignment(m_ref);
  }

  bool get_volatile() const {
    check_valid();
    return LLVMGetVolatile(m_ref);
  }

  LLVMAtomicOrdering get_ordering() const {
    check_valid();
    return LLVMGetOrdering(m_ref);
  }

  // Call/invoke properties
  unsigned get_num_arg_operands() const {
    check_valid();
    return LLVMGetNumArgOperands(m_ref);
  }

  // PHI node properties
  unsigned count_incoming() const {
    check_valid();
    return LLVMCountIncoming(m_ref);
  }

  LLVMValueWrapper get_incoming_value(unsigned index) const {
    check_valid();
    return LLVMValueWrapper(LLVMGetIncomingValue(m_ref, index), m_context_token);
  }

  // Forward declaration - defined after LLVMBasicBlockWrapper
  LLVMBasicBlockWrapper get_incoming_block(unsigned index) const;

  // Alloca properties
  LLVMTypeWrapper get_allocated_type() const {
    check_valid();
    return LLVMTypeWrapper(LLVMGetAllocatedType(m_ref), m_context_token);
  }

  // =========================================================================
  // Phase 5.7: Operand Bundle Support
  // =========================================================================
  unsigned get_num_operand_bundles() const {
    check_valid();
    return LLVMGetNumOperandBundles(m_ref);
  }

  // Get operand bundle at index - returns raw LLVMOperandBundleRef for cloning
  // Note: Caller is responsible for disposing of the returned bundle
  LLVMOperandBundleRef get_operand_bundle_at_index_raw(unsigned index) const {
    check_valid();
    return LLVMGetOperandBundleAtIndex(m_ref, index);
  }

  // =========================================================================
  // Phase 5.8: Inline Assembly Support
  // =========================================================================
  bool is_a_inline_asm() const {
    check_valid();
    return LLVMIsAInlineAsm(m_ref) != nullptr;
  }

  std::string get_inline_asm_asm_string() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMInvalidOperationError("Value is not inline assembly");
    size_t len = 0;
    const char *str = LLVMGetInlineAsmAsmString(m_ref, &len);
    return std::string(str, len);
  }

  std::string get_inline_asm_constraint_string() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMInvalidOperationError("Value is not inline assembly");
    size_t len = 0;
    const char *str = LLVMGetInlineAsmConstraintString(m_ref, &len);
    return std::string(str, len);
  }

  LLVMInlineAsmDialect get_inline_asm_dialect() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMInvalidOperationError("Value is not inline assembly");
    return LLVMGetInlineAsmDialect(m_ref);
  }

  LLVMTypeWrapper get_inline_asm_function_type() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMInvalidOperationError("Value is not inline assembly");
    return LLVMTypeWrapper(LLVMGetInlineAsmFunctionType(m_ref), m_context_token);
  }

  bool get_inline_asm_has_side_effects() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMInvalidOperationError("Value is not inline assembly");
    return LLVMGetInlineAsmHasSideEffects(m_ref);
  }

  bool get_inline_asm_needs_aligned_stack() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMInvalidOperationError("Value is not inline assembly");
    return LLVMGetInlineAsmNeedsAlignedStack(m_ref);
  }

  bool get_inline_asm_can_unwind() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMInvalidOperationError("Value is not inline assembly");
    return LLVMGetInlineAsmCanUnwind(m_ref);
  }

  // =========================================================================
  // Phase 5.9: Flag Setters and Additional Properties
  // =========================================================================

  // Flag setters (pairs with existing getters)
  void set_nsw(bool nsw) {
    check_valid();
    LLVMSetNSW(m_ref, nsw);
  }

  void set_nuw(bool nuw) {
    check_valid();
    LLVMSetNUW(m_ref, nuw);
  }

  void set_exact(bool exact) {
    check_valid();
    LLVMSetExact(m_ref, exact);
  }

  void set_nneg(bool nneg) {
    check_valid();
    LLVMSetNNeg(m_ref, nneg);
  }

  // Disjoint flag for Or instruction
  bool get_is_disjoint() const {
    check_valid();
    return LLVMGetIsDisjoint(m_ref);
  }

  void set_is_disjoint(bool is_disjoint) {
    check_valid();
    LLVMSetIsDisjoint(m_ref, is_disjoint);
  }

  // ICmp same sign flag
  bool get_icmp_same_sign() const {
    check_valid();
    return LLVMGetICmpSameSign(m_ref);
  }

  void set_icmp_same_sign(bool same_sign) {
    check_valid();
    LLVMSetICmpSameSign(m_ref, same_sign);
  }

  // Memory instruction setters
  void set_ordering(LLVMAtomicOrdering ordering) {
    check_valid();
    LLVMSetOrdering(m_ref, ordering);
  }

  void set_volatile(bool is_volatile) {
    check_valid();
    LLVMSetVolatile(m_ref, is_volatile);
  }

  // Atomic properties
  bool is_atomic() const {
    check_valid();
    return LLVMIsAtomic(m_ref);
  }

  unsigned get_atomic_sync_scope_id() const {
    check_valid();
    return LLVMGetAtomicSyncScopeID(m_ref);
  }

  void set_atomic_sync_scope_id(unsigned scope_id) {
    check_valid();
    LLVMSetAtomicSyncScopeID(m_ref, scope_id);
  }

  LLVMAtomicRMWBinOp get_atomic_rmw_bin_op() const {
    check_valid();
    return LLVMGetAtomicRMWBinOp(m_ref);
  }

  // CmpXchg ordering
  LLVMAtomicOrdering get_cmpxchg_success_ordering() const {
    check_valid();
    return LLVMGetCmpXchgSuccessOrdering(m_ref);
  }

  LLVMAtomicOrdering get_cmpxchg_failure_ordering() const {
    check_valid();
    return LLVMGetCmpXchgFailureOrdering(m_ref);
  }

  // CmpXchg weak flag
  bool get_weak() const {
    check_valid();
    return LLVMGetWeak(m_ref);
  }

  void set_weak(bool is_weak) {
    check_valid();
    LLVMSetWeak(m_ref, is_weak);
  }

  // Tail call kind
  LLVMTailCallKind get_tail_call_kind() const {
    check_valid();
    return LLVMGetTailCallKind(m_ref);
  }

  void set_tail_call_kind(LLVMTailCallKind kind) {
    check_valid();
    LLVMSetTailCallKind(m_ref, kind);
  }

  // Called function type and value
  LLVMTypeWrapper get_called_function_type() const {
    check_valid();
    return LLVMTypeWrapper(LLVMGetCalledFunctionType(m_ref), m_context_token);
  }

  LLVMValueWrapper get_called_value() const {
    check_valid();
    return LLVMValueWrapper(LLVMGetCalledValue(m_ref), m_context_token);
  }

  // Branch condition
  bool is_conditional() const {
    check_valid();
    return LLVMIsConditional(m_ref);
  }

  LLVMValueWrapper get_condition() const {
    check_valid();
    return LLVMValueWrapper(LLVMGetCondition(m_ref), m_context_token);
  }

  // Landing pad properties
  unsigned get_num_clauses() const {
    check_valid();
    return LLVMGetNumClauses(m_ref);
  }

  LLVMValueWrapper get_clause(unsigned index) const {
    check_valid();
    return LLVMValueWrapper(LLVMGetClause(m_ref, index), m_context_token);
  }

  bool is_cleanup() const {
    check_valid();
    return LLVMIsCleanup(m_ref);
  }

  void set_cleanup(bool is_cleanup_val) {
    check_valid();
    LLVMSetCleanup(m_ref, is_cleanup_val);
  }

  // CatchSwitch/CatchPad properties
  LLVMValueWrapper get_parent_catch_switch() const {
    check_valid();
    return LLVMValueWrapper(LLVMGetParentCatchSwitch(m_ref), m_context_token);
  }

  unsigned get_num_handlers() const {
    check_valid();
    return LLVMGetNumHandlers(m_ref);
  }

  // Get handlers for catch switch - forward declared, implemented after LLVMBasicBlockWrapper
  std::vector<LLVMBasicBlockWrapper> get_handlers() const;

  // Add clause to landing pad
  void add_clause(const LLVMValueWrapper &clause_val) {
    check_valid();
    LLVMAddClause(m_ref, clause_val.m_ref);
  }

  // Add handler to catch switch - forward declared, implemented after LLVMBasicBlockWrapper
  void add_handler(const LLVMBasicBlockWrapper &handler);

  // Get operand bundle at index
  LLVMOperandBundleWrapper get_operand_bundle_at_index(unsigned index) const {
    check_valid();
    LLVMOperandBundleRef bundle = LLVMGetOperandBundleAtIndex(m_ref, index);
    return LLVMOperandBundleWrapper(bundle, m_context_token);
  }

  // Get indices for extractvalue/insertvalue
  std::vector<unsigned> get_indices() const {
    check_valid();
    unsigned num_indices = LLVMGetNumIndices(m_ref);
    const unsigned *indices = LLVMGetIndices(m_ref);
    return std::vector<unsigned>(indices, indices + num_indices);
  }

  // ShuffleVector mask
  unsigned get_num_mask_elements() const {
    check_valid();
    return LLVMGetNumMaskElements(m_ref);
  }

  int get_mask_value(unsigned index) const {
    check_valid();
    return LLVMGetMaskValue(m_ref, index);
  }

  // Fast-math flags
  bool can_use_fast_math_flags() const {
    check_valid();
    return LLVMCanValueUseFastMathFlags(m_ref);
  }

  LLVMFastMathFlags get_fast_math_flags() const {
    check_valid();
    return LLVMGetFastMathFlags(m_ref);
  }

  void set_fast_math_flags(LLVMFastMathFlags flags) {
    check_valid();
    LLVMSetFastMathFlags(m_ref, flags);
  }

  // Get arg operand (for call instructions)
  LLVMValueWrapper get_arg_operand(unsigned index) const {
    check_valid();
    return LLVMValueWrapper(LLVMGetArgOperand(m_ref, index), m_context_token);
  }

  // Instruction manipulation
  void remove_from_parent() {
    check_valid();
    LLVMInstructionRemoveFromParent(m_ref);
  }

  bool is_a_instruction() const {
    check_valid();
    return LLVMIsAInstruction(m_ref) != nullptr;
  }

  // BasicBlock properties - forward declared
  LLVMBasicBlockWrapper get_instruction_parent() const;
  LLVMBasicBlockWrapper get_normal_dest() const;
  std::optional<LLVMBasicBlockWrapper> get_unwind_dest() const;
  LLVMBasicBlockWrapper get_successor(unsigned index) const;
  LLVMBasicBlockWrapper get_callbr_default_dest() const;
  unsigned get_callbr_num_indirect_dests() const;
  LLVMBasicBlockWrapper get_callbr_indirect_dest(unsigned index) const;

  // Value/BasicBlock conversion
  bool value_is_basic_block() const {
    check_valid();
    return LLVMValueIsBasicBlock(m_ref);
  }

  LLVMBasicBlockWrapper value_as_basic_block() const;
};

// =============================================================================
// BasicBlock Wrapper
// =============================================================================

struct LLVMBasicBlockWrapper {
  LLVMBasicBlockRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMBasicBlockWrapper() = default;
  LLVMBasicBlockWrapper(LLVMBasicBlockRef ref,
                        std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}

  bool operator==(const LLVMBasicBlockWrapper &other) const {
    return m_ref == other.m_ref;
  }
  bool operator!=(const LLVMBasicBlockWrapper &other) const {
    return m_ref != other.m_ref;
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("BasicBlock is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError(
          "BasicBlock used after context was destroyed");
  }

  std::string get_name() const {
    check_valid();
    const char *name = LLVMGetBasicBlockName(m_ref);
    return std::string(name ? name : "");
  }

  LLVMValueWrapper as_value() const {
    check_valid();
    return LLVMValueWrapper(LLVMBasicBlockAsValue(m_ref), m_context_token);
  }

  std::optional<LLVMBasicBlockWrapper> next_block() const {
    check_valid();
    LLVMBasicBlockRef next = LLVMGetNextBasicBlock(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMBasicBlockWrapper(next, m_context_token);
  }

  std::optional<LLVMBasicBlockWrapper> prev_block() const {
    check_valid();
    LLVMBasicBlockRef prev = LLVMGetPreviousBasicBlock(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMBasicBlockWrapper(prev, m_context_token);
  }

  std::optional<LLVMValueWrapper> terminator() const {
    check_valid();
    LLVMValueRef term = LLVMGetBasicBlockTerminator(m_ref);
    if (!term)
      return std::nullopt;
    return LLVMValueWrapper(term, m_context_token);
  }

  std::optional<LLVMValueWrapper> first_instruction() const {
    check_valid();
    LLVMValueRef inst = LLVMGetFirstInstruction(m_ref);
    if (!inst)
      return std::nullopt;
    return LLVMValueWrapper(inst, m_context_token);
  }

  std::optional<LLVMValueWrapper> last_instruction() const {
    check_valid();
    LLVMValueRef inst = LLVMGetLastInstruction(m_ref);
    if (!inst)
      return std::nullopt;
    return LLVMValueWrapper(inst, m_context_token);
  }

  // Get parent function - declared here, defined after LLVMFunctionWrapper
  LLVMFunctionWrapper parent() const;

  void move_before(const LLVMBasicBlockWrapper &other) {
    check_valid();
    other.check_valid();
    LLVMMoveBasicBlockBefore(m_ref, other.m_ref);
  }

  void move_after(const LLVMBasicBlockWrapper &other) {
    check_valid();
    other.check_valid();
    LLVMMoveBasicBlockAfter(m_ref, other.m_ref);
  }
};

// =============================================================================
// Function Wrapper
// =============================================================================

struct LLVMFunctionWrapper : LLVMValueWrapper {
  LLVMFunctionWrapper() = default;
  LLVMFunctionWrapper(LLVMValueRef ref, std::shared_ptr<ValidityToken> token)
      : LLVMValueWrapper(ref, std::move(token)) {}

  unsigned param_count() const {
    check_valid();
    return LLVMCountParams(m_ref);
  }

  LLVMValueWrapper get_param(unsigned index) const {
    check_valid();
    if (index >= param_count())
      throw LLVMInvalidOperationError("Parameter index out of range");
    return LLVMValueWrapper(LLVMGetParam(m_ref, index), m_context_token);
  }

  std::vector<LLVMValueWrapper> params() const {
    check_valid();
    unsigned count = param_count();
    std::vector<LLVMValueRef> raw_params(count);
    LLVMGetParams(m_ref, raw_params.data());
    std::vector<LLVMValueWrapper> result;
    result.reserve(count);
    for (auto p : raw_params) {
      result.emplace_back(p, m_context_token);
    }
    return result;
  }

  LLVMLinkage get_linkage() const {
    check_valid();
    return LLVMGetLinkage(m_ref);
  }

  void set_linkage(LLVMLinkage linkage) {
    check_valid();
    LLVMSetLinkage(m_ref, linkage);
  }

  unsigned get_calling_conv() const {
    check_valid();
    return LLVMGetFunctionCallConv(m_ref);
  }

  void set_calling_conv(unsigned cc) {
    check_valid();
    LLVMSetFunctionCallConv(m_ref, cc);
  }

  LLVMBasicBlockWrapper append_basic_block(const std::string &name,
                                           LLVMContextRef ctx) {
    check_valid();
    LLVMBasicBlockRef bb =
        LLVMAppendBasicBlockInContext(ctx, m_ref, name.c_str());
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  std::optional<LLVMBasicBlockWrapper> entry_block() const {
    check_valid();
    LLVMBasicBlockRef bb = LLVMGetEntryBasicBlock(m_ref);
    if (!bb)
      return std::nullopt;
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  unsigned basic_block_count() const {
    check_valid();
    return LLVMCountBasicBlocks(m_ref);
  }

  std::optional<LLVMBasicBlockWrapper> first_basic_block() const {
    check_valid();
    LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(m_ref);
    if (!bb)
      return std::nullopt;
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  std::optional<LLVMBasicBlockWrapper> last_basic_block() const {
    check_valid();
    LLVMBasicBlockRef bb = LLVMGetLastBasicBlock(m_ref);
    if (!bb)
      return std::nullopt;
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  std::vector<LLVMBasicBlockWrapper> basic_blocks() const {
    check_valid();
    std::vector<LLVMBasicBlockWrapper> result;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(m_ref); bb;
         bb = LLVMGetNextBasicBlock(bb)) {
      result.emplace_back(bb, m_context_token);
    }
    return result;
  }

  void append_existing_basic_block(const LLVMBasicBlockWrapper &bb) {
    check_valid();
    bb.check_valid();
    LLVMAppendExistingBasicBlock(m_ref, bb.m_ref);
  }

  void erase() {
    check_valid();
    LLVMDeleteFunction(m_ref);
    m_ref = nullptr;
  }

  // Parameter iteration for echo command
  std::optional<LLVMValueWrapper> first_param() const {
    check_valid();
    LLVMValueRef param = LLVMGetFirstParam(m_ref);
    if (!param)
      return std::nullopt;
    return LLVMValueWrapper(param, m_context_token);
  }

  std::optional<LLVMValueWrapper> last_param() const {
    check_valid();
    LLVMValueRef param = LLVMGetLastParam(m_ref);
    if (!param)
      return std::nullopt;
    return LLVMValueWrapper(param, m_context_token);
  }

  // Function iteration
  std::optional<LLVMFunctionWrapper> next_function() const {
    check_valid();
    LLVMValueRef next = LLVMGetNextFunction(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMFunctionWrapper(next, m_context_token);
  }

  std::optional<LLVMFunctionWrapper> prev_function() const {
    check_valid();
    LLVMValueRef prev = LLVMGetPreviousFunction(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMFunctionWrapper(prev, m_context_token);
  }
};

// Implementation of LLVMBasicBlockWrapper::parent() - needs LLVMFunctionWrapper
inline LLVMFunctionWrapper LLVMBasicBlockWrapper::parent() const {
  check_valid();
  LLVMValueRef parent_fn = LLVMGetBasicBlockParent(m_ref);
  if (!parent_fn)
    throw LLVMInvalidOperationError("BasicBlock has no parent function");
  return LLVMFunctionWrapper(parent_fn, m_context_token);
}

// Implementation of LLVMValueWrapper::get_incoming_block() - needs LLVMBasicBlockWrapper
inline LLVMBasicBlockWrapper LLVMValueWrapper::get_incoming_block(unsigned index) const {
  check_valid();
  return LLVMBasicBlockWrapper(LLVMGetIncomingBlock(m_ref, index),
                               m_context_token);
}

// Implementation of LLVMOperandBundleWrapper::get_arg_at_index - needs LLVMValueWrapper
inline LLVMValueWrapper LLVMOperandBundleWrapper::get_arg_at_index(unsigned index) const {
  check_valid();
  LLVMValueRef arg = LLVMGetOperandBundleArgAtIndex(m_ref, index);
  if (!arg)
    throw LLVMInvalidOperationError("Invalid operand bundle argument index");
  return LLVMValueWrapper(arg, m_context_token);
}

// Implementation of LLVMValueWrapper BasicBlock methods - need LLVMBasicBlockWrapper
inline LLVMBasicBlockWrapper LLVMValueWrapper::get_instruction_parent() const {
  check_valid();
  LLVMBasicBlockRef bb = LLVMGetInstructionParent(m_ref);
  if (!bb)
    throw LLVMInvalidOperationError("Instruction has no parent basic block");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline LLVMBasicBlockWrapper LLVMValueWrapper::get_normal_dest() const {
  check_valid();
  LLVMBasicBlockRef bb = LLVMGetNormalDest(m_ref);
  if (!bb)
    throw LLVMInvalidOperationError("Invoke instruction has no normal dest");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline std::optional<LLVMBasicBlockWrapper>
LLVMValueWrapper::get_unwind_dest() const {
  check_valid();
  LLVMBasicBlockRef bb = LLVMGetUnwindDest(m_ref);
  // Unwind dest can be null for cleanupret and catchswitch
  if (!bb)
    return std::nullopt;
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline LLVMBasicBlockWrapper LLVMValueWrapper::get_successor(unsigned index) const {
  check_valid();
  LLVMBasicBlockRef bb = LLVMGetSuccessor(m_ref, index);
  if (!bb)
    throw LLVMInvalidOperationError("Invalid successor index");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline LLVMBasicBlockWrapper LLVMValueWrapper::get_callbr_default_dest() const {
  check_valid();
  LLVMBasicBlockRef bb = LLVMGetCallBrDefaultDest(m_ref);
  if (!bb)
    throw LLVMInvalidOperationError("CallBr has no default dest");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline unsigned LLVMValueWrapper::get_callbr_num_indirect_dests() const {
  check_valid();
  return LLVMGetCallBrNumIndirectDests(m_ref);
}

inline LLVMBasicBlockWrapper LLVMValueWrapper::get_callbr_indirect_dest(unsigned index) const {
  check_valid();
  LLVMBasicBlockRef bb = LLVMGetCallBrIndirectDest(m_ref, index);
  if (!bb)
    throw LLVMInvalidOperationError("Invalid callbr indirect dest index");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline LLVMBasicBlockWrapper LLVMValueWrapper::value_as_basic_block() const {
  check_valid();
  if (!value_is_basic_block())
    throw LLVMInvalidOperationError("Value is not a basic block");
  LLVMBasicBlockRef bb = LLVMValueAsBasicBlock(m_ref);
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline std::vector<LLVMBasicBlockWrapper> LLVMValueWrapper::get_handlers() const {
  check_valid();
  unsigned num_handlers = LLVMGetNumHandlers(m_ref);
  std::vector<LLVMBasicBlockRef> handlers(num_handlers);
  if (num_handlers > 0) {
    LLVMGetHandlers(m_ref, handlers.data());
  }
  std::vector<LLVMBasicBlockWrapper> result;
  result.reserve(num_handlers);
  for (auto bb : handlers) {
    result.push_back(LLVMBasicBlockWrapper(bb, m_context_token));
  }
  return result;
}

inline void LLVMValueWrapper::add_handler(const LLVMBasicBlockWrapper &handler) {
  check_valid();
  LLVMAddHandler(m_ref, handler.m_ref);
}

// =============================================================================
// Builder Wrapper
// =============================================================================

struct LLVMBuilderWrapper : NoMoveCopy {
  LLVMBuilderRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;
  std::shared_ptr<ValidityToken> m_token;

  LLVMBuilderWrapper(LLVMContextRef ctx,
                     std::shared_ptr<ValidityToken> context_token)
      : m_context_token(std::move(context_token)),
        m_token(std::make_shared<ValidityToken>()) {
    m_ref = LLVMCreateBuilderInContext(ctx);
  }

  ~LLVMBuilderWrapper() {
    if (m_ref) {
      LLVMDisposeBuilder(m_ref);
      m_ref = nullptr;
    }
    if (m_token) {
      m_token->invalidate();
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Builder has been disposed");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError("Builder used after context was destroyed");
  }

  void dispose() {
    if (m_ref) {
      LLVMDisposeBuilder(m_ref);
      m_ref = nullptr;
    }
    if (m_token) {
      m_token->invalidate();
    }
  }

  // Positioning
  void position_at_end(const LLVMBasicBlockWrapper &bb) {
    check_valid();
    bb.check_valid();
    LLVMPositionBuilderAtEnd(m_ref, bb.m_ref);
  }

  void position_before(const LLVMValueWrapper &inst) {
    check_valid();
    inst.check_valid();
    LLVMPositionBuilderBefore(m_ref, inst.m_ref);
  }

  std::optional<LLVMBasicBlockWrapper> insert_block() const {
    check_valid();
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(m_ref);
    if (!bb)
      return std::nullopt;
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  // Arithmetic operations
  LLVMValueWrapper add(const LLVMValueWrapper &lhs, const LLVMValueWrapper &rhs,
                       const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildAdd(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper nsw_add(const LLVMValueWrapper &lhs,
                           const LLVMValueWrapper &rhs,
                           const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildNSWAdd(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper nuw_add(const LLVMValueWrapper &lhs,
                           const LLVMValueWrapper &rhs,
                           const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildNUWAdd(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper sub(const LLVMValueWrapper &lhs, const LLVMValueWrapper &rhs,
                       const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildSub(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper nsw_sub(const LLVMValueWrapper &lhs,
                           const LLVMValueWrapper &rhs,
                           const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildNSWSub(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper nuw_sub(const LLVMValueWrapper &lhs,
                           const LLVMValueWrapper &rhs,
                           const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildNUWSub(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper mul(const LLVMValueWrapper &lhs, const LLVMValueWrapper &rhs,
                       const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildMul(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper nsw_mul(const LLVMValueWrapper &lhs,
                           const LLVMValueWrapper &rhs,
                           const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildNSWMul(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper nuw_mul(const LLVMValueWrapper &lhs,
                           const LLVMValueWrapper &rhs,
                           const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildNUWMul(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper sdiv(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildSDiv(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper udiv(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildUDiv(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper exact_sdiv(const LLVMValueWrapper &lhs,
                              const LLVMValueWrapper &rhs,
                              const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildExactSDiv(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper srem(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildSRem(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper urem(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildURem(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  // Floating point arithmetic
  LLVMValueWrapper fadd(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildFAdd(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper fsub(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildFSub(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper fmul(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildFMul(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper fdiv(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildFDiv(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper frem(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildFRem(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  // Unary operations
  LLVMValueWrapper neg(const LLVMValueWrapper &val,
                       const std::string &name = "") {
    check_valid();
    val.check_valid();
    return LLVMValueWrapper(LLVMBuildNeg(m_ref, val.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper nsw_neg(const LLVMValueWrapper &val,
                           const std::string &name = "") {
    check_valid();
    val.check_valid();
    return LLVMValueWrapper(LLVMBuildNSWNeg(m_ref, val.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper fneg(const LLVMValueWrapper &val,
                        const std::string &name = "") {
    check_valid();
    val.check_valid();
    return LLVMValueWrapper(LLVMBuildFNeg(m_ref, val.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper not_(const LLVMValueWrapper &val,
                        const std::string &name = "") {
    check_valid();
    val.check_valid();
    return LLVMValueWrapper(LLVMBuildNot(m_ref, val.m_ref, name.c_str()),
                            m_context_token);
  }

  // Bitwise operations
  LLVMValueWrapper shl(const LLVMValueWrapper &lhs, const LLVMValueWrapper &rhs,
                       const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildShl(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper lshr(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildLShr(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper ashr(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildAShr(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper and_(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildAnd(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper or_(const LLVMValueWrapper &lhs, const LLVMValueWrapper &rhs,
                       const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildOr(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper xor_(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(LLVMBuildXor(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
                            m_context_token);
  }

  // Generic binary operation
  LLVMValueWrapper binop(LLVMOpcode opcode, const LLVMValueWrapper &lhs,
                         const LLVMValueWrapper &rhs,
                         const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildBinOp(m_ref, opcode, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  // Memory operations
  LLVMValueWrapper build_alloca(const LLVMTypeWrapper &ty,
                                const std::string &name = "") {
    check_valid();
    ty.check_valid();
    return LLVMValueWrapper(LLVMBuildAlloca(m_ref, ty.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper build_array_alloca(const LLVMTypeWrapper &ty,
                                      const LLVMValueWrapper &size,
                                      const std::string &name = "") {
    check_valid();
    ty.check_valid();
    size.check_valid();
    return LLVMValueWrapper(
        LLVMBuildArrayAlloca(m_ref, ty.m_ref, size.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper load(const LLVMTypeWrapper &ty, const LLVMValueWrapper &ptr,
                        const std::string &name = "") {
    check_valid();
    ty.check_valid();
    ptr.check_valid();
    return LLVMValueWrapper(
        LLVMBuildLoad2(m_ref, ty.m_ref, ptr.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper store(const LLVMValueWrapper &val,
                         const LLVMValueWrapper &ptr) {
    check_valid();
    val.check_valid();
    ptr.check_valid();
    return LLVMValueWrapper(LLVMBuildStore(m_ref, val.m_ref, ptr.m_ref),
                            m_context_token);
  }

  LLVMValueWrapper gep(const LLVMTypeWrapper &ty, const LLVMValueWrapper &ptr,
                       const std::vector<LLVMValueWrapper> &indices,
                       const std::string &name = "") {
    check_valid();
    ty.check_valid();
    ptr.check_valid();
    std::vector<LLVMValueRef> idx_refs;
    idx_refs.reserve(indices.size());
    for (const auto &idx : indices) {
      idx.check_valid();
      idx_refs.push_back(idx.m_ref);
    }
    return LLVMValueWrapper(
        LLVMBuildGEP2(m_ref, ty.m_ref, ptr.m_ref, idx_refs.data(),
                      static_cast<unsigned>(idx_refs.size()), name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper inbounds_gep(const LLVMTypeWrapper &ty,
                                const LLVMValueWrapper &ptr,
                                const std::vector<LLVMValueWrapper> &indices,
                                const std::string &name = "") {
    check_valid();
    ty.check_valid();
    ptr.check_valid();
    std::vector<LLVMValueRef> idx_refs;
    idx_refs.reserve(indices.size());
    for (const auto &idx : indices) {
      idx.check_valid();
      idx_refs.push_back(idx.m_ref);
    }
    return LLVMValueWrapper(
        LLVMBuildInBoundsGEP2(m_ref, ty.m_ref, ptr.m_ref, idx_refs.data(),
                              static_cast<unsigned>(idx_refs.size()),
                              name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper struct_gep(const LLVMTypeWrapper &ty,
                              const LLVMValueWrapper &ptr, unsigned idx,
                              const std::string &name = "") {
    check_valid();
    ty.check_valid();
    ptr.check_valid();
    return LLVMValueWrapper(
        LLVMBuildStructGEP2(m_ref, ty.m_ref, ptr.m_ref, idx, name.c_str()),
        m_context_token);
  }

  // Comparisons
  LLVMValueWrapper icmp(LLVMIntPredicate pred, const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildICmp(m_ref, pred, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fcmp(LLVMRealPredicate pred, const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFCmp(m_ref, pred, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper select(const LLVMValueWrapper &cond,
                          const LLVMValueWrapper &then_val,
                          const LLVMValueWrapper &else_val,
                          const std::string &name = "") {
    check_valid();
    cond.check_valid();
    then_val.check_valid();
    else_val.check_valid();
    return LLVMValueWrapper(LLVMBuildSelect(m_ref, cond.m_ref, then_val.m_ref,
                                            else_val.m_ref, name.c_str()),
                            m_context_token);
  }

  // Cast operations
  LLVMValueWrapper trunc(const LLVMValueWrapper &val, const LLVMTypeWrapper &ty,
                         const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildTrunc(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper zext(const LLVMValueWrapper &val, const LLVMTypeWrapper &ty,
                        const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildZExt(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper sext(const LLVMValueWrapper &val, const LLVMTypeWrapper &ty,
                        const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildSExt(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fptrunc(const LLVMValueWrapper &val,
                           const LLVMTypeWrapper &ty,
                           const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFPTrunc(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fpext(const LLVMValueWrapper &val, const LLVMTypeWrapper &ty,
                         const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFPExt(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fptosi(const LLVMValueWrapper &val,
                          const LLVMTypeWrapper &ty,
                          const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFPToSI(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fptoui(const LLVMValueWrapper &val,
                          const LLVMTypeWrapper &ty,
                          const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFPToUI(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper sitofp(const LLVMValueWrapper &val,
                          const LLVMTypeWrapper &ty,
                          const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildSIToFP(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper uitofp(const LLVMValueWrapper &val,
                          const LLVMTypeWrapper &ty,
                          const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildUIToFP(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper ptrtoint(const LLVMValueWrapper &val,
                            const LLVMTypeWrapper &ty,
                            const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildPtrToInt(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper inttoptr(const LLVMValueWrapper &val,
                            const LLVMTypeWrapper &ty,
                            const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildIntToPtr(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper bitcast(const LLVMValueWrapper &val,
                           const LLVMTypeWrapper &ty,
                           const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildBitCast(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper int_cast2(const LLVMValueWrapper &val,
                             const LLVMTypeWrapper &ty, bool is_signed,
                             const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildIntCast2(m_ref, val.m_ref, ty.m_ref, is_signed, name.c_str()),
        m_context_token);
  }

  // Control flow
  LLVMValueWrapper ret(const LLVMValueWrapper &val) {
    check_valid();
    val.check_valid();
    return LLVMValueWrapper(LLVMBuildRet(m_ref, val.m_ref), m_context_token);
  }

  LLVMValueWrapper ret_void() {
    check_valid();
    return LLVMValueWrapper(LLVMBuildRetVoid(m_ref), m_context_token);
  }

  LLVMValueWrapper br(const LLVMBasicBlockWrapper &dest) {
    check_valid();
    dest.check_valid();
    return LLVMValueWrapper(LLVMBuildBr(m_ref, dest.m_ref), m_context_token);
  }

  LLVMValueWrapper cond_br(const LLVMValueWrapper &cond,
                           const LLVMBasicBlockWrapper &then_bb,
                           const LLVMBasicBlockWrapper &else_bb) {
    check_valid();
    cond.check_valid();
    then_bb.check_valid();
    else_bb.check_valid();
    return LLVMValueWrapper(
        LLVMBuildCondBr(m_ref, cond.m_ref, then_bb.m_ref, else_bb.m_ref),
        m_context_token);
  }

  LLVMValueWrapper switch_(const LLVMValueWrapper &val,
                           const LLVMBasicBlockWrapper &else_bb,
                           unsigned num_cases) {
    check_valid();
    val.check_valid();
    else_bb.check_valid();
    return LLVMValueWrapper(
        LLVMBuildSwitch(m_ref, val.m_ref, else_bb.m_ref, num_cases),
        m_context_token);
  }

  LLVMValueWrapper call(const LLVMTypeWrapper &func_ty,
                        const LLVMValueWrapper &func,
                        const std::vector<LLVMValueWrapper> &args,
                        const std::string &name = "") {
    check_valid();
    func_ty.check_valid();
    func.check_valid();
    std::vector<LLVMValueRef> arg_refs;
    arg_refs.reserve(args.size());
    for (const auto &arg : args) {
      arg.check_valid();
      arg_refs.push_back(arg.m_ref);
    }
    return LLVMValueWrapper(
        LLVMBuildCall2(m_ref, func_ty.m_ref, func.m_ref, arg_refs.data(),
                       static_cast<unsigned>(arg_refs.size()), name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper unreachable() {
    check_valid();
    return LLVMValueWrapper(LLVMBuildUnreachable(m_ref), m_context_token);
  }

  // PHI
  LLVMValueWrapper phi(const LLVMTypeWrapper &ty,
                       const std::string &name = "") {
    check_valid();
    ty.check_valid();
    return LLVMValueWrapper(LLVMBuildPhi(m_ref, ty.m_ref, name.c_str()),
                            m_context_token);
  }

  // =========================================================================
  // Phase 5.6: Additional Instruction Builders for Echo Command
  // =========================================================================

  // Exception handling - Invoke with operand bundles
  LLVMValueWrapper invoke_with_operand_bundles(
      const LLVMTypeWrapper &fn_ty, const LLVMValueWrapper &fn,
      const std::vector<LLVMValueWrapper> &args,
      const LLVMBasicBlockWrapper &then_bb,
      const LLVMBasicBlockWrapper &catch_bb,
      const std::vector<LLVMOperandBundleRef> &bundles,
      const std::string &name = "") {
    check_valid();
    fn_ty.check_valid();
    fn.check_valid();
    then_bb.check_valid();
    catch_bb.check_valid();

    std::vector<LLVMValueRef> arg_refs;
    arg_refs.reserve(args.size());
    for (const auto &arg : args) {
      arg.check_valid();
      arg_refs.push_back(arg.m_ref);
    }

    return LLVMValueWrapper(
        LLVMBuildInvokeWithOperandBundles(
            m_ref, fn_ty.m_ref, fn.m_ref, arg_refs.data(),
            static_cast<unsigned>(arg_refs.size()), then_bb.m_ref,
            catch_bb.m_ref, const_cast<LLVMOperandBundleRef *>(bundles.data()),
            static_cast<unsigned>(bundles.size()), name.c_str()),
        m_context_token);
  }

  // Call with operand bundles
  LLVMValueWrapper call_with_operand_bundles(
      const LLVMTypeWrapper &fn_ty, const LLVMValueWrapper &fn,
      const std::vector<LLVMValueWrapper> &args,
      const std::vector<LLVMOperandBundleRef> &bundles,
      const std::string &name = "") {
    check_valid();
    fn_ty.check_valid();
    fn.check_valid();

    std::vector<LLVMValueRef> arg_refs;
    arg_refs.reserve(args.size());
    for (const auto &arg : args) {
      arg.check_valid();
      arg_refs.push_back(arg.m_ref);
    }

    return LLVMValueWrapper(
        LLVMBuildCallWithOperandBundles(
            m_ref, fn_ty.m_ref, fn.m_ref, arg_refs.data(),
            static_cast<unsigned>(arg_refs.size()),
            const_cast<LLVMOperandBundleRef *>(bundles.data()),
            static_cast<unsigned>(bundles.size()), name.c_str()),
        m_context_token);
  }

  // CallBr instruction
  LLVMValueWrapper callbr(const LLVMTypeWrapper &fn_ty,
                          const LLVMValueWrapper &fn,
                          const LLVMBasicBlockWrapper &default_dest,
                          const std::vector<LLVMBasicBlockWrapper> &indirect_dests,
                          const std::vector<LLVMValueWrapper> &args,
                          const std::vector<LLVMOperandBundleRef> &bundles,
                          const std::string &name = "") {
    check_valid();
    fn_ty.check_valid();
    fn.check_valid();
    default_dest.check_valid();

    std::vector<LLVMBasicBlockRef> dest_refs;
    dest_refs.reserve(indirect_dests.size());
    for (const auto &dest : indirect_dests) {
      dest.check_valid();
      dest_refs.push_back(dest.m_ref);
    }

    std::vector<LLVMValueRef> arg_refs;
    arg_refs.reserve(args.size());
    for (const auto &arg : args) {
      arg.check_valid();
      arg_refs.push_back(arg.m_ref);
    }

    return LLVMValueWrapper(
        LLVMBuildCallBr(m_ref, fn_ty.m_ref, fn.m_ref, default_dest.m_ref,
                        dest_refs.data(), static_cast<unsigned>(dest_refs.size()),
                        arg_refs.data(), static_cast<unsigned>(arg_refs.size()),
                        const_cast<LLVMOperandBundleRef *>(bundles.data()),
                        static_cast<unsigned>(bundles.size()), name.c_str()),
        m_context_token);
  }

  // Resume instruction
  LLVMValueWrapper resume(const LLVMValueWrapper &exn) {
    check_valid();
    exn.check_valid();
    return LLVMValueWrapper(LLVMBuildResume(m_ref, exn.m_ref), m_context_token);
  }

  // Landing pad instruction
  LLVMValueWrapper landing_pad(const LLVMTypeWrapper &ty,
                               unsigned num_clauses,
                               const std::string &name = "") {
    check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildLandingPad(m_ref, ty.m_ref, nullptr, num_clauses, name.c_str()),
        m_context_token);
  }

  // Cleanup return
  LLVMValueWrapper
  cleanup_ret(const LLVMValueWrapper &catch_pad,
              std::optional<LLVMBasicBlockWrapper> unwind_bb = std::nullopt) {
    check_valid();
    catch_pad.check_valid();
    LLVMBasicBlockRef unwind_ref =
        unwind_bb.has_value() ? unwind_bb->m_ref : nullptr;
    return LLVMValueWrapper(
        LLVMBuildCleanupRet(m_ref, catch_pad.m_ref, unwind_ref),
        m_context_token);
  }

  // Catch return
  LLVMValueWrapper catch_ret(const LLVMValueWrapper &catch_pad,
                             const LLVMBasicBlockWrapper &bb) {
    check_valid();
    catch_pad.check_valid();
    bb.check_valid();
    return LLVMValueWrapper(
        LLVMBuildCatchRet(m_ref, catch_pad.m_ref, bb.m_ref), m_context_token);
  }

  // Catch pad
  LLVMValueWrapper catch_pad(const LLVMValueWrapper &parent_pad,
                             const std::vector<LLVMValueWrapper> &args,
                             const std::string &name = "") {
    check_valid();
    parent_pad.check_valid();

    std::vector<LLVMValueRef> arg_refs;
    arg_refs.reserve(args.size());
    for (const auto &arg : args) {
      arg.check_valid();
      arg_refs.push_back(arg.m_ref);
    }

    return LLVMValueWrapper(
        LLVMBuildCatchPad(m_ref, parent_pad.m_ref, arg_refs.data(),
                          static_cast<unsigned>(arg_refs.size()), name.c_str()),
        m_context_token);
  }

  // Cleanup pad
  LLVMValueWrapper cleanup_pad(const LLVMValueWrapper &parent_pad,
                               const std::vector<LLVMValueWrapper> &args,
                               const std::string &name = "") {
    check_valid();
    parent_pad.check_valid();

    std::vector<LLVMValueRef> arg_refs;
    arg_refs.reserve(args.size());
    for (const auto &arg : args) {
      arg.check_valid();
      arg_refs.push_back(arg.m_ref);
    }

    return LLVMValueWrapper(
        LLVMBuildCleanupPad(m_ref, parent_pad.m_ref, arg_refs.data(),
                            static_cast<unsigned>(arg_refs.size()), name.c_str()),
        m_context_token);
  }

  // Catch switch
  LLVMValueWrapper
  catch_switch(const LLVMValueWrapper &parent_pad,
               std::optional<LLVMBasicBlockWrapper> unwind_bb, unsigned num_handlers,
               const std::string &name = "") {
    check_valid();
    parent_pad.check_valid();
    LLVMBasicBlockRef unwind_ref =
        unwind_bb.has_value() ? unwind_bb->m_ref : nullptr;
    return LLVMValueWrapper(
        LLVMBuildCatchSwitch(m_ref, parent_pad.m_ref, unwind_ref, num_handlers,
                             name.c_str()),
        m_context_token);
  }

  // Extract value from aggregate
  LLVMValueWrapper extract_value(const LLVMValueWrapper &agg, unsigned index,
                                 const std::string &name = "") {
    check_valid();
    agg.check_valid();
    return LLVMValueWrapper(
        LLVMBuildExtractValue(m_ref, agg.m_ref, index, name.c_str()),
        m_context_token);
  }

  // Insert value into aggregate
  LLVMValueWrapper insert_value(const LLVMValueWrapper &agg,
                                const LLVMValueWrapper &val, unsigned index,
                                const std::string &name = "") {
    check_valid();
    agg.check_valid();
    val.check_valid();
    return LLVMValueWrapper(
        LLVMBuildInsertValue(m_ref, agg.m_ref, val.m_ref, index, name.c_str()),
        m_context_token);
  }

  // Extract element from vector
  LLVMValueWrapper extract_element(const LLVMValueWrapper &vec,
                                   const LLVMValueWrapper &index,
                                   const std::string &name = "") {
    check_valid();
    vec.check_valid();
    index.check_valid();
    return LLVMValueWrapper(
        LLVMBuildExtractElement(m_ref, vec.m_ref, index.m_ref, name.c_str()),
        m_context_token);
  }

  // Insert element into vector
  LLVMValueWrapper insert_element(const LLVMValueWrapper &vec,
                                  const LLVMValueWrapper &val,
                                  const LLVMValueWrapper &index,
                                  const std::string &name = "") {
    check_valid();
    vec.check_valid();
    val.check_valid();
    index.check_valid();
    return LLVMValueWrapper(
        LLVMBuildInsertElement(m_ref, vec.m_ref, val.m_ref, index.m_ref,
                               name.c_str()),
        m_context_token);
  }

  // Shuffle vector
  LLVMValueWrapper shuffle_vector(const LLVMValueWrapper &v1,
                                  const LLVMValueWrapper &v2,
                                  const LLVMValueWrapper &mask,
                                  const std::string &name = "") {
    check_valid();
    v1.check_valid();
    v2.check_valid();
    mask.check_valid();
    return LLVMValueWrapper(
        LLVMBuildShuffleVector(m_ref, v1.m_ref, v2.m_ref, mask.m_ref,
                               name.c_str()),
        m_context_token);
  }

  // Freeze instruction
  LLVMValueWrapper freeze(const LLVMValueWrapper &val,
                          const std::string &name = "") {
    check_valid();
    val.check_valid();
    return LLVMValueWrapper(LLVMBuildFreeze(m_ref, val.m_ref, name.c_str()),
                            m_context_token);
  }

  // GEP with no-wrap flags
  LLVMValueWrapper gep_with_no_wrap_flags(const LLVMTypeWrapper &ty,
                                          const LLVMValueWrapper &ptr,
                                          const std::vector<LLVMValueWrapper> &indices,
                                          LLVMGEPNoWrapFlags flags,
                                          const std::string &name = "") {
    check_valid();
    ty.check_valid();
    ptr.check_valid();

    std::vector<LLVMValueRef> idx_refs;
    idx_refs.reserve(indices.size());
    for (const auto &idx : indices) {
      idx.check_valid();
      idx_refs.push_back(idx.m_ref);
    }

    return LLVMValueWrapper(
        LLVMBuildGEPWithNoWrapFlags(m_ref, ty.m_ref, ptr.m_ref, idx_refs.data(),
                                    static_cast<unsigned>(idx_refs.size()),
                                    name.c_str(), flags),
        m_context_token);
  }

  // Atomic RMW with sync scope
  LLVMValueWrapper atomic_rmw_sync_scope(LLVMAtomicRMWBinOp op,
                                         const LLVMValueWrapper &ptr,
                                         const LLVMValueWrapper &val,
                                         LLVMAtomicOrdering ordering,
                                         unsigned sync_scope_id) {
    check_valid();
    ptr.check_valid();
    val.check_valid();
    return LLVMValueWrapper(
        LLVMBuildAtomicRMWSyncScope(m_ref, op, ptr.m_ref, val.m_ref, ordering,
                                    sync_scope_id),
        m_context_token);
  }

  // Atomic CmpXchg with sync scope
  LLVMValueWrapper atomic_cmpxchg_sync_scope(const LLVMValueWrapper &ptr,
                                             const LLVMValueWrapper &cmp,
                                             const LLVMValueWrapper &new_val,
                                             LLVMAtomicOrdering success_ordering,
                                             LLVMAtomicOrdering failure_ordering,
                                             unsigned sync_scope_id) {
    check_valid();
    ptr.check_valid();
    cmp.check_valid();
    new_val.check_valid();
    return LLVMValueWrapper(
        LLVMBuildAtomicCmpXchgSyncScope(m_ref, ptr.m_ref, cmp.m_ref,
                                        new_val.m_ref, success_ordering,
                                        failure_ordering, sync_scope_id),
        m_context_token);
  }

  // Fence with sync scope
  LLVMValueWrapper fence_sync_scope(LLVMAtomicOrdering ordering,
                                    unsigned sync_scope_id,
                                    const std::string &name = "") {
    check_valid();
    return LLVMValueWrapper(
        LLVMBuildFenceSyncScope(m_ref, ordering, sync_scope_id, name.c_str()),
        m_context_token);
  }

  // Insert instruction at builder position with name
  void insert_into_builder_with_name(const LLVMValueWrapper &instr,
                                     const std::string &name) {
    check_valid();
    instr.check_valid();
    LLVMInsertIntoBuilderWithName(m_ref, instr.m_ref, name.c_str());
  }

  // Add metadata to instruction (from builder's metadata attachments)
  void add_metadata_to_inst(const LLVMValueWrapper &instr) {
    check_valid();
    instr.check_valid();
    LLVMAddMetadataToInst(m_ref, instr.m_ref);
  }
};

// =============================================================================
// Module Wrapper
// =============================================================================

struct LLVMModuleWrapper : NoMoveCopy {
  LLVMModuleRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;
  std::shared_ptr<ValidityToken> m_token;
  LLVMContextRef m_ctx_ref = nullptr;

  LLVMModuleWrapper(const std::string &name, LLVMContextRef ctx,
                    std::shared_ptr<ValidityToken> context_token)
      : m_context_token(std::move(context_token)),
        m_token(std::make_shared<ValidityToken>()), m_ctx_ref(ctx) {
    m_ref = LLVMModuleCreateWithNameInContext(name.c_str(), ctx);
  }

  // Constructor for wrapping an existing module (from bitcode parsing)
  LLVMModuleWrapper(LLVMModuleRef mod, LLVMContextRef ctx,
                    std::shared_ptr<ValidityToken> context_token)
      : m_ref(mod), m_context_token(std::move(context_token)),
        m_token(std::make_shared<ValidityToken>()), m_ctx_ref(ctx) {
  }

  ~LLVMModuleWrapper() {
    if (m_ref) {
      // Only dispose the module if its context is still alive.
      // If the context was already destroyed, the module memory is already
      // freed and calling LLVMDisposeModule would cause a use-after-free crash.
      if (m_context_token && m_context_token->is_valid()) {
        LLVMDisposeModule(m_ref);
      } else {
        // Context is gone - module was already freed with it, or we must leak
        // to avoid crash. Log a warning for debugging.
        // Note: In LLVM, modules are NOT automatically freed when context is
        // destroyed, so this represents a leak. But leaking is better than
        // crashing.
        fprintf(stderr,
                "Warning: LLVM Module outlived its Context. "
                "This may cause a memory leak. "
                "Ensure modules are deleted before their context.\n");
      }
      m_ref = nullptr;
    }
    if (m_token) {
      m_token->invalidate();
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Module has been disposed");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError("Module used after context was destroyed");
  }

  void dispose() {
    if (m_ref) {
      LLVMDisposeModule(m_ref);
      m_ref = nullptr;
    }
    if (m_token) {
      m_token->invalidate();
    }
  }

  // Properties
  std::string get_name() const {
    check_valid();
    size_t len;
    const char *name = LLVMGetModuleIdentifier(m_ref, &len);
    return std::string(name, len);
  }

  void set_name(const std::string &name) {
    check_valid();
    LLVMSetModuleIdentifier(m_ref, name.c_str(), name.size());
  }

  std::string get_source_filename() const {
    check_valid();
    size_t len;
    const char *name = LLVMGetSourceFileName(m_ref, &len);
    return std::string(name, len);
  }

  void set_source_filename(const std::string &name) {
    check_valid();
    LLVMSetSourceFileName(m_ref, name.c_str(), name.size());
  }

  std::string get_data_layout() const {
    check_valid();
    return std::string(LLVMGetDataLayoutStr(m_ref));
  }

  void set_data_layout(const std::string &dl) {
    check_valid();
    LLVMSetDataLayout(m_ref, dl.c_str());
  }

  std::string get_target_triple() const {
    check_valid();
    return std::string(LLVMGetTarget(m_ref));
  }

  void set_target_triple(const std::string &triple) {
    check_valid();
    LLVMSetTarget(m_ref, triple.c_str());
  }

  // Functions
  LLVMFunctionWrapper add_function(const std::string &name,
                                   const LLVMTypeWrapper &func_ty) {
    check_valid();
    func_ty.check_valid();
    LLVMValueRef func = LLVMAddFunction(m_ref, name.c_str(), func_ty.m_ref);
    return LLVMFunctionWrapper(func, m_context_token);
  }

  std::optional<LLVMFunctionWrapper> get_function(const std::string &name) {
    check_valid();
    LLVMValueRef func = LLVMGetNamedFunction(m_ref, name.c_str());
    if (!func)
      return std::nullopt;
    return LLVMFunctionWrapper(func, m_context_token);
  }

  // Global variables
  LLVMValueWrapper add_global(const LLVMTypeWrapper &ty,
                              const std::string &name) {
    check_valid();
    ty.check_valid();
    return LLVMValueWrapper(LLVMAddGlobal(m_ref, ty.m_ref, name.c_str()),
                            m_context_token);
  }

  LLVMValueWrapper add_global_in_address_space(const LLVMTypeWrapper &ty,
                                               const std::string &name,
                                               unsigned address_space) {
    check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMAddGlobalInAddressSpace(m_ref, ty.m_ref, name.c_str(), address_space),
        m_context_token);
  }

  std::optional<LLVMValueWrapper> get_global(const std::string &name) {
    check_valid();
    LLVMValueRef global = LLVMGetNamedGlobal(m_ref, name.c_str());
    if (!global)
      return std::nullopt;
    return LLVMValueWrapper(global, m_context_token);
  }

  std::optional<LLVMValueWrapper> first_global() {
    check_valid();
    LLVMValueRef g = LLVMGetFirstGlobal(m_ref);
    if (!g)
      return std::nullopt;
    return LLVMValueWrapper(g, m_context_token);
  }

  std::optional<LLVMValueWrapper> last_global() {
    check_valid();
    LLVMValueRef g = LLVMGetLastGlobal(m_ref);
    if (!g)
      return std::nullopt;
    return LLVMValueWrapper(g, m_context_token);
  }

  std::vector<LLVMValueWrapper> globals() {
    check_valid();
    std::vector<LLVMValueWrapper> result;
    for (LLVMValueRef g = LLVMGetFirstGlobal(m_ref); g;
         g = LLVMGetNextGlobal(g)) {
      result.emplace_back(g, m_context_token);
    }
    return result;
  }

  // Function iteration
  std::vector<LLVMFunctionWrapper> functions() {
    check_valid();
    std::vector<LLVMFunctionWrapper> result;
    for (LLVMValueRef fn = LLVMGetFirstFunction(m_ref); fn;
         fn = LLVMGetNextFunction(fn)) {
      result.emplace_back(fn, m_context_token);
    }
    return result;
  }

  std::optional<LLVMFunctionWrapper> first_function() {
    check_valid();
    LLVMValueRef fn = LLVMGetFirstFunction(m_ref);
    if (!fn)
      return std::nullopt;
    return LLVMFunctionWrapper(fn, m_context_token);
  }

  std::optional<LLVMFunctionWrapper> last_function() {
    check_valid();
    LLVMValueRef fn = LLVMGetLastFunction(m_ref);
    if (!fn)
      return std::nullopt;
    return LLVMFunctionWrapper(fn, m_context_token);
  }

  // Output
  std::string to_string() const {
    check_valid();
    char *str = LLVMPrintModuleToString(m_ref);
    std::string result(str);
    LLVMDisposeMessage(str);
    return result;
  }

  // Verification
  bool verify() const {
    check_valid();
    char *error = nullptr;
    LLVMBool failed =
        LLVMVerifyModule(m_ref, LLVMReturnStatusAction, &error);
    if (error)
      LLVMDisposeMessage(error);
    return !failed;
  }

  std::string get_verification_error() const {
    check_valid();
    char *error = nullptr;
    LLVMVerifyModule(m_ref, LLVMReturnStatusAction, &error);
    std::string result;
    if (error) {
      result = error;
      LLVMDisposeMessage(error);
    }
    return result;
  }

  // Global alias support for echo command
  std::optional<LLVMValueWrapper> first_global_alias() {
    check_valid();
    LLVMValueRef alias = LLVMGetFirstGlobalAlias(m_ref);
    if (!alias)
      return std::nullopt;
    return LLVMValueWrapper(alias, m_context_token);
  }

  std::optional<LLVMValueWrapper> last_global_alias() {
    check_valid();
    LLVMValueRef alias = LLVMGetLastGlobalAlias(m_ref);
    if (!alias)
      return std::nullopt;
    return LLVMValueWrapper(alias, m_context_token);
  }

  std::optional<LLVMValueWrapper> get_named_global_alias(const std::string &name) {
    check_valid();
    LLVMValueRef alias = LLVMGetNamedGlobalAlias(m_ref, name.c_str(), name.size());
    if (!alias)
      return std::nullopt;
    return LLVMValueWrapper(alias, m_context_token);
  }

  LLVMValueWrapper add_alias(const LLVMTypeWrapper &value_ty,
                             unsigned addr_space,
                             const LLVMValueWrapper &aliasee,
                             const std::string &name) {
    check_valid();
    value_ty.check_valid();
    aliasee.check_valid();
    return LLVMValueWrapper(
        LLVMAddAlias2(m_ref, value_ty.m_ref, addr_space, aliasee.m_ref,
                      name.c_str()),
        m_context_token);
  }

  // Global IFunc support for echo command
  std::optional<LLVMValueWrapper> first_global_ifunc() {
    check_valid();
    LLVMValueRef ifunc = LLVMGetFirstGlobalIFunc(m_ref);
    if (!ifunc)
      return std::nullopt;
    return LLVMValueWrapper(ifunc, m_context_token);
  }

  std::optional<LLVMValueWrapper> last_global_ifunc() {
    check_valid();
    LLVMValueRef ifunc = LLVMGetLastGlobalIFunc(m_ref);
    if (!ifunc)
      return std::nullopt;
    return LLVMValueWrapper(ifunc, m_context_token);
  }

  std::optional<LLVMValueWrapper> get_named_global_ifunc(const std::string &name) {
    check_valid();
    LLVMValueRef ifunc = LLVMGetNamedGlobalIFunc(m_ref, name.c_str(), name.size());
    if (!ifunc)
      return std::nullopt;
    return LLVMValueWrapper(ifunc, m_context_token);
  }

  LLVMValueWrapper add_global_ifunc(const std::string &name,
                                    const LLVMTypeWrapper &ty,
                                    unsigned addr_space,
                                    const LLVMValueWrapper &resolver) {
    check_valid();
    ty.check_valid();
    resolver.check_valid();
    return LLVMValueWrapper(
        LLVMAddGlobalIFunc(m_ref, name.c_str(), name.size(), ty.m_ref,
                          addr_space, resolver.m_ref),
        m_context_token);
  }

  // Named metadata support for echo command
  std::optional<LLVMNamedMDNodeWrapper> first_named_metadata() {
    check_valid();
    LLVMNamedMDNodeRef md = LLVMGetFirstNamedMetadata(m_ref);
    if (!md)
      return std::nullopt;
    return LLVMNamedMDNodeWrapper(md, m_context_token);
  }

  std::optional<LLVMNamedMDNodeWrapper> last_named_metadata() {
    check_valid();
    LLVMNamedMDNodeRef md = LLVMGetLastNamedMetadata(m_ref);
    if (!md)
      return std::nullopt;
    return LLVMNamedMDNodeWrapper(md, m_context_token);
  }

  std::optional<LLVMNamedMDNodeWrapper> get_named_metadata(const std::string &name) {
    check_valid();
    LLVMNamedMDNodeRef md = LLVMGetNamedMetadata(m_ref, name.c_str(), name.size());
    if (!md)
      return std::nullopt;
    return LLVMNamedMDNodeWrapper(md, m_context_token);
  }

  LLVMNamedMDNodeWrapper get_or_insert_named_metadata(const std::string &name) {
    check_valid();
    return LLVMNamedMDNodeWrapper(
        LLVMGetOrInsertNamedMetadata(m_ref, name.c_str(), name.size()),
        m_context_token);
  }

  unsigned get_named_metadata_num_operands(const std::string &name) {
    check_valid();
    return LLVMGetNamedMetadataNumOperands(m_ref, name.c_str());
  }

  std::vector<LLVMValueWrapper> get_named_metadata_operands(const std::string &name) {
    check_valid();
    unsigned count = LLVMGetNamedMetadataNumOperands(m_ref, name.c_str());
    if (count == 0)
      return {};
    
    std::vector<LLVMValueRef> operands(count);
    LLVMGetNamedMetadataOperands(m_ref, name.c_str(), operands.data());
    
    std::vector<LLVMValueWrapper> result;
    result.reserve(count);
    for (LLVMValueRef op : operands) {
      result.emplace_back(op, m_context_token);
    }
    return result;
  }

  // Module inline assembly for echo command
  std::string get_inline_asm() const {
    check_valid();
    size_t len = 0;
    const char *asm_str = LLVMGetModuleInlineAsm(m_ref, &len);
    return std::string(asm_str, len);
  }

  void set_inline_asm(const std::string &asm_str) {
    check_valid();
    LLVMSetModuleInlineAsm2(m_ref, asm_str.c_str(), asm_str.size());
  }

  // Clone - returns a ModuleManager that must be used with 'with' or .dispose()
  LLVMModuleManager *clone() const;

private:
  // Private constructor for clone
  LLVMModuleWrapper() = default;
  friend struct LLVMModuleManager;
};

// =============================================================================
// Context Wrapper
// =============================================================================

struct LLVMContextWrapper : NoMoveCopy {
  LLVMContextRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_token;
  bool m_global = false;

  explicit LLVMContextWrapper(bool global = false)
      : m_token(std::make_shared<ValidityToken>()), m_global(global) {
    if (global) {
      m_ref = LLVMGetGlobalContext();
    } else {
      m_ref = LLVMContextCreate();
    }
  }

  ~LLVMContextWrapper() {
    if (m_ref && !m_global) {
      LLVMContextDispose(m_ref);
      m_ref = nullptr;
    }
    if (m_token) {
      m_token->invalidate();
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Context has been disposed");
    if (!m_token || !m_token->is_valid())
      throw LLVMUseAfterFreeError("Context is no longer valid");
  }

  void dispose() {
    if (m_ref && !m_global) {
      LLVMContextDispose(m_ref);
      m_ref = nullptr;
    }
    if (m_token) {
      m_token->invalidate();
    }
  }

  // Properties
  bool get_discard_value_names() const {
    check_valid();
    return LLVMContextShouldDiscardValueNames(m_ref);
  }

  void set_discard_value_names(bool discard) {
    check_valid();
    LLVMContextSetDiscardValueNames(m_ref, discard);
  }

  // Type factory methods
  LLVMTypeWrapper void_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMVoidTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper int1_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMInt1TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper int8_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMInt8TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper int16_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMInt16TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper int32_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMInt32TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper int64_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMInt64TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper int128_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMInt128TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper int_type(unsigned bits) {
    check_valid();
    return LLVMTypeWrapper(LLVMIntTypeInContext(m_ref, bits), m_token);
  }

  LLVMTypeWrapper half_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMHalfTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper float_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMFloatTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper double_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMDoubleTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper bfloat_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMBFloatTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper pointer_type(unsigned address_space = 0) {
    check_valid();
    return LLVMTypeWrapper(LLVMPointerTypeInContext(m_ref, address_space),
                           m_token);
  }

  LLVMTypeWrapper array_type(const LLVMTypeWrapper &elem_ty, uint64_t count) {
    check_valid();
    elem_ty.check_valid();
    return LLVMTypeWrapper(LLVMArrayType2(elem_ty.m_ref, count), m_token);
  }

  LLVMTypeWrapper vector_type(const LLVMTypeWrapper &elem_ty,
                              unsigned elem_count) {
    check_valid();
    elem_ty.check_valid();
    return LLVMTypeWrapper(LLVMVectorType(elem_ty.m_ref, elem_count), m_token);
  }

  LLVMTypeWrapper function_type(const LLVMTypeWrapper &ret_ty,
                                const std::vector<LLVMTypeWrapper> &param_types,
                                bool vararg = false) {
    check_valid();
    ret_ty.check_valid();
    std::vector<LLVMTypeRef> params;
    params.reserve(param_types.size());
    for (const auto &p : param_types) {
      p.check_valid();
      params.push_back(p.m_ref);
    }
    return LLVMTypeWrapper(
        LLVMFunctionType(ret_ty.m_ref, params.data(),
                         static_cast<unsigned>(params.size()), vararg),
        m_token);
  }

  LLVMTypeWrapper struct_type(const std::vector<LLVMTypeWrapper> &elem_types,
                              bool packed = false) {
    check_valid();
    std::vector<LLVMTypeRef> elems;
    elems.reserve(elem_types.size());
    for (const auto &e : elem_types) {
      e.check_valid();
      elems.push_back(e.m_ref);
    }
    return LLVMTypeWrapper(
        LLVMStructTypeInContext(m_ref, elems.data(),
                                static_cast<unsigned>(elems.size()), packed),
        m_token);
  }

  LLVMTypeWrapper named_struct_type(const std::string &name) {
    check_valid();
    return LLVMTypeWrapper(LLVMStructCreateNamed(m_ref, name.c_str()), m_token);
  }

  // Additional type creation functions for echo command
  LLVMTypeWrapper x86_fp80_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMX86FP80TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper fp128_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMFP128TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper ppc_fp128_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMPPCFP128TypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper label_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMLabelTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper metadata_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMMetadataTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper x86_amx_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMX86AMXTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper token_type() {
    check_valid();
    return LLVMTypeWrapper(LLVMTokenTypeInContext(m_ref), m_token);
  }

  LLVMTypeWrapper scalable_vector_type(const LLVMTypeWrapper &elem_ty,
                                        unsigned elem_count) {
    check_valid();
    elem_ty.check_valid();
    return LLVMTypeWrapper(LLVMScalableVectorType(elem_ty.m_ref, elem_count),
                           m_token);
  }

  LLVMTypeWrapper target_ext_type(const std::string &name,
                                   const std::vector<LLVMTypeWrapper> &type_params,
                                   const std::vector<unsigned> &int_params) {
    check_valid();
    std::vector<LLVMTypeRef> type_refs;
    type_refs.reserve(type_params.size());
    for (const auto &t : type_params) {
      t.check_valid();
      type_refs.push_back(t.m_ref);
    }
    return LLVMTypeWrapper(
        LLVMTargetExtTypeInContext(
            m_ref, name.c_str(), type_refs.data(), type_refs.size(),
            const_cast<unsigned *>(int_params.data()), int_params.size()),
        m_token);
  }

  std::optional<LLVMTypeWrapper> get_type_by_name(const std::string &name) {
    check_valid();
    LLVMTypeRef ty = LLVMGetTypeByName2(m_ref, name.c_str());
    if (!ty)
      return std::nullopt; // Return None to Python
    return LLVMTypeWrapper(ty, m_token);
  }

  // Create an unattached basic block (must be attached to a function later)
  LLVMBasicBlockWrapper create_basic_block(const std::string &name) {
    check_valid();
    LLVMBasicBlockRef bb = LLVMCreateBasicBlockInContext(m_ref, name.c_str());
    return LLVMBasicBlockWrapper(bb, m_token);
  }

  // Module creation (returns context manager) - defined after LLVMModuleManager
  LLVMModuleManager *create_module(const std::string &name);

  // Builder creation (returns context manager) - defined after LLVMBuilderManager
  LLVMBuilderManager *create_builder();
};

// =============================================================================
// Context Manager for Python with statement
// =============================================================================

struct LLVMContextManager : NoMoveCopy {
  std::unique_ptr<LLVMContextWrapper> m_context;

  LLVMContextWrapper &enter() {
    if (m_context)
      throw LLVMException("Context manager already entered");
    m_context = std::make_unique<LLVMContextWrapper>();
    return *m_context;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (!m_context)
      throw LLVMException("Context manager not entered");
    m_context.reset();
  }
};

// =============================================================================
// Module Manager for Python with statement
// =============================================================================

struct LLVMModuleManager : NoMoveCopy {
  std::string m_name;
  LLVMContextWrapper *m_context = nullptr;
  std::unique_ptr<LLVMModuleWrapper> m_module;
  bool m_entered = false;
  bool m_disposed = false;
  bool m_from_clone = false;  // True if this manager owns a cloned module

  // Constructor for ctx.create_module("name")
  LLVMModuleManager(std::string name, LLVMContextWrapper *context)
      : m_name(std::move(name)), m_context(context) {}

  // Constructor for mod.clone() - takes ownership of pre-created module
  explicit LLVMModuleManager(std::unique_ptr<LLVMModuleWrapper> cloned_module)
      : m_module(std::move(cloned_module)), m_from_clone(true) {}

  LLVMModuleWrapper &enter() {
    if (m_disposed)
      throw LLVMException("Module has been disposed");
    if (m_entered)
      throw LLVMException("Module manager already entered");

    m_entered = true;

    if (m_from_clone) {
      // Already have a module from clone - just validate and return it
      m_module->check_valid();
      return *m_module;
    }

    // Create new module
    if (!m_context)
      throw LLVMException("No context provided");
    m_context->check_valid();
    m_module = std::make_unique<LLVMModuleWrapper>(m_name, m_context->m_ref,
                                                   m_context->m_token);
    return *m_module;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (m_disposed)
      throw LLVMException("Module has already been disposed");
    if (!m_entered)
      throw LLVMException("Module manager was not entered");
    m_module.reset();
    m_disposed = true;
  }

  void dispose() {
    if (m_disposed)
      throw LLVMException("Module has already been disposed");
    if (m_entered)
      throw LLVMException("Cannot call dispose() after __enter__; use __exit__ or 'with' statement");
    if (!m_from_clone && !m_module)
      throw LLVMException("Module has not been created");
    m_module.reset();
    m_disposed = true;
  }
};

// =============================================================================
// Builder Manager for Python with statement
// =============================================================================

struct LLVMBuilderManager : NoMoveCopy {
  LLVMContextWrapper *m_context = nullptr;
  std::unique_ptr<LLVMBuilderWrapper> m_builder;
  bool m_entered = false;
  bool m_disposed = false;

  explicit LLVMBuilderManager(LLVMContextWrapper *context)
      : m_context(context) {}

  LLVMBuilderWrapper &enter() {
    if (m_disposed)
      throw LLVMException("Builder has been disposed");
    if (m_entered)
      throw LLVMException("Builder manager already entered");
    if (!m_context)
      throw LLVMException("No context provided");
    m_context->check_valid();
    m_builder =
        std::make_unique<LLVMBuilderWrapper>(m_context->m_ref, m_context->m_token);
    m_entered = true;
    return *m_builder;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (m_disposed)
      throw LLVMException("Builder has already been disposed");
    if (!m_entered)
      throw LLVMException("Builder manager was not entered");
    m_builder.reset();
    m_disposed = true;
  }

  void dispose() {
    if (m_disposed)
      throw LLVMException("Builder has already been disposed");
    if (m_entered)
      throw LLVMException("Cannot call dispose() after __enter__; use __exit__ or 'with' statement");
    // For builder, there's nothing to dispose if not entered
    m_disposed = true;
  }
};

// =============================================================================
// LLVMContextWrapper method implementations (defined after Manager classes)
// =============================================================================

LLVMModuleManager *LLVMContextWrapper::create_module(const std::string &name) {
  check_valid();
  return new LLVMModuleManager(name, this);
}

LLVMBuilderManager *LLVMContextWrapper::create_builder() {
  check_valid();
  return new LLVMBuilderManager(this);
}

// =============================================================================
// LLVMModuleWrapper method implementations (defined after Manager classes)
// =============================================================================

LLVMModuleManager *LLVMModuleWrapper::clone() const {
  check_valid();
  LLVMModuleRef cloned_ref = LLVMCloneModule(m_ref);

  // Create wrapper for the cloned module using raw new (private constructor)
  auto *raw_wrapper = new LLVMModuleWrapper();
  raw_wrapper->m_ref = cloned_ref;
  raw_wrapper->m_context_token = m_context_token;
  raw_wrapper->m_token = std::make_shared<ValidityToken>();
  raw_wrapper->m_ctx_ref = m_ctx_ref;

  // Wrap in unique_ptr and return a manager that owns the cloned module
  return new LLVMModuleManager(std::unique_ptr<LLVMModuleWrapper>(raw_wrapper));
}

// =============================================================================
// Helper functions for constants
// =============================================================================

LLVMValueWrapper const_int(const LLVMTypeWrapper &ty, long long val,
                           bool sign_extend = false) {
  ty.check_valid();
  return LLVMValueWrapper(LLVMConstInt(ty.m_ref, val, sign_extend),
                          ty.m_context_token);
}

LLVMValueWrapper const_real(const LLVMTypeWrapper &ty, double val) {
  ty.check_valid();
  return LLVMValueWrapper(LLVMConstReal(ty.m_ref, val), ty.m_context_token);
}

LLVMValueWrapper const_null(const LLVMTypeWrapper &ty) {
  ty.check_valid();
  return LLVMValueWrapper(LLVMConstNull(ty.m_ref), ty.m_context_token);
}

LLVMValueWrapper const_all_ones(const LLVMTypeWrapper &ty) {
  ty.check_valid();
  return LLVMValueWrapper(LLVMConstAllOnes(ty.m_ref), ty.m_context_token);
}

LLVMValueWrapper get_undef(const LLVMTypeWrapper &ty) {
  ty.check_valid();
  return LLVMValueWrapper(LLVMGetUndef(ty.m_ref), ty.m_context_token);
}

LLVMValueWrapper get_poison(const LLVMTypeWrapper &ty) {
  ty.check_valid();
  return LLVMValueWrapper(LLVMGetPoison(ty.m_ref), ty.m_context_token);
}

LLVMValueWrapper const_array(const LLVMTypeWrapper &elem_ty,
                             const std::vector<LLVMValueWrapper> &vals) {
  elem_ty.check_valid();
  std::vector<LLVMValueRef> refs;
  refs.reserve(vals.size());
  for (const auto &v : vals) {
    v.check_valid();
    refs.push_back(v.m_ref);
  }
  return LLVMValueWrapper(
      LLVMConstArray2(elem_ty.m_ref, refs.data(), refs.size()),
      elem_ty.m_context_token);
}

LLVMValueWrapper const_struct(const std::vector<LLVMValueWrapper> &vals,
                              bool packed, LLVMContextWrapper *ctx) {
  ctx->check_valid();
  std::vector<LLVMValueRef> refs;
  refs.reserve(vals.size());
  for (const auto &v : vals) {
    v.check_valid();
    refs.push_back(v.m_ref);
  }
  return LLVMValueWrapper(
      LLVMConstStructInContext(ctx->m_ref, refs.data(),
                               static_cast<unsigned>(refs.size()), packed),
      ctx->m_token);
}

LLVMValueWrapper const_vector(const std::vector<LLVMValueWrapper> &vals) {
  if (vals.empty())
    throw LLVMInvalidOperationError("Cannot create empty vector constant");
  vals[0].check_valid();
  std::vector<LLVMValueRef> refs;
  refs.reserve(vals.size());
  for (const auto &v : vals) {
    v.check_valid();
    refs.push_back(v.m_ref);
  }
  return LLVMValueWrapper(
      LLVMConstVector(refs.data(), static_cast<unsigned>(refs.size())),
      vals[0].m_context_token);
}

LLVMValueWrapper const_string(LLVMContextWrapper *ctx, const std::string &str,
                              bool dont_null_terminate = false) {
  ctx->check_valid();
  return LLVMValueWrapper(
      LLVMConstStringInContext2(ctx->m_ref, str.c_str(), str.size(),
                                dont_null_terminate),
      ctx->m_token);
}

LLVMValueWrapper const_pointer_null(const LLVMTypeWrapper &ty) {
  ty.check_valid();
  return LLVMValueWrapper(LLVMConstPointerNull(ty.m_ref), ty.m_context_token);
}

LLVMValueWrapper const_named_struct(const LLVMTypeWrapper &struct_ty,
                                    const std::vector<LLVMValueWrapper> &vals) {
  struct_ty.check_valid();
  std::vector<LLVMValueRef> refs;
  refs.reserve(vals.size());
  for (const auto &v : vals) {
    v.check_valid();
    refs.push_back(v.m_ref);
  }
  return LLVMValueWrapper(
      LLVMConstNamedStruct(struct_ty.m_ref, refs.data(),
                           static_cast<unsigned>(refs.size())),
      struct_ty.m_context_token);
}

// Value inspection helpers
bool value_is_null(const LLVMValueWrapper &val) {
  val.check_valid();
  return LLVMIsNull(val.m_ref);
}

unsigned long long const_int_get_zext_value(const LLVMValueWrapper &val) {
  val.check_valid();
  return LLVMConstIntGetZExtValue(val.m_ref);
}

long long const_int_get_sext_value(const LLVMValueWrapper &val) {
  val.check_valid();
  return LLVMConstIntGetSExtValue(val.m_ref);
}

LLVMValueWrapper const_int_of_arbitrary_precision(
    const LLVMTypeWrapper &ty, const std::vector<uint64_t> &words) {
  ty.check_valid();
  return LLVMValueWrapper(
      LLVMConstIntOfArbitraryPrecision(ty.m_ref,
                                       static_cast<unsigned>(words.size()),
                                       words.data()),
      ty.m_context_token);
}

// Constant creation functions for echo command
LLVMValueWrapper const_data_array(const LLVMTypeWrapper &elem_ty,
                                   const std::string &data) {
  elem_ty.check_valid();
  return LLVMValueWrapper(
      LLVMConstDataArray(elem_ty.m_ref, data.c_str(), data.size()),
      elem_ty.m_context_token);
}

LLVMValueWrapper const_bitcast(const LLVMValueWrapper &val,
                                const LLVMTypeWrapper &ty) {
  val.check_valid();
  ty.check_valid();
  return LLVMValueWrapper(LLVMConstBitCast(val.m_ref, ty.m_ref),
                          val.m_context_token);
}

LLVMValueWrapper const_gep_with_no_wrap_flags(
    const LLVMTypeWrapper &ty, const LLVMValueWrapper &ptr,
    const std::vector<LLVMValueWrapper> &indices, unsigned no_wrap_flags) {
  ty.check_valid();
  ptr.check_valid();
  std::vector<LLVMValueRef> idx_refs;
  idx_refs.reserve(indices.size());
  for (const auto &idx : indices) {
    idx.check_valid();
    idx_refs.push_back(idx.m_ref);
  }
  return LLVMValueWrapper(
      LLVMConstGEPWithNoWrapFlags(ty.m_ref, ptr.m_ref, idx_refs.data(),
                                  static_cast<unsigned>(idx_refs.size()),
                                  no_wrap_flags),
      ptr.m_context_token);
}

LLVMValueWrapper const_ptr_auth(const LLVMValueWrapper &ptr,
                                 const LLVMValueWrapper &key,
                                 const LLVMValueWrapper &discriminator,
                                 const LLVMValueWrapper &addr_discriminator) {
  ptr.check_valid();
  key.check_valid();
  discriminator.check_valid();
  addr_discriminator.check_valid();
  return LLVMValueWrapper(
      LLVMConstantPtrAuth(ptr.m_ref, key.m_ref, discriminator.m_ref,
                          addr_discriminator.m_ref),
      ptr.m_context_token);
}

// Intrinsic support functions
bool intrinsic_is_overloaded(unsigned id) {
  return LLVMIntrinsicIsOverloaded(id);
}

LLVMValueWrapper get_intrinsic_declaration(
    LLVMModuleWrapper *mod, unsigned id,
    const std::vector<LLVMTypeWrapper> &param_types) {
  mod->check_valid();
  std::vector<LLVMTypeRef> type_refs;
  type_refs.reserve(param_types.size());
  for (const auto &ty : param_types) {
    ty.check_valid();
    type_refs.push_back(ty.m_ref);
  }
  return LLVMValueWrapper(
      LLVMGetIntrinsicDeclaration(mod->m_ref, id, type_refs.data(),
                                  type_refs.size()),
      mod->m_context_token);
}

// Helper for PHI nodes
void phi_add_incoming(LLVMValueWrapper &phi, const LLVMValueWrapper &val,
                      const LLVMBasicBlockWrapper &bb) {
  phi.check_valid();
  val.check_valid();
  bb.check_valid();
  LLVMValueRef vals[] = {val.m_ref};
  LLVMBasicBlockRef bbs[] = {bb.m_ref};
  LLVMAddIncoming(phi.m_ref, vals, bbs, 1);
}

// Helper for switch
void switch_add_case(LLVMValueWrapper &switch_inst, const LLVMValueWrapper &val,
                     const LLVMBasicBlockWrapper &bb) {
  switch_inst.check_valid();
  val.check_valid();
  bb.check_valid();
  LLVMAddCase(switch_inst.m_ref, val.m_ref, bb.m_ref);
}

// Helper for globals
void global_set_initializer(LLVMValueWrapper &global,
                            const LLVMValueWrapper &init) {
  global.check_valid();
  init.check_valid();
  LLVMSetInitializer(global.m_ref, init.m_ref);
}

void global_set_constant(LLVMValueWrapper &global, bool is_const) {
  global.check_valid();
  LLVMSetGlobalConstant(global.m_ref, is_const);
}

void global_set_linkage(LLVMValueWrapper &global, LLVMLinkage linkage) {
  global.check_valid();
  LLVMSetLinkage(global.m_ref, linkage);
}

void global_set_alignment(LLVMValueWrapper &global, unsigned align) {
  global.check_valid();
  LLVMSetAlignment(global.m_ref, align);
}

unsigned global_get_alignment(const LLVMValueWrapper &global) {
  global.check_valid();
  return LLVMGetAlignment(global.m_ref);
}

bool global_is_constant(const LLVMValueWrapper &global) {
  global.check_valid();
  return LLVMIsGlobalConstant(global.m_ref);
}

LLVMLinkage global_get_linkage(const LLVMValueWrapper &global) {
  global.check_valid();
  return LLVMGetLinkage(global.m_ref);
}

void global_set_visibility(LLVMValueWrapper &global, LLVMVisibility vis) {
  global.check_valid();
  LLVMSetVisibility(global.m_ref, vis);
}

LLVMVisibility global_get_visibility(const LLVMValueWrapper &global) {
  global.check_valid();
  return LLVMGetVisibility(global.m_ref);
}

void global_set_section(LLVMValueWrapper &global, const std::string &section) {
  global.check_valid();
  LLVMSetSection(global.m_ref, section.c_str());
}

std::string global_get_section(const LLVMValueWrapper &global) {
  global.check_valid();
  const char *section = LLVMGetSection(global.m_ref);
  return section ? std::string(section) : "";
}

void global_set_thread_local(LLVMValueWrapper &global, bool is_tls) {
  global.check_valid();
  LLVMSetThreadLocal(global.m_ref, is_tls);
}

bool global_is_thread_local(const LLVMValueWrapper &global) {
  global.check_valid();
  return LLVMIsThreadLocal(global.m_ref);
}

void global_set_externally_initialized(LLVMValueWrapper &global, bool is_ext) {
  global.check_valid();
  LLVMSetExternallyInitialized(global.m_ref, is_ext);
}

bool global_is_externally_initialized(const LLVMValueWrapper &global) {
  global.check_valid();
  return LLVMIsExternallyInitialized(global.m_ref);
}

std::optional<LLVMValueWrapper>
global_get_initializer(const LLVMValueWrapper &global) {
  global.check_valid();
  LLVMValueRef init = LLVMGetInitializer(global.m_ref);
  if (!init)
    return std::nullopt;
  return LLVMValueWrapper(init, global.m_context_token);
}

void global_delete(LLVMValueWrapper &global) {
  global.check_valid();
  LLVMDeleteGlobal(global.m_ref);
  global.m_ref = nullptr;
}

// PHI node helpers
unsigned phi_count_incoming(const LLVMValueWrapper &phi) {
  phi.check_valid();
  return LLVMCountIncoming(phi.m_ref);
}

LLVMValueWrapper phi_get_incoming_value(const LLVMValueWrapper &phi,
                                        unsigned index) {
  phi.check_valid();
  return LLVMValueWrapper(LLVMGetIncomingValue(phi.m_ref, index),
                          phi.m_context_token);
}

LLVMBasicBlockWrapper phi_get_incoming_block(const LLVMValueWrapper &phi,
                                             unsigned index) {
  phi.check_valid();
  return LLVMBasicBlockWrapper(LLVMGetIncomingBlock(phi.m_ref, index),
                               phi.m_context_token);
}

// Instruction property helpers
bool instruction_is_conditional(const LLVMValueWrapper &inst) {
  inst.check_valid();
  return LLVMIsConditional(inst.m_ref);
}

LLVMValueWrapper instruction_get_condition(const LLVMValueWrapper &inst) {
  inst.check_valid();
  return LLVMValueWrapper(LLVMGetCondition(inst.m_ref), inst.m_context_token);
}

unsigned instruction_get_num_successors(const LLVMValueWrapper &inst) {
  inst.check_valid();
  return LLVMGetNumSuccessors(inst.m_ref);
}

LLVMBasicBlockWrapper instruction_get_successor(const LLVMValueWrapper &inst,
                                                unsigned index) {
  inst.check_valid();
  return LLVMBasicBlockWrapper(LLVMGetSuccessor(inst.m_ref, index),
                               inst.m_context_token);
}

void instruction_set_volatile(LLVMValueWrapper &inst, bool is_volatile) {
  inst.check_valid();
  LLVMSetVolatile(inst.m_ref, is_volatile);
}

bool instruction_get_volatile(const LLVMValueWrapper &inst) {
  inst.check_valid();
  return LLVMGetVolatile(inst.m_ref);
}

void instruction_set_alignment(LLVMValueWrapper &inst, unsigned align) {
  inst.check_valid();
  LLVMSetAlignment(inst.m_ref, align);
}

unsigned instruction_get_alignment(const LLVMValueWrapper &inst) {
  inst.check_valid();
  return LLVMGetAlignment(inst.m_ref);
}

LLVMIntPredicate instruction_get_icmp_predicate(const LLVMValueWrapper &inst) {
  inst.check_valid();
  return LLVMGetICmpPredicate(inst.m_ref);
}

LLVMRealPredicate instruction_get_fcmp_predicate(const LLVMValueWrapper &inst) {
  inst.check_valid();
  return LLVMGetFCmpPredicate(inst.m_ref);
}

// Type helpers
unsigned type_count_struct_element_types(const LLVMTypeWrapper &ty) {
  ty.check_valid();
  return LLVMCountStructElementTypes(ty.m_ref);
}

// Helper for struct types
void struct_set_body(LLVMTypeWrapper &struct_ty,
                     const std::vector<LLVMTypeWrapper> &elem_types,
                     bool packed) {
  struct_ty.check_valid();
  std::vector<LLVMTypeRef> elems;
  elems.reserve(elem_types.size());
  for (const auto &e : elem_types) {
    e.check_valid();
    elems.push_back(e.m_ref);
  }
  LLVMStructSetBody(struct_ty.m_ref, elems.data(),
                    static_cast<unsigned>(elems.size()), packed);
}

// =============================================================================
// Target Wrapper
// =============================================================================

struct LLVMTargetWrapper {
  LLVMTargetRef m_ref = nullptr;

  LLVMTargetWrapper() = default;
  explicit LLVMTargetWrapper(LLVMTargetRef ref) : m_ref(ref) {}

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Target is null");
  }

  std::string get_name() const {
    check_valid();
    const char *name = LLVMGetTargetName(m_ref);
    return name ? std::string(name) : std::string();
  }

  std::string get_description() const {
    check_valid();
    const char *desc = LLVMGetTargetDescription(m_ref);
    return desc ? std::string(desc) : std::string();
  }

  bool has_jit() const {
    check_valid();
    return LLVMTargetHasJIT(m_ref);
  }

  bool has_target_machine() const {
    check_valid();
    return LLVMTargetHasTargetMachine(m_ref);
  }

  bool has_asm_backend() const {
    check_valid();
    return LLVMTargetHasAsmBackend(m_ref);
  }

  std::optional<LLVMTargetWrapper> next() const {
    check_valid();
    LLVMTargetRef next_ref = LLVMGetNextTarget(m_ref);
    if (next_ref)
      return LLVMTargetWrapper(next_ref);
    return std::nullopt;
  }
};

// Target initialization functions
void initialize_all_target_infos() { LLVMInitializeAllTargetInfos(); }

void initialize_all_targets() { LLVMInitializeAllTargets(); }

void initialize_all_target_mcs() { LLVMInitializeAllTargetMCs(); }

void initialize_all_asm_printers() { LLVMInitializeAllAsmPrinters(); }

void initialize_all_asm_parsers() { LLVMInitializeAllAsmParsers(); }

void initialize_all_disassemblers() { LLVMInitializeAllDisassemblers(); }

std::optional<LLVMTargetWrapper> get_first_target() {
  LLVMTargetRef ref = LLVMGetFirstTarget();
  if (ref)
    return LLVMTargetWrapper(ref);
  return std::nullopt;
}

// =============================================================================
// Memory Buffer Wrapper
// =============================================================================

struct LLVMMemoryBufferWrapper : NoMoveCopy {
  LLVMMemoryBufferRef m_ref = nullptr;

  LLVMMemoryBufferWrapper() = default;
  explicit LLVMMemoryBufferWrapper(LLVMMemoryBufferRef ref) : m_ref(ref) {}

  ~LLVMMemoryBufferWrapper() {
    if (m_ref) {
      LLVMDisposeMemoryBuffer(m_ref);
      m_ref = nullptr;
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("MemoryBuffer is null");
  }

  const char *get_buffer_start() const {
    check_valid();
    return LLVMGetBufferStart(m_ref);
  }

  size_t get_buffer_size() const {
    check_valid();
    return LLVMGetBufferSize(m_ref);
  }
};

// Memory buffer creation functions
LLVMMemoryBufferWrapper *create_memory_buffer_with_stdin() {
  LLVMMemoryBufferRef buf;
  char *error_msg = nullptr;
  
  if (LLVMCreateMemoryBufferWithSTDIN(&buf, &error_msg)) {
    std::string err = error_msg ? error_msg : "Unknown error reading stdin";
    if (error_msg)
      LLVMDisposeMessage(error_msg);
    throw LLVMException(err);
  }
  
  return new LLVMMemoryBufferWrapper(buf);
}

// =============================================================================
// Disassembler Wrapper
// =============================================================================

struct LLVMDisasmContextWrapper : NoMoveCopy {
  LLVMDisasmContextRef m_ref = nullptr;

  LLVMDisasmContextWrapper() = default;
  explicit LLVMDisasmContextWrapper(LLVMDisasmContextRef ref) : m_ref(ref) {}

  ~LLVMDisasmContextWrapper() {
    if (m_ref) {
      LLVMDisasmDispose(m_ref);
      m_ref = nullptr;
    }
  }

  bool is_valid() const { return m_ref != nullptr; }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("DisasmContext is null or invalid");
  }

  // Disassemble a single instruction
  // Returns (bytes_consumed, disassembly_string)
  std::pair<size_t, std::string> disasm_instruction(const std::vector<uint8_t> &bytes,
                                                     size_t offset, uint64_t pc) {
    check_valid();
    if (offset >= bytes.size())
      return {0, ""};

    char outline[1024];
    size_t consumed = LLVMDisasmInstruction(
        m_ref, const_cast<uint8_t *>(bytes.data() + offset),
        bytes.size() - offset, pc, outline, sizeof(outline));

    return {consumed, std::string(outline)};
  }
};

// Create disassembler with triple, cpu, and features
LLVMDisasmContextWrapper *create_disasm_cpu_features(const std::string &triple,
                                                      const std::string &cpu,
                                                      const std::string &features) {
  LLVMDisasmContextRef ref = LLVMCreateDisasmCPUFeatures(
      triple.c_str(), cpu.c_str(), features.c_str(), nullptr, 0, nullptr, nullptr);

  return new LLVMDisasmContextWrapper(ref);
}

// =============================================================================
// Object File Wrappers
// =============================================================================

// Forward declarations
struct LLVMBinaryWrapper;
struct LLVMSectionIteratorWrapper;
struct LLVMSymbolIteratorWrapper;

struct LLVMBinaryWrapper : NoMoveCopy {
  LLVMBinaryRef m_ref = nullptr;

  LLVMBinaryWrapper() = default;
  explicit LLVMBinaryWrapper(LLVMBinaryRef ref) : m_ref(ref) {}

  ~LLVMBinaryWrapper() {
    if (m_ref) {
      LLVMDisposeBinary(m_ref);
      m_ref = nullptr;
    }
  }

  bool is_valid() const { return m_ref != nullptr; }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Binary is null or invalid");
  }
};

struct LLVMSectionIteratorWrapper : NoMoveCopy {
  LLVMSectionIteratorRef m_ref = nullptr;
  LLVMBinaryWrapper *m_binary = nullptr; // Non-owning reference

  LLVMSectionIteratorWrapper() = default;
  LLVMSectionIteratorWrapper(LLVMSectionIteratorRef ref, LLVMBinaryWrapper *binary)
      : m_ref(ref), m_binary(binary) {}

  ~LLVMSectionIteratorWrapper() {
    if (m_ref) {
      LLVMDisposeSectionIterator(m_ref);
      m_ref = nullptr;
    }
  }

  bool is_valid() const { return m_ref != nullptr; }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("SectionIterator is null or invalid");
    if (!m_binary || !m_binary->is_valid())
      throw LLVMUseAfterFreeError("Binary associated with iterator is invalid");
  }

  bool is_at_end() const {
    check_valid();
    return LLVMObjectFileIsSectionIteratorAtEnd(m_binary->m_ref, m_ref);
  }

  void move_next() {
    check_valid();
    LLVMMoveToNextSection(m_ref);
  }

  std::string get_name() const {
    check_valid();
    const char *name = LLVMGetSectionName(m_ref);
    return name ? std::string(name) : std::string();
  }

  uint64_t get_address() const {
    check_valid();
    return LLVMGetSectionAddress(m_ref);
  }

  uint64_t get_size() const {
    check_valid();
    return LLVMGetSectionSize(m_ref);
  }
};

struct LLVMSymbolIteratorWrapper : NoMoveCopy {
  LLVMSymbolIteratorRef m_ref = nullptr;
  LLVMBinaryWrapper *m_binary = nullptr; // Non-owning reference

  LLVMSymbolIteratorWrapper() = default;
  LLVMSymbolIteratorWrapper(LLVMSymbolIteratorRef ref, LLVMBinaryWrapper *binary)
      : m_ref(ref), m_binary(binary) {}

  ~LLVMSymbolIteratorWrapper() {
    if (m_ref) {
      LLVMDisposeSymbolIterator(m_ref);
      m_ref = nullptr;
    }
  }

  bool is_valid() const { return m_ref != nullptr; }

  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("SymbolIterator is null or invalid");
    if (!m_binary || !m_binary->is_valid())
      throw LLVMUseAfterFreeError("Binary associated with iterator is invalid");
  }

  bool is_at_end() const {
    check_valid();
    return LLVMObjectFileIsSymbolIteratorAtEnd(m_binary->m_ref, m_ref);
  }

  void move_next() {
    check_valid();
    LLVMMoveToNextSymbol(m_ref);
  }

  std::string get_name() const {
    check_valid();
    const char *name = LLVMGetSymbolName(m_ref);
    return name ? std::string(name) : std::string();
  }

  uint64_t get_address() const {
    check_valid();
    return LLVMGetSymbolAddress(m_ref);
  }

  uint64_t get_size() const {
    check_valid();
    return LLVMGetSymbolSize(m_ref);
  }
};

// Create binary from memory buffer
// Returns (binary_wrapper, error_message)
// If binary_wrapper is null, error_message contains the error
std::pair<LLVMBinaryWrapper *, std::string>
create_binary(LLVMMemoryBufferWrapper &membuf) {
  membuf.check_valid();

  char *error_msg = nullptr;
  LLVMBinaryRef ref = LLVMCreateBinary(membuf.m_ref, LLVMGetGlobalContext(), &error_msg);

  if (!ref || error_msg) {
    std::string err = error_msg ? std::string(error_msg) : "Unknown error creating binary";
    if (error_msg)
      LLVMDisposeMessage(error_msg);
    return {nullptr, err};
  }

  return {new LLVMBinaryWrapper(ref), ""};
}

// Create section iterator from binary
LLVMSectionIteratorWrapper *copy_section_iterator(LLVMBinaryWrapper &binary) {
  binary.check_valid();
  LLVMSectionIteratorRef ref = LLVMObjectFileCopySectionIterator(binary.m_ref);
  return new LLVMSectionIteratorWrapper(ref, &binary);
}

// Create symbol iterator from binary
LLVMSymbolIteratorWrapper *copy_symbol_iterator(LLVMBinaryWrapper &binary) {
  binary.check_valid();
  LLVMSymbolIteratorRef ref = LLVMObjectFileCopySymbolIterator(binary.m_ref);
  return new LLVMSymbolIteratorWrapper(ref, &binary);
}

// Move section iterator to containing section of symbol
void move_to_containing_section(LLVMSectionIteratorWrapper &sect,
                                 LLVMSymbolIteratorWrapper &sym) {
  sect.check_valid();
  sym.check_valid();
  LLVMMoveToContainingSection(sect.m_ref, sym.m_ref);
}

// =============================================================================
// BitReader Functions
// =============================================================================

// Parse bitcode (legacy API with error message)
LLVMModuleWrapper *parse_bitcode_in_context(LLVMContextWrapper &ctx,
                                             LLVMMemoryBufferWrapper &membuf,
                                             bool lazy, bool new_api) {
  membuf.check_valid();
  ctx.check_valid();
  
  LLVMModuleRef mod;
  char *error_msg = nullptr;
  LLVMBool failed;
  
  if (new_api) {
    // New API - uses diagnostic handler instead of error message
    if (lazy) {
      failed = LLVMGetBitcodeModuleInContext2(ctx.m_ref, membuf.m_ref, &mod);
    } else {
      failed = LLVMParseBitcodeInContext2(ctx.m_ref, membuf.m_ref, &mod);
    }
    
    if (failed) {
      throw LLVMException("Failed to parse bitcode (new API)");
    }
  } else {
    // Legacy API with error message
    if (lazy) {
      failed = LLVMGetBitcodeModuleInContext(ctx.m_ref, membuf.m_ref, &mod,
                                             &error_msg);
    } else {
      failed = LLVMParseBitcodeInContext(ctx.m_ref, membuf.m_ref, &mod,
                                         &error_msg);
    }
    
    if (failed) {
      std::string err = error_msg ? error_msg : "Unknown error parsing bitcode";
      if (error_msg)
        LLVMDisposeMessage(error_msg);
      throw LLVMException(err);
    }
  }
  
  return new LLVMModuleWrapper(mod, ctx.m_ref, ctx.m_token);
}

// =============================================================================
// DIBuilder Wrapper
// =============================================================================

struct LLVMDIBuilderWrapper : NoMoveCopy {
  LLVMDIBuilderRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_module_token;
  
  LLVMDIBuilderWrapper(LLVMModuleRef mod, std::shared_ptr<ValidityToken> module_token)
      : m_module_token(std::move(module_token)) {
    m_ref = LLVMCreateDIBuilder(mod);
  }
  
  ~LLVMDIBuilderWrapper() {
    if (m_ref) {
      LLVMDisposeDIBuilder(m_ref);
      m_ref = nullptr;
    }
  }
  
  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("DIBuilder is null");
    if (!m_module_token || !m_module_token->is_valid())
      throw LLVMUseAfterFreeError("DIBuilder used after module was destroyed");
  }
  
  void finalize() {
    check_valid();
    LLVMDIBuilderFinalize(m_ref);
  }
};

// =============================================================================
// Metadata Wrapper
// =============================================================================

struct LLVMMetadataWrapper {
  LLVMMetadataRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;
  
  LLVMMetadataWrapper() = default;
  LLVMMetadataWrapper(LLVMMetadataRef ref, std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}
  
  void check_valid() const {
    if (!m_ref)
      throw LLVMUseAfterFreeError("Metadata is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMUseAfterFreeError("Metadata used after context was destroyed");
  }
};

// Implementation of get_metadata for ValueMetadataEntriesWrapper
inline LLVMMetadataWrapper
LLVMValueMetadataEntriesWrapper_get_metadata(
    const LLVMValueMetadataEntriesWrapper &entries, unsigned index) {
  entries.check_valid();
  if (index >= entries.m_count)
    throw std::out_of_range("Index out of range");
  return LLVMMetadataWrapper(
      LLVMValueMetadataEntriesGetMetadata(entries.m_entries, index),
      entries.m_context_token);
}

// =============================================================================
// Diagnostic Handler Support (Thread-Local Storage)
// =============================================================================

// Thread-local storage for diagnostic info
struct DiagnosticInfo {
  std::string description;
  LLVMDiagnosticSeverity severity = LLVMDSError;
  bool was_called = false;
};

static thread_local DiagnosticInfo g_diagnostic_info;

// Global diagnostic handler callback
static void diagnostic_handler_callback(LLVMDiagnosticInfoRef di, void *context) {
  g_diagnostic_info.was_called = true;
  
  // Get severity
  g_diagnostic_info.severity = LLVMGetDiagInfoSeverity(di);
  
  // Get description
  char *desc = LLVMGetDiagInfoDescription(di);
  if (desc) {
    g_diagnostic_info.description = std::string(desc);
    LLVMDisposeMessage(desc);
  }
}

// =============================================================================
// Module Registration
// =============================================================================

NB_MODULE(llvm, m) {
  // Register exceptions
  nb::exception<LLVMException>(m, "LLVMError");
  nb::exception<LLVMUseAfterFreeError>(m, "LLVMUseAfterFreeError");
  nb::exception<LLVMInvalidOperationError>(m, "LLVMInvalidOperationError");
  nb::exception<LLVMVerificationError>(m, "LLVMVerificationError");

  // Enums
  nb::enum_<LLVMLinkage>(m, "Linkage")
      .value("External", LLVMExternalLinkage)
      .value("AvailableExternally", LLVMAvailableExternallyLinkage)
      .value("LinkOnceAny", LLVMLinkOnceAnyLinkage)
      .value("LinkOnceODR", LLVMLinkOnceODRLinkage)
      .value("WeakAny", LLVMWeakAnyLinkage)
      .value("WeakODR", LLVMWeakODRLinkage)
      .value("Appending", LLVMAppendingLinkage)
      .value("Internal", LLVMInternalLinkage)
      .value("Private", LLVMPrivateLinkage)
      .value("ExternalWeak", LLVMExternalWeakLinkage)
      .value("Common", LLVMCommonLinkage)
      .export_values();

  nb::enum_<LLVMVisibility>(m, "Visibility")
      .value("Default", LLVMDefaultVisibility)
      .value("Hidden", LLVMHiddenVisibility)
      .value("Protected", LLVMProtectedVisibility)
      .export_values();

  nb::enum_<LLVMUnnamedAddr>(m, "UnnamedAddr")
      .value("No", LLVMNoUnnamedAddr)
      .value("Local", LLVMLocalUnnamedAddr)
      .value("Global", LLVMGlobalUnnamedAddr)
      .export_values();

  nb::enum_<LLVMCallConv>(m, "CallConv")
      .value("C", LLVMCCallConv)
      .value("Fast", LLVMFastCallConv)
      .value("Cold", LLVMColdCallConv)
      .value("X86Stdcall", LLVMX86StdcallCallConv)
      .value("X86Fastcall", LLVMX86FastcallCallConv)
      .export_values();

  nb::enum_<LLVMIntPredicate>(m, "IntPredicate")
      .value("EQ", LLVMIntEQ)
      .value("NE", LLVMIntNE)
      .value("UGT", LLVMIntUGT)
      .value("UGE", LLVMIntUGE)
      .value("ULT", LLVMIntULT)
      .value("ULE", LLVMIntULE)
      .value("SGT", LLVMIntSGT)
      .value("SGE", LLVMIntSGE)
      .value("SLT", LLVMIntSLT)
      .value("SLE", LLVMIntSLE)
      .export_values();

  nb::enum_<LLVMRealPredicate>(m, "RealPredicate")
      .value("PredicateFalse", LLVMRealPredicateFalse)
      .value("OEQ", LLVMRealOEQ)
      .value("OGT", LLVMRealOGT)
      .value("OGE", LLVMRealOGE)
      .value("OLT", LLVMRealOLT)
      .value("OLE", LLVMRealOLE)
      .value("ONE", LLVMRealONE)
      .value("ORD", LLVMRealORD)
      .value("UNO", LLVMRealUNO)
      .value("UEQ", LLVMRealUEQ)
      .value("UGT", LLVMRealUGT)
      .value("UGE", LLVMRealUGE)
      .value("ULT", LLVMRealULT)
      .value("ULE", LLVMRealULE)
      .value("UNE", LLVMRealUNE)
      .value("PredicateTrue", LLVMRealPredicateTrue)
      .export_values();

  nb::enum_<LLVMAtomicOrdering>(m, "AtomicOrdering")
      .value("NotAtomic", LLVMAtomicOrderingNotAtomic)
      .value("Unordered", LLVMAtomicOrderingUnordered)
      .value("Monotonic", LLVMAtomicOrderingMonotonic)
      .value("Acquire", LLVMAtomicOrderingAcquire)
      .value("Release", LLVMAtomicOrderingRelease)
      .value("AcquireRelease", LLVMAtomicOrderingAcquireRelease)
      .value("SequentiallyConsistent", LLVMAtomicOrderingSequentiallyConsistent)
      .export_values();

  // AtomicRMW binary operations
  nb::enum_<LLVMAtomicRMWBinOp>(m, "AtomicRMWBinOp")
      .value("Xchg", LLVMAtomicRMWBinOpXchg)
      .value("Add", LLVMAtomicRMWBinOpAdd)
      .value("Sub", LLVMAtomicRMWBinOpSub)
      .value("And", LLVMAtomicRMWBinOpAnd)
      .value("Nand", LLVMAtomicRMWBinOpNand)
      .value("Or", LLVMAtomicRMWBinOpOr)
      .value("Xor", LLVMAtomicRMWBinOpXor)
      .value("Max", LLVMAtomicRMWBinOpMax)
      .value("Min", LLVMAtomicRMWBinOpMin)
      .value("UMax", LLVMAtomicRMWBinOpUMax)
      .value("UMin", LLVMAtomicRMWBinOpUMin)
      .value("FAdd", LLVMAtomicRMWBinOpFAdd)
      .value("FSub", LLVMAtomicRMWBinOpFSub)
      .value("FMax", LLVMAtomicRMWBinOpFMax)
      .value("FMin", LLVMAtomicRMWBinOpFMin)
      .value("UIncWrap", LLVMAtomicRMWBinOpUIncWrap)
      .value("UDecWrap", LLVMAtomicRMWBinOpUDecWrap)
      .value("USubCond", LLVMAtomicRMWBinOpUSubCond)
      .value("USubSat", LLVMAtomicRMWBinOpUSubSat)
      .value("FMaximum", LLVMAtomicRMWBinOpFMaximum)
      .value("FMinimum", LLVMAtomicRMWBinOpFMinimum)
      .export_values();

  // Tail call kind
  nb::enum_<LLVMTailCallKind>(m, "TailCallKind")
      .value("None", LLVMTailCallKindNone)
      .value("Tail", LLVMTailCallKindTail)
      .value("MustTail", LLVMTailCallKindMustTail)
      .value("NoTail", LLVMTailCallKindNoTail)
      .export_values();

  // Inline assembly dialect
  nb::enum_<LLVMInlineAsmDialect>(m, "InlineAsmDialect")
      .value("ATT", LLVMInlineAsmDialectATT)
      .value("Intel", LLVMInlineAsmDialectIntel)
      .export_values();

  nb::enum_<LLVMTypeKind>(m, "TypeKind")
      .value("Void", LLVMVoidTypeKind)
      .value("Half", LLVMHalfTypeKind)
      .value("BFloat", LLVMBFloatTypeKind)
      .value("Float", LLVMFloatTypeKind)
      .value("Double", LLVMDoubleTypeKind)
      .value("X86_FP80", LLVMX86_FP80TypeKind)
      .value("FP128", LLVMFP128TypeKind)
      .value("PPC_FP128", LLVMPPC_FP128TypeKind)
      .value("Label", LLVMLabelTypeKind)
      .value("Integer", LLVMIntegerTypeKind)
      .value("Function", LLVMFunctionTypeKind)
      .value("Struct", LLVMStructTypeKind)
      .value("Array", LLVMArrayTypeKind)
      .value("Pointer", LLVMPointerTypeKind)
      .value("Vector", LLVMVectorTypeKind)
      .value("Metadata", LLVMMetadataTypeKind)
      .value("X86_AMX", LLVMX86_AMXTypeKind)
      .value("Token", LLVMTokenTypeKind)
      .value("ScalableVector", LLVMScalableVectorTypeKind)
      .value("TargetExt", LLVMTargetExtTypeKind)
      .export_values();

  nb::enum_<LLVMOpcode>(m, "Opcode")
      .value("Ret", LLVMRet)
      .value("Br", LLVMBr)
      .value("Switch", LLVMSwitch)
      .value("IndirectBr", LLVMIndirectBr)
      .value("Invoke", LLVMInvoke)
      .value("Unreachable", LLVMUnreachable)
      .value("CallBr", LLVMCallBr)
      .value("FNeg", LLVMFNeg)
      .value("Add", LLVMAdd)
      .value("FAdd", LLVMFAdd)
      .value("Sub", LLVMSub)
      .value("FSub", LLVMFSub)
      .value("Mul", LLVMMul)
      .value("FMul", LLVMFMul)
      .value("UDiv", LLVMUDiv)
      .value("SDiv", LLVMSDiv)
      .value("FDiv", LLVMFDiv)
      .value("URem", LLVMURem)
      .value("SRem", LLVMSRem)
      .value("FRem", LLVMFRem)
      .value("Shl", LLVMShl)
      .value("LShr", LLVMLShr)
      .value("AShr", LLVMAShr)
      .value("And", LLVMAnd)
      .value("Or", LLVMOr)
      .value("Xor", LLVMXor)
      .value("Alloca", LLVMAlloca)
      .value("Load", LLVMLoad)
      .value("Store", LLVMStore)
      .value("GetElementPtr", LLVMGetElementPtr)
      .value("Trunc", LLVMTrunc)
      .value("ZExt", LLVMZExt)
      .value("SExt", LLVMSExt)
      .value("FPToUI", LLVMFPToUI)
      .value("FPToSI", LLVMFPToSI)
      .value("UIToFP", LLVMUIToFP)
      .value("SIToFP", LLVMSIToFP)
      .value("FPTrunc", LLVMFPTrunc)
      .value("FPExt", LLVMFPExt)
      .value("PtrToInt", LLVMPtrToInt)
      .value("IntToPtr", LLVMIntToPtr)
      .value("BitCast", LLVMBitCast)
      .value("AddrSpaceCast", LLVMAddrSpaceCast)
      .value("ICmp", LLVMICmp)
      .value("FCmp", LLVMFCmp)
      .value("PHI", LLVMPHI)
      .value("Call", LLVMCall)
      .value("Select", LLVMSelect)
      .value("UserOp1", LLVMUserOp1)
      .value("UserOp2", LLVMUserOp2)
      .value("VAArg", LLVMVAArg)
      .value("ExtractElement", LLVMExtractElement)
      .value("InsertElement", LLVMInsertElement)
      .value("ShuffleVector", LLVMShuffleVector)
      .value("ExtractValue", LLVMExtractValue)
      .value("InsertValue", LLVMInsertValue)
      .value("Freeze", LLVMFreeze)
      .value("Fence", LLVMFence)
      .value("AtomicCmpXchg", LLVMAtomicCmpXchg)
      .value("AtomicRMW", LLVMAtomicRMW)
      .value("Resume", LLVMResume)
      .value("LandingPad", LLVMLandingPad)
      .value("CleanupRet", LLVMCleanupRet)
      .value("CatchRet", LLVMCatchRet)
      .value("CatchPad", LLVMCatchPad)
      .value("CleanupPad", LLVMCleanupPad)
      .value("CatchSwitch", LLVMCatchSwitch)
      .export_values();

  nb::enum_<LLVMValueKind>(m, "ValueKind")
      .value("Argument", LLVMArgumentValueKind)
      .value("BasicBlock", LLVMBasicBlockValueKind)
      .value("MemoryUse", LLVMMemoryUseValueKind)
      .value("MemoryDef", LLVMMemoryDefValueKind)
      .value("MemoryPhi", LLVMMemoryPhiValueKind)
      .value("Function", LLVMFunctionValueKind)
      .value("GlobalAlias", LLVMGlobalAliasValueKind)
      .value("GlobalIFunc", LLVMGlobalIFuncValueKind)
      .value("GlobalVariable", LLVMGlobalVariableValueKind)
      .value("BlockAddress", LLVMBlockAddressValueKind)
      .value("ConstantExpr", LLVMConstantExprValueKind)
      .value("ConstantArray", LLVMConstantArrayValueKind)
      .value("ConstantStruct", LLVMConstantStructValueKind)
      .value("ConstantVector", LLVMConstantVectorValueKind)
      .value("UndefValue", LLVMUndefValueValueKind)
      .value("ConstantAggregateZero", LLVMConstantAggregateZeroValueKind)
      .value("ConstantDataArray", LLVMConstantDataArrayValueKind)
      .value("ConstantDataVector", LLVMConstantDataVectorValueKind)
      .value("ConstantInt", LLVMConstantIntValueKind)
      .value("ConstantFP", LLVMConstantFPValueKind)
      .value("ConstantPointerNull", LLVMConstantPointerNullValueKind)
      .value("ConstantTokenNone", LLVMConstantTokenNoneValueKind)
      .value("MetadataAsValue", LLVMMetadataAsValueValueKind)
      .value("InlineAsm", LLVMInlineAsmValueKind)
      .value("Instruction", LLVMInstructionValueKind)
      .value("PoisonValue", LLVMPoisonValueValueKind)
      .value("ConstantTargetNone", LLVMConstantTargetNoneValueKind)
      .value("ConstantPtrAuth", LLVMConstantPtrAuthValueKind)
      .export_values();

  // Type wrapper
  nb::class_<LLVMTypeWrapper>(m, "Type")
      .def("__eq__",
           [](const LLVMTypeWrapper &a, const LLVMTypeWrapper &b) {
             return a == b;
           })
      .def("__ne__",
           [](const LLVMTypeWrapper &a, const LLVMTypeWrapper &b) {
             return a != b;
           })
      .def("__hash__",
           [](const LLVMTypeWrapper &v) {
             return std::hash<LLVMTypeRef>{}(v.m_ref);
           })
      .def_prop_ro("kind", &LLVMTypeWrapper::kind)
      .def("__str__", &LLVMTypeWrapper::to_string)
      .def("__repr__", &LLVMTypeWrapper::to_string)
      .def_prop_ro("is_void", &LLVMTypeWrapper::is_void)
      .def_prop_ro("is_integer", &LLVMTypeWrapper::is_integer)
      .def_prop_ro("is_float", &LLVMTypeWrapper::is_float)
      .def_prop_ro("is_pointer", &LLVMTypeWrapper::is_pointer)
      .def_prop_ro("is_function", &LLVMTypeWrapper::is_function)
      .def_prop_ro("is_struct", &LLVMTypeWrapper::is_struct)
      .def_prop_ro("is_array", &LLVMTypeWrapper::is_array)
      .def_prop_ro("is_vector", &LLVMTypeWrapper::is_vector)
      .def_prop_ro("int_width", &LLVMTypeWrapper::get_int_width)
      .def_prop_ro("is_sized", &LLVMTypeWrapper::is_sized)
      .def_prop_ro("is_packed_struct", &LLVMTypeWrapper::is_packed_struct)
      .def_prop_ro("is_opaque_struct", &LLVMTypeWrapper::is_opaque_struct)
      .def_prop_ro("struct_name", &LLVMTypeWrapper::get_struct_name)
      .def_prop_ro("is_vararg", &LLVMTypeWrapper::is_vararg_function)
      .def("get_struct_element_type", &LLVMTypeWrapper::get_struct_element_type, "index"_a)
      .def_prop_ro("is_opaque_pointer", &LLVMTypeWrapper::is_opaque_pointer)
      .def_prop_ro("element_type", &LLVMTypeWrapper::get_element_type)
      .def_prop_ro("array_length", &LLVMTypeWrapper::get_array_length)
      .def_prop_ro("vector_size", &LLVMTypeWrapper::get_vector_size)
      .def_prop_ro("pointer_address_space", &LLVMTypeWrapper::get_pointer_address_space)
      .def_prop_ro("return_type", &LLVMTypeWrapper::get_return_type)
      .def_prop_ro("param_count", &LLVMTypeWrapper::count_param_types)
      .def_prop_ro("param_types", &LLVMTypeWrapper::get_param_types)
      .def_prop_ro("target_ext_type_name", &LLVMTypeWrapper::get_target_ext_type_name)
      .def_prop_ro("target_ext_type_num_type_params", &LLVMTypeWrapper::get_target_ext_type_num_type_params)
      .def_prop_ro("target_ext_type_num_int_params", &LLVMTypeWrapper::get_target_ext_type_num_int_params)
      .def("get_target_ext_type_type_param", &LLVMTypeWrapper::get_target_ext_type_type_param, "index"_a)
      .def("get_target_ext_type_int_param", &LLVMTypeWrapper::get_target_ext_type_int_param, "index"_a)
      .def("set_body", &struct_set_body, "elem_types"_a, "packed"_a = false)
      .def_prop_ro("struct_element_count", &type_count_struct_element_types);

  // Value wrapper
  nb::class_<LLVMValueWrapper>(m, "Value")
      .def("__eq__",
           [](const LLVMValueWrapper &a, const LLVMValueWrapper &b) {
             return a == b;
           })
      .def("__ne__",
           [](const LLVMValueWrapper &a, const LLVMValueWrapper &b) {
             return a != b;
           })
      .def("__hash__",
           [](const LLVMValueWrapper &v) {
             return std::hash<LLVMValueRef>{}(v.m_ref);
           })
      .def_prop_ro("type", &LLVMValueWrapper::type)
      .def_prop_rw("name", &LLVMValueWrapper::get_name,
                   &LLVMValueWrapper::set_name)
      .def("__str__", &LLVMValueWrapper::to_string)
      .def("__repr__", &LLVMValueWrapper::to_string)
      .def_prop_ro("is_constant", &LLVMValueWrapper::is_constant)
      .def_prop_ro("is_undef", &LLVMValueWrapper::is_undef)
      .def_prop_ro("is_poison", &LLVMValueWrapper::is_poison)
      .def_prop_ro("next_global", &LLVMValueWrapper::next_global)
      .def_prop_ro("prev_global", &LLVMValueWrapper::prev_global)
      .def("add_incoming", &phi_add_incoming, "val"_a, "bb"_a)
      .def("add_case", &switch_add_case, "val"_a, "bb"_a)
      .def("set_initializer", &global_set_initializer, "init"_a)
      .def("get_initializer", &global_get_initializer)
      .def("set_constant", &global_set_constant, "is_const"_a)
      .def("is_global_constant", &global_is_constant)
      .def("set_linkage", &global_set_linkage, "linkage"_a)
      .def("get_linkage", &global_get_linkage)
      .def("set_visibility", &global_set_visibility, "vis"_a)
      .def("get_visibility", &global_get_visibility)
      .def("set_alignment", &global_set_alignment, "align"_a)
      .def("get_alignment", &global_get_alignment)
      .def("set_section", &global_set_section, "section"_a)
      .def("get_section", &global_get_section)
      .def("set_thread_local", &global_set_thread_local, "is_tls"_a)
      .def("is_thread_local", &global_is_thread_local)
      .def("set_externally_initialized", &global_set_externally_initialized,
           "is_ext"_a)
      .def("is_externally_initialized", &global_is_externally_initialized)
      .def("delete_global", &global_delete)
      // PHI helpers
      .def("count_incoming", &phi_count_incoming)
      .def("get_incoming_value", &phi_get_incoming_value, "index"_a)
      .def("get_incoming_block", &phi_get_incoming_block, "index"_a)
      // Branch instruction helpers
      .def("is_conditional", &instruction_is_conditional)
      .def("get_condition", &instruction_get_condition)
      .def("get_num_successors", &instruction_get_num_successors)
      .def("get_successor", &instruction_get_successor, "index"_a)
      // Load/Store helpers
      .def("set_volatile", &instruction_set_volatile, "is_volatile"_a)
      .def("get_volatile", &instruction_get_volatile)
      .def("set_inst_alignment", &instruction_set_alignment, "align"_a)
      .def("get_inst_alignment", &instruction_get_alignment)
      // Comparison helpers
      .def("get_icmp_predicate", &instruction_get_icmp_predicate)
      .def("get_fcmp_predicate", &instruction_get_fcmp_predicate)
      // Instruction iteration
      .def_prop_ro("next_instruction", &LLVMValueWrapper::next_instruction)
      .def_prop_ro("prev_instruction", &LLVMValueWrapper::prev_instruction)
      // Instruction predicates
      .def("is_a_call_inst", &LLVMValueWrapper::is_a_call_inst)
      .def("is_declaration", &LLVMValueWrapper::is_declaration)
      // Operand access
      .def("get_num_operands", &LLVMValueWrapper::get_num_operands)
      .def("get_operand", &LLVMValueWrapper::get_operand, "index"_a)
      // Constant type checking
      .def("is_a_global_value", &LLVMValueWrapper::is_a_global_value)
      .def("is_a_function", &LLVMValueWrapper::is_a_function)
      .def("is_a_global_variable", &LLVMValueWrapper::is_a_global_variable)
      .def("is_a_global_alias", &LLVMValueWrapper::is_a_global_alias)
      .def("is_a_constant_int", &LLVMValueWrapper::is_a_constant_int)
      .def("is_a_constant_fp", &LLVMValueWrapper::is_a_constant_fp)
      .def("is_a_constant_aggregate_zero",
           &LLVMValueWrapper::is_a_constant_aggregate_zero)
      .def("is_a_constant_data_array",
           &LLVMValueWrapper::is_a_constant_data_array)
      .def("is_a_constant_array", &LLVMValueWrapper::is_a_constant_array)
      .def("is_a_constant_struct", &LLVMValueWrapper::is_a_constant_struct)
      .def("is_a_constant_pointer_null",
           &LLVMValueWrapper::is_a_constant_pointer_null)
      .def("is_a_constant_vector", &LLVMValueWrapper::is_a_constant_vector)
      .def("is_a_constant_data_vector",
           &LLVMValueWrapper::is_a_constant_data_vector)
      .def("is_a_constant_expr", &LLVMValueWrapper::is_a_constant_expr)
      .def("is_a_constant_ptr_auth", &LLVMValueWrapper::is_a_constant_ptr_auth)
      .def("is_null", &LLVMValueWrapper::is_null)
      // Intrinsic support
      .def("get_intrinsic_id", &LLVMValueWrapper::get_intrinsic_id)
      // Constant data access
      .def("get_raw_data_values", &LLVMValueWrapper::get_raw_data_values)
      .def("get_aggregate_element", &LLVMValueWrapper::get_aggregate_element,
           "index"_a)
      // Constant expression support
      .def("get_const_opcode", &LLVMValueWrapper::get_const_opcode)
      .def("get_gep_source_element_type",
           &LLVMValueWrapper::get_gep_source_element_type)
      .def("get_num_indices", &LLVMValueWrapper::get_num_indices)
      .def("get_gep_no_wrap_flags", &LLVMValueWrapper::get_gep_no_wrap_flags)
      // Pointer auth constant support
      .def("get_constant_ptr_auth_pointer",
           &LLVMValueWrapper::get_constant_ptr_auth_pointer)
      .def("get_constant_ptr_auth_key",
           &LLVMValueWrapper::get_constant_ptr_auth_key)
      .def("get_constant_ptr_auth_discriminator",
           &LLVMValueWrapper::get_constant_ptr_auth_discriminator)
      .def("get_constant_ptr_auth_addr_discriminator",
           &LLVMValueWrapper::get_constant_ptr_auth_addr_discriminator)
      // Parameter iteration
      .def_prop_ro("next_param", &LLVMValueWrapper::next_param)
      .def_prop_ro("prev_param", &LLVMValueWrapper::prev_param)
      // Global alias iteration
      .def_prop_ro("next_global_alias", &LLVMValueWrapper::next_global_alias)
      .def_prop_ro("prev_global_alias", &LLVMValueWrapper::prev_global_alias)
      .def("alias_get_aliasee", &LLVMValueWrapper::alias_get_aliasee)
      .def("alias_set_aliasee", &LLVMValueWrapper::alias_set_aliasee,
           "aliasee"_a)
      // Global IFunc iteration
      .def_prop_ro("next_global_ifunc", &LLVMValueWrapper::next_global_ifunc)
      .def_prop_ro("prev_global_ifunc", &LLVMValueWrapper::prev_global_ifunc)
      .def("get_global_ifunc_resolver",
           &LLVMValueWrapper::get_global_ifunc_resolver)
      .def("set_global_ifunc_resolver",
           &LLVMValueWrapper::set_global_ifunc_resolver, "resolver"_a)
      // Global properties
      .def("global_get_value_type", &LLVMValueWrapper::global_get_value_type)
      .def("get_unnamed_address", &LLVMValueWrapper::get_unnamed_address)
      .def("set_unnamed_address", &LLVMValueWrapper::set_unnamed_address,
           "unnamed_addr"_a)
      .def("has_personality_fn", &LLVMValueWrapper::has_personality_fn)
      .def("get_personality_fn", &LLVMValueWrapper::get_personality_fn)
      .def("set_personality_fn", &LLVMValueWrapper::set_personality_fn, "fn"_a)
      .def("has_prefix_data", &LLVMValueWrapper::has_prefix_data)
      .def("get_prefix_data", &LLVMValueWrapper::get_prefix_data)
      .def("set_prefix_data", &LLVMValueWrapper::set_prefix_data, "data"_a)
      .def("has_prologue_data", &LLVMValueWrapper::has_prologue_data)
      .def("get_prologue_data", &LLVMValueWrapper::get_prologue_data)
      .def("set_prologue_data", &LLVMValueWrapper::set_prologue_data, "data"_a)
      // Instruction properties
      .def("get_instruction_opcode", &LLVMValueWrapper::get_instruction_opcode)
      // Instruction flags
      .def("get_nsw", &LLVMValueWrapper::get_nsw)
      .def("get_nuw", &LLVMValueWrapper::get_nuw)
      .def("get_exact", &LLVMValueWrapper::get_exact)
      .def("get_nneg", &LLVMValueWrapper::get_nneg)
      // Memory access properties (ordering not duplicated)
      .def("get_ordering", &LLVMValueWrapper::get_ordering)
      // Call/invoke properties
      .def("get_num_arg_operands", &LLVMValueWrapper::get_num_arg_operands)
      // Alloca properties
      .def("get_allocated_type", &LLVMValueWrapper::get_allocated_type)
      .def_prop_ro("value_kind", &LLVMValueWrapper::value_kind)
      // Phase 5.7: Operand bundle support
      .def("get_num_operand_bundles", &LLVMValueWrapper::get_num_operand_bundles)
      // Phase 5.8: Inline assembly support
      .def("is_a_inline_asm", &LLVMValueWrapper::is_a_inline_asm)
      .def("get_inline_asm_asm_string", &LLVMValueWrapper::get_inline_asm_asm_string)
      .def("get_inline_asm_constraint_string", &LLVMValueWrapper::get_inline_asm_constraint_string)
      .def("get_inline_asm_dialect", &LLVMValueWrapper::get_inline_asm_dialect)
      .def("get_inline_asm_function_type", &LLVMValueWrapper::get_inline_asm_function_type)
      .def("get_inline_asm_has_side_effects", &LLVMValueWrapper::get_inline_asm_has_side_effects)
      .def("get_inline_asm_needs_aligned_stack", &LLVMValueWrapper::get_inline_asm_needs_aligned_stack)
      .def("get_inline_asm_can_unwind", &LLVMValueWrapper::get_inline_asm_can_unwind)
      // Phase 5.9: Flag setters
      .def("set_nsw", &LLVMValueWrapper::set_nsw, "nsw"_a)
      .def("set_nuw", &LLVMValueWrapper::set_nuw, "nuw"_a)
      .def("set_exact", &LLVMValueWrapper::set_exact, "exact"_a)
      .def("set_nneg", &LLVMValueWrapper::set_nneg, "nneg"_a)
      .def("get_is_disjoint", &LLVMValueWrapper::get_is_disjoint)
      .def("set_is_disjoint", &LLVMValueWrapper::set_is_disjoint, "is_disjoint"_a)
      .def("get_icmp_same_sign", &LLVMValueWrapper::get_icmp_same_sign)
      .def("set_icmp_same_sign", &LLVMValueWrapper::set_icmp_same_sign, "same_sign"_a)
      .def("set_ordering", &LLVMValueWrapper::set_ordering, "ordering"_a)
      .def("set_volatile", &LLVMValueWrapper::set_volatile, "is_volatile"_a)
      // Atomic properties
      .def("is_atomic", &LLVMValueWrapper::is_atomic)
      .def("get_atomic_sync_scope_id", &LLVMValueWrapper::get_atomic_sync_scope_id)
      .def("set_atomic_sync_scope_id", &LLVMValueWrapper::set_atomic_sync_scope_id, "scope_id"_a)
      .def("get_atomic_rmw_bin_op", &LLVMValueWrapper::get_atomic_rmw_bin_op)
      .def("get_cmpxchg_success_ordering", &LLVMValueWrapper::get_cmpxchg_success_ordering)
      .def("get_cmpxchg_failure_ordering", &LLVMValueWrapper::get_cmpxchg_failure_ordering)
      .def("get_weak", &LLVMValueWrapper::get_weak)
      .def("set_weak", &LLVMValueWrapper::set_weak, "is_weak"_a)
      // Tail call kind
      .def("get_tail_call_kind", &LLVMValueWrapper::get_tail_call_kind)
      .def("set_tail_call_kind", &LLVMValueWrapper::set_tail_call_kind, "kind"_a)
      // Called function
      .def("get_called_function_type", &LLVMValueWrapper::get_called_function_type)
      .def("get_called_value", &LLVMValueWrapper::get_called_value)
      // Branch properties
      .def("is_conditional", &LLVMValueWrapper::is_conditional)
      .def("get_condition", &LLVMValueWrapper::get_condition)
      // Landing pad properties
      .def("get_num_clauses", &LLVMValueWrapper::get_num_clauses)
      .def("get_clause", &LLVMValueWrapper::get_clause, "index"_a)
      .def("is_cleanup", &LLVMValueWrapper::is_cleanup)
      .def("set_cleanup", &LLVMValueWrapper::set_cleanup, "is_cleanup"_a)
      // Catch switch/pad properties
      .def("get_parent_catch_switch", &LLVMValueWrapper::get_parent_catch_switch)
      .def("get_num_handlers", &LLVMValueWrapper::get_num_handlers)
      // Shuffle vector mask
      .def("get_num_mask_elements", &LLVMValueWrapper::get_num_mask_elements)
      .def("get_mask_value", &LLVMValueWrapper::get_mask_value, "index"_a)
      // Fast-math flags
      .def("can_use_fast_math_flags", &LLVMValueWrapper::can_use_fast_math_flags)
      .def("get_fast_math_flags", &LLVMValueWrapper::get_fast_math_flags)
      .def("set_fast_math_flags", &LLVMValueWrapper::set_fast_math_flags, "flags"_a)
      // Call instruction arg operand
      .def("get_arg_operand", &LLVMValueWrapper::get_arg_operand, "index"_a)
      // Instruction manipulation
      .def("remove_from_parent", &LLVMValueWrapper::remove_from_parent)
      .def("is_a_instruction", &LLVMValueWrapper::is_a_instruction)
      // BasicBlock properties
      .def("get_instruction_parent", &LLVMValueWrapper::get_instruction_parent)
      .def("get_normal_dest", &LLVMValueWrapper::get_normal_dest)
      .def("get_unwind_dest", &LLVMValueWrapper::get_unwind_dest)
      .def("get_successor", &LLVMValueWrapper::get_successor, "index"_a)
      .def("get_callbr_default_dest", &LLVMValueWrapper::get_callbr_default_dest)
      .def("get_callbr_num_indirect_dests", &LLVMValueWrapper::get_callbr_num_indirect_dests)
      .def("get_callbr_indirect_dest", &LLVMValueWrapper::get_callbr_indirect_dest, "index"_a)
      // Value/BasicBlock conversion
      .def("value_is_basic_block", &LLVMValueWrapper::value_is_basic_block)
      .def("value_as_basic_block", &LLVMValueWrapper::value_as_basic_block)
      // Echo command support - landing pad and catch switch operations
      .def("add_clause", &LLVMValueWrapper::add_clause, "clause_val"_a)
      .def("add_handler", &LLVMValueWrapper::add_handler, "handler"_a)
      .def("get_handlers", &LLVMValueWrapper::get_handlers)
      .def("get_operand_bundle_at_index", &LLVMValueWrapper::get_operand_bundle_at_index, "index"_a)
      .def("get_indices", &LLVMValueWrapper::get_indices)
      // Global/instruction metadata for echo command
      .def("global_copy_all_metadata", &LLVMValueWrapper::global_copy_all_metadata,
           "Copy all metadata from this global value.")
      .def("instruction_get_all_metadata_other_than_debug_loc",
           &LLVMValueWrapper::instruction_get_all_metadata_other_than_debug_loc,
           "Get all metadata from this instruction except debug locations.");

  // BasicBlock wrapper
  nb::class_<LLVMBasicBlockWrapper>(m, "BasicBlock")
      .def("__eq__",
           [](const LLVMBasicBlockWrapper &a, const LLVMBasicBlockWrapper &b) {
             return a == b;
           })
      .def("__ne__",
           [](const LLVMBasicBlockWrapper &a, const LLVMBasicBlockWrapper &b) {
             return a != b;
           })
      .def("__hash__",
           [](const LLVMBasicBlockWrapper &v) {
             return std::hash<LLVMBasicBlockRef>{}(v.m_ref);
           })
      .def_prop_ro("name", &LLVMBasicBlockWrapper::get_name)
      .def("as_value", &LLVMBasicBlockWrapper::as_value)
      .def_prop_ro("next_block", &LLVMBasicBlockWrapper::next_block)
      .def_prop_ro("prev_block", &LLVMBasicBlockWrapper::prev_block)
      .def_prop_ro("terminator", &LLVMBasicBlockWrapper::terminator)
      .def_prop_ro("first_instruction",
                   &LLVMBasicBlockWrapper::first_instruction)
      .def_prop_ro("last_instruction", &LLVMBasicBlockWrapper::last_instruction)
      .def_prop_ro("parent", &LLVMBasicBlockWrapper::parent)
      .def("move_before", &LLVMBasicBlockWrapper::move_before, "other"_a)
      .def("move_after", &LLVMBasicBlockWrapper::move_after, "other"_a);

  // Function wrapper
  nb::class_<LLVMFunctionWrapper, LLVMValueWrapper>(m, "Function")
      .def_prop_ro("param_count", &LLVMFunctionWrapper::param_count)
      .def("get_param", &LLVMFunctionWrapper::get_param, "index"_a)
      .def_prop_ro("params", &LLVMFunctionWrapper::params)
      .def_prop_rw("linkage", &LLVMFunctionWrapper::get_linkage,
                   &LLVMFunctionWrapper::set_linkage)
      .def_prop_rw("calling_conv", &LLVMFunctionWrapper::get_calling_conv,
                   &LLVMFunctionWrapper::set_calling_conv)
      .def(
          "append_basic_block",
          [](LLVMFunctionWrapper &self, const std::string &name,
             LLVMContextWrapper *ctx) {
            return self.append_basic_block(name, ctx->m_ref);
          },
          "name"_a, "ctx"_a)
      .def_prop_ro("entry_block", &LLVMFunctionWrapper::entry_block)
      .def_prop_ro("basic_block_count", &LLVMFunctionWrapper::basic_block_count)
      .def_prop_ro("first_basic_block", &LLVMFunctionWrapper::first_basic_block)
      .def_prop_ro("last_basic_block", &LLVMFunctionWrapper::last_basic_block)
      .def_prop_ro("basic_blocks", &LLVMFunctionWrapper::basic_blocks)
      .def("append_existing_basic_block",
           &LLVMFunctionWrapper::append_existing_basic_block, "bb"_a)
      .def("erase", &LLVMFunctionWrapper::erase)
      // Echo command support - parameter iteration
      .def("first_param", &LLVMFunctionWrapper::first_param)
      .def("last_param", &LLVMFunctionWrapper::last_param)
      // Echo command support - function iteration
      .def_prop_ro("next_function", &LLVMFunctionWrapper::next_function)
      .def_prop_ro("prev_function", &LLVMFunctionWrapper::prev_function);

  // Builder wrapper
  nb::class_<LLVMBuilderWrapper>(m, "Builder")
      .def("position_at_end", &LLVMBuilderWrapper::position_at_end, "bb"_a)
      .def("position_before", &LLVMBuilderWrapper::position_before, "inst"_a)
      .def_prop_ro("insert_block", &LLVMBuilderWrapper::insert_block)
      // Arithmetic
      .def("add", &LLVMBuilderWrapper::add, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("nsw_add", &LLVMBuilderWrapper::nsw_add, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("nuw_add", &LLVMBuilderWrapper::nuw_add, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("sub", &LLVMBuilderWrapper::sub, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("nsw_sub", &LLVMBuilderWrapper::nsw_sub, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("nuw_sub", &LLVMBuilderWrapper::nuw_sub, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("mul", &LLVMBuilderWrapper::mul, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("nsw_mul", &LLVMBuilderWrapper::nsw_mul, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("nuw_mul", &LLVMBuilderWrapper::nuw_mul, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("sdiv", &LLVMBuilderWrapper::sdiv, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("udiv", &LLVMBuilderWrapper::udiv, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("exact_sdiv", &LLVMBuilderWrapper::exact_sdiv, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("srem", &LLVMBuilderWrapper::srem, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("urem", &LLVMBuilderWrapper::urem, "lhs"_a, "rhs"_a, "name"_a = "")
      // Float arithmetic
      .def("fadd", &LLVMBuilderWrapper::fadd, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("fsub", &LLVMBuilderWrapper::fsub, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("fmul", &LLVMBuilderWrapper::fmul, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("fdiv", &LLVMBuilderWrapper::fdiv, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("frem", &LLVMBuilderWrapper::frem, "lhs"_a, "rhs"_a, "name"_a = "")
      // Unary
      .def("neg", &LLVMBuilderWrapper::neg, "val"_a, "name"_a = "")
      .def("nsw_neg", &LLVMBuilderWrapper::nsw_neg, "val"_a, "name"_a = "")
      .def("fneg", &LLVMBuilderWrapper::fneg, "val"_a, "name"_a = "")
      .def("not_", &LLVMBuilderWrapper::not_, "val"_a, "name"_a = "")
      // Bitwise
      .def("shl", &LLVMBuilderWrapper::shl, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("lshr", &LLVMBuilderWrapper::lshr, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("ashr", &LLVMBuilderWrapper::ashr, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("and_", &LLVMBuilderWrapper::and_, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("or_", &LLVMBuilderWrapper::or_, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("xor_", &LLVMBuilderWrapper::xor_, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("binop", &LLVMBuilderWrapper::binop, "opcode"_a, "lhs"_a, "rhs"_a,
           "name"_a = "")
      // Memory
      .def("alloca", &LLVMBuilderWrapper::build_alloca, "ty"_a, "name"_a = "")
      .def("array_alloca", &LLVMBuilderWrapper::build_array_alloca, "ty"_a,
           "size"_a, "name"_a = "")
      .def("load", &LLVMBuilderWrapper::load, "ty"_a, "ptr"_a, "name"_a = "")
      .def("store", &LLVMBuilderWrapper::store, "val"_a, "ptr"_a)
      .def("gep", &LLVMBuilderWrapper::gep, "ty"_a, "ptr"_a, "indices"_a,
           "name"_a = "")
      .def("inbounds_gep", &LLVMBuilderWrapper::inbounds_gep, "ty"_a, "ptr"_a,
           "indices"_a, "name"_a = "")
      .def("struct_gep", &LLVMBuilderWrapper::struct_gep, "ty"_a, "ptr"_a,
           "idx"_a, "name"_a = "")
      // Comparisons
      .def("icmp", &LLVMBuilderWrapper::icmp, "pred"_a, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("fcmp", &LLVMBuilderWrapper::fcmp, "pred"_a, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("select", &LLVMBuilderWrapper::select, "cond"_a, "then_val"_a,
           "else_val"_a, "name"_a = "")
      // Casts
      .def("trunc", &LLVMBuilderWrapper::trunc, "val"_a, "ty"_a, "name"_a = "")
      .def("zext", &LLVMBuilderWrapper::zext, "val"_a, "ty"_a, "name"_a = "")
      .def("sext", &LLVMBuilderWrapper::sext, "val"_a, "ty"_a, "name"_a = "")
      .def("fptrunc", &LLVMBuilderWrapper::fptrunc, "val"_a, "ty"_a,
           "name"_a = "")
      .def("fpext", &LLVMBuilderWrapper::fpext, "val"_a, "ty"_a, "name"_a = "")
      .def("fptosi", &LLVMBuilderWrapper::fptosi, "val"_a, "ty"_a,
           "name"_a = "")
      .def("fptoui", &LLVMBuilderWrapper::fptoui, "val"_a, "ty"_a,
           "name"_a = "")
      .def("sitofp", &LLVMBuilderWrapper::sitofp, "val"_a, "ty"_a,
           "name"_a = "")
      .def("uitofp", &LLVMBuilderWrapper::uitofp, "val"_a, "ty"_a,
           "name"_a = "")
      .def("ptrtoint", &LLVMBuilderWrapper::ptrtoint, "val"_a, "ty"_a,
           "name"_a = "")
      .def("inttoptr", &LLVMBuilderWrapper::inttoptr, "val"_a, "ty"_a,
           "name"_a = "")
      .def("bitcast", &LLVMBuilderWrapper::bitcast, "val"_a, "ty"_a,
           "name"_a = "")
      .def("int_cast2", &LLVMBuilderWrapper::int_cast2, "val"_a, "ty"_a,
           "is_signed"_a, "name"_a = "")
      // Control flow
      .def("ret", &LLVMBuilderWrapper::ret, "val"_a)
      .def("ret_void", &LLVMBuilderWrapper::ret_void)
      .def("br", &LLVMBuilderWrapper::br, "dest"_a)
      .def("cond_br", &LLVMBuilderWrapper::cond_br, "cond"_a, "then_bb"_a,
           "else_bb"_a)
      .def("switch_", &LLVMBuilderWrapper::switch_, "val"_a, "else_bb"_a,
           "num_cases"_a)
      .def("call", &LLVMBuilderWrapper::call, "func_ty"_a, "func"_a, "args"_a,
           "name"_a = "")
       .def("unreachable", &LLVMBuilderWrapper::unreachable)
       .def("phi", &LLVMBuilderWrapper::phi, "ty"_a, "name"_a = "")
       // Phase 5.6: Additional instruction builders
       .def("resume", &LLVMBuilderWrapper::resume, "exn"_a)
       .def("landing_pad", &LLVMBuilderWrapper::landing_pad, "ty"_a,
            "num_clauses"_a, "name"_a = "")
       .def("catch_ret", &LLVMBuilderWrapper::catch_ret, "catch_pad"_a, "bb"_a)
       .def("catch_pad", &LLVMBuilderWrapper::catch_pad, "parent_pad"_a, "args"_a,
            "name"_a = "")
       .def("cleanup_pad", &LLVMBuilderWrapper::cleanup_pad, "parent_pad"_a,
            "args"_a, "name"_a = "")
       .def("extract_value", &LLVMBuilderWrapper::extract_value, "agg"_a,
            "index"_a, "name"_a = "")
       .def("insert_value", &LLVMBuilderWrapper::insert_value, "agg"_a, "val"_a,
            "index"_a, "name"_a = "")
       .def("extract_element", &LLVMBuilderWrapper::extract_element, "vec"_a,
            "index"_a, "name"_a = "")
       .def("insert_element", &LLVMBuilderWrapper::insert_element, "vec"_a,
            "val"_a, "index"_a, "name"_a = "")
       .def("shuffle_vector", &LLVMBuilderWrapper::shuffle_vector, "v1"_a, "v2"_a,
            "mask"_a, "name"_a = "")
       .def("freeze", &LLVMBuilderWrapper::freeze, "val"_a, "name"_a = "")
       .def("gep_with_no_wrap_flags", &LLVMBuilderWrapper::gep_with_no_wrap_flags,
            "ty"_a, "ptr"_a, "indices"_a, "flags"_a, "name"_a = "")
       .def("atomic_rmw_sync_scope", &LLVMBuilderWrapper::atomic_rmw_sync_scope,
            "op"_a, "ptr"_a, "val"_a, "ordering"_a, "sync_scope_id"_a)
       .def("atomic_cmpxchg_sync_scope",
            &LLVMBuilderWrapper::atomic_cmpxchg_sync_scope, "ptr"_a, "cmp"_a,
            "new_val"_a, "success_ordering"_a, "failure_ordering"_a,
            "sync_scope_id"_a)
       .def("fence_sync_scope", &LLVMBuilderWrapper::fence_sync_scope,
            "ordering"_a, "sync_scope_id"_a, "name"_a = "")
       .def("insert_into_builder_with_name",
             &LLVMBuilderWrapper::insert_into_builder_with_name, "instr"_a,
             "name"_a)
        .def("add_metadata_to_inst", &LLVMBuilderWrapper::add_metadata_to_inst,
             "instr"_a)
        // Missing builders for echo command - wrapped to convert OperandBundleWrapper
        .def("invoke_with_operand_bundles",
             [](LLVMBuilderWrapper &self, const LLVMTypeWrapper &fn_ty,
                const LLVMValueWrapper &fn,
                const std::vector<LLVMValueWrapper> &args,
                const LLVMBasicBlockWrapper &then_bb,
                const LLVMBasicBlockWrapper &catch_bb,
                const std::vector<LLVMOperandBundleWrapper*> &bundles,
                const std::string &name) {
               std::vector<LLVMOperandBundleRef> bundle_refs;
               for (auto *b : bundles)
                 bundle_refs.push_back(b->m_ref);
               return self.invoke_with_operand_bundles(fn_ty, fn, args, then_bb,
                                                       catch_bb, bundle_refs, name);
             },
             "fn_ty"_a, "fn"_a, "args"_a, "then_bb"_a, "catch_bb"_a, "bundles"_a,
             "name"_a = "")
        .def("call_with_operand_bundles",
             [](LLVMBuilderWrapper &self, const LLVMTypeWrapper &fn_ty,
                const LLVMValueWrapper &fn,
                const std::vector<LLVMValueWrapper> &args,
                const std::vector<LLVMOperandBundleWrapper*> &bundles,
                const std::string &name) {
               std::vector<LLVMOperandBundleRef> bundle_refs;
               for (auto *b : bundles)
                 bundle_refs.push_back(b->m_ref);
               return self.call_with_operand_bundles(fn_ty, fn, args, bundle_refs,
                                                     name);
             },
             "fn_ty"_a, "fn"_a, "args"_a, "bundles"_a, "name"_a = "")
        .def("callbr",
             [](LLVMBuilderWrapper &self, const LLVMTypeWrapper &fn_ty,
                const LLVMValueWrapper &fn,
                const LLVMBasicBlockWrapper &default_dest,
                const std::vector<LLVMBasicBlockWrapper> &indirect_dests,
                const std::vector<LLVMValueWrapper> &args,
                const std::vector<LLVMOperandBundleWrapper*> &bundles,
                const std::string &name) {
               std::vector<LLVMOperandBundleRef> bundle_refs;
               for (auto *b : bundles)
                 bundle_refs.push_back(b->m_ref);
               return self.callbr(fn_ty, fn, default_dest, indirect_dests, args,
                                  bundle_refs, name);
             },
             "fn_ty"_a, "fn"_a, "default_dest"_a, "indirect_dests"_a, "args"_a,
             "bundles"_a, "name"_a = "")
        .def("catch_switch", &LLVMBuilderWrapper::catch_switch, "parent_pad"_a,
             "unwind_bb"_a = nb::none(), "num_handlers"_a = 0, "name"_a = "")
        .def("cleanup_ret", &LLVMBuilderWrapper::cleanup_ret, "catch_pad"_a,
             "bb"_a = nb::none());

  // Operand Bundle wrapper
  nb::class_<LLVMOperandBundleWrapper>(m, "OperandBundle")
      .def_prop_ro("tag", &LLVMOperandBundleWrapper::get_tag)
      .def_prop_ro("num_args", &LLVMOperandBundleWrapper::get_num_args)
      .def("get_arg_at_index", &LLVMOperandBundleWrapper::get_arg_at_index, "index"_a);

  // Attribute wrapper
  nb::class_<LLVMAttributeWrapper>(m, "Attribute")
      .def_prop_ro("is_valid", &LLVMAttributeWrapper::is_valid)
      .def_prop_ro("kind", &LLVMAttributeWrapper::get_kind,
                   "Get the kind ID of this enum attribute.")
      .def_prop_ro("value", &LLVMAttributeWrapper::get_value,
                   "Get the value of this enum attribute (0 if none).");

  // Value metadata entries wrapper (for global/instruction metadata copying)
  nb::class_<LLVMValueMetadataEntriesWrapper>(m, "ValueMetadataEntries")
      .def("__len__", &LLVMValueMetadataEntriesWrapper::size)
      .def("get_kind", &LLVMValueMetadataEntriesWrapper::get_kind, "index"_a,
           "Get the metadata kind at the given index.")
      .def("get_metadata",
           &LLVMValueMetadataEntriesWrapper_get_metadata, "index"_a,
           "Get the metadata at the given index.");

  // Named metadata node wrapper
  nb::class_<LLVMNamedMDNodeWrapper>(m, "NamedMDNode")
      .def("__eq__",
           [](const LLVMNamedMDNodeWrapper &a,
              const LLVMNamedMDNodeWrapper &b) { return a == b; })
      .def("__ne__",
           [](const LLVMNamedMDNodeWrapper &a,
              const LLVMNamedMDNodeWrapper &b) { return a != b; })
      .def("__hash__",
           [](const LLVMNamedMDNodeWrapper &v) {
             return std::hash<LLVMNamedMDNodeRef>{}(v.m_ref);
           })
      .def_prop_ro("name", &LLVMNamedMDNodeWrapper::get_name)
      .def_prop_ro("next", &LLVMNamedMDNodeWrapper::next)
      .def_prop_ro("prev", &LLVMNamedMDNodeWrapper::prev);

  // Module wrapper
  nb::class_<LLVMModuleWrapper>(m, "Module")
      .def_prop_rw("name", &LLVMModuleWrapper::get_name,
                   &LLVMModuleWrapper::set_name)
      .def_prop_rw("source_filename", &LLVMModuleWrapper::get_source_filename,
                   &LLVMModuleWrapper::set_source_filename)
      .def_prop_rw("data_layout", &LLVMModuleWrapper::get_data_layout,
                   &LLVMModuleWrapper::set_data_layout)
      .def_prop_rw("target_triple", &LLVMModuleWrapper::get_target_triple,
                   &LLVMModuleWrapper::set_target_triple)
      .def("add_function", &LLVMModuleWrapper::add_function, "name"_a,
           "func_ty"_a)
      .def("get_function", &LLVMModuleWrapper::get_function, "name"_a)
      .def("add_global", &LLVMModuleWrapper::add_global, "ty"_a, "name"_a)
      .def("add_global_in_address_space",
           &LLVMModuleWrapper::add_global_in_address_space, "ty"_a, "name"_a,
           "address_space"_a)
      .def("get_global", &LLVMModuleWrapper::get_global, "name"_a)
      .def_prop_ro("first_global", &LLVMModuleWrapper::first_global)
      .def_prop_ro("last_global", &LLVMModuleWrapper::last_global)
      .def_prop_ro("globals", &LLVMModuleWrapper::globals)
      .def_prop_ro("functions", &LLVMModuleWrapper::functions)
      .def_prop_ro("first_function", &LLVMModuleWrapper::first_function)
      .def_prop_ro("last_function", &LLVMModuleWrapper::last_function)
      .def("__str__", &LLVMModuleWrapper::to_string)
      .def("to_string", &LLVMModuleWrapper::to_string)
      .def("verify", &LLVMModuleWrapper::verify)
      .def("get_verification_error", &LLVMModuleWrapper::get_verification_error)
      // Global alias support
      .def_prop_ro("first_global_alias",
                   &LLVMModuleWrapper::first_global_alias)
      .def_prop_ro("last_global_alias", &LLVMModuleWrapper::last_global_alias)
      .def("get_named_global_alias", &LLVMModuleWrapper::get_named_global_alias,
           "name"_a)
      .def("add_alias", &LLVMModuleWrapper::add_alias, "value_ty"_a,
           "addr_space"_a, "aliasee"_a, "name"_a)
      // Global IFunc support
      .def_prop_ro("first_global_ifunc",
                   &LLVMModuleWrapper::first_global_ifunc)
      .def_prop_ro("last_global_ifunc", &LLVMModuleWrapper::last_global_ifunc)
      .def("get_named_global_ifunc", &LLVMModuleWrapper::get_named_global_ifunc,
           "name"_a)
      .def("add_global_ifunc", &LLVMModuleWrapper::add_global_ifunc, "name"_a,
           "ty"_a, "addr_space"_a, "resolver"_a)
      // Named metadata support
      .def_prop_ro("first_named_metadata",
                   &LLVMModuleWrapper::first_named_metadata)
      .def_prop_ro("last_named_metadata",
                   &LLVMModuleWrapper::last_named_metadata)
      .def("get_named_metadata", &LLVMModuleWrapper::get_named_metadata,
           "name"_a)
      .def("get_or_insert_named_metadata",
           &LLVMModuleWrapper::get_or_insert_named_metadata, "name"_a)
      .def("get_named_metadata_num_operands",
           &LLVMModuleWrapper::get_named_metadata_num_operands, "name"_a)
      .def("get_named_metadata_operands",
           &LLVMModuleWrapper::get_named_metadata_operands, "name"_a)
      // Inline assembly support
      .def_prop_rw("inline_asm", &LLVMModuleWrapper::get_inline_asm,
                   &LLVMModuleWrapper::set_inline_asm)
      .def("clone", &LLVMModuleWrapper::clone, nb::rv_policy::take_ownership);

  // Context wrapper
  nb::class_<LLVMContextWrapper>(m, "Context")
      .def_prop_rw("discard_value_names",
                   &LLVMContextWrapper::get_discard_value_names,
                   &LLVMContextWrapper::set_discard_value_names)
      // Type factories
      .def("void_type", &LLVMContextWrapper::void_type)
      .def("int1_type", &LLVMContextWrapper::int1_type)
      .def("int8_type", &LLVMContextWrapper::int8_type)
      .def("int16_type", &LLVMContextWrapper::int16_type)
      .def("int32_type", &LLVMContextWrapper::int32_type)
      .def("int64_type", &LLVMContextWrapper::int64_type)
      .def("int128_type", &LLVMContextWrapper::int128_type)
      .def("int_type", &LLVMContextWrapper::int_type, "bits"_a)
      .def("half_type", &LLVMContextWrapper::half_type)
      .def("float_type", &LLVMContextWrapper::float_type)
      .def("double_type", &LLVMContextWrapper::double_type)
      .def("bfloat_type", &LLVMContextWrapper::bfloat_type)
      .def("x86_fp80_type", &LLVMContextWrapper::x86_fp80_type)
      .def("fp128_type", &LLVMContextWrapper::fp128_type)
      .def("ppc_fp128_type", &LLVMContextWrapper::ppc_fp128_type)
      .def("label_type", &LLVMContextWrapper::label_type)
      .def("metadata_type", &LLVMContextWrapper::metadata_type)
      .def("x86_amx_type", &LLVMContextWrapper::x86_amx_type)
      .def("token_type", &LLVMContextWrapper::token_type)
      .def("pointer_type", &LLVMContextWrapper::pointer_type,
           "address_space"_a = 0)
      .def("array_type", &LLVMContextWrapper::array_type, "elem_ty"_a,
           "count"_a)
      .def("vector_type", &LLVMContextWrapper::vector_type, "elem_ty"_a,
           "elem_count"_a)
      .def("scalable_vector_type", &LLVMContextWrapper::scalable_vector_type,
           "elem_ty"_a, "elem_count"_a)
      .def("target_ext_type", &LLVMContextWrapper::target_ext_type, "name"_a,
           "type_params"_a, "int_params"_a)
      .def("get_type_by_name", &LLVMContextWrapper::get_type_by_name, "name"_a)
      .def("function_type", &LLVMContextWrapper::function_type, "ret_ty"_a,
           "param_types"_a, "vararg"_a = false)
      .def("struct_type", &LLVMContextWrapper::struct_type, "elem_types"_a,
           "packed"_a = false)
      .def("named_struct_type", &LLVMContextWrapper::named_struct_type,
           "name"_a)
      // Module/Builder creation
      .def("create_module", &LLVMContextWrapper::create_module, "name"_a,
           nb::rv_policy::take_ownership)
      .def("create_builder", &LLVMContextWrapper::create_builder,
           nb::rv_policy::take_ownership)
      .def("create_basic_block", &LLVMContextWrapper::create_basic_block,
           "name"_a);

  // Context manager
  nb::class_<LLVMContextManager>(m, "ContextManager")
      .def("__enter__", &LLVMContextManager::enter,
           nb::rv_policy::reference_internal)
      .def("__exit__", &LLVMContextManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none());

  // Module manager
  nb::class_<LLVMModuleManager>(m, "ModuleManager")
      .def("__enter__", &LLVMModuleManager::enter,
           nb::rv_policy::reference_internal)
      .def("__exit__", &LLVMModuleManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none())
      .def("dispose", &LLVMModuleManager::dispose,
           R"(Dispose the module without using a 'with' statement. Can only be called before __enter__.)");

  // Builder manager
  nb::class_<LLVMBuilderManager>(m, "BuilderManager")
      .def("__enter__", &LLVMBuilderManager::enter,
           nb::rv_policy::reference_internal)
      .def("__exit__", &LLVMBuilderManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none())
      .def("dispose", &LLVMBuilderManager::dispose,
           R"(Dispose the builder without using a 'with' statement. Can only be called before __enter__.)");

  // Module-level factory functions
  m.def(
      "create_context", [] { return new LLVMContextManager(); },
      nb::rv_policy::take_ownership,
      R"(Create a new LLVM context manager for use with 'with' statement.)");

  m.def(
      "global_context",
      [] {
        static LLVMContextWrapper context(true);
        return &context;
      },
      nb::rv_policy::reference,
      R"(Get the global LLVM context (use sparingly).)");

  // Constant creation functions
  m.def("const_int", &const_int, "ty"_a, "val"_a, "sign_extend"_a = false,
        R"(Create an integer constant.)");
  m.def("const_real", &const_real, "ty"_a, "val"_a,
        R"(Create a floating-point constant.)");
  m.def("const_null", &const_null, "ty"_a, R"(Create a null pointer constant.)");
  m.def("const_all_ones", &const_all_ones, "ty"_a,
        R"(Create an all-ones constant.)");
  m.def("undef", &get_undef, "ty"_a, R"(Create an undef value.)");
  m.def("poison", &get_poison, "ty"_a, R"(Create a poison value.)");
  m.def("const_array", &const_array, "elem_ty"_a, "vals"_a,
        R"(Create an array constant.)");
  m.def("const_struct", &const_struct, "vals"_a, "packed"_a, "ctx"_a,
        R"(Create a struct constant.)");
  m.def("const_vector", &const_vector, "vals"_a,
        R"(Create a vector constant.)");
  m.def("const_string", &const_string, "ctx"_a, "str"_a,
        "dont_null_terminate"_a = false,
        R"(Create a string constant.)");
  m.def("const_pointer_null", &const_pointer_null, "ty"_a,
        R"(Create a null pointer constant for a specific pointer type.)");
  m.def("const_named_struct", &const_named_struct, "struct_ty"_a, "vals"_a,
        R"(Create a named struct constant.)");
  m.def("value_is_null", &value_is_null, "val"_a,
        R"(Check if a value is null.)");
  m.def("const_int_get_zext_value", &const_int_get_zext_value, "val"_a,
        R"(Get the zero-extended value of an integer constant.)");
  m.def("const_int_get_sext_value", &const_int_get_sext_value, "val"_a,
        R"(Get the sign-extended value of an integer constant.)");
  m.def("const_int_of_arbitrary_precision", &const_int_of_arbitrary_precision,
        "ty"_a, "words"_a,
        R"(Create an integer constant of arbitrary precision from 64-bit words (little-endian).)");
  m.def("const_data_array", &const_data_array, "elem_ty"_a, "data"_a,
        R"(Create a constant data array from raw bytes.)");
  m.def("const_bitcast", &const_bitcast, "val"_a, "ty"_a,
        R"(Create a constant bitcast expression.)");
  m.def("const_gep_with_no_wrap_flags", &const_gep_with_no_wrap_flags, "ty"_a,
        "ptr"_a, "indices"_a, "no_wrap_flags"_a,
        R"(Create a constant GEP expression with no-wrap flags.)");
  m.def("const_ptr_auth", &const_ptr_auth, "ptr"_a, "key"_a,
        "discriminator"_a, "addr_discriminator"_a,
        R"(Create a constant pointer authentication expression.)");
  m.def("intrinsic_is_overloaded", &intrinsic_is_overloaded, "id"_a,
        R"(Check if an intrinsic is overloaded.)");
  m.def("get_intrinsic_declaration", &get_intrinsic_declaration, "mod"_a,
        "id"_a, "param_types"_a,
        R"(Get or insert an intrinsic function declaration.)");
  
  // Operand bundle creation
  m.def("create_operand_bundle",
        [](const std::string &tag, const std::vector<LLVMValueWrapper> &args,
           LLVMContextWrapper *ctx) {
          std::vector<LLVMValueRef> arg_refs;
          for (const auto &a : args)
            arg_refs.push_back(a.m_ref);
          LLVMOperandBundleRef bundle = LLVMCreateOperandBundle(
              tag.c_str(), tag.size(), arg_refs.data(), arg_refs.size());
          return new LLVMOperandBundleWrapper(bundle, ctx->m_token);
        },
        "tag"_a, "args"_a, "ctx"_a, nb::rv_policy::take_ownership,
        R"(Create an operand bundle with the given tag and arguments.)");
  
  // Get undef mask element constant
  m.def("get_undef_mask_elem", []() { return LLVMGetUndefMaskElem(); },
        R"(Get the value that indicates an undef element in a shuffle mask.)");

  // Get inline assembly
  m.def("get_inline_asm",
        [](const LLVMTypeWrapper &fn_ty, const std::string &asm_string,
           const std::string &constraints, bool has_side_effects,
           bool needs_aligned_stack, LLVMInlineAsmDialect dialect,
           bool can_unwind) {
          return LLVMValueWrapper(
              LLVMGetInlineAsm(fn_ty.m_ref, asm_string.c_str(), asm_string.size(),
                               constraints.c_str(), constraints.size(),
                               has_side_effects, needs_aligned_stack, dialect,
                               can_unwind),
              fn_ty.m_context_token);
        },
        "fn_ty"_a, "asm_string"_a, "constraints"_a, "has_side_effects"_a,
        "needs_aligned_stack"_a, "dialect"_a, "can_unwind"_a,
        R"(Create an inline assembly value.)");

  // Target wrapper
  nb::class_<LLVMTargetWrapper>(m, "Target")
      .def_prop_ro("name", &LLVMTargetWrapper::get_name)
      .def_prop_ro("description", &LLVMTargetWrapper::get_description)
      .def_prop_ro("has_jit", &LLVMTargetWrapper::has_jit)
      .def_prop_ro("has_target_machine", &LLVMTargetWrapper::has_target_machine)
      .def_prop_ro("has_asm_backend", &LLVMTargetWrapper::has_asm_backend)
      .def_prop_ro("next", &LLVMTargetWrapper::next);

  // Target functions
  m.def("initialize_all_target_infos", &initialize_all_target_infos,
        R"(Initialize all target infos.)");
  m.def("initialize_all_targets", &initialize_all_targets,
        R"(Initialize all targets.)");
  m.def("initialize_all_target_mcs", &initialize_all_target_mcs,
        R"(Initialize all target MCs.)");
  m.def("initialize_all_asm_printers", &initialize_all_asm_printers,
        R"(Initialize all ASM printers.)");
  m.def("initialize_all_asm_parsers", &initialize_all_asm_parsers,
        R"(Initialize all ASM parsers.)");
  m.def("initialize_all_disassemblers", &initialize_all_disassemblers,
        R"(Initialize all disassemblers.)");
  m.def("get_first_target", &get_first_target,
        R"(Get the first registered target (returns None if no targets).)");

  // Memory buffer wrapper
  nb::class_<LLVMMemoryBufferWrapper>(m, "MemoryBuffer")
      .def_prop_ro("buffer_start", &LLVMMemoryBufferWrapper::get_buffer_start)
      .def_prop_ro("buffer_size", &LLVMMemoryBufferWrapper::get_buffer_size);

  // Memory buffer functions
  m.def("create_memory_buffer_with_stdin", &create_memory_buffer_with_stdin,
        nb::rv_policy::take_ownership,
        R"(Read stdin into a memory buffer.)");

  // =============================================================================
  // Disassembler Bindings
  // =============================================================================

  nb::class_<LLVMDisasmContextWrapper>(m, "DisasmContext")
      .def_prop_ro("is_valid", &LLVMDisasmContextWrapper::is_valid,
                   R"(Check if disassembler context is valid.)")
      .def("disasm_instruction", &LLVMDisasmContextWrapper::disasm_instruction,
           "bytes"_a, "offset"_a, "pc"_a,
           R"(Disassemble a single instruction.
           
           Args:
               bytes: The byte array containing machine code
               offset: Offset into bytes to start disassembling
               pc: Program counter value for the instruction
               
           Returns:
               Tuple of (bytes_consumed, disassembly_string)
               If bytes_consumed is 0, disassembly failed.)");

  m.def("create_disasm_cpu_features", &create_disasm_cpu_features,
        "triple"_a, "cpu"_a = "", "features"_a = "",
        nb::rv_policy::take_ownership,
        R"(Create a disassembler for the given triple, CPU, and features.
        
        Args:
            triple: Target triple string (e.g., "x86_64-linux-unknown")
            cpu: CPU name (can be empty)
            features: Feature string (can be empty or "NULL")
            
        Returns:
            DisasmContext, or one with is_valid=False if creation failed.)");

  // =============================================================================
  // Object File Bindings
  // =============================================================================

  nb::class_<LLVMBinaryWrapper>(m, "Binary")
      .def_prop_ro("is_valid", &LLVMBinaryWrapper::is_valid,
                   R"(Check if binary is valid.)");

  nb::class_<LLVMSectionIteratorWrapper>(m, "SectionIterator")
      .def_prop_ro("is_valid", &LLVMSectionIteratorWrapper::is_valid,
                   R"(Check if section iterator is valid.)")
      .def("is_at_end", &LLVMSectionIteratorWrapper::is_at_end,
           R"(Check if iterator is at end.)")
      .def("move_next", &LLVMSectionIteratorWrapper::move_next,
           R"(Move to next section.)")
      .def_prop_ro("name", &LLVMSectionIteratorWrapper::get_name,
                   R"(Get section name.)")
      .def_prop_ro("address", &LLVMSectionIteratorWrapper::get_address,
                   R"(Get section address.)")
      .def_prop_ro("size", &LLVMSectionIteratorWrapper::get_size,
                   R"(Get section size.)");

  nb::class_<LLVMSymbolIteratorWrapper>(m, "SymbolIterator")
      .def_prop_ro("is_valid", &LLVMSymbolIteratorWrapper::is_valid,
                   R"(Check if symbol iterator is valid.)")
      .def("is_at_end", &LLVMSymbolIteratorWrapper::is_at_end,
           R"(Check if iterator is at end.)")
      .def("move_next", &LLVMSymbolIteratorWrapper::move_next,
           R"(Move to next symbol.)")
      .def_prop_ro("name", &LLVMSymbolIteratorWrapper::get_name,
                   R"(Get symbol name.)")
      .def_prop_ro("address", &LLVMSymbolIteratorWrapper::get_address,
                   R"(Get symbol address.)")
      .def_prop_ro("size", &LLVMSymbolIteratorWrapper::get_size,
                   R"(Get symbol size.)");

  m.def(
      "create_binary",
      [](LLVMMemoryBufferWrapper &membuf) -> LLVMBinaryWrapper * {
        auto [binary, error] = create_binary(membuf);
        if (!binary) {
          throw LLVMException("Error creating binary: " + error);
        }
        return binary;
      },
      "membuf"_a, nb::rv_policy::take_ownership,
      R"(Create a binary from a memory buffer.
      
      Args:
          membuf: Memory buffer containing the binary data
          
      Returns:
          Binary object
          
      Raises:
          LLVMException if binary creation fails.)");

  m.def(
      "create_binary_or_error",
      [](LLVMMemoryBufferWrapper &membuf) -> std::pair<LLVMBinaryWrapper *, std::string> {
        return create_binary(membuf);
      },
      "membuf"_a, nb::rv_policy::take_ownership,
      R"(Create a binary from a memory buffer, returning error as string.
      
      Args:
          membuf: Memory buffer containing the binary data
          
      Returns:
          Tuple of (Binary or None, error_message)
          If Binary is None, error_message contains the error.)");

  m.def("copy_section_iterator", &copy_section_iterator,
        "binary"_a, nb::rv_policy::take_ownership,
        R"(Create a section iterator for the binary.)");

  m.def("copy_symbol_iterator", &copy_symbol_iterator,
        "binary"_a, nb::rv_policy::take_ownership,
        R"(Create a symbol iterator for the binary.)");

  m.def("move_to_containing_section", &move_to_containing_section,
        "section_iter"_a, "symbol_iter"_a,
        R"(Move section iterator to the section containing the symbol.)");

  // BitReader functions
  m.def("parse_bitcode_in_context", &parse_bitcode_in_context, "ctx"_a,
        "membuf"_a, "lazy"_a = false, "new_api"_a = false,
        nb::rv_policy::take_ownership,
        R"(Parse bitcode from memory buffer into a module.)");

  // Attribute index constants
  m.attr("AttributeReturnIndex") = nb::int_(static_cast<int>(LLVMAttributeReturnIndex));
  m.attr("AttributeFunctionIndex") = nb::int_(static_cast<int>(LLVMAttributeFunctionIndex));

  // Attribute functions
  m.def(
      "get_attribute_count_at_index",
      [](const LLVMFunctionWrapper &func, int idx) {
        func.check_valid();
        return LLVMGetAttributeCountAtIndex(func.m_ref, static_cast<unsigned>(idx));
      },
      "func"_a, "idx"_a,
      R"(Get the number of attributes at the given index.)");

  m.def(
      "get_callsite_attribute_count",
      [](const LLVMValueWrapper &call_inst, int idx) {
        call_inst.check_valid();
        return LLVMGetCallSiteAttributeCount(call_inst.m_ref, static_cast<unsigned>(idx));
      },
      "call_inst"_a, "idx"_a,
      R"(Get the number of call site attributes at the given index.)");

  // Additional attribute functions for echo command
  m.def(
      "get_last_enum_attribute_kind",
      []() { return LLVMGetLastEnumAttributeKind(); },
      R"(Get the last enum attribute kind (highest attribute number).)");

  m.def(
      "create_enum_attribute",
      [](const LLVMContextWrapper &ctx, unsigned kind_id, uint64_t val) {
        ctx.check_valid();
        LLVMAttributeRef ref = LLVMCreateEnumAttribute(ctx.m_ref, kind_id, val);
        return LLVMAttributeWrapper(ref, ctx.m_token);
      },
      "ctx"_a, "kind_id"_a, "val"_a,
      R"(Create an enum attribute.)");

  m.def(
      "get_enum_attribute_at_index",
      [](const LLVMFunctionWrapper &func, int idx, unsigned kind_id)
          -> std::optional<LLVMAttributeWrapper> {
        func.check_valid();
        LLVMAttributeRef ref = LLVMGetEnumAttributeAtIndex(
            func.m_ref, static_cast<unsigned>(idx), kind_id);
        if (!ref)
          return std::nullopt;
        return LLVMAttributeWrapper(ref, func.m_context_token);
      },
      "func"_a, "idx"_a, "kind_id"_a,
      R"(Get an enum attribute at the given index on a function. Returns None if not found.)");

  m.def(
      "add_attribute_at_index",
      [](LLVMFunctionWrapper &func, int idx, const LLVMAttributeWrapper &attr) {
        func.check_valid();
        attr.check_valid();
        LLVMAddAttributeAtIndex(func.m_ref, static_cast<unsigned>(idx),
                                attr.m_ref);
      },
      "func"_a, "idx"_a, "attr"_a,
      R"(Add an attribute to a function at the given index.)");

  m.def(
      "get_callsite_enum_attribute",
      [](const LLVMValueWrapper &call_inst, int idx, unsigned kind_id)
          -> std::optional<LLVMAttributeWrapper> {
        call_inst.check_valid();
        LLVMAttributeRef ref = LLVMGetCallSiteEnumAttribute(
            call_inst.m_ref, static_cast<unsigned>(idx), kind_id);
        if (!ref)
          return std::nullopt;
        return LLVMAttributeWrapper(ref, call_inst.m_context_token);
      },
      "call_inst"_a, "idx"_a, "kind_id"_a,
      R"(Get an enum attribute at the given call site index. Returns None if not found.)");

  m.def(
      "add_callsite_attribute",
      [](LLVMValueWrapper &call_inst, int idx,
         const LLVMAttributeWrapper &attr) {
        call_inst.check_valid();
        attr.check_valid();
        LLVMAddCallSiteAttribute(call_inst.m_ref, static_cast<unsigned>(idx),
                                 attr.m_ref);
      },
      "call_inst"_a, "idx"_a, "attr"_a,
      R"(Add an attribute to a call site at the given index.)");

  // Global metadata functions for echo command
  m.def(
      "global_set_metadata",
      [](LLVMValueWrapper &global_val, unsigned kind, const LLVMMetadataWrapper &md) {
        global_val.check_valid();
        md.check_valid();
        LLVMGlobalSetMetadata(global_val.m_ref, kind, md.m_ref);
      },
      "global_val"_a, "kind"_a, "md"_a,
      R"(Set metadata on a global value at the given kind.)");

  // Metadata functions
  m.def(
      "md_node",
      [](const std::vector<LLVMValueWrapper> &vals) {
        std::vector<LLVMValueRef> refs;
        refs.reserve(vals.size());
        for (const auto &v : vals) {
          v.check_valid();
          refs.push_back(v.m_ref);
        }
        return LLVMValueWrapper(
            LLVMMDNode(refs.data(), static_cast<unsigned>(refs.size())),
            vals.empty() ? nullptr : vals[0].m_context_token);
      },
      "vals"_a, R"(Create metadata node from values (global context).)");

  m.def(
      "add_named_metadata_operand",
      [](LLVMModuleWrapper &mod, const std::string &name,
         const LLVMValueWrapper &val) {
        mod.check_valid();
        val.check_valid();
        LLVMAddNamedMetadataOperand(mod.m_ref, name.c_str(), val.m_ref);
      },
      "mod"_a, "name"_a, "val"_a,
      R"(Add operand to named metadata.)");

  m.def(
      "set_metadata",
      [](LLVMValueWrapper &inst, unsigned kind_id, const LLVMValueWrapper &val) {
        inst.check_valid();
        val.check_valid();
        LLVMSetMetadata(inst.m_ref, kind_id, val.m_ref);
      },
      "inst"_a, "kind_id"_a, "val"_a, R"(Set metadata on instruction.)");

  m.def(
      "get_md_kind_id",
      [](const std::string &name) {
        return LLVMGetMDKindID(name.c_str(), static_cast<unsigned>(name.size()));
      },
      "name"_a, R"(Get metadata kind ID for name.)");

  m.def(
      "delete_instruction",
      [](LLVMValueWrapper &inst) {
        inst.check_valid();
        LLVMDeleteInstruction(inst.m_ref);
        // Invalidate the wrapper after deletion
        inst.m_ref = nullptr;
      },
      "inst"_a, R"(Delete an instruction.)");

  m.def(
      "get_module_context",
      [](const LLVMModuleWrapper &mod) -> LLVMContextWrapper * {
        mod.check_valid();
        LLVMContextRef ctx = LLVMGetModuleContext(mod.m_ref);
        // Return a wrapper for the context (note: this doesn't own it)
        static thread_local LLVMContextWrapper global_ctx_wrapper(true);
        return &global_ctx_wrapper;
      },
      nb::rv_policy::reference, "mod"_a, R"(Get module's context.)");

  m.def(
      "is_a_value_as_metadata",
      [](const LLVMValueWrapper &val) {
        val.check_valid();
        LLVMValueRef result = LLVMIsAValueAsMetadata(val.m_ref);
        return result != nullptr;
      },
      "val"_a, R"(Check if value is ValueAsMetadata.)");

  // ==========================================================================
  // Diagnostic Handler Support
  // ==========================================================================
  
  nb::enum_<LLVMDiagnosticSeverity>(m, "DiagnosticSeverity")
      .value("Error", LLVMDSError)
      .value("Warning", LLVMDSWarning)
      .value("Remark", LLVMDSRemark)
      .value("Note", LLVMDSNote);
  
  m.def(
      "context_set_diagnostic_handler",
      [](LLVMContextWrapper &ctx) {
        ctx.check_valid();
        g_diagnostic_info = DiagnosticInfo(); // Reset
        LLVMContextSetDiagnosticHandler(ctx.m_ref, diagnostic_handler_callback, nullptr);
      },
      "ctx"_a, R"(Set diagnostic handler for context (stores info in thread-local storage).)");
  
  m.def(
      "diagnostic_was_called",
      []() { return g_diagnostic_info.was_called; },
      R"(Check if diagnostic handler was called since last reset.)");
  
  m.def(
      "get_diagnostic_severity",
      []() { return g_diagnostic_info.severity; },
      R"(Get severity of last diagnostic.)");
  
  m.def(
      "get_diagnostic_description",
      []() { return g_diagnostic_info.description; },
      R"(Get description of last diagnostic.)");
  
  m.def(
      "reset_diagnostic_info",
      []() { g_diagnostic_info = DiagnosticInfo(); },
      R"(Reset diagnostic info.)");

  // Bitcode parsing API that uses LLVMGetBitcodeModule2 (global context)
  m.def(
      "get_bitcode_module_2",
      [](LLVMMemoryBufferWrapper &membuf) -> LLVMModuleWrapper * {
        membuf.check_valid();
        LLVMModuleRef mod = nullptr;
        
        if (LLVMGetBitcodeModule2(membuf.m_ref, &mod)) {
          throw LLVMException("Failed to parse bitcode");
        }
        
        // Get global context and create wrapper
        LLVMContextRef global_ctx = LLVMGetGlobalContext();
        static thread_local auto global_token = std::make_shared<ValidityToken>();
        return new LLVMModuleWrapper(mod, global_ctx, global_token);
      },
      nb::rv_policy::take_ownership, "membuf"_a, 
      R"(Parse bitcode from memory buffer using global context (uses diagnostic handler).)");

  // ==========================================================================
  // Debug Info Builder
  // ==========================================================================
  
  nb::class_<LLVMDIBuilderWrapper>(m, "DIBuilder")
      .def("finalize", &LLVMDIBuilderWrapper::finalize,
           R"(Finalize the debug info builder.)");
  
  m.def(
      "create_dibuilder",
      [](LLVMModuleWrapper &mod) -> LLVMDIBuilderWrapper * {
        mod.check_valid();
        return new LLVMDIBuilderWrapper(mod.m_ref, mod.m_token);
      },
      nb::rv_policy::take_ownership, "mod"_a,
      R"(Create a debug info builder for a module.)");
  
  nb::class_<LLVMMetadataWrapper>(m, "Metadata");
  
  m.def(
      "md_string_in_context_2",
      [](LLVMContextWrapper &ctx, const std::string &str) -> LLVMMetadataWrapper {
        ctx.check_valid();
        return LLVMMetadataWrapper(
            LLVMMDStringInContext2(ctx.m_ref, str.c_str(), str.size()),
            ctx.m_token);
      },
      "ctx"_a, "str"_a,
      R"(Create metadata string in context (returns LLVMMetadataRef).)");
  
  m.def(
      "md_node_in_context_2",
      [](LLVMContextWrapper &ctx, const std::vector<LLVMMetadataWrapper> &mds) -> LLVMMetadataWrapper {
        ctx.check_valid();
        std::vector<LLVMMetadataRef> refs;
        refs.reserve(mds.size());
        for (const auto &md : mds) {
          md.check_valid();
          refs.push_back(md.m_ref);
        }
        return LLVMMetadataWrapper(
            LLVMMDNodeInContext2(ctx.m_ref, refs.data(), refs.size()),
            ctx.m_token);
      },
      "ctx"_a, "mds"_a,
      R"(Create metadata node in context from metadata refs.)");
  
  m.def(
      "get_di_node_tag",
      [](const LLVMMetadataWrapper &md) -> unsigned {
        md.check_valid();
        return LLVMGetDINodeTag(md.m_ref);
      },
      "md"_a,
      R"(Get DWARF tag from debug info node.)");
  
  m.def(
      "dibuilder_create_file",
      [](LLVMDIBuilderWrapper &dib, const std::string &filename,
         const std::string &directory) -> LLVMMetadataWrapper {
        dib.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateFile(dib.m_ref, filename.c_str(), filename.size(),
                                   directory.c_str(), directory.size()),
            dib.m_module_token);
      },
      "dib"_a, "filename"_a, "directory"_a,
      R"(Create file debug info metadata.)");
  
  m.def(
      "dibuilder_create_struct_type",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, const LLVMMetadataWrapper &file,
         unsigned line_number, uint64_t size_in_bits, uint32_t align_in_bits,
         unsigned flags) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateStructType(
                dib.m_ref, scope.m_ref, name.c_str(), name.size(),
                file.m_ref, line_number, size_in_bits, align_in_bits,
                (LLVMDIFlags)flags, nullptr, nullptr, 0, 0, nullptr,
                nullptr, 0),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "file"_a, "line_number"_a,
      "size_in_bits"_a, "align_in_bits"_a, "flags"_a,
      R"(Create struct type debug info metadata.)");
  
  m.def(
      "di_type_get_name",
      [](const LLVMMetadataWrapper &di_type) -> std::string {
        di_type.check_valid();
        size_t len;
        const char *name = LLVMDITypeGetName(di_type.m_ref, &len);
        return std::string(name, len);
      },
      "di_type"_a,
      R"(Get name from debug info type.)");
  
  // DIBuilder - Compile Unit & Module
  m.def(
      "dibuilder_create_compile_unit",
      [](LLVMDIBuilderWrapper &dib, int lang, const LLVMMetadataWrapper &file,
         const std::string &producer, bool is_optimized, const std::string &flags,
         unsigned runtime_ver, const std::string &split_name,
         unsigned kind, unsigned dwo_id, bool split_debug_inlining,
         bool debug_info_for_profiling, const std::string &sys_root,
         const std::string &sdk) -> LLVMMetadataWrapper {
        dib.check_valid();
        file.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateCompileUnit(
                dib.m_ref, (LLVMDWARFSourceLanguage)lang, file.m_ref, producer.c_str(), producer.size(),
                is_optimized, flags.c_str(), flags.size(), runtime_ver,
                split_name.c_str(), split_name.size(), (LLVMDWARFEmissionKind)kind,
                dwo_id, split_debug_inlining, debug_info_for_profiling,
                sys_root.c_str(), sys_root.size(), sdk.c_str(), sdk.size()),
            dib.m_module_token);
      },
      "dib"_a, "lang"_a, "file"_a, "producer"_a, "is_optimized"_a, "flags"_a,
      "runtime_ver"_a, "split_name"_a, "kind"_a, "dwo_id"_a,
      "split_debug_inlining"_a, "debug_info_for_profiling"_a,
      "sys_root"_a, "sdk"_a,
      R"(Create compile unit debug info.)");
  
  m.def(
      "dibuilder_create_module",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &parent_scope,
         const std::string &name, const std::string &config_macros,
         const std::string &include_path, const std::string &api_notes_file)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        parent_scope.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateModule(
                dib.m_ref, parent_scope.m_ref, name.c_str(), name.size(),
                config_macros.c_str(), config_macros.size(),
                include_path.c_str(), include_path.size(),
                api_notes_file.c_str(), api_notes_file.size()),
            dib.m_module_token);
      },
      "dib"_a, "parent_scope"_a, "name"_a, "config_macros"_a,
      "include_path"_a, "api_notes_file"_a,
      R"(Create module debug info.)");
  
  m.def(
      "dibuilder_create_namespace",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &parent_scope,
         const std::string &name, bool export_symbols) -> LLVMMetadataWrapper {
        dib.check_valid();
        parent_scope.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateNameSpace(dib.m_ref, parent_scope.m_ref,
                                        name.c_str(), name.size(), export_symbols),
            dib.m_module_token);
      },
      "dib"_a, "parent_scope"_a, "name"_a, "export_symbols"_a,
      R"(Create namespace debug info.)");
  
  m.def(
      "dibuilder_create_function",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, const std::string &linkage_name,
         const LLVMMetadataWrapper &file, unsigned line_no,
         const LLVMMetadataWrapper *subroutine_type, bool is_local_to_unit,
         bool is_definition, unsigned scope_line, unsigned flags,
         bool is_optimized) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        LLVMMetadataRef type = subroutine_type ? subroutine_type->m_ref : nullptr;
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateFunction(
                dib.m_ref, scope.m_ref, name.c_str(), name.size(),
                linkage_name.c_str(), linkage_name.size(), file.m_ref, line_no,
                type, is_local_to_unit, is_definition, scope_line,
                (LLVMDIFlags)flags, is_optimized),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "linkage_name"_a, "file"_a, "line_no"_a,
      "subroutine_type"_a.none(), "is_local_to_unit"_a, "is_definition"_a,
      "scope_line"_a, "flags"_a, "is_optimized"_a,
      R"(Create function debug info.)");
  
  // DIBuilder - Type Creation
  m.def(
      "dibuilder_create_basic_type",
      [](LLVMDIBuilderWrapper &dib, const std::string &name,
         uint64_t size_in_bits, unsigned encoding, unsigned flags)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateBasicType(dib.m_ref, name.c_str(), name.size(),
                                        size_in_bits, encoding, (LLVMDIFlags)flags),
            dib.m_module_token);
      },
      "dib"_a, "name"_a, "size_in_bits"_a, "encoding"_a, "flags"_a,
      R"(Create basic type debug info.)");
  
  m.def(
      "dibuilder_create_pointer_type",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &pointee_type,
         uint64_t size_in_bits, uint32_t align_in_bits, unsigned address_space,
         const std::string &name) -> LLVMMetadataWrapper {
        dib.check_valid();
        pointee_type.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreatePointerType(dib.m_ref, pointee_type.m_ref,
                                          size_in_bits, align_in_bits,
                                          address_space, name.c_str(), name.size()),
            dib.m_module_token);
      },
      "dib"_a, "pointee_type"_a, "size_in_bits"_a, "align_in_bits"_a,
      "address_space"_a, "name"_a,
      R"(Create pointer type debug info.)");
  
  m.def(
      "dibuilder_create_subroutine_type",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &file,
         const std::vector<LLVMMetadataWrapper> &param_types, unsigned flags)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        file.check_valid();
        std::vector<LLVMMetadataRef> param_refs;
        param_refs.reserve(param_types.size());
        for (const auto &p : param_types) {
          p.check_valid();
          param_refs.push_back(p.m_ref);
        }
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateSubroutineType(
                dib.m_ref, file.m_ref, param_refs.data(), param_refs.size(),
                (LLVMDIFlags)flags),
            dib.m_module_token);
      },
      "dib"_a, "file"_a, "param_types"_a, "flags"_a,
      R"(Create subroutine type debug info.)");
  
  m.def(
      "dibuilder_create_vector_type",
      [](LLVMDIBuilderWrapper &dib, uint64_t size_in_bits, uint32_t align_in_bits,
         const LLVMMetadataWrapper &element_type,
         const std::vector<LLVMMetadataWrapper> &subscripts) -> LLVMMetadataWrapper {
        dib.check_valid();
        element_type.check_valid();
        std::vector<LLVMMetadataRef> sub_refs;
        sub_refs.reserve(subscripts.size());
        for (const auto &s : subscripts) {
          s.check_valid();
          sub_refs.push_back(s.m_ref);
        }
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateVectorType(dib.m_ref, size_in_bits, align_in_bits,
                                         element_type.m_ref, sub_refs.data(),
                                         sub_refs.size()),
            dib.m_module_token);
      },
      "dib"_a, "size_in_bits"_a, "align_in_bits"_a, "element_type"_a, "subscripts"_a,
      R"(Create vector type debug info.)");
  
  m.def(
      "dibuilder_create_typedef",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &type,
         const std::string &name, const LLVMMetadataWrapper &file,
         unsigned line_no, const LLVMMetadataWrapper &scope,
         uint32_t align_in_bits) -> LLVMMetadataWrapper {
        dib.check_valid();
        type.check_valid();
        file.check_valid();
        scope.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateTypedef(dib.m_ref, type.m_ref, name.c_str(),
                                      name.size(), file.m_ref, line_no,
                                      scope.m_ref, align_in_bits),
            dib.m_module_token);
      },
      "dib"_a, "type"_a, "name"_a, "file"_a, "line_no"_a, "scope"_a, "align_in_bits"_a,
      R"(Create typedef debug info.)");
  
  // DIBuilder - Variables
  m.def(
      "dibuilder_create_parameter_variable",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, unsigned arg_no, const LLVMMetadataWrapper &file,
         unsigned line_no, const LLVMMetadataWrapper &type, bool always_preserve,
         unsigned flags) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        type.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateParameterVariable(
                dib.m_ref, scope.m_ref, name.c_str(), name.size(), arg_no,
                file.m_ref, line_no, type.m_ref, always_preserve, (LLVMDIFlags)flags),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "arg_no"_a, "file"_a, "line_no"_a,
      "type"_a, "always_preserve"_a, "flags"_a,
      R"(Create parameter variable debug info.)");
  
  m.def(
      "dibuilder_create_auto_variable",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, const LLVMMetadataWrapper &file,
         unsigned line_no, const LLVMMetadataWrapper &type, bool always_preserve,
         unsigned flags, uint32_t align_in_bits) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        type.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateAutoVariable(
                dib.m_ref, scope.m_ref, name.c_str(), name.size(), file.m_ref,
                line_no, type.m_ref, always_preserve, (LLVMDIFlags)flags,
                align_in_bits),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "file"_a, "line_no"_a, "type"_a,
      "always_preserve"_a, "flags"_a, "align_in_bits"_a,
      R"(Create auto variable debug info.)");
  
  m.def(
      "dibuilder_create_global_variable_expression",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, const std::string &linkage,
         const LLVMMetadataWrapper &file, unsigned line_no,
         const LLVMMetadataWrapper &type, bool is_local_to_unit,
         const LLVMMetadataWrapper &expr, const LLVMMetadataWrapper *decl,
         uint32_t align_in_bits) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        type.check_valid();
        expr.check_valid();
        LLVMMetadataRef decl_ref = decl ? decl->m_ref : nullptr;
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateGlobalVariableExpression(
                dib.m_ref, scope.m_ref, name.c_str(), name.size(),
                linkage.c_str(), linkage.size(), file.m_ref, line_no,
                type.m_ref, is_local_to_unit, expr.m_ref, decl_ref, align_in_bits),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "linkage"_a, "file"_a, "line_no"_a,
      "type"_a, "is_local_to_unit"_a, "expr"_a, "decl"_a.none(), "align_in_bits"_a,
      R"(Create global variable expression debug info.)");
  
  // DIBuilder - Expressions and Locations
  m.def(
      "dibuilder_create_expression",
      [](LLVMDIBuilderWrapper &dib, const std::vector<uint64_t> &addr)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        // Need to copy to non-const for LLVM C API
        std::vector<uint64_t> addr_copy = addr;
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateExpression(dib.m_ref, addr_copy.data(), addr_copy.size()),
            dib.m_module_token);
      },
      "dib"_a, "addr"_a,
      R"(Create debug info expression.)");
  
  m.def(
      "dibuilder_create_constant_value_expression",
      [](LLVMDIBuilderWrapper &dib, uint64_t value) -> LLVMMetadataWrapper {
        dib.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateConstantValueExpression(dib.m_ref, value),
            dib.m_module_token);
      },
      "dib"_a, "value"_a,
      R"(Create constant value expression.)");
  
  m.def(
      "dibuilder_create_debug_location",
      [](LLVMContextWrapper &ctx, unsigned line, unsigned column,
         const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper *inlined_at)
          -> LLVMMetadataWrapper {
        ctx.check_valid();
        scope.check_valid();
        LLVMMetadataRef inlined = inlined_at ? inlined_at->m_ref : nullptr;
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateDebugLocation(ctx.m_ref, line, column,
                                            scope.m_ref, inlined),
            ctx.m_token);
      },
      "ctx"_a, "line"_a, "column"_a, "scope"_a, "inlined_at"_a.none(),
      R"(Create debug location.)");
  
  // DIBuilder - Lexical Blocks and Labels
  m.def(
      "dibuilder_create_lexical_block",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const LLVMMetadataWrapper &file, unsigned line, unsigned column)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateLexicalBlock(dib.m_ref, scope.m_ref, file.m_ref,
                                           line, column),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "file"_a, "line"_a, "column"_a,
      R"(Create lexical block debug info.)");
  
  m.def(
      "dibuilder_create_label",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, const LLVMMetadataWrapper &file,
         unsigned line_no, bool always_preserve) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateLabel(dib.m_ref, scope.m_ref, name.c_str(),
                                    name.size(), file.m_ref, line_no,
                                    always_preserve),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "file"_a, "line_no"_a, "always_preserve"_a,
      R"(Create label debug info.)");
  
  m.def(
      "dibuilder_insert_declare_record_at_end",
      [](LLVMDIBuilderWrapper &dib, LLVMValueWrapper &storage,
         const LLVMMetadataWrapper &var_info, const LLVMMetadataWrapper &expr,
         const LLVMMetadataWrapper &debug_loc, LLVMBasicBlockWrapper &block) {
        dib.check_valid();
        storage.check_valid();
        var_info.check_valid();
        expr.check_valid();
        debug_loc.check_valid();
        block.check_valid();
        LLVMDIBuilderInsertDeclareRecordAtEnd(dib.m_ref, storage.m_ref,
                                             var_info.m_ref, expr.m_ref,
                                             debug_loc.m_ref, block.m_ref);
      },
      "dib"_a, "storage"_a, "var_info"_a, "expr"_a, "debug_loc"_a, "block"_a,
      R"(Insert declare record at end of block.)");
  
  m.def(
      "dibuilder_insert_dbg_value_record_at_end",
      [](LLVMDIBuilderWrapper &dib, LLVMValueWrapper &val,
         const LLVMMetadataWrapper &var_info, const LLVMMetadataWrapper &expr,
         const LLVMMetadataWrapper &debug_loc, LLVMBasicBlockWrapper &block) {
        dib.check_valid();
        val.check_valid();
        var_info.check_valid();
        expr.check_valid();
        debug_loc.check_valid();
        block.check_valid();
        LLVMDIBuilderInsertDbgValueRecordAtEnd(dib.m_ref, val.m_ref,
                                              var_info.m_ref, expr.m_ref,
                                              debug_loc.m_ref, block.m_ref);
      },
      "dib"_a, "val"_a, "var_info"_a, "expr"_a, "debug_loc"_a, "block"_a,
      R"(Insert dbg value record at end of block.)");
  
  m.def(
      "dibuilder_insert_label_at_end",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &label_info,
         const LLVMMetadataWrapper &debug_loc, LLVMBasicBlockWrapper &block) {
        dib.check_valid();
        label_info.check_valid();
        debug_loc.check_valid();
        block.check_valid();
        LLVMDIBuilderInsertLabelAtEnd(dib.m_ref, label_info.m_ref,
                                     debug_loc.m_ref, block.m_ref);
      },
      "dib"_a, "label_info"_a, "debug_loc"_a, "block"_a,
      R"(Insert label at end of block.)");
  
  // Additional DIBuilder APIs needed for test
  m.def(
      "dibuilder_get_or_create_subrange",
      [](LLVMDIBuilderWrapper &dib, int64_t lo, int64_t count) -> LLVMMetadataWrapper {
        dib.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderGetOrCreateSubrange(dib.m_ref, lo, count),
            dib.m_module_token);
      },
      "dib"_a, "lo"_a, "count"_a,
      R"(Get or create subrange.)");
  
  m.def(
      "dibuilder_get_or_create_array",
      [](LLVMDIBuilderWrapper &dib, const std::vector<LLVMMetadataWrapper> &elements)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        std::vector<LLVMMetadataRef> elem_refs;
        elem_refs.reserve(elements.size());
        for (const auto &e : elements) {
          e.check_valid();
          elem_refs.push_back(e.m_ref);
        }
        return LLVMMetadataWrapper(
            LLVMDIBuilderGetOrCreateArray(dib.m_ref, elem_refs.data(),
                                         elem_refs.size()),
            dib.m_module_token);
      },
      "dib"_a, "elements"_a,
      R"(Get or create array of metadata.)");
  
  m.def(
      "metadata_as_value",
      [](LLVMContextWrapper &ctx, const LLVMMetadataWrapper &md) -> LLVMValueWrapper {
        ctx.check_valid();
        md.check_valid();
        return LLVMValueWrapper(LLVMMetadataAsValue(ctx.m_ref, md.m_ref), ctx.m_token);
      },
      "ctx"_a, "md"_a,
      R"(Convert metadata to value.)");
  
  m.def(
      "value_as_metadata",
      [](const LLVMValueWrapper &val) -> LLVMMetadataWrapper {
        val.check_valid();
        return LLVMMetadataWrapper(LLVMValueAsMetadata(val.m_ref),
                                  val.m_context_token);
      },
      "val"_a,
      R"(Convert value to metadata.)");
  
  m.def(
      "set_subprogram",
      [](LLVMFunctionWrapper &func, const LLVMMetadataWrapper &sp) {
        func.check_valid();
        sp.check_valid();
        LLVMSetSubprogram(func.m_ref, sp.m_ref);
      },
      "func"_a, "sp"_a,
      R"(Set subprogram metadata for function.)");
  
  // More complex type creation
  m.def(
      "dibuilder_create_objc_property",
      [](LLVMDIBuilderWrapper &dib, const std::string &name,
         const LLVMMetadataWrapper &file, unsigned line_no,
         const std::string &getter_name, const std::string &setter_name,
         unsigned property_attributes, const LLVMMetadataWrapper &type)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        file.check_valid();
        type.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateObjCProperty(
                dib.m_ref, name.c_str(), name.size(), file.m_ref, line_no,
                getter_name.c_str(), getter_name.size(),
                setter_name.c_str(), setter_name.size(),
                property_attributes, type.m_ref),
            dib.m_module_token);
      },
      "dib"_a, "name"_a, "file"_a, "line_no"_a, "getter_name"_a,
      "setter_name"_a, "property_attributes"_a, "type"_a,
      R"(Create ObjC property debug info.)");
  
  m.def(
      "dibuilder_create_objc_ivar",
      [](LLVMDIBuilderWrapper &dib, const std::string &name,
         const LLVMMetadataWrapper &file, unsigned line_no, uint64_t size_in_bits,
         uint32_t align_in_bits, uint64_t offset_in_bits, unsigned flags,
         const LLVMMetadataWrapper &type, const LLVMMetadataWrapper &property)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        file.check_valid();
        type.check_valid();
        property.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateObjCIVar(
                dib.m_ref, name.c_str(), name.size(), file.m_ref, line_no,
                size_in_bits, align_in_bits, offset_in_bits, (LLVMDIFlags)flags,
                type.m_ref, property.m_ref),
            dib.m_module_token);
      },
      "dib"_a, "name"_a, "file"_a, "line_no"_a, "size_in_bits"_a,
      "align_in_bits"_a, "offset_in_bits"_a, "flags"_a, "type"_a, "property"_a,
      R"(Create ObjC ivar debug info.)");
  
  m.def(
      "dibuilder_create_inheritance",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &derived_type,
         const LLVMMetadataWrapper &base_type, uint64_t offset_in_bits,
         uint32_t v_bptr_offset, unsigned flags) -> LLVMMetadataWrapper {
        dib.check_valid();
        derived_type.check_valid();
        base_type.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateInheritance(dib.m_ref, derived_type.m_ref,
                                          base_type.m_ref, offset_in_bits,
                                          v_bptr_offset, (LLVMDIFlags)flags),
            dib.m_module_token);
      },
      "dib"_a, "derived_type"_a, "base_type"_a, "offset_in_bits"_a,
      "v_bptr_offset"_a, "flags"_a,
      R"(Create inheritance debug info.)");
  
  // Enums and other types (simplified signatures for now)
  m.def(
      "dibuilder_create_enumeration_type",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, const LLVMMetadataWrapper &file,
         unsigned line_number, uint64_t size_in_bits, uint32_t align_in_bits,
         const std::vector<LLVMMetadataWrapper> &elements,
         const LLVMMetadataWrapper &underlying_type) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        underlying_type.check_valid();
        std::vector<LLVMMetadataRef> elem_refs;
        elem_refs.reserve(elements.size());
        for (const auto &e : elements) {
          e.check_valid();
          elem_refs.push_back(e.m_ref);
        }
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateEnumerationType(
                dib.m_ref, scope.m_ref, name.c_str(), name.size(), file.m_ref,
                line_number, size_in_bits, align_in_bits, elem_refs.data(),
                elem_refs.size(), underlying_type.m_ref),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "file"_a, "line_number"_a, "size_in_bits"_a,
      "align_in_bits"_a, "elements"_a, "underlying_type"_a,
      R"(Create enumeration type debug info.)");
  
  m.def(
      "dibuilder_create_enumerator",
      [](LLVMDIBuilderWrapper &dib, const std::string &name, int64_t value,
         bool is_unsigned) -> LLVMMetadataWrapper {
        dib.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateEnumerator(dib.m_ref, name.c_str(), name.size(),
                                         value, is_unsigned),
            dib.m_module_token);
      },
      "dib"_a, "name"_a, "value"_a, "is_unsigned"_a,
      R"(Create enumerator debug info.)");
  
  m.def(
      "dibuilder_create_enumerator_of_arbitrary_precision",
      [](LLVMDIBuilderWrapper &dib, const std::string &name,
         const std::vector<uint64_t> &value, bool is_unsigned) -> LLVMMetadataWrapper {
        dib.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateEnumeratorOfArbitraryPrecision(
                dib.m_ref, name.c_str(), name.size(), value.size() * 64,
                value.data(), is_unsigned),
            dib.m_module_token);
      },
      "dib"_a, "name"_a, "value"_a, "is_unsigned"_a,
      R"(Create enumerator with arbitrary precision.)");
  
  // Advanced type creation
  m.def(
      "dibuilder_create_forward_decl",
      [](LLVMDIBuilderWrapper &dib, unsigned tag, const std::string &name,
         const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper &file,
         unsigned line, unsigned runtime_lang, uint64_t size_in_bits,
         uint32_t align_in_bits, const std::string &unique_id) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateForwardDecl(
                dib.m_ref, tag, name.c_str(), name.size(), scope.m_ref,
                file.m_ref, line, runtime_lang, size_in_bits, align_in_bits,
                unique_id.c_str(), unique_id.size()),
            dib.m_module_token);
      },
      "dib"_a, "tag"_a, "name"_a, "scope"_a, "file"_a, "line"_a,
      "runtime_lang"_a, "size_in_bits"_a, "align_in_bits"_a, "unique_id"_a,
      R"(Create forward declaration.)");
  
  m.def(
      "dibuilder_create_replaceable_composite_type",
      [](LLVMDIBuilderWrapper &dib, unsigned tag, const std::string &name,
         const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper &file,
         unsigned line, unsigned runtime_lang, uint64_t size_in_bits,
         uint32_t align_in_bits, unsigned flags, const std::string &unique_id)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateReplaceableCompositeType(
                dib.m_ref, tag, name.c_str(), name.size(), scope.m_ref,
                file.m_ref, line, runtime_lang, size_in_bits, align_in_bits,
                (LLVMDIFlags)flags, unique_id.c_str(), unique_id.size()),
            dib.m_module_token);
      },
      "dib"_a, "tag"_a, "name"_a, "scope"_a, "file"_a, "line"_a,
      "runtime_lang"_a, "size_in_bits"_a, "align_in_bits"_a, "flags"_a, "unique_id"_a,
      R"(Create replaceable composite type.)");
  
  m.def(
      "dibuilder_create_subrange_type",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, unsigned line, const LLVMMetadataWrapper &file,
         uint64_t size_in_bits, uint32_t align_in_bits, unsigned flags,
         const LLVMMetadataWrapper &element_type,
         const LLVMMetadataWrapper *lower_bound,
         const LLVMMetadataWrapper *upper_bound,
         const LLVMMetadataWrapper *stride,
         const LLVMMetadataWrapper *bias) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        element_type.check_valid();
        LLVMMetadataRef lb = lower_bound ? lower_bound->m_ref : nullptr;
        LLVMMetadataRef ub = upper_bound ? upper_bound->m_ref : nullptr;
        LLVMMetadataRef st = stride ? stride->m_ref : nullptr;
        LLVMMetadataRef bi = bias ? bias->m_ref : nullptr;
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateSubrangeType(
                dib.m_ref, scope.m_ref, name.c_str(), name.size(), line,
                file.m_ref, size_in_bits, align_in_bits, (LLVMDIFlags)flags,
                element_type.m_ref, lb, ub, st, bi),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "line"_a, "file"_a, "size_in_bits"_a,
      "align_in_bits"_a, "flags"_a, "element_type"_a, "lower_bound"_a.none(),
      "upper_bound"_a.none(), "stride"_a.none(), "bias"_a.none(),
      R"(Create subrange type with metadata bounds.)");
  
  m.def(
      "dibuilder_create_set_type",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, const LLVMMetadataWrapper &file, unsigned line,
         uint64_t size_in_bits, uint32_t align_in_bits,
         const LLVMMetadataWrapper &base_type) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        base_type.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateSetType(dib.m_ref, scope.m_ref, name.c_str(),
                                      name.size(), file.m_ref, line,
                                      size_in_bits, align_in_bits, base_type.m_ref),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "file"_a, "line"_a, "size_in_bits"_a,
      "align_in_bits"_a, "base_type"_a,
      R"(Create set type.)");
  
  m.def(
      "dibuilder_create_dynamic_array_type",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const std::string &name, unsigned line, const LLVMMetadataWrapper &file,
         uint64_t size_in_bits, uint32_t align_in_bits,
         const LLVMMetadataWrapper &element_type,
         const std::vector<LLVMMetadataWrapper> &subscripts,
         const LLVMMetadataWrapper &data_location,
         const LLVMMetadataWrapper *associated,
         const LLVMMetadataWrapper *allocated,
         const LLVMMetadataWrapper *rank,
         const LLVMMetadataWrapper *bit_stride) -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        file.check_valid();
        element_type.check_valid();
        data_location.check_valid();
        std::vector<LLVMMetadataRef> sub_refs;
        sub_refs.reserve(subscripts.size());
        for (const auto &s : subscripts) {
          s.check_valid();
          sub_refs.push_back(s.m_ref);
        }
        LLVMMetadataRef assoc = associated ? associated->m_ref : nullptr;
        LLVMMetadataRef alloc = allocated ? allocated->m_ref : nullptr;
        LLVMMetadataRef rnk = rank ? rank->m_ref : nullptr;
        LLVMMetadataRef stride = bit_stride ? bit_stride->m_ref : nullptr;
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateDynamicArrayType(
                dib.m_ref, scope.m_ref, name.c_str(), name.size(), line,
                file.m_ref, size_in_bits, align_in_bits, element_type.m_ref,
                sub_refs.data(), sub_refs.size(), data_location.m_ref,
                assoc, alloc, rnk, stride),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "name"_a, "line"_a, "file"_a, "size_in_bits"_a,
      "align_in_bits"_a, "element_type"_a, "subscripts"_a, "data_location"_a,
      "associated"_a.none(), "allocated"_a.none(), "rank"_a.none(), "bit_stride"_a.none(),
      R"(Create dynamic array type.)");
  
  // Module imports
  m.def(
      "dibuilder_create_imported_module_from_module",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const LLVMMetadataWrapper &import_module, const LLVMMetadataWrapper &file,
         unsigned line, const std::vector<LLVMMetadataWrapper> &elements)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        import_module.check_valid();
        file.check_valid();
        std::vector<LLVMMetadataRef> elem_refs;
        elem_refs.reserve(elements.size());
        for (const auto &e : elements) {
          e.check_valid();
          elem_refs.push_back(e.m_ref);
        }
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateImportedModuleFromModule(
                dib.m_ref, scope.m_ref, import_module.m_ref, file.m_ref, line,
                elem_refs.data(), elem_refs.size()),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "import_module"_a, "file"_a, "line"_a, "elements"_a,
      R"(Create imported module from module.)");
  
  m.def(
      "dibuilder_create_imported_module_from_alias",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &scope,
         const LLVMMetadataWrapper &imported_entity, const LLVMMetadataWrapper &file,
         unsigned line, const std::vector<LLVMMetadataWrapper> &elements)
          -> LLVMMetadataWrapper {
        dib.check_valid();
        scope.check_valid();
        imported_entity.check_valid();
        file.check_valid();
        std::vector<LLVMMetadataRef> elem_refs;
        elem_refs.reserve(elements.size());
        for (const auto &e : elements) {
          e.check_valid();
          elem_refs.push_back(e.m_ref);
        }
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateImportedModuleFromAlias(
                dib.m_ref, scope.m_ref, imported_entity.m_ref, file.m_ref, line,
                elem_refs.data(), elem_refs.size()),
            dib.m_module_token);
      },
      "dib"_a, "scope"_a, "imported_entity"_a, "file"_a, "line"_a, "elements"_a,
      R"(Create imported module from alias.)");
  
  // Macros
  m.def(
      "dibuilder_create_temp_macro_file",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper *parent_macro_file,
         unsigned line, const LLVMMetadataWrapper &file) -> LLVMMetadataWrapper {
        dib.check_valid();
        file.check_valid();
        LLVMMetadataRef parent = parent_macro_file ? parent_macro_file->m_ref : nullptr;
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateTempMacroFile(dib.m_ref, parent, line, file.m_ref),
            dib.m_module_token);
      },
      "dib"_a, "parent_macro_file"_a.none(), "line"_a, "file"_a,
      R"(Create temporary macro file.)");
  
  m.def(
      "dibuilder_create_macro",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &parent_macro_file,
         unsigned line, unsigned macro_type, const std::string &name,
         const std::string &value) -> LLVMMetadataWrapper {
        dib.check_valid();
        parent_macro_file.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIBuilderCreateMacro(dib.m_ref, parent_macro_file.m_ref, line,
                                    (LLVMDWARFMacinfoRecordType)macro_type,
                                    name.c_str(), name.size(),
                                    value.c_str(), value.size()),
            dib.m_module_token);
      },
      "dib"_a, "parent_macro_file"_a, "line"_a, "macro_type"_a, "name"_a, "value"_a,
      R"(Create macro.)");
  
  // Label insertion
  m.def(
      "dibuilder_insert_label_before",
      [](LLVMDIBuilderWrapper &dib, const LLVMMetadataWrapper &label_info,
         const LLVMMetadataWrapper &debug_loc, LLVMValueWrapper &insert_before) {
        dib.check_valid();
        label_info.check_valid();
        debug_loc.check_valid();
        insert_before.check_valid();
        LLVMDIBuilderInsertLabelBefore(dib.m_ref, label_info.m_ref,
                                      debug_loc.m_ref, insert_before.m_ref);
      },
      "dib"_a, "label_info"_a, "debug_loc"_a, "insert_before"_a,
      R"(Insert label before instruction.)");
  
  // Metadata operations
  m.def(
      "metadata_replace_all_uses_with",
      [](LLVMMetadataWrapper &temp_md, const LLVMMetadataWrapper &md) {
        temp_md.check_valid();
        md.check_valid();
        LLVMMetadataReplaceAllUsesWith(temp_md.m_ref, md.m_ref);
      },
      "temp_md"_a, "md"_a,
      R"(Replace all uses of temporary metadata.)");
  
  m.def(
      "di_subprogram_replace_type",
      [](const LLVMMetadataWrapper &subprogram, const LLVMMetadataWrapper &type) {
        subprogram.check_valid();
        type.check_valid();
        LLVMDISubprogramReplaceType(subprogram.m_ref, type.m_ref);
      },
      "subprogram"_a, "type"_a,
      R"(Replace subprogram type.)");
  
  m.def(
      "replace_arrays",
      [](LLVMDIBuilderWrapper &dib, std::vector<LLVMMetadataWrapper> &composite_types,
         std::vector<LLVMMetadataWrapper> &arrays) {
        dib.check_valid();
        if (composite_types.size() != 1 || arrays.size() != 1) {
          throw std::invalid_argument("Currently only supports single composite type and array");
        }
        composite_types[0].check_valid();
        arrays[0].check_valid();
        LLVMMetadataRef ct_ref = composite_types[0].m_ref;
        LLVMMetadataRef ar_ref = arrays[0].m_ref;
        LLVMReplaceArrays(dib.m_ref, &ct_ref, &ar_ref, 1);
      },
      "dib"_a, "composite_types"_a, "arrays"_a,
      R"(Replace arrays in composite type.)");
  
  // ==========================================================================
  // Builder Positioning and Debug Records
  // ==========================================================================
  
  m.def(
      "set_is_new_dbg_info_format",
      [](LLVMModuleWrapper &mod, bool use_new_format) {
        mod.check_valid();
        LLVMSetIsNewDbgInfoFormat(mod.m_ref, use_new_format);
      },
      "mod"_a, "use_new_format"_a,
      R"(Set whether to use new debug info format.)");
  
  m.def(
      "is_new_dbg_info_format",
      [](const LLVMModuleWrapper &mod) -> bool {
        mod.check_valid();
        return LLVMIsNewDbgInfoFormat(mod.m_ref);
      },
      "mod"_a,
      R"(Check if using new debug info format.)");
  
  m.def(
      "position_builder_before_instr_and_dbg_records",
      [](LLVMBuilderWrapper &builder, LLVMValueWrapper &instr) {
        builder.check_valid();
        instr.check_valid();
        LLVMPositionBuilderBeforeInstrAndDbgRecords(builder.m_ref, instr.m_ref);
      },
      "builder"_a, "instr"_a,
      R"(Position builder before instruction and debug records.)");
  
  m.def(
      "position_builder_before_dbg_records",
      [](LLVMBuilderWrapper &builder, LLVMBasicBlockWrapper &block,
         LLVMValueWrapper &instr) {
        builder.check_valid();
        block.check_valid();
        instr.check_valid();
        LLVMPositionBuilderBeforeDbgRecords(builder.m_ref, block.m_ref, instr.m_ref);
      },
      "builder"_a, "block"_a, "instr"_a,
      R"(Position builder before debug records.)");
  
  // Debug record iteration (opaque DbgRecord type - no wrapper needed)
  m.def(
      "get_first_dbg_record",
      [](const LLVMValueWrapper &instr) -> void * {
        instr.check_valid();
        return LLVMGetFirstDbgRecord(instr.m_ref);
      },
      "instr"_a,
      R"(Get first debug record attached to instruction.)");
  
  m.def(
      "get_last_dbg_record",
      [](const LLVMValueWrapper &instr) -> void * {
        instr.check_valid();
        return LLVMGetLastDbgRecord(instr.m_ref);
      },
      "instr"_a,
      R"(Get last debug record attached to instruction.)");
  
  m.def(
      "get_next_dbg_record",
      [](void *dbg_record) -> void * {
        return LLVMGetNextDbgRecord((LLVMDbgRecordRef)dbg_record);
      },
      "dbg_record"_a,
      R"(Get next debug record.)");
  
  m.def(
      "get_previous_dbg_record",
      [](void *dbg_record) -> void * {
        return LLVMGetPreviousDbgRecord((LLVMDbgRecordRef)dbg_record);
      },
      "dbg_record"_a,
      R"(Get previous debug record.)");
  
  // Constants for DIFlags
  m.attr("DIFlagZero") = nb::int_((unsigned)LLVMDIFlagZero);
  m.attr("DIFlagPrivate") = nb::int_((unsigned)LLVMDIFlagPrivate);
  m.attr("DIFlagProtected") = nb::int_((unsigned)LLVMDIFlagProtected);
  m.attr("DIFlagPublic") = nb::int_((unsigned)LLVMDIFlagPublic);
  m.attr("DIFlagFwdDecl") = nb::int_((unsigned)LLVMDIFlagFwdDecl);
  m.attr("DIFlagObjcClassComplete") = nb::int_((unsigned)LLVMDIFlagObjcClassComplete);
  
  // Constants for DWARF
  m.attr("DWARFSourceLanguageC") = nb::int_((unsigned)LLVMDWARFSourceLanguageC);
  m.attr("DWARFEmissionFull") = nb::int_((unsigned)LLVMDWARFEmissionFull);
  m.attr("DWARFMacinfoRecordTypeDefine") = nb::int_((unsigned)LLVMDWARFMacinfoRecordTypeDefine);
}
