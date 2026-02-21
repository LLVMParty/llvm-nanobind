#include <nanobind/nanobind.h>
#include <nanobind/stl/filesystem.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/pair.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <atomic>
#include <cctype>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

#include <llvm-c/Analysis.h>
#include <llvm-c/BitReader.h>
#include <llvm-c/BitWriter.h>
#include <llvm-c/Comdat.h>
#include <llvm-c/Core.h>
#include <llvm-c/DebugInfo.h>
#include <llvm-c/Disassembler.h>
#include <llvm-c/IRReader.h>
#include <llvm-c/Linker.h>
#include <llvm-c/Object.h>
#include <llvm-c/Support.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/PassBuilder.h>

namespace nb = nanobind;
namespace fs = std::filesystem;
using namespace nb::literals;

// =============================================================================
// Exceptions
// =============================================================================

struct LLVMError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct LLVMMemoryError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

struct LLVMAssertionError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

// =============================================================================
// Diagnostic Information
// =============================================================================

struct Diagnostic {
  std::string severity;
  std::string message;
  std::optional<int> line;
  std::optional<int> column;
};

struct LLVMParseError : LLVMError {
  std::vector<Diagnostic> m_diagnostics;

  explicit LLVMParseError(const std::vector<Diagnostic> &diags)
      : LLVMError(format_diagnostics(diags)), m_diagnostics(diags) {}

  std::vector<Diagnostic> get_diagnostics() const { return m_diagnostics; }

private:
  static std::string format_diagnostics(const std::vector<Diagnostic> &diags) {
    if (diags.empty()) {
      return "Failed to parse LLVM IR (no diagnostic information available)";
    }

    std::string result = "Failed to parse LLVM IR:\n";
    for (const auto &diag : diags) {
      result += "  " + diag.severity + ": " + diag.message + "\n";
    }
    return result;
  }
};

// =============================================================================
// Global Diagnostic Registry
// =============================================================================
// Diagnostics are stored in a global map keyed by context ref. This allows
// borrowed context wrappers (from get_module_context) to access the same
// diagnostics as the owning context wrapper. Protected by a mutex for thread
// safety.

struct DiagnosticRegistry {
  std::mutex mutex;
  std::unordered_map<LLVMContextRef, std::vector<Diagnostic>> diagnostics;

  static DiagnosticRegistry &instance() {
    static DiagnosticRegistry registry;
    return registry;
  }

  void add_diagnostic(LLVMContextRef ctx, const Diagnostic &diag) {
    std::lock_guard<std::mutex> lock(mutex);
    diagnostics[ctx].push_back(diag);
  }

  std::vector<Diagnostic> get_diagnostics(LLVMContextRef ctx) {
    std::lock_guard<std::mutex> lock(mutex);
    auto it = diagnostics.find(ctx);
    if (it != diagnostics.end()) {
      return it->second;
    }
    return {};
  }

  void clear_diagnostics(LLVMContextRef ctx) {
    std::lock_guard<std::mutex> lock(mutex);
    diagnostics.erase(ctx);
  }

  void remove_context(LLVMContextRef ctx) {
    std::lock_guard<std::mutex> lock(mutex);
    diagnostics.erase(ctx);
  }
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
struct LLVMDIBuilderManager;
struct LLVMNamedMDNodeWrapper;
struct LLVMOperandBundleWrapper;
struct LLVMUseWrapper;

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
  LLVMOperandBundleWrapper &
  operator=(const LLVMOperandBundleWrapper &) = delete;
  LLVMOperandBundleWrapper(LLVMOperandBundleWrapper &&other) noexcept
      : m_ref(other.m_ref), m_context_token(std::move(other.m_context_token)) {
    other.m_ref = nullptr;
  }
  LLVMOperandBundleWrapper &
  operator=(LLVMOperandBundleWrapper &&other) noexcept {
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
      throw LLVMMemoryError("OperandBundle is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("OperandBundle used after context was destroyed");
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
      throw LLVMMemoryError("Attribute is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Attribute used after context was destroyed");
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

  /// Check if this is an enum attribute.
  bool is_enum_attribute() const {
    check_valid();
    return LLVMIsEnumAttribute(m_ref);
  }

  /// Check if this is a string attribute.
  bool is_string_attribute() const {
    check_valid();
    return LLVMIsStringAttribute(m_ref);
  }

  /// Check if this is a type attribute.
  bool is_type_attribute() const {
    check_valid();
    return LLVMIsTypeAttribute(m_ref);
  }

  /// Get the string attribute kind (key).
  std::string get_string_kind() const {
    check_valid();
    if (!LLVMIsStringAttribute(m_ref))
      throw LLVMError("Attribute is not a string attribute");
    unsigned len;
    const char *kind = LLVMGetStringAttributeKind(m_ref, &len);
    return std::string(kind, len);
  }

  /// Get the string attribute value.
  std::string get_string_value() const {
    check_valid();
    if (!LLVMIsStringAttribute(m_ref))
      throw LLVMError("Attribute is not a string attribute");
    unsigned len;
    const char *val = LLVMGetStringAttributeValue(m_ref, &len);
    return std::string(val, len);
  }

  /// Get the type attribute value.
  /// Returns the type wrapped in this type attribute.
  LLVMTypeWrapper get_type_value() const;
};

// =============================================================================
// Comdat Wrapper (for COMDAT sections - Windows/COFF linking)
// =============================================================================

struct LLVMComdatWrapper {
  LLVMComdatRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_module_token;

  LLVMComdatWrapper() = default;
  LLVMComdatWrapper(LLVMComdatRef ref, std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_module_token(std::move(token)) {}

  // Comdats are not owned (owned by module), so no destructor needed

  void check_valid() const {
    if (!m_ref)
      throw LLVMMemoryError("Comdat is null");
    if (!m_module_token || !m_module_token->is_valid())
      throw LLVMMemoryError("Comdat used after module was destroyed");
  }

  bool is_valid() const {
    return m_ref != nullptr && m_module_token && m_module_token->is_valid();
  }

  LLVMComdatSelectionKind get_selection_kind() const {
    check_valid();
    return LLVMGetComdatSelectionKind(m_ref);
  }

  void set_selection_kind(LLVMComdatSelectionKind kind) {
    check_valid();
    LLVMSetComdatSelectionKind(m_ref, kind);
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
      : m_entries(entries), m_count(count), m_context_token(std::move(token)) {}

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
      throw LLVMMemoryError("ValueMetadataEntries is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError(
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
      throw LLVMMemoryError("Type is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Type used after context was destroyed");
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
           k == LLVMDoubleTypeKind || k == LLVMFP128TypeKind ||
           k == LLVMBFloatTypeKind || k == LLVMX86_FP80TypeKind ||
           k == LLVMPPC_FP128TypeKind;
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
      throw LLVMAssertionError("Type is not an integer type");
    return LLVMGetIntTypeWidth(m_ref);
  }

  bool is_sized() const {
    check_valid();
    return LLVMTypeIsSized(m_ref);
  }

  bool is_packed_struct() const {
    check_valid();
    if (!is_struct())
      throw LLVMAssertionError("Type is not a struct type");
    return LLVMIsPackedStruct(m_ref);
  }

  bool is_opaque_struct() const {
    check_valid();
    if (!is_struct())
      throw LLVMAssertionError("Type is not a struct type");
    return LLVMIsOpaqueStruct(m_ref);
  }

  /// Check if this is a literal (unnamed) struct type.
  bool is_literal_struct() const {
    check_valid();
    if (!is_struct())
      throw LLVMAssertionError("Type is not a struct type");
    return LLVMIsLiteralStruct(m_ref);
  }

  std::optional<std::string> get_struct_name() const {
    check_valid();
    if (!is_struct())
      throw LLVMAssertionError("Type is not a struct type");
    const char *name = LLVMGetStructName(m_ref);
    if (!name)
      return std::nullopt;
    return std::string(name);
  }

  bool is_vararg_function() const {
    check_valid();
    if (!is_function())
      throw LLVMAssertionError("Type is not a function type");
    return LLVMIsFunctionVarArg(m_ref);
  }

  // Type introspection for echo command
  LLVMTypeWrapper get_struct_element_type(unsigned index) const {
    check_valid();
    if (!is_struct())
      throw LLVMAssertionError("Type is not a struct type");
    unsigned count = LLVMCountStructElementTypes(m_ref);
    if (index >= count) {
      throw LLVMAssertionError("get_struct_element_type: struct element index " +
                               std::to_string(index) +
                               " out of range (struct_element_count=" +
                               std::to_string(count) + ")");
    }
    return LLVMTypeWrapper(LLVMStructGetTypeAtIndex(m_ref, index),
                           m_context_token);
  }

  bool is_opaque_pointer() const {
    check_valid();
    if (!is_pointer())
      throw LLVMAssertionError("Type is not a pointer type");
    return LLVMPointerTypeIsOpaque(m_ref);
  }

  LLVMTypeWrapper get_element_type() const {
    check_valid();
    auto k = kind();
    if (k != LLVMPointerTypeKind && k != LLVMVectorTypeKind &&
        k != LLVMScalableVectorTypeKind && k != LLVMArrayTypeKind)
      throw LLVMAssertionError(
          "Type does not have an element type (not pointer/vector/array)");
    return LLVMTypeWrapper(LLVMGetElementType(m_ref), m_context_token);
  }

  uint64_t get_array_length() const {
    check_valid();
    if (!is_array())
      throw LLVMAssertionError("Type is not an array type");
    return LLVMGetArrayLength2(m_ref);
  }

  unsigned get_vector_size() const {
    check_valid();
    if (!is_vector())
      throw LLVMAssertionError("Type is not a vector type");
    return LLVMGetVectorSize(m_ref);
  }

  unsigned get_pointer_address_space() const {
    check_valid();
    if (!is_pointer())
      throw LLVMAssertionError("Type is not a pointer type");
    return LLVMGetPointerAddressSpace(m_ref);
  }

  LLVMTypeWrapper get_return_type() const {
    check_valid();
    if (!is_function())
      throw LLVMAssertionError("Type is not a function type");
    return LLVMTypeWrapper(LLVMGetReturnType(m_ref), m_context_token);
  }

  unsigned count_param_types() const {
    check_valid();
    if (!is_function())
      throw LLVMAssertionError("Type is not a function type");
    return LLVMCountParamTypes(m_ref);
  }

  std::vector<LLVMTypeWrapper> get_param_types() const {
    check_valid();
    if (!is_function())
      throw LLVMAssertionError("Type is not a function type");
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
      throw LLVMAssertionError("Type is not a struct type");
    return LLVMCountStructElementTypes(m_ref);
  }

  // Target extension type info
  std::string get_target_ext_type_name() const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMAssertionError("Type is not a target extension type");
    return std::string(LLVMGetTargetExtTypeName(m_ref));
  }

  unsigned get_target_ext_type_num_type_params() const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMAssertionError("Type is not a target extension type");
    return LLVMGetTargetExtTypeNumTypeParams(m_ref);
  }

  unsigned get_target_ext_type_num_int_params() const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMAssertionError("Type is not a target extension type");
    return LLVMGetTargetExtTypeNumIntParams(m_ref);
  }

  LLVMTypeWrapper get_target_ext_type_type_param(unsigned index) const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMAssertionError("Type is not a target extension type");
    unsigned count = LLVMGetTargetExtTypeNumTypeParams(m_ref);
    if (index >= count) {
      throw LLVMAssertionError(
          "get_target_ext_type_type_param: type parameter index " +
          std::to_string(index) + " out of range (num_type_params=" +
          std::to_string(count) + ")");
    }
    return LLVMTypeWrapper(LLVMGetTargetExtTypeTypeParam(m_ref, index),
                           m_context_token);
  }

  unsigned get_target_ext_type_int_param(unsigned index) const {
    check_valid();
    if (kind() != LLVMTargetExtTypeKind)
      throw LLVMAssertionError("Type is not a target extension type");
    unsigned count = LLVMGetTargetExtTypeNumIntParams(m_ref);
    if (index >= count) {
      throw LLVMAssertionError(
          "get_target_ext_type_int_param: int parameter index " +
          std::to_string(index) + " out of range (num_int_params=" +
          std::to_string(count) + ")");
    }
    return LLVMGetTargetExtTypeIntParam(m_ref, index);
  }

  // =========================================================================
  // Constant creation methods (Phase 2: Type-based constants)
  // Declarations only - implementations after LLVMValueWrapper is defined
  // =========================================================================

  // Integer constant: ty.constant(42)
  LLVMValueWrapper constant(long long val, bool sign_extend = false) const;

  // Integer constant from string: ty.constant_from_string("123456789", 10)
  LLVMValueWrapper constant_from_string(const std::string &text,
                                        unsigned radix = 10) const;

  // Float constant: ty.real_constant(3.14)
  LLVMValueWrapper real_constant(double val) const;

  // Float constant from string: ty.real_constant_from_string("3.14159")
  LLVMValueWrapper real_constant_from_string(const std::string &text) const;

  // Null value (works for all types): ty.null()
  LLVMValueWrapper null() const;

  // All-ones constant: ty.all_ones()
  LLVMValueWrapper all_ones() const;

  // Undef value: ty.undef()
  LLVMValueWrapper undef() const;

  // Poison value: ty.poison()
  LLVMValueWrapper poison() const;

  // =========================================================================
  // Composite type factory methods (Phase 2)
  // =========================================================================

  // Create array type from this element type: i32.array(10) -> [10 x i32]
  LLVMTypeWrapper array(uint64_t count) const {
    check_valid();
    return LLVMTypeWrapper(LLVMArrayType2(m_ref, count), m_context_token);
  }

  // Create vector type from this element type: i32.vector(4) -> <4 x i32>
  LLVMTypeWrapper vector(unsigned count) const {
    check_valid();
    return LLVMTypeWrapper(LLVMVectorType(m_ref, count), m_context_token);
  }

  // Create pointer type (in the same context): ty.pointer() -> ptr
  LLVMTypeWrapper pointer(unsigned address_space = 0) const {
    check_valid();
    return LLVMTypeWrapper(
        LLVMPointerTypeInContext(LLVMGetTypeContext(m_ref), address_space),
        m_context_token);
  }

  // Get the context this type belongs to
  LLVMContextWrapper *context() const;
};

// Implementation of LLVMAttributeWrapper::get_type_value() - needs
// LLVMTypeWrapper
inline LLVMTypeWrapper LLVMAttributeWrapper::get_type_value() const {
  check_valid();
  if (!LLVMIsTypeAttribute(m_ref))
    throw LLVMError("Attribute is not a type attribute");
  LLVMTypeRef ty = LLVMGetTypeAttributeValue(m_ref);
  return LLVMTypeWrapper(ty, m_context_token);
}

// =============================================================================
// Type Factory Wrapper (property-based namespace for type creation)
// =============================================================================

struct LLVMTypeFactoryWrapper {
  LLVMContextRef m_ctx_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMTypeFactoryWrapper() = default;
  LLVMTypeFactoryWrapper(LLVMContextRef ctx,
                         std::shared_ptr<ValidityToken> token)
      : m_ctx_ref(ctx), m_context_token(std::move(token)) {}

  void check_valid() const {
    if (!m_ctx_ref)
      throw LLVMMemoryError("TypeFactory context is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("TypeFactory used after context was destroyed");
  }

  // =========================================================================
  // Fixed-width integer types (properties)
  // =========================================================================
  LLVMTypeWrapper i1() const {
    check_valid();
    return LLVMTypeWrapper(LLVMInt1TypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper i8() const {
    check_valid();
    return LLVMTypeWrapper(LLVMInt8TypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper i16() const {
    check_valid();
    return LLVMTypeWrapper(LLVMInt16TypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper i32() const {
    check_valid();
    return LLVMTypeWrapper(LLVMInt32TypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper i64() const {
    check_valid();
    return LLVMTypeWrapper(LLVMInt64TypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper i128() const {
    check_valid();
    return LLVMTypeWrapper(LLVMInt128TypeInContext(m_ctx_ref), m_context_token);
  }

  // =========================================================================
  // Floating-point types (properties)
  // =========================================================================
  LLVMTypeWrapper f16() const {
    check_valid();
    return LLVMTypeWrapper(LLVMHalfTypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper bf16() const {
    check_valid();
    return LLVMTypeWrapper(LLVMBFloatTypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper f32() const {
    check_valid();
    return LLVMTypeWrapper(LLVMFloatTypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper f64() const {
    check_valid();
    return LLVMTypeWrapper(LLVMDoubleTypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper x86_fp80() const {
    check_valid();
    return LLVMTypeWrapper(LLVMX86FP80TypeInContext(m_ctx_ref),
                           m_context_token);
  }

  LLVMTypeWrapper fp128() const {
    check_valid();
    return LLVMTypeWrapper(LLVMFP128TypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper ppc_fp128() const {
    check_valid();
    return LLVMTypeWrapper(LLVMPPCFP128TypeInContext(m_ctx_ref),
                           m_context_token);
  }

  // =========================================================================
  // Other types (properties)
  // =========================================================================
  LLVMTypeWrapper void_() const {
    check_valid();
    return LLVMTypeWrapper(LLVMVoidTypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper label() const {
    check_valid();
    return LLVMTypeWrapper(LLVMLabelTypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper metadata() const {
    check_valid();
    return LLVMTypeWrapper(LLVMMetadataTypeInContext(m_ctx_ref),
                           m_context_token);
  }

  LLVMTypeWrapper token() const {
    check_valid();
    return LLVMTypeWrapper(LLVMTokenTypeInContext(m_ctx_ref), m_context_token);
  }

  LLVMTypeWrapper x86_amx() const {
    check_valid();
    return LLVMTypeWrapper(LLVMX86AMXTypeInContext(m_ctx_ref), m_context_token);
  }

  // =========================================================================
  // Pointer type (property for default AS 0, method for custom AS)
  // =========================================================================
  LLVMTypeWrapper ptr_default() const {
    check_valid();
    return LLVMTypeWrapper(LLVMPointerTypeInContext(m_ctx_ref, 0),
                           m_context_token);
  }

  LLVMTypeWrapper addrspace_ptr(unsigned address_space) const {
    check_valid();
    return LLVMTypeWrapper(LLVMPointerTypeInContext(m_ctx_ref, address_space),
                           m_context_token);
  }

  // =========================================================================
  // Parameterized types (methods)
  // =========================================================================

  LLVMTypeWrapper int_n(unsigned bits) const {
    check_valid();
    return LLVMTypeWrapper(LLVMIntTypeInContext(m_ctx_ref, bits),
                           m_context_token);
  }

  LLVMTypeWrapper function(const LLVMTypeWrapper &ret_ty,
                           const std::vector<LLVMTypeWrapper> &param_types,
                           bool vararg = false) const {
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
        m_context_token);
  }

  // Unified struct() method: named if name is provided, anonymous otherwise
  LLVMTypeWrapper struct_(const std::vector<LLVMTypeWrapper> &elem_types,
                          bool packed = false,
                          const std::string &name = "") const {
    check_valid();

    if (!name.empty()) {
      // Named struct
      LLVMTypeRef s = LLVMStructCreateNamed(m_ctx_ref, name.c_str());
      if (!elem_types.empty()) {
        std::vector<LLVMTypeRef> elems;
        elems.reserve(elem_types.size());
        for (const auto &e : elem_types) {
          e.check_valid();
          elems.push_back(e.m_ref);
        }
        LLVMStructSetBody(s, elems.data(), static_cast<unsigned>(elems.size()),
                          packed);
      }
      return LLVMTypeWrapper(s, m_context_token);
    } else {
      // Anonymous struct
      std::vector<LLVMTypeRef> elems;
      elems.reserve(elem_types.size());
      for (const auto &e : elem_types) {
        e.check_valid();
        elems.push_back(e.m_ref);
      }
      return LLVMTypeWrapper(
          LLVMStructTypeInContext(m_ctx_ref, elems.data(),
                                  static_cast<unsigned>(elems.size()), packed),
          m_context_token);
    }
  }

  // Opaque named struct (forward declaration)
  LLVMTypeWrapper opaque_struct(const std::string &name) const {
    check_valid();
    return LLVMTypeWrapper(LLVMStructCreateNamed(m_ctx_ref, name.c_str()),
                           m_context_token);
  }

  // Array type
  LLVMTypeWrapper array(const LLVMTypeWrapper &elem_ty, uint64_t count) const {
    check_valid();
    elem_ty.check_valid();
    return LLVMTypeWrapper(LLVMArrayType2(elem_ty.m_ref, count),
                           m_context_token);
  }

  // Vector type
  LLVMTypeWrapper vector(const LLVMTypeWrapper &elem_ty,
                         unsigned elem_count) const {
    check_valid();
    elem_ty.check_valid();
    return LLVMTypeWrapper(LLVMVectorType(elem_ty.m_ref, elem_count),
                           m_context_token);
  }

  // Scalable vector type
  LLVMTypeWrapper scalable_vector(const LLVMTypeWrapper &elem_ty,
                                  unsigned elem_count) const {
    check_valid();
    elem_ty.check_valid();
    return LLVMTypeWrapper(LLVMScalableVectorType(elem_ty.m_ref, elem_count),
                           m_context_token);
  }

  // Target extension type
  LLVMTypeWrapper target_ext(const std::string &name,
                             const std::vector<LLVMTypeWrapper> &type_params,
                             const std::vector<unsigned> &int_params) const {
    check_valid();
    std::vector<LLVMTypeRef> type_refs;
    type_refs.reserve(type_params.size());
    for (const auto &t : type_params) {
      t.check_valid();
      type_refs.push_back(t.m_ref);
    }
    return LLVMTypeWrapper(
        LLVMTargetExtTypeInContext(
            m_ctx_ref, name.c_str(), type_refs.data(), type_refs.size(),
            const_cast<unsigned *>(int_params.data()), int_params.size()),
        m_context_token);
  }

  // Get type by name (for looking up named structs)
  std::optional<LLVMTypeWrapper> get(const std::string &name) const {
    check_valid();
    LLVMTypeRef ty = LLVMGetTypeByName2(m_ctx_ref, name.c_str());
    if (!ty)
      return std::nullopt;
    return LLVMTypeWrapper(ty, m_context_token);
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
      throw LLVMMemoryError("NamedMDNode is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("NamedMDNode used after context was destroyed");
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
// Use Wrapper (for use-def chain iteration)
// =============================================================================

struct LLVMUseWrapper {
  LLVMUseRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMUseWrapper() = default;
  LLVMUseWrapper(LLVMUseRef ref, std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}

  void check_valid() const {
    if (!m_ref)
      throw LLVMMemoryError("Use is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Use used after context was destroyed");
  }

  // get_user, get_used_value, and get_operand_index are implemented after
  // LLVMValueWrapper is defined
  LLVMValueWrapper get_user() const;
  LLVMValueWrapper get_used_value() const;

  // Get the operand index of this use within the user instruction.
  // Matches this Use pointer against LLVMGetOperandUse(user, i) to handle
  // duplicate operands correctly.
  unsigned get_operand_index() const;
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
      throw LLVMMemoryError("Value is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Value used after context was destroyed");
  }

  static const char *opcode_name(LLVMOpcode op) {
    switch (op) {
    case LLVMRet:
      return "ret";
    case LLVMBr:
      return "br";
    case LLVMSwitch:
      return "switch";
    case LLVMIndirectBr:
      return "indirectbr";
    case LLVMInvoke:
      return "invoke";
    case LLVMUnreachable:
      return "unreachable";
    case LLVMCallBr:
      return "callbr";
    case LLVMFNeg:
      return "fneg";
    case LLVMAdd:
      return "add";
    case LLVMFAdd:
      return "fadd";
    case LLVMSub:
      return "sub";
    case LLVMFSub:
      return "fsub";
    case LLVMMul:
      return "mul";
    case LLVMFMul:
      return "fmul";
    case LLVMUDiv:
      return "udiv";
    case LLVMSDiv:
      return "sdiv";
    case LLVMFDiv:
      return "fdiv";
    case LLVMURem:
      return "urem";
    case LLVMSRem:
      return "srem";
    case LLVMFRem:
      return "frem";
    case LLVMShl:
      return "shl";
    case LLVMLShr:
      return "lshr";
    case LLVMAShr:
      return "ashr";
    case LLVMAnd:
      return "and";
    case LLVMOr:
      return "or";
    case LLVMXor:
      return "xor";
    case LLVMAlloca:
      return "alloca";
    case LLVMLoad:
      return "load";
    case LLVMStore:
      return "store";
    case LLVMGetElementPtr:
      return "getelementptr";
    case LLVMTrunc:
      return "trunc";
    case LLVMZExt:
      return "zext";
    case LLVMSExt:
      return "sext";
    case LLVMFPToUI:
      return "fptoui";
    case LLVMFPToSI:
      return "fptosi";
    case LLVMUIToFP:
      return "uitofp";
    case LLVMSIToFP:
      return "sitofp";
    case LLVMFPTrunc:
      return "fptrunc";
    case LLVMFPExt:
      return "fpext";
    case LLVMPtrToInt:
      return "ptrtoint";
    case LLVMIntToPtr:
      return "inttoptr";
    case LLVMBitCast:
      return "bitcast";
    case LLVMAddrSpaceCast:
      return "addrspacecast";
    case LLVMICmp:
      return "icmp";
    case LLVMFCmp:
      return "fcmp";
    case LLVMPHI:
      return "phi";
    case LLVMCall:
      return "call";
    case LLVMSelect:
      return "select";
    case LLVMUserOp1:
      return "userop1";
    case LLVMUserOp2:
      return "userop2";
    case LLVMVAArg:
      return "va_arg";
    case LLVMExtractElement:
      return "extractelement";
    case LLVMInsertElement:
      return "insertelement";
    case LLVMShuffleVector:
      return "shufflevector";
    case LLVMExtractValue:
      return "extractvalue";
    case LLVMInsertValue:
      return "insertvalue";
    case LLVMFreeze:
      return "freeze";
    case LLVMFence:
      return "fence";
    case LLVMAtomicCmpXchg:
      return "cmpxchg";
    case LLVMAtomicRMW:
      return "atomicrmw";
    case LLVMResume:
      return "resume";
    case LLVMLandingPad:
      return "landingpad";
    case LLVMCleanupRet:
      return "cleanupret";
    case LLVMCatchRet:
      return "catchret";
    case LLVMCatchPad:
      return "catchpad";
    case LLVMCleanupPad:
      return "cleanuppad";
    case LLVMCatchSwitch:
      return "catchswitch";
    default:
      return "unknown";
    }
  }

  template <LLVMOpcode... AllowedOps>
  static std::string expected_opcode_list() {
    static_assert(sizeof...(AllowedOps) > 0,
                  "At least one opcode must be provided");
    const char *names[] = {opcode_name(AllowedOps)...};
    constexpr size_t count = sizeof...(AllowedOps);

    std::string result;
    for (size_t i = 0; i < count; ++i) {
      if (i > 0) {
        if (i + 1 == count) {
          result += (count == 2) ? " or " : ", or ";
        } else {
          result += ", ";
        }
      }
      result += names[i];
    }
    return result;
  }

  static std::string with_indefinite_article(const char *name) {
    if (!name || !name[0])
      return "an instruction";
    char c = static_cast<char>(std::tolower(static_cast<unsigned char>(name[0])));
    const bool starts_with_vowel =
        (c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u');
    return std::string(starts_with_vowel ? "an " : "a ") + name;
  }

  template <LLVMOpcode... AllowedOps>
  static std::string expected_opcode_phrase() {
    constexpr size_t count = sizeof...(AllowedOps);
    if constexpr (count == 1) {
      const LLVMOpcode opcodes[] = {AllowedOps...};
      return with_indefinite_article(opcode_name(opcodes[0]));
    } else {
      return expected_opcode_list<AllowedOps...>();
    }
  }

  void require_instruction_value(const char *api_name) const {
    if (!LLVMIsAInstruction(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires an instruction value");
    }
  }

  template <LLVMOpcode... AllowedOps>
  void require_instruction_opcodes(const char *api_name,
                                   const char *custom_expected = nullptr) const {
    const std::string expected =
        custom_expected ? custom_expected : expected_opcode_phrase<AllowedOps...>();

    if (!LLVMIsAInstruction(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) + " requires " + expected +
                               " instruction (got non-instruction value)");
    }

    LLVMOpcode op = LLVMGetInstructionOpcode(m_ref);
    bool matches_opcode = ((op == AllowedOps) || ...);
    if (matches_opcode)
      return;

    throw LLVMAssertionError(std::string(api_name) + " requires " + expected +
                             " instruction (got " + opcode_name(op) + ")");
  }

  void require_global_variable(const char *api_name) const {
    if (!LLVMIsAGlobalVariable(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a global variable");
    }
  }

  void require_global_value(const char *api_name) const {
    if (!LLVMIsAGlobalValue(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a global value");
    }
  }

  void require_global_object(const char *api_name) const {
    if (!LLVMIsAGlobalObject(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a global object");
    }
  }

  void require_global_alias(const char *api_name) const {
    if (!LLVMIsAGlobalAlias(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a global alias");
    }
  }

  void require_global_ifunc(const char *api_name) const {
    if (LLVMGetValueKind(m_ref) != LLVMGlobalIFuncValueKind) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a global ifunc value");
    }
  }

  void require_function_value(const char *api_name) const {
    if (!LLVMIsAFunction(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a function value");
    }
  }

  void require_argument_value(const char *api_name) const {
    if (!LLVMIsAArgument(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a function argument value");
    }
  }

  void require_phi_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMPHI>(api_name);
  }

  void require_call_like_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMCall, LLVMInvoke, LLVMCallBr>(api_name);
  }

  void require_arg_operands_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMCall, LLVMInvoke, LLVMCallBr,
                                LLVMCatchPad, LLVMCleanupPad>(api_name);
  }

  void require_invoke_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMInvoke>(api_name);
  }

  void require_callbr_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMCallBr>(api_name);
  }

  void require_terminator_instruction(const char *api_name) const {
    if (!LLVMIsAInstruction(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a terminator instruction (got "
                               "non-instruction value)");
    }
    if (!LLVMIsATerminatorInst(m_ref)) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires a terminator instruction (got " +
                               opcode_name(LLVMGetInstructionOpcode(m_ref)) +
                               ")");
    }
  }

  void require_landingpad_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMLandingPad>(api_name);
  }

  void require_catchpad_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMCatchPad>(api_name);
  }

  void require_catchswitch_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMCatchSwitch>(api_name);
  }

  void require_shufflevector_instruction(const char *api_name) const {
    require_instruction_opcodes<LLVMShuffleVector>(api_name,
                                                   "a shufflevector");
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

  // Use-def chain iteration (pythonic API)
  std::vector<LLVMUseWrapper> uses() const {
    check_valid();
    std::vector<LLVMUseWrapper> result;
    for (LLVMUseRef use = LLVMGetFirstUse(m_ref); use != nullptr;
         use = LLVMGetNextUse(use)) {
      result.emplace_back(use, m_context_token);
    }
    return result;
  }

  // Get all users of this value as a vector (pythonic iteration)
  // Forward declared - implemented after LLVMValueWrapper is complete
  std::vector<LLVMValueWrapper> users() const {
    check_valid();
    std::unordered_set<LLVMValueRef> visited;
    std::vector<LLVMValueWrapper> result;
    for (LLVMUseRef use = LLVMGetFirstUse(m_ref); use != nullptr;
         use = LLVMGetNextUse(use)) {
      auto user = LLVMGetUser(use);
      if (visited.find(user) == visited.end()) {
        visited.insert(user);
        result.emplace_back(user, m_context_token);
      }
    }
    return result;
  }

  std::optional<LLVMValueWrapper> next_global() const {
    check_valid();
    require_global_variable("next_global");
    LLVMValueRef next = LLVMGetNextGlobal(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_global() const {
    check_valid();
    require_global_variable("prev_global");
    LLVMValueRef prev = LLVMGetPreviousGlobal(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  // Global alias iteration for echo command
  std::optional<LLVMValueWrapper> next_global_alias() const {
    check_valid();
    require_global_alias("next_global_alias");
    LLVMValueRef next = LLVMGetNextGlobalAlias(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_global_alias() const {
    check_valid();
    require_global_alias("prev_global_alias");
    LLVMValueRef prev = LLVMGetPreviousGlobalAlias(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  std::optional<LLVMValueWrapper> alias_get_aliasee() const {
    check_valid();
    require_global_alias("aliasee");
    LLVMValueRef aliasee = LLVMAliasGetAliasee(m_ref);
    if (!aliasee)
      return std::nullopt;
    return LLVMValueWrapper(aliasee, m_context_token);
  }

  void alias_set_aliasee(const LLVMValueWrapper &aliasee) {
    check_valid();
    require_global_alias("alias_set_aliasee");
    aliasee.check_valid();
    LLVMAliasSetAliasee(m_ref, aliasee.m_ref);
  }

  // Global IFunc iteration for echo command
  std::optional<LLVMValueWrapper> next_global_ifunc() const {
    check_valid();
    require_global_ifunc("next_global_ifunc");
    LLVMValueRef next = LLVMGetNextGlobalIFunc(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_global_ifunc() const {
    check_valid();
    require_global_ifunc("prev_global_ifunc");
    LLVMValueRef prev = LLVMGetPreviousGlobalIFunc(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  std::optional<LLVMValueWrapper> get_global_ifunc_resolver() const {
    check_valid();
    require_global_ifunc("global_ifunc_resolver");
    LLVMValueRef resolver = LLVMGetGlobalIFuncResolver(m_ref);
    if (!resolver)
      return std::nullopt;
    return LLVMValueWrapper(resolver, m_context_token);
  }

  void set_global_ifunc_resolver(const LLVMValueWrapper &resolver) {
    check_valid();
    require_global_ifunc("set_global_ifunc_resolver");
    resolver.check_valid();
    LLVMSetGlobalIFuncResolver(m_ref, resolver.m_ref);
  }

  // Erase global IFunc from parent module and delete it
  void erase_from_parent_ifunc() {
    check_valid();
    require_global_ifunc("erase_from_parent_ifunc");
    LLVMEraseGlobalIFunc(m_ref);
    m_ref = nullptr; // IFunc is now deleted
  }

  // Remove global IFunc from parent module but keep it alive
  void remove_from_parent_ifunc() {
    check_valid();
    require_global_ifunc("remove_from_parent_ifunc");
    LLVMRemoveGlobalIFunc(m_ref);
    // IFunc is still alive, just unlinked from module
  }

  // Global properties for echo command
  LLVMTypeWrapper global_get_value_type() const {
    check_valid();
    require_global_value("global_value_type");
    return LLVMTypeWrapper(LLVMGlobalGetValueType(m_ref), m_context_token);
  }

  LLVMUnnamedAddr get_unnamed_address() const {
    check_valid();
    require_global_value("unnamed_address");
    return LLVMGetUnnamedAddress(m_ref);
  }

  void set_unnamed_address(LLVMUnnamedAddr unnamed_addr) {
    check_valid();
    require_global_value("set_unnamed_address");
    LLVMSetUnnamedAddress(m_ref, unnamed_addr);
  }

  bool has_personality_fn() const {
    check_valid();
    require_function_value("has_personality_fn");
    return LLVMHasPersonalityFn(m_ref);
  }

  LLVMValueWrapper get_personality_fn() const {
    check_valid();
    require_function_value("personality_fn");
    if (!LLVMHasPersonalityFn(m_ref)) {
      throw LLVMAssertionError(
          "personality_fn requires a personality function "
          "(check has_personality_fn first)");
    }
    LLVMValueRef fn = LLVMGetPersonalityFn(m_ref);
    if (!fn) {
      throw LLVMAssertionError(
          "personality_fn: personality function is unexpectedly null");
    }
    return LLVMValueWrapper(fn, m_context_token);
  }

  void set_personality_fn(const LLVMValueWrapper &fn) {
    check_valid();
    require_function_value("set_personality_fn");
    fn.check_valid();
    LLVMSetPersonalityFn(m_ref, fn.m_ref);
  }

  bool has_prefix_data() const {
    check_valid();
    require_function_value("has_prefix_data");
    return LLVMHasPrefixData(m_ref);
  }

  LLVMValueWrapper get_prefix_data() const {
    check_valid();
    require_function_value("prefix_data");
    if (!LLVMHasPrefixData(m_ref)) {
      throw LLVMAssertionError(
          "prefix_data requires prefix data (check has_prefix_data first)");
    }
    LLVMValueRef data = LLVMGetPrefixData(m_ref);
    if (!data) {
      throw LLVMAssertionError("prefix_data: prefix data is unexpectedly null");
    }
    return LLVMValueWrapper(data, m_context_token);
  }

  void set_prefix_data(const LLVMValueWrapper &data) {
    check_valid();
    require_function_value("set_prefix_data");
    data.check_valid();
    LLVMSetPrefixData(m_ref, data.m_ref);
  }

  bool has_prologue_data() const {
    check_valid();
    require_function_value("has_prologue_data");
    return LLVMHasPrologueData(m_ref);
  }

  LLVMValueWrapper get_prologue_data() const {
    check_valid();
    require_function_value("prologue_data");
    if (!LLVMHasPrologueData(m_ref)) {
      throw LLVMAssertionError(
          "prologue_data requires prologue data (check has_prologue_data "
          "first)");
    }
    LLVMValueRef data = LLVMGetPrologueData(m_ref);
    if (!data) {
      throw LLVMAssertionError(
          "prologue_data: prologue data is unexpectedly null");
    }
    return LLVMValueWrapper(data, m_context_token);
  }

  void set_prologue_data(const LLVMValueWrapper &data) {
    check_valid();
    require_function_value("set_prologue_data");
    data.check_valid();
    LLVMSetPrologueData(m_ref, data.m_ref);
  }

  // Global/Instruction metadata copying for echo command
  LLVMValueMetadataEntriesWrapper global_copy_all_metadata() const {
    check_valid();
    require_global_value("global_copy_all_metadata");
    size_t num_entries = 0;
    LLVMValueMetadataEntry *entries =
        LLVMGlobalCopyAllMetadata(m_ref, &num_entries);
    return LLVMValueMetadataEntriesWrapper(entries, num_entries,
                                           m_context_token);
  }

  LLVMValueMetadataEntriesWrapper
  instruction_get_all_metadata_other_than_debug_loc() const {
    check_valid();
    require_instruction_value("instruction_get_all_metadata_other_than_debug_loc");
    size_t num_entries = 0;
    LLVMValueMetadataEntry *entries =
        LLVMInstructionGetAllMetadataOtherThanDebugLoc(m_ref, &num_entries);
    return LLVMValueMetadataEntriesWrapper(entries, num_entries,
                                           m_context_token);
  }

  // Instruction iteration
  std::optional<LLVMValueWrapper> next_instruction() const {
    check_valid();
    require_instruction_value("next_instruction");
    LLVMValueRef next = LLVMGetNextInstruction(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_instruction() const {
    check_valid();
    require_instruction_value("prev_instruction");
    LLVMValueRef prev = LLVMGetPreviousInstruction(m_ref);
    if (!prev)
      return std::nullopt;
    return LLVMValueWrapper(prev, m_context_token);
  }

  // Parameter iteration for echo command
  std::optional<LLVMValueWrapper> next_param() const {
    check_valid();
    require_argument_value("next_param");
    LLVMValueRef next = LLVMGetNextParam(m_ref);
    if (!next)
      return std::nullopt;
    return LLVMValueWrapper(next, m_context_token);
  }

  std::optional<LLVMValueWrapper> prev_param() const {
    check_valid();
    require_argument_value("prev_param");
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
    unsigned num_ops = static_cast<unsigned>(LLVMGetNumOperands(m_ref));
    if (index >= num_ops)
      throw LLVMAssertionError("Operand index " + std::to_string(index) +
                               " out of range (num_operands=" +
                               std::to_string(num_ops) + ")");
    LLVMValueRef op = LLVMGetOperand(m_ref, index);
    if (!op)
      throw LLVMAssertionError("Operand at index " + std::to_string(index) +
                               " is null");
    return LLVMValueWrapper(op, m_context_token);
  }

  // Get all operands as a vector (Pythonic iteration)
  std::vector<LLVMValueWrapper> operands() const {
    check_valid();
    unsigned n = LLVMGetNumOperands(m_ref);
    std::vector<LLVMValueWrapper> result;
    result.reserve(n);
    for (unsigned i = 0; i < n; ++i) {
      LLVMValueRef op = LLVMGetOperand(m_ref, i);
      result.emplace_back(op ? op : nullptr, m_context_token);
    }
    return result;
  }

  // Check if this value has any uses
  bool has_uses() const {
    check_valid();
    return LLVMGetFirstUse(m_ref) != nullptr;
  }

  /// Set the operand at the given index.
  void set_operand(unsigned index, const LLVMValueWrapper &val) {
    check_valid();
    val.check_valid();
    if (index >= static_cast<unsigned>(LLVMGetNumOperands(m_ref)))
      throw LLVMAssertionError("Invalid operand index");
    LLVMSetOperand(m_ref, index, val.m_ref);
  }

  /// Get the use object for an operand at the given index.
  LLVMUseWrapper
  get_operand_use(unsigned index) const; // defined after LLVMUseWrapper

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

  // Constant integer value access
  unsigned long long const_zext_value() const {
    check_valid();
    if (!LLVMIsAConstantInt(m_ref))
      throw LLVMAssertionError("const_zext_value requires a constant integer");
    return LLVMConstIntGetZExtValue(m_ref);
  }

  long long const_sext_value() const {
    check_valid();
    if (!LLVMIsAConstantInt(m_ref))
      throw LLVMAssertionError("const_sext_value requires a constant integer");
    return LLVMConstIntGetSExtValue(m_ref);
  }

  double const_real_double() const {
    check_valid();
    if (!LLVMIsAConstantFP(m_ref))
      throw LLVMAssertionError("const_real_double requires a constant floating point");
    LLVMBool loses_info = 0;
    return LLVMConstRealGetDouble(m_ref, &loses_info);
  }

  // Check if value is ValueAsMetadata
  bool is_value_as_metadata() const {
    check_valid();
    return LLVMIsAValueAsMetadata(m_ref) != nullptr;
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
      throw LLVMAssertionError("Value is not a constant data array");
    size_t size;
    const char *data = LLVMGetRawDataValues(m_ref, &size);
    return {size, std::string(data, size)};
  }

  LLVMValueWrapper get_aggregate_element(unsigned index) const {
    check_valid();
    if (!LLVMIsAConstantStruct(m_ref) && !LLVMIsAConstantArray(m_ref) &&
        !LLVMIsAConstantVector(m_ref) && !LLVMIsAConstantDataArray(m_ref) &&
        !LLVMIsAConstantDataVector(m_ref) &&
        !LLVMIsAConstantAggregateZero(m_ref)) {
      throw LLVMAssertionError(
          "get_aggregate_element requires a constant aggregate value");
    }
    LLVMValueRef elem = LLVMGetAggregateElement(m_ref, index);
    if (!elem)
      throw LLVMAssertionError("Invalid aggregate element index");
    return LLVMValueWrapper(elem, m_context_token);
  }

  // Constant expression support
  LLVMOpcode get_const_opcode() const {
    check_valid();
    if (!is_a_constant_expr())
      throw LLVMAssertionError("Value is not a constant expression");
    return LLVMGetConstOpcode(m_ref);
  }

  LLVMTypeWrapper get_gep_source_element_type() const {
    check_valid();
    bool is_gep_instruction =
        LLVMIsAInstruction(m_ref) &&
        LLVMGetInstructionOpcode(m_ref) == LLVMGetElementPtr;
    bool is_gep_const_expr =
        LLVMIsAConstantExpr(m_ref) &&
        LLVMGetConstOpcode(m_ref) == LLVMGetElementPtr;
    if (!is_gep_instruction && !is_gep_const_expr) {
      throw LLVMAssertionError("gep_source_element_type requires a GEP value");
    }
    LLVMTypeRef ty = LLVMGetGEPSourceElementType(m_ref);
    if (!ty)
      throw LLVMAssertionError("GEP source element type is null");
    return LLVMTypeWrapper(ty, m_context_token);
  }

  unsigned get_num_indices() const {
    check_valid();
    bool is_indexed_instruction =
        LLVMIsAInstruction(m_ref) &&
        (LLVMGetInstructionOpcode(m_ref) == LLVMGetElementPtr ||
         LLVMGetInstructionOpcode(m_ref) == LLVMExtractValue ||
         LLVMGetInstructionOpcode(m_ref) == LLVMInsertValue);
    bool is_indexed_const_expr =
        LLVMIsAConstantExpr(m_ref) &&
        (LLVMGetConstOpcode(m_ref) == LLVMGetElementPtr ||
         LLVMGetConstOpcode(m_ref) == LLVMExtractValue ||
         LLVMGetConstOpcode(m_ref) == LLVMInsertValue);
    if (!is_indexed_instruction && !is_indexed_const_expr) {
      throw LLVMAssertionError(
          "num_indices requires getelementptr, extractvalue, or insertvalue");
    }
    return LLVMGetNumIndices(m_ref);
  }

  unsigned get_gep_no_wrap_flags() const {
    check_valid();
    bool is_gep_instruction =
        LLVMIsAInstruction(m_ref) &&
        LLVMGetInstructionOpcode(m_ref) == LLVMGetElementPtr;
    bool is_gep_const_expr =
        LLVMIsAConstantExpr(m_ref) &&
        LLVMGetConstOpcode(m_ref) == LLVMGetElementPtr;
    if (!is_gep_instruction && !is_gep_const_expr) {
      throw LLVMAssertionError("gep_no_wrap_flags requires a GEP value");
    }
    return LLVMGEPGetNoWrapFlags(m_ref);
  }

  // Pointer auth constant support
  LLVMValueWrapper get_constant_ptr_auth_pointer() const {
    check_valid();
    if (!is_a_constant_ptr_auth())
      throw LLVMAssertionError("Value is not a pointer auth constant");
    return LLVMValueWrapper(LLVMGetConstantPtrAuthPointer(m_ref),
                            m_context_token);
  }

  LLVMValueWrapper get_constant_ptr_auth_key() const {
    check_valid();
    if (!is_a_constant_ptr_auth())
      throw LLVMAssertionError("Value is not a pointer auth constant");
    return LLVMValueWrapper(LLVMGetConstantPtrAuthKey(m_ref), m_context_token);
  }

  LLVMValueWrapper get_constant_ptr_auth_discriminator() const {
    check_valid();
    if (!is_a_constant_ptr_auth())
      throw LLVMAssertionError("Value is not a pointer auth constant");
    return LLVMValueWrapper(LLVMGetConstantPtrAuthDiscriminator(m_ref),
                            m_context_token);
  }

  LLVMValueWrapper get_constant_ptr_auth_addr_discriminator() const {
    check_valid();
    if (!is_a_constant_ptr_auth())
      throw LLVMAssertionError("Value is not a pointer auth constant");
    return LLVMValueWrapper(LLVMGetConstantPtrAuthAddrDiscriminator(m_ref),
                            m_context_token);
  }

  // Instruction properties for echo command
  LLVMOpcode get_instruction_opcode() const {
    check_valid();
    require_instruction_value("opcode");
    return LLVMGetInstructionOpcode(m_ref);
  }

  // Get the mnemonic string for this instruction's opcode
  std::string get_opcode_name() const {
    check_valid();
    require_instruction_value("opcode_name");
    return opcode_name(LLVMGetInstructionOpcode(m_ref));
  }

  LLVMIntPredicate get_icmp_predicate() const {
    check_valid();
    require_instruction_opcodes<LLVMICmp>("icmp_predicate");
    return LLVMGetICmpPredicate(m_ref);
  }

  LLVMRealPredicate get_fcmp_predicate() const {
    check_valid();
    require_instruction_opcodes<LLVMFCmp>("fcmp_predicate");
    return LLVMGetFCmpPredicate(m_ref);
  }

  // Instruction flags
  bool get_nsw() const {
    check_valid();
    require_instruction_opcodes<LLVMAdd, LLVMSub, LLVMMul, LLVMShl>(
        "nsw", "an overflowing binary operator");
    return LLVMGetNSW(m_ref);
  }

  bool get_nuw() const {
    check_valid();
    require_instruction_opcodes<LLVMAdd, LLVMSub, LLVMMul, LLVMShl>(
        "nuw", "an overflowing binary operator");
    return LLVMGetNUW(m_ref);
  }

  bool get_exact() const {
    check_valid();
    require_instruction_opcodes<LLVMUDiv, LLVMSDiv, LLVMLShr, LLVMAShr>(
        "exact", "an exact-eligible binary operator");
    return LLVMGetExact(m_ref);
  }

  bool get_nneg() const {
    check_valid();
    require_instruction_opcodes<LLVMZExt>("nneg");
    return LLVMGetNNeg(m_ref);
  }

  // Memory access properties
  unsigned get_alignment() const {
    check_valid();
    bool global_alignment = LLVMIsAGlobalObject(m_ref);
    bool instruction_alignment =
        LLVMIsAInstruction(m_ref) &&
        (LLVMGetInstructionOpcode(m_ref) == LLVMAlloca ||
         LLVMGetInstructionOpcode(m_ref) == LLVMLoad ||
         LLVMGetInstructionOpcode(m_ref) == LLVMStore ||
         LLVMGetInstructionOpcode(m_ref) == LLVMAtomicRMW ||
         LLVMGetInstructionOpcode(m_ref) == LLVMAtomicCmpXchg);
    if (!global_alignment && !instruction_alignment) {
      throw LLVMAssertionError(
          "alignment requires a global object or aligned memory instruction");
    }
    return LLVMGetAlignment(m_ref);
  }

  void set_alignment(unsigned align) {
    check_valid();
    bool global_alignment = LLVMIsAGlobalObject(m_ref);
    bool instruction_alignment =
        LLVMIsAInstruction(m_ref) &&
        (LLVMGetInstructionOpcode(m_ref) == LLVMAlloca ||
         LLVMGetInstructionOpcode(m_ref) == LLVMLoad ||
         LLVMGetInstructionOpcode(m_ref) == LLVMStore ||
         LLVMGetInstructionOpcode(m_ref) == LLVMAtomicRMW ||
         LLVMGetInstructionOpcode(m_ref) == LLVMAtomicCmpXchg);
    if (!global_alignment && !instruction_alignment) {
      throw LLVMAssertionError(
          "set_alignment requires a global object or aligned memory instruction");
    }
    LLVMSetAlignment(m_ref, align);
  }

  bool get_volatile() const {
    check_valid();
    require_instruction_opcodes<LLVMLoad, LLVMStore, LLVMAtomicRMW,
                                LLVMAtomicCmpXchg>(
        "is_volatile", "a load, store, atomicrmw, or cmpxchg");
    return LLVMGetVolatile(m_ref);
  }

  void set_volatile(bool is_volatile) {
    check_valid();
    require_instruction_opcodes<LLVMLoad, LLVMStore, LLVMAtomicRMW,
                                LLVMAtomicCmpXchg>(
        "set_volatile", "a load, store, atomicrmw, or cmpxchg");
    LLVMSetVolatile(m_ref, is_volatile);
  }

  LLVMAtomicOrdering get_ordering() const {
    check_valid();
    require_instruction_opcodes<LLVMLoad, LLVMStore, LLVMFence, LLVMAtomicRMW,
                                LLVMAtomicCmpXchg>(
        "ordering", "an atomic-capable memory instruction");
    return LLVMGetOrdering(m_ref);
  }

  // Call/invoke properties
  unsigned get_num_arg_operands() const {
    check_valid();
    require_arg_operands_instruction("num_arg_operands");
    return LLVMGetNumArgOperands(m_ref);
  }

  // PHI node properties
  unsigned count_incoming() const {
    check_valid();
    require_phi_instruction("num_incoming");
    return LLVMCountIncoming(m_ref);
  }

  LLVMValueWrapper get_incoming_value(unsigned index) const {
    check_valid();
    require_phi_instruction("get_incoming_value");
    unsigned count = LLVMCountIncoming(m_ref);
    if (index >= count) {
      throw LLVMAssertionError(
          "get_incoming_value: incoming value index " + std::to_string(index) +
          " out of range (num_incoming=" + std::to_string(count) + ")");
    }
    LLVMValueRef incoming = LLVMGetIncomingValue(m_ref, index);
    if (!incoming) {
      throw LLVMAssertionError("get_incoming_value: incoming value at index " +
                               std::to_string(index) + " is null");
    }
    return LLVMValueWrapper(incoming, m_context_token);
  }

  // Forward declaration - defined after LLVMBasicBlockWrapper
  LLVMBasicBlockWrapper get_incoming_block(unsigned index) const;

  // Alloca properties
  LLVMTypeWrapper get_allocated_type() const {
    check_valid();
    require_instruction_opcodes<LLVMAlloca>("allocated_type");
    return LLVMTypeWrapper(LLVMGetAllocatedType(m_ref), m_context_token);
  }

  LLVMTypeWrapper get_function_type() const {
    check_valid();
    if (!is_a_function()) {
      throw LLVMAssertionError("Cannot get function_type from non-function");
    }
    return LLVMTypeWrapper(LLVMGlobalGetValueType(m_ref), m_context_token);
  }

  // =========================================================================
  // Phase 5.7: Operand Bundle Support
  // =========================================================================
  unsigned get_num_operand_bundles() const {
    check_valid();
    require_call_like_instruction("num_operand_bundles");
    return LLVMGetNumOperandBundles(m_ref);
  }

  // Get operand bundle at index - returns raw LLVMOperandBundleRef for cloning
  // Note: Caller is responsible for disposing of the returned bundle
  LLVMOperandBundleRef get_operand_bundle_at_index_raw(unsigned index) const {
    check_valid();
    require_call_like_instruction("get_operand_bundle_at_index");
    unsigned bundle_count = LLVMGetNumOperandBundles(m_ref);
    if (index >= bundle_count) {
      throw LLVMAssertionError(
          "get_operand_bundle_at_index: operand bundle index " +
          std::to_string(index) +
          " out of range (num_operand_bundles=" +
          std::to_string(bundle_count) + ")");
    }
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
      throw LLVMAssertionError("Value is not inline assembly");
    size_t len = 0;
    const char *str = LLVMGetInlineAsmAsmString(m_ref, &len);
    return std::string(str, len);
  }

  std::string get_inline_asm_constraint_string() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMAssertionError("Value is not inline assembly");
    size_t len = 0;
    const char *str = LLVMGetInlineAsmConstraintString(m_ref, &len);
    return std::string(str, len);
  }

  LLVMInlineAsmDialect get_inline_asm_dialect() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMAssertionError("Value is not inline assembly");
    return LLVMGetInlineAsmDialect(m_ref);
  }

  LLVMTypeWrapper get_inline_asm_function_type() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMAssertionError("Value is not inline assembly");
    return LLVMTypeWrapper(LLVMGetInlineAsmFunctionType(m_ref),
                           m_context_token);
  }

  bool get_inline_asm_has_side_effects() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMAssertionError("Value is not inline assembly");
    return LLVMGetInlineAsmHasSideEffects(m_ref);
  }

  bool get_inline_asm_needs_aligned_stack() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMAssertionError("Value is not inline assembly");
    return LLVMGetInlineAsmNeedsAlignedStack(m_ref);
  }

  bool get_inline_asm_can_unwind() const {
    check_valid();
    if (!is_a_inline_asm())
      throw LLVMAssertionError("Value is not inline assembly");
    return LLVMGetInlineAsmCanUnwind(m_ref);
  }

  // =========================================================================
  // Phase 5.9: Flag Setters and Additional Properties
  // =========================================================================

  // Flag setters (pairs with existing getters)
  void set_nsw(bool nsw) {
    check_valid();
    require_instruction_opcodes<LLVMAdd, LLVMSub, LLVMMul, LLVMShl>(
        "set_nsw", "an overflowing binary operator");
    LLVMSetNSW(m_ref, nsw);
  }

  void set_nuw(bool nuw) {
    check_valid();
    require_instruction_opcodes<LLVMAdd, LLVMSub, LLVMMul, LLVMShl>(
        "set_nuw", "an overflowing binary operator");
    LLVMSetNUW(m_ref, nuw);
  }

  void set_exact(bool exact) {
    check_valid();
    require_instruction_opcodes<LLVMUDiv, LLVMSDiv, LLVMLShr, LLVMAShr>(
        "set_exact", "an exact-eligible binary operator");
    LLVMSetExact(m_ref, exact);
  }

  void set_nneg(bool nneg) {
    check_valid();
    require_instruction_opcodes<LLVMZExt>("set_nneg");
    LLVMSetNNeg(m_ref, nneg);
  }

  // Disjoint flag for Or instruction
  bool get_is_disjoint() const {
    check_valid();
    require_instruction_opcodes<LLVMOr>("is_disjoint");
    return LLVMGetIsDisjoint(m_ref);
  }

  void set_is_disjoint(bool is_disjoint) {
    check_valid();
    require_instruction_opcodes<LLVMOr>("set_is_disjoint");
    LLVMSetIsDisjoint(m_ref, is_disjoint);
  }

  // ICmp same sign flag
  bool get_icmp_same_sign() const {
    check_valid();
    require_instruction_opcodes<LLVMICmp>("icmp_same_sign");
    return LLVMGetICmpSameSign(m_ref);
  }

  void set_icmp_same_sign(bool same_sign) {
    check_valid();
    require_instruction_opcodes<LLVMICmp>("set_icmp_same_sign");
    LLVMSetICmpSameSign(m_ref, same_sign);
  }

  // Memory instruction setters
  void set_ordering(LLVMAtomicOrdering ordering) {
    check_valid();
    require_instruction_opcodes<LLVMLoad, LLVMStore, LLVMFence, LLVMAtomicRMW,
                                LLVMAtomicCmpXchg>(
        "set_ordering", "an atomic-capable memory instruction");
    LLVMSetOrdering(m_ref, ordering);
  }

  // Atomic properties
  bool is_atomic() const {
    check_valid();
    require_instruction_value("is_atomic");
    return LLVMIsAtomic(m_ref);
  }

  unsigned get_atomic_sync_scope_id() const {
    check_valid();
    require_instruction_opcodes<LLVMLoad, LLVMStore, LLVMFence, LLVMAtomicRMW,
                                LLVMAtomicCmpXchg>(
        "atomic_sync_scope_id", "an atomic-capable memory instruction");
    return LLVMGetAtomicSyncScopeID(m_ref);
  }

  void set_atomic_sync_scope_id(unsigned scope_id) {
    check_valid();
    require_instruction_opcodes<LLVMLoad, LLVMStore, LLVMFence, LLVMAtomicRMW,
                                LLVMAtomicCmpXchg>(
        "set_atomic_sync_scope_id", "an atomic-capable memory instruction");
    LLVMSetAtomicSyncScopeID(m_ref, scope_id);
  }

  LLVMAtomicRMWBinOp get_atomic_rmw_bin_op() const {
    check_valid();
    require_instruction_opcodes<LLVMAtomicRMW>("atomic_rmw_bin_op",
                                               "an atomicrmw");
    return LLVMGetAtomicRMWBinOp(m_ref);
  }

  // CmpXchg ordering
  LLVMAtomicOrdering get_cmpxchg_success_ordering() const {
    check_valid();
    require_instruction_opcodes<LLVMAtomicCmpXchg>(
        "cmpxchg_success_ordering", "a cmpxchg");
    return LLVMGetCmpXchgSuccessOrdering(m_ref);
  }

  LLVMAtomicOrdering get_cmpxchg_failure_ordering() const {
    check_valid();
    require_instruction_opcodes<LLVMAtomicCmpXchg>(
        "cmpxchg_failure_ordering", "a cmpxchg");
    return LLVMGetCmpXchgFailureOrdering(m_ref);
  }

  // CmpXchg weak flag
  bool get_weak() const {
    check_valid();
    require_instruction_opcodes<LLVMAtomicCmpXchg>("weak");
    return LLVMGetWeak(m_ref);
  }

  void set_weak(bool is_weak) {
    check_valid();
    require_instruction_opcodes<LLVMAtomicCmpXchg>("set_weak");
    LLVMSetWeak(m_ref, is_weak);
  }

  // Tail call kind
  LLVMTailCallKind get_tail_call_kind() const {
    check_valid();
    require_instruction_opcodes<LLVMCall, LLVMInvoke>("tail_call_kind",
                                                      "a call or invoke");
    return LLVMGetTailCallKind(m_ref);
  }

  void set_tail_call_kind(LLVMTailCallKind kind) {
    check_valid();
    require_instruction_opcodes<LLVMCall, LLVMInvoke>("set_tail_call_kind",
                                                      "a call or invoke");
    LLVMSetTailCallKind(m_ref, kind);
  }

  // Called function type and value
  LLVMTypeWrapper get_called_function_type() const {
    check_valid();
    require_call_like_instruction("called_function_type");
    return LLVMTypeWrapper(LLVMGetCalledFunctionType(m_ref), m_context_token);
  }

  LLVMValueWrapper get_called_value() const {
    check_valid();
    require_call_like_instruction("called_value");
    return LLVMValueWrapper(LLVMGetCalledValue(m_ref), m_context_token);
  }

  LLVMCallConv get_instruction_call_conv() const {
    check_valid();
    require_call_like_instruction("instruction_call_conv");
    return static_cast<LLVMCallConv>(LLVMGetInstructionCallConv(m_ref));
  }

  void set_instruction_call_conv(LLVMCallConv cc) {
    check_valid();
    require_call_like_instruction("instruction_call_conv");
    LLVMSetInstructionCallConv(m_ref, cc);
  }

  void set_called_operand(const LLVMValueWrapper &val) {
    check_valid();
    require_call_like_instruction("set_called_operand");
    val.check_valid();
    // In LLVM's CallBase, the callee is the last operand
    unsigned num_ops = static_cast<unsigned>(LLVMGetNumOperands(m_ref));
    if (num_ops == 0)
      throw LLVMAssertionError("Cannot set called operand on value with no operands");
    LLVMSetOperand(m_ref, num_ops - 1, val.m_ref);
  }

  // Branch condition
  bool is_conditional() const {
    check_valid();
    require_instruction_opcodes<LLVMBr>("is_conditional");
    return LLVMIsConditional(m_ref);
  }

  LLVMValueWrapper get_condition() const {
    check_valid();
    require_instruction_opcodes<LLVMBr>("condition");
    LLVMValueRef cond = LLVMGetCondition(m_ref);
    if (!cond)
      throw LLVMAssertionError(
          "condition: conditional branch has no condition");
    return LLVMValueWrapper(cond, m_context_token);
  }

  // Branch successors
  unsigned get_num_successors() const {
    check_valid();
    require_terminator_instruction("num_successors");
    return LLVMGetNumSuccessors(m_ref);
  }

  // Landing pad properties
  unsigned get_num_clauses() const {
    check_valid();
    require_landingpad_instruction("num_clauses");
    return LLVMGetNumClauses(m_ref);
  }

  LLVMValueWrapper get_clause(unsigned index) const {
    check_valid();
    require_landingpad_instruction("get_clause");
    unsigned count = LLVMGetNumClauses(m_ref);
    if (index >= count) {
      throw LLVMAssertionError("get_clause: clause index " +
                               std::to_string(index) +
                               " out of range (num_clauses=" +
                               std::to_string(count) + ")");
    }
    LLVMValueRef clause = LLVMGetClause(m_ref, index);
    if (!clause)
      throw LLVMAssertionError("get_clause: clause at index " +
                               std::to_string(index) + " is null");
    return LLVMValueWrapper(clause, m_context_token);
  }

  bool is_cleanup() const {
    check_valid();
    require_landingpad_instruction("is_cleanup");
    return LLVMIsCleanup(m_ref);
  }

  void set_cleanup(bool is_cleanup_val) {
    check_valid();
    require_landingpad_instruction("set_cleanup");
    LLVMSetCleanup(m_ref, is_cleanup_val);
  }

  // CatchSwitch/CatchPad properties
  LLVMValueWrapper get_parent_catch_switch() const {
    check_valid();
    require_catchpad_instruction("parent_catch_switch");
    return LLVMValueWrapper(LLVMGetParentCatchSwitch(m_ref), m_context_token);
  }

  unsigned get_num_handlers() const {
    check_valid();
    require_catchswitch_instruction("num_handlers");
    return LLVMGetNumHandlers(m_ref);
  }

  // Get handlers for catch switch - forward declared, implemented after
  // LLVMBasicBlockWrapper
  std::vector<LLVMBasicBlockWrapper> get_handlers() const;

  // Add clause to landing pad
  void add_clause(const LLVMValueWrapper &clause_val) {
    check_valid();
    clause_val.check_valid();
    require_landingpad_instruction("add_clause");
    LLVMAddClause(m_ref, clause_val.m_ref);
  }

  // Add handler to catch switch - forward declared, implemented after
  // LLVMBasicBlockWrapper
  void add_handler(const LLVMBasicBlockWrapper &handler);

  // Get operand bundle at index
  LLVMOperandBundleWrapper get_operand_bundle_at_index(unsigned index) const {
    check_valid();
    require_call_like_instruction("get_operand_bundle_at_index");
    unsigned bundle_count = LLVMGetNumOperandBundles(m_ref);
    if (index >= bundle_count) {
      throw LLVMAssertionError(
          "get_operand_bundle_at_index: operand bundle index " +
          std::to_string(index) +
          " out of range (num_operand_bundles=" +
          std::to_string(bundle_count) + ")");
    }
    LLVMOperandBundleRef bundle = LLVMGetOperandBundleAtIndex(m_ref, index);
    if (!bundle) {
      throw LLVMAssertionError("get_operand_bundle_at_index: operand bundle at index " +
                               std::to_string(index) + " is null");
    }
    return LLVMOperandBundleWrapper(bundle, m_context_token);
  }

  // Get indices for extractvalue/insertvalue
  std::vector<unsigned> get_indices() const {
    check_valid();
    bool is_indexed_instruction =
        LLVMIsAInstruction(m_ref) &&
        (LLVMGetInstructionOpcode(m_ref) == LLVMExtractValue ||
         LLVMGetInstructionOpcode(m_ref) == LLVMInsertValue);
    bool is_indexed_const_expr =
        LLVMIsAConstantExpr(m_ref) &&
        (LLVMGetConstOpcode(m_ref) == LLVMGetElementPtr ||
         LLVMGetConstOpcode(m_ref) == LLVMExtractValue ||
         LLVMGetConstOpcode(m_ref) == LLVMInsertValue);
    if (!is_indexed_instruction && !is_indexed_const_expr) {
      throw LLVMAssertionError(
          "indices requires extractvalue, insertvalue, or constant-expression "
          "getelementptr");
    }
    unsigned num_indices = LLVMGetNumIndices(m_ref);
    const unsigned *indices = LLVMGetIndices(m_ref);
    if (!indices && num_indices > 0) {
      throw LLVMAssertionError("indices returned null pointer");
    }
    return std::vector<unsigned>(indices, indices + num_indices);
  }

  // ShuffleVector mask
  unsigned get_num_mask_elements() const {
    check_valid();
    require_shufflevector_instruction("num_mask_elements");
    return LLVMGetNumMaskElements(m_ref);
  }

  int get_mask_value(unsigned index) const {
    check_valid();
    require_shufflevector_instruction("get_mask_value");
    unsigned count = LLVMGetNumMaskElements(m_ref);
    if (index >= count) {
      throw LLVMAssertionError("get_mask_value: mask index " +
                               std::to_string(index) +
                               " out of range (num_mask_elements=" +
                               std::to_string(count) + ")");
    }
    return LLVMGetMaskValue(m_ref, index);
  }

  // Fast-math flags
  bool can_use_fast_math_flags() const {
    check_valid();
    return LLVMCanValueUseFastMathFlags(m_ref);
  }

  LLVMFastMathFlags get_fast_math_flags() const {
    check_valid();
    if (!LLVMCanValueUseFastMathFlags(m_ref)) {
      throw LLVMAssertionError(
          "fast_math_flags requires a value that supports fast-math flags");
    }
    return LLVMGetFastMathFlags(m_ref);
  }

  void set_fast_math_flags(LLVMFastMathFlags flags) {
    check_valid();
    if (!LLVMCanValueUseFastMathFlags(m_ref)) {
      throw LLVMAssertionError(
          "set_fast_math_flags requires a value that supports fast-math flags");
    }
    LLVMSetFastMathFlags(m_ref, flags);
  }

  // Get arg operand (for call instructions)
  LLVMValueWrapper get_arg_operand(unsigned index) const {
    check_valid();
    require_arg_operands_instruction("get_arg_operand");
    unsigned count = LLVMGetNumArgOperands(m_ref);
    if (index >= count) {
      throw LLVMAssertionError("get_arg_operand: arg operand index " +
                               std::to_string(index) +
                               " out of range (num_arg_operands=" +
                               std::to_string(count) + ")");
    }
    LLVMValueRef arg = LLVMGetArgOperand(m_ref, index);
    if (!arg) {
      throw LLVMAssertionError("get_arg_operand: arg operand at index " +
                               std::to_string(index) + " is null");
    }
    return LLVMValueWrapper(arg, m_context_token);
  }

  // Instruction manipulation
  void remove_from_parent() {
    check_valid();
    require_instruction_value("remove_from_parent");
    LLVMInstructionRemoveFromParent(m_ref);
  }

  // Validate whether this instruction can be inserted at (dest_bb, insert_before).
  // insert_before == nullptr means insertion at end of dest_bb.
  void validate_instruction_move_insertion(LLVMBasicBlockRef dest_bb,
                                           LLVMValueRef insert_before) const {
    if (!dest_bb)
      throw LLVMAssertionError("Move destination basic block is null");
    if (insert_before && LLVMGetInstructionParent(insert_before) != dest_bb)
      throw LLVMAssertionError(
          "Move insertion point does not belong to destination basic block");

    LLVMOpcode moving_op = LLVMGetInstructionOpcode(m_ref);
    bool moving_phi = (moving_op == LLVMPHI);
    bool moving_landingpad = (moving_op == LLVMLandingPad);
    bool moving_terminator = (LLVMIsATerminatorInst(m_ref) != nullptr);

    // Find the first non-PHI instruction in destination block.
    LLVMValueRef first_non_phi = LLVMGetFirstInstruction(dest_bb);
    while (first_non_phi &&
           LLVMGetInstructionOpcode(first_non_phi) == LLVMPHI) {
      first_non_phi = LLVMGetNextInstruction(first_non_phi);
    }

    // PHI placement constraints: PHIs must stay in the PHI prefix.
    if (moving_phi) {
      if (!insert_before)
        throw LLVMAssertionError("Cannot insert PHI node at end of basic block");
      if (insert_before != first_non_phi &&
          LLVMGetInstructionOpcode(insert_before) != LLVMPHI) {
        throw LLVMAssertionError(
            "PHI nodes must be inserted in the PHI prefix of the basic block");
      }
    } else {
      if (insert_before && LLVMGetInstructionOpcode(insert_before) == LLVMPHI) {
        throw LLVMAssertionError("Cannot insert non-PHI instruction before PHI "
                                 "nodes in a basic block");
      }
    }

    // LandingPad must be the first non-PHI instruction.
    if (moving_landingpad && insert_before != first_non_phi) {
      throw LLVMAssertionError(
          "LandingPad must be inserted as the first non-PHI instruction");
    }

    // Terminator placement constraints.
    LLVMValueRef current_term = LLVMGetBasicBlockTerminator(dest_bb);
    if (moving_terminator) {
      if (insert_before)
        throw LLVMAssertionError(
            "Terminator instructions can only be moved to the end of a basic "
            "block");
      if (current_term && current_term != m_ref) {
        throw LLVMAssertionError(
            "Destination basic block already has a different terminator");
      }
    } else {
      if (!insert_before && current_term && current_term != m_ref) {
        throw LLVMAssertionError(
            "Cannot insert non-terminator instruction after block terminator");
      }
    }
  }

  // Move this instruction before another instruction.
  void move_before(const LLVMValueWrapper &other, bool preserve = false) {
    check_valid();
    other.check_valid();
    if (m_context_token != other.m_context_token)
      throw LLVMAssertionError(
          "Cannot move instructions across different contexts");
    if (!LLVMIsAInstruction(m_ref))
      throw LLVMAssertionError("move_before requires an instruction");
    if (!LLVMIsAInstruction(other.m_ref))
      throw LLVMAssertionError("move_before target must be an instruction");
    if (m_ref == other.m_ref)
      return;

    LLVMBasicBlockRef other_parent = LLVMGetInstructionParent(other.m_ref);
    if (!other_parent)
      throw LLVMAssertionError(
          "move_before target instruction has no parent block");

    LLVMValueRef fn = LLVMGetBasicBlockParent(other_parent);
    LLVMModuleRef mod = fn ? LLVMGetGlobalParent(fn) : nullptr;
    if (!mod)
      throw LLVMAssertionError(
          "move_before target instruction has no parent module");
    LLVMContextRef ctx = LLVMGetModuleContext(mod);

    validate_instruction_move_insertion(other_parent, other.m_ref);

    LLVMBasicBlockRef this_parent = LLVMGetInstructionParent(m_ref);
    if (this_parent)
      LLVMInstructionRemoveFromParent(m_ref);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
    if (preserve)
      LLVMPositionBuilderBeforeInstrAndDbgRecords(builder, other.m_ref);
    else
      LLVMPositionBuilderBefore(builder, other.m_ref);
    LLVMInsertIntoBuilder(builder, m_ref);
    LLVMDisposeBuilder(builder);
  }

  // Move this instruction after another instruction.
  void move_after(const LLVMValueWrapper &other, bool preserve = false) {
    check_valid();
    other.check_valid();
    if (m_context_token != other.m_context_token)
      throw LLVMAssertionError(
          "Cannot move instructions across different contexts");
    if (!LLVMIsAInstruction(m_ref))
      throw LLVMAssertionError("move_after requires an instruction");
    if (!LLVMIsAInstruction(other.m_ref))
      throw LLVMAssertionError("move_after target must be an instruction");
    if (m_ref == other.m_ref)
      return;
    if (LLVMIsATerminatorInst(other.m_ref))
      throw LLVMAssertionError("Cannot move instruction after a terminator");

    LLVMBasicBlockRef other_parent = LLVMGetInstructionParent(other.m_ref);
    if (!other_parent)
      throw LLVMAssertionError(
          "move_after target instruction has no parent block");

    LLVMValueRef fn = LLVMGetBasicBlockParent(other_parent);
    LLVMModuleRef mod = fn ? LLVMGetGlobalParent(fn) : nullptr;
    if (!mod)
      throw LLVMAssertionError(
          "move_after target instruction has no parent module");
    LLVMContextRef ctx = LLVMGetModuleContext(mod);

    LLVMBasicBlockRef this_parent = LLVMGetInstructionParent(m_ref);

    // Compute insertion point in the destination block *after* conceptual
    // unlinking of self, but validate before mutating anything.
    LLVMValueRef next = LLVMGetNextInstruction(other.m_ref);
    if (this_parent == other_parent && next == m_ref) {
      next = LLVMGetNextInstruction(m_ref);
    }
    validate_instruction_move_insertion(other_parent, next);

    if (this_parent)
      LLVMInstructionRemoveFromParent(m_ref);

    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
    if (next) {
      if (preserve)
        LLVMPositionBuilderBeforeInstrAndDbgRecords(builder, next);
      else
        LLVMPositionBuilderBefore(builder, next);
    }
    else
      LLVMPositionBuilderAtEnd(builder, other_parent);
    LLVMInsertIntoBuilder(builder, m_ref);
    LLVMDisposeBuilder(builder);
  }

  bool is_a_instruction() const {
    check_valid();
    return LLVMIsAInstruction(m_ref) != nullptr;
  }

  bool is_a_terminator_inst() const {
    check_valid();
    return LLVMIsATerminatorInst(m_ref) != nullptr;
  }

  // Check if this value is a parameter (argument)
  bool is_a_argument() const {
    check_valid();
    return LLVMIsAArgument(m_ref) != nullptr;
  }

  // =========================================================================
  // Parent navigation for Values (instructions, parameters, globals)
  // =========================================================================

  // Get the block this instruction belongs to (throws if not an instruction)
  LLVMBasicBlockWrapper block() const;

  // Get the function this value belongs to:
  // - For instructions: derived via block
  // - For parameters: uses LLVMGetParamParent
  // - For globals: throws (use .module instead)
  LLVMFunctionWrapper get_function() const;

  // Get the module this value belongs to:
  // - For instructions: derived via block -> function -> module
  // - For parameters: derived via function -> module
  // - For globals: uses LLVMGetGlobalParent
  LLVMModuleWrapper *get_module() const;

  // Get the context this value belongs to:
  // - For all value types: derived via module -> context
  LLVMContextWrapper *get_context() const;

  // BasicBlock properties - forward declared
  LLVMBasicBlockWrapper get_normal_dest() const;
  std::optional<LLVMBasicBlockWrapper> get_unwind_dest() const;
  LLVMBasicBlockWrapper get_successor(unsigned index) const;
  std::vector<LLVMBasicBlockWrapper> successors() const;
  LLVMBasicBlockWrapper get_callbr_default_dest() const;
  unsigned get_callbr_num_indirect_dests() const;
  LLVMBasicBlockWrapper get_callbr_indirect_dest(unsigned index) const;

  // Value/BasicBlock conversion
  bool value_is_basic_block() const {
    check_valid();
    return LLVMValueIsBasicBlock(m_ref);
  }

  LLVMBasicBlockWrapper value_as_basic_block() const;

  // Constant bitcast - creates a ConstantExpr bitcast
  LLVMValueWrapper const_bitcast(const LLVMTypeWrapper &ty) const {
    check_valid();
    ty.check_valid();
    if (!LLVMIsConstant(m_ref))
      throw LLVMAssertionError("const_bitcast requires a constant value");
    return LLVMValueWrapper(LLVMConstBitCast(m_ref, ty.m_ref), m_context_token);
  }

  // Delete an instruction from its parent basic block
  void delete_instruction() {
    check_valid();
    require_instruction_value("delete_instruction");
    // LLVMDeleteInstruction can be fragile for parented instructions on some
    // builds. Prefer erase-from-parent when attached to a block.
    LLVMBasicBlockRef parent = LLVMGetInstructionParent(m_ref);
    if (parent) {
      LLVMInstructionEraseFromParent(m_ref);
    } else {
      LLVMDeleteInstruction(m_ref);
    }
    m_ref = nullptr; // Invalidate after deletion
  }

  // Erase instruction from parent block and delete it (combines
  // remove_from_parent + delete_instruction). Mirrors LLVM C++
  // Instruction::eraseFromParent().
  void erase_from_parent() {
    check_valid();
    require_instruction_value("erase_from_parent");
    LLVMInstructionEraseFromParent(m_ref);
    m_ref = nullptr;
  }

  // Clone an instruction. The clone has no parent and no name.
  LLVMValueWrapper instruction_clone() const {
    check_valid();
    require_instruction_value("instruction_clone");
    LLVMValueRef cloned = LLVMInstructionClone(m_ref);
    if (!cloned)
      throw LLVMAssertionError("Failed to clone instruction");
    return LLVMValueWrapper(cloned, m_context_token);
  }

  // Replace all uses of this value with another value.
  // Wraps LLVMReplaceAllUsesWith.
  void replace_all_uses_with(const LLVMValueWrapper &new_value) {
    check_valid();
    new_value.check_valid();
    if (m_context_token != new_value.m_context_token) {
      throw LLVMAssertionError(
          "replace_all_uses_with requires values from the same context");
    }
    LLVMTypeRef self_ty = LLVMTypeOf(m_ref);
    LLVMTypeRef other_ty = LLVMTypeOf(new_value.m_ref);
    if (self_ty != other_ty) {
      throw LLVMAssertionError(
          "replace_all_uses_with requires replacement value of identical type");
    }
    LLVMReplaceAllUsesWith(m_ref, new_value.m_ref);
  }

  // Convert value to metadata - declared here, implemented after
  // LLVMMetadataWrapper
  LLVMMetadataWrapper as_metadata() const;

  // Callsite attribute methods (for call/invoke instructions)
  unsigned get_callsite_attribute_count(int idx) const {
    check_valid();
    require_call_like_instruction("get_callsite_attribute_count");
    if (idx < -1) {
      throw LLVMAssertionError(
          "get_callsite_attribute_count requires idx >= -1");
    }
    unsigned num_args = LLVMGetNumArgOperands(m_ref);
    if (idx > static_cast<int>(num_args)) {
      throw LLVMAssertionError(
          "get_callsite_attribute_count: idx " + std::to_string(idx) +
          " out of range for callsite (valid: -1..num_arg_operands, "
          "num_arg_operands=" +
          std::to_string(num_args) + ")");
    }
    return LLVMGetCallSiteAttributeCount(m_ref, static_cast<unsigned>(idx));
  }

  std::optional<LLVMAttributeWrapper>
  get_callsite_enum_attribute(int idx, unsigned kind_id) const {
    check_valid();
    require_call_like_instruction("get_callsite_enum_attribute");
    if (idx < -1) {
      throw LLVMAssertionError(
          "get_callsite_enum_attribute requires idx >= -1");
    }
    unsigned num_args = LLVMGetNumArgOperands(m_ref);
    if (idx > static_cast<int>(num_args)) {
      throw LLVMAssertionError(
          "get_callsite_enum_attribute: idx " + std::to_string(idx) +
          " out of range for callsite (valid: -1..num_arg_operands, "
          "num_arg_operands=" +
          std::to_string(num_args) + ")");
    }
    LLVMAttributeRef ref = LLVMGetCallSiteEnumAttribute(
        m_ref, static_cast<unsigned>(idx), kind_id);
    if (!ref)
      return std::nullopt;
    return LLVMAttributeWrapper(ref, m_context_token);
  }

  void add_callsite_attribute(int idx, const LLVMAttributeWrapper &attr) {
    check_valid();
    attr.check_valid();
    require_call_like_instruction("add_callsite_attribute");
    if (idx < -1) {
      throw LLVMAssertionError("add_callsite_attribute requires idx >= -1");
    }
    unsigned num_args = LLVMGetNumArgOperands(m_ref);
    if (idx > static_cast<int>(num_args)) {
      throw LLVMAssertionError(
          "add_callsite_attribute: idx " + std::to_string(idx) +
          " out of range for callsite (valid: -1..num_arg_operands, "
          "num_arg_operands=" +
          std::to_string(num_args) + ")");
    }
    LLVMAddCallSiteAttribute(m_ref, static_cast<unsigned>(idx), attr.m_ref);
  }

  // Unified set_metadata - works for both instructions and globals
  // Declared here, implemented after LLVMMetadataWrapper
  // Takes a context for converting metadata to value (needed for instructions)
  void set_metadata(unsigned kind, const LLVMMetadataWrapper &md,
                    LLVMContextWrapper &ctx);

  // Create a builder positioned before this instruction
  LLVMBuilderManager *create_builder(bool before_dbg) const;
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
      throw LLVMMemoryError("BasicBlock is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("BasicBlock used after context was destroyed");
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

  // Print the basic block's IR as a string
  std::string to_string() const {
    check_valid();
    LLVMValueRef val = LLVMBasicBlockAsValue(m_ref);
    char *str = LLVMPrintValueToString(val);
    std::string result(str);
    LLVMDisposeMessage(str);
    return result;
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

  bool has_terminator() const {
    check_valid();
    return LLVMGetBasicBlockTerminator(m_ref) != nullptr;
  }

  LLVMValueWrapper terminator() const {
    check_valid();
    LLVMValueRef term = LLVMGetBasicBlockTerminator(m_ref);
    if (!term)
      throw LLVMAssertionError("BasicBlock has no terminator");
    return LLVMValueWrapper(term, m_context_token);
  }

  std::vector<LLVMValueWrapper> instructions() const {
    check_valid();
    std::vector<LLVMValueWrapper> result;
    for (LLVMValueRef inst = LLVMGetFirstInstruction(m_ref); inst;
         inst = LLVMGetNextInstruction(inst)) {
      result.emplace_back(inst, m_context_token);
    }
    return result;
  }

  // Get the first instruction that is not a PHI node
  std::optional<LLVMValueWrapper> first_non_phi() const {
    check_valid();
    for (LLVMValueRef inst = LLVMGetFirstInstruction(m_ref); inst;
         inst = LLVMGetNextInstruction(inst)) {
      if (LLVMGetInstructionOpcode(inst) != LLVMPHI) {
        return LLVMValueWrapper(inst, m_context_token);
      }
    }
    return std::nullopt;
  }

  // Get all PHI nodes at the beginning of this block
  std::vector<LLVMValueWrapper> phis() const {
    check_valid();
    std::vector<LLVMValueWrapper> result;
    for (LLVMValueRef inst = LLVMGetFirstInstruction(m_ref); inst;
         inst = LLVMGetNextInstruction(inst)) {
      if (LLVMGetInstructionOpcode(inst) != LLVMPHI)
        break;
      result.emplace_back(inst, m_context_token);
    }
    return result;
  }

  // Get uses of this block's value (delegates to as_value)
  std::vector<LLVMUseWrapper> uses() const {
    check_valid();
    LLVMValueRef block_value = LLVMBasicBlockAsValue(m_ref);
    std::vector<LLVMUseWrapper> result;
    for (LLVMUseRef use = LLVMGetFirstUse(block_value); use != nullptr;
         use = LLVMGetNextUse(use)) {
      result.emplace_back(use, m_context_token);
    }
    return result;
  }

  // Get users of this block's value (delegates to as_value)
  std::vector<LLVMValueWrapper> users() const {
    check_valid();
    LLVMValueRef block_value = LLVMBasicBlockAsValue(m_ref);
    std::unordered_set<LLVMValueRef> visited;
    std::vector<LLVMValueWrapper> result;
    for (LLVMUseRef use = LLVMGetFirstUse(block_value); use != nullptr;
         use = LLVMGetNextUse(use)) {
      auto user = LLVMGetUser(use);
      if (visited.find(user) == visited.end()) {
        visited.insert(user);
        result.emplace_back(user, m_context_token);
      }
    }
    return result;
  }

  // A block can be temporarily empty while being constructed.
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

  // =========================================================================
  // Parent navigation: block -> function -> module -> context
  // =========================================================================

  // Get the function this basic block belongs to
  LLVMFunctionWrapper function() const;

  // Get the module this basic block belongs to (derived via function)
  LLVMModuleWrapper *module() const;

  // Get the context this basic block belongs to (derived via function ->
  // module)
  LLVMContextWrapper *context() const;

  // Split this block at an instruction, creating a new successor block.
  LLVMBasicBlockWrapper split_basic_block(const LLVMValueWrapper &instruction,
                                          const std::string &name = "") const {
    check_valid();
    instruction.check_valid();
    if (!LLVMIsAInstruction(instruction.m_ref))
      throw LLVMAssertionError("instruction must be an instruction");
    if (LLVMGetInstructionParent(instruction.m_ref) != m_ref)
      throw LLVMAssertionError("instruction not found in this block");
    if (LLVMGetInstructionOpcode(instruction.m_ref) == LLVMPHI)
      throw LLVMAssertionError(
          "instruction cannot be a PHI node (would produce invalid IR)");
    if (!LLVMGetBasicBlockTerminator(m_ref))
      throw LLVMAssertionError(
          "split_basic_block requires a well-formed block with a terminator");

    LLVMValueRef func = LLVMGetBasicBlockParent(m_ref);
    LLVMModuleRef mod = LLVMGetGlobalParent(func);
    LLVMContextRef ctx = LLVMGetModuleContext(mod);

    // Collect instructions to move (from instruction to end, inclusive)
    std::vector<LLVMValueRef> to_move;
    bool found = false;
    for (LLVMValueRef inst = LLVMGetFirstInstruction(m_ref); inst;
         inst = LLVMGetNextInstruction(inst)) {
      if (inst == instruction.m_ref)
        found = true;
      if (found)
        to_move.push_back(inst);
    }

    if (!found || to_move.empty())
      throw LLVMAssertionError("instruction not found in this block");

    // Create a new basic block appended to the function, then move it
    LLVMBasicBlockRef new_bb =
        LLVMAppendBasicBlockInContext(ctx, func, name.c_str());
    LLVMMoveBasicBlockAfter(new_bb, m_ref);

    // Move each instruction to the new block using a builder
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(builder, new_bb);
    for (LLVMValueRef inst : to_move) {
      LLVMInstructionRemoveFromParent(inst);
      LLVMInsertIntoBuilder(builder, inst);
    }
    LLVMDisposeBuilder(builder);

    // Add an unconditional branch from old block to new block
    LLVMBuilderRef br_builder = LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(br_builder, m_ref);
    LLVMBuildBr(br_builder, new_bb);
    LLVMDisposeBuilder(br_builder);

    // Update PHI nodes in successor blocks: replace references to old block
    // with new block. Since the C API has no LLVMSetIncomingBlock, we rebuild
    // each affected PHI node.
    LLVMValueRef new_term = LLVMGetBasicBlockTerminator(new_bb);
    if (new_term) {
      unsigned num_succ = LLVMGetNumSuccessors(new_term);
      for (unsigned si = 0; si < num_succ; ++si) {
        LLVMBasicBlockRef succ = LLVMGetSuccessor(new_term, si);

        // Collect all PHI nodes in this successor
        std::vector<LLVMValueRef> phis;
        for (LLVMValueRef inst = LLVMGetFirstInstruction(succ); inst;
             inst = LLVMGetNextInstruction(inst)) {
          if (LLVMGetInstructionOpcode(inst) != LLVMPHI)
            break;
          phis.push_back(inst);
        }

        for (LLVMValueRef phi : phis) {
          unsigned num_inc = LLVMCountIncoming(phi);
          bool needs_update = false;

          // Check if any incoming block references old block
          for (unsigned i = 0; i < num_inc; ++i) {
            if (LLVMGetIncomingBlock(phi, i) == m_ref) {
              needs_update = true;
              break;
            }
          }

          if (!needs_update)
            continue;

          // Collect all incoming (value, block) pairs
          std::vector<LLVMValueRef> values(num_inc);
          std::vector<LLVMBasicBlockRef> blocks(num_inc);
          for (unsigned i = 0; i < num_inc; ++i) {
            values[i] = LLVMGetIncomingValue(phi, i);
            blocks[i] = LLVMGetIncomingBlock(phi, i);
            // Replace old block with new block
            if (blocks[i] == m_ref)
              blocks[i] = new_bb;
          }

          // Build a new PHI node with updated incoming blocks
          LLVMBuilderRef phi_builder = LLVMCreateBuilderInContext(ctx);
          // Position before the first non-PHI instruction in successor
          LLVMValueRef first_non_phi = nullptr;
          for (LLVMValueRef inst = LLVMGetFirstInstruction(succ); inst;
               inst = LLVMGetNextInstruction(inst)) {
            if (LLVMGetInstructionOpcode(inst) != LLVMPHI) {
              first_non_phi = inst;
              break;
            }
          }
          if (first_non_phi)
            LLVMPositionBuilderBefore(phi_builder, first_non_phi);
          else
            LLVMPositionBuilderAtEnd(phi_builder, succ);

          LLVMValueRef new_phi =
              LLVMBuildPhi(phi_builder, LLVMTypeOf(phi), "");
          LLVMAddIncoming(new_phi, values.data(), blocks.data(), num_inc);

          // Copy the name from old PHI
          const char *phi_name = LLVMGetValueName(phi);
          if (phi_name && phi_name[0] != '\0')
            LLVMSetValueName(new_phi, phi_name);

          // Replace all uses and erase old PHI
          LLVMReplaceAllUsesWith(phi, new_phi);
          LLVMInstructionEraseFromParent(phi);

          LLVMDisposeBuilder(phi_builder);
        }
      }
    }

    return LLVMBasicBlockWrapper(new_bb, m_context_token);
  }

  // Split this block before an instruction, creating a new predecessor block.
  LLVMBasicBlockWrapper
  split_basic_block_before(const LLVMValueWrapper &instruction,
                           const std::string &name = "") const {
    check_valid();
    instruction.check_valid();
    if (!LLVMIsAInstruction(instruction.m_ref))
      throw LLVMAssertionError("instruction must be an instruction");
    if (LLVMGetInstructionParent(instruction.m_ref) != m_ref)
      throw LLVMAssertionError("instruction not found in this block");
    if (LLVMGetInstructionOpcode(instruction.m_ref) == LLVMPHI)
      throw LLVMAssertionError(
          "instruction cannot be a PHI node");
    if (!LLVMGetBasicBlockTerminator(m_ref))
      throw LLVMAssertionError(
          "split_basic_block_before requires a well-formed block with a terminator");

    LLVMValueRef func = LLVMGetBasicBlockParent(m_ref);
    LLVMModuleRef mod = LLVMGetGlobalParent(func);
    LLVMContextRef ctx = LLVMGetModuleContext(mod);

    // Collect instructions to move (from block start to instruction, exclusive)
    std::vector<LLVMValueRef> to_move;
    bool found = false;
    for (LLVMValueRef inst = LLVMGetFirstInstruction(m_ref); inst;
         inst = LLVMGetNextInstruction(inst)) {
      if (inst == instruction.m_ref) {
        found = true;
        break;
      }
      to_move.push_back(inst);
    }

    if (!found)
      throw LLVMAssertionError("instruction not found in this block");

    // Collect predecessor terminators before mutating CFG.
    std::vector<LLVMValueRef> predecessor_terms;
    LLVMValueRef block_value = LLVMBasicBlockAsValue(m_ref);
    for (LLVMUseRef use = LLVMGetFirstUse(block_value); use != nullptr;
         use = LLVMGetNextUse(use)) {
      LLVMValueRef user = LLVMGetUser(use);
      if (LLVMIsATerminatorInst(user))
        predecessor_terms.push_back(user);
    }

    // Create new predecessor block immediately before this block.
    LLVMBasicBlockRef new_bb =
        LLVMInsertBasicBlockInContext(ctx, m_ref, name.c_str());

    // Move instructions to the new predecessor block.
    if (!to_move.empty()) {
      LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);
      LLVMPositionBuilderAtEnd(builder, new_bb);
      for (LLVMValueRef inst : to_move) {
        LLVMInstructionRemoveFromParent(inst);
        LLVMInsertIntoBuilder(builder, inst);
      }
      LLVMDisposeBuilder(builder);
    }

    // Redirect predecessors: pred -> this becomes pred -> new_bb.
    for (LLVMValueRef term : predecessor_terms) {
      unsigned num_succ = LLVMGetNumSuccessors(term);
      for (unsigned i = 0; i < num_succ; ++i) {
        if (LLVMGetSuccessor(term, i) == m_ref)
          LLVMSetSuccessor(term, i, new_bb);
      }
    }

    // New predecessor now unconditionally branches to this block.
    LLVMBuilderRef br_builder = LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(br_builder, new_bb);
    LLVMBuildBr(br_builder, m_ref);
    LLVMDisposeBuilder(br_builder);

    return LLVMBasicBlockWrapper(new_bb, m_context_token);
  }

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

  // Get successor blocks (blocks this block branches to)
  std::vector<LLVMBasicBlockWrapper> successors() const {
    check_valid();
    LLVMValueRef term = LLVMGetBasicBlockTerminator(m_ref);
    if (!term)
      return {}; // No terminator = no successors
    unsigned num = LLVMGetNumSuccessors(term);
    std::vector<LLVMBasicBlockWrapper> result;
    result.reserve(num);
    for (unsigned i = 0; i < num; ++i) {
      result.emplace_back(LLVMGetSuccessor(term, i), m_context_token);
    }
    return result;
  }

  // Get predecessor blocks (blocks that branch to this block)
  // Uses the use-def chain: iterates through uses of this block's value,
  // finds terminator instructions that use it, and returns their parent blocks.
  std::vector<LLVMBasicBlockWrapper> predecessors() const {
    check_valid();
    std::vector<LLVMBasicBlockWrapper> result;
    LLVMValueRef block_value = LLVMBasicBlockAsValue(m_ref);

    for (LLVMUseRef use = LLVMGetFirstUse(block_value); use != nullptr;
         use = LLVMGetNextUse(use)) {
      LLVMValueRef user = LLVMGetUser(use);
      if (LLVMIsATerminatorInst(user)) {
        LLVMBasicBlockRef pred = LLVMGetInstructionParent(user);
        result.emplace_back(pred, m_context_token);
      }
    }
    return result;
  }

  // Create a builder positioned at the end of this basic block, or before the
  // first non-PHI instruction when requested.
  LLVMBuilderManager *create_builder(bool first_non_phi = false) const;
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
      throw LLVMAssertionError("Parameter index out of range");
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

  LLVMCallConv get_calling_conv() const {
    check_valid();
    return static_cast<LLVMCallConv>(LLVMGetFunctionCallConv(m_ref));
  }

  void set_calling_conv(LLVMCallConv cc) {
    check_valid();
    LLVMSetFunctionCallConv(m_ref, cc);
  }

  LLVMBasicBlockWrapper append_basic_block(const std::string &name) {
    check_valid();
    auto module = LLVMGetGlobalParent(m_ref);
    if (module == nullptr)
      throw LLVMAssertionError("Function has no parent module");
    auto context = LLVMGetModuleContext(module);
    if (context == nullptr)
      throw LLVMAssertionError("Function module has no context");
    LLVMBasicBlockRef bb =
        LLVMAppendBasicBlockInContext(context, m_ref, name.c_str());
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  LLVMBasicBlockWrapper entry_block() const {
    check_valid();
    if (LLVMIsDeclaration(m_ref)) {
      throw LLVMAssertionError(
          "entry_block requires a function definition (check is_declaration "
          "first)");
    }
    LLVMBasicBlockRef bb = LLVMGetEntryBasicBlock(m_ref);
    if (!bb) {
      throw LLVMAssertionError(
          "entry_block: function has no basic blocks");
    }
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  unsigned basic_block_count() const {
    check_valid();
    return LLVMCountBasicBlocks(m_ref);
  }

  LLVMBasicBlockWrapper first_basic_block() const {
    check_valid();
    unsigned count = LLVMCountBasicBlocks(m_ref);
    if (count == 0) {
      throw LLVMAssertionError(
          "first_basic_block requires a function with at least one basic "
          "block (check basic_block_count > 0 or is_declaration)");
    }
    LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(m_ref);
    if (!bb) {
      throw LLVMAssertionError("first_basic_block: first block is null");
    }
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  LLVMBasicBlockWrapper last_basic_block() const {
    check_valid();
    unsigned count = LLVMCountBasicBlocks(m_ref);
    if (count == 0) {
      throw LLVMAssertionError(
          "last_basic_block requires a function with at least one basic "
          "block (check basic_block_count > 0 or is_declaration)");
    }
    LLVMBasicBlockRef bb = LLVMGetLastBasicBlock(m_ref);
    if (!bb) {
      throw LLVMAssertionError("last_basic_block: last block is null");
    }
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
    if (LLVMGetBasicBlockParent(bb.m_ref) != nullptr) {
      throw LLVMAssertionError(
          "append_existing_basic_block requires an unattached basic block");
    }
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

  // Attribute methods (moved from global functions)
  unsigned normalize_attribute_index(const char *api_name, int idx) const {
    if (idx < -1) {
      throw LLVMAssertionError(std::string(api_name) + " requires idx >= -1");
    }
    int max_idx = static_cast<int>(LLVMCountParams(m_ref));
    if (idx > max_idx) {
      throw LLVMAssertionError(
          std::string(api_name) + ": idx " + std::to_string(idx) +
          " out of range (valid: -1..param_count, param_count=" +
          std::to_string(max_idx) + ")");
    }
    return static_cast<unsigned>(idx);
  }

  unsigned get_attribute_count(int idx) const {
    check_valid();
    unsigned attr_idx = normalize_attribute_index("get_attribute_count", idx);
    return LLVMGetAttributeCountAtIndex(m_ref, attr_idx);
  }

  std::optional<LLVMAttributeWrapper>
  get_enum_attribute(int idx, unsigned kind_id) const {
    check_valid();
    unsigned attr_idx = normalize_attribute_index("get_enum_attribute", idx);
    LLVMAttributeRef ref = LLVMGetEnumAttributeAtIndex(m_ref, attr_idx, kind_id);
    if (!ref)
      return std::nullopt;
    return LLVMAttributeWrapper(ref, m_context_token);
  }

  void add_attribute(int idx, const LLVMAttributeWrapper &attr) {
    check_valid();
    attr.check_valid();
    unsigned attr_idx = normalize_attribute_index("add_attribute", idx);
    LLVMAddAttributeAtIndex(m_ref, attr_idx, attr.m_ref);
  }

  std::vector<LLVMAttributeWrapper> get_attributes(int idx) const {
    check_valid();
    unsigned attr_idx = normalize_attribute_index("get_attributes", idx);
    unsigned count = LLVMGetAttributeCountAtIndex(m_ref, attr_idx);
    if (count == 0)
      return {};
    std::vector<LLVMAttributeRef> refs(count);
    LLVMGetAttributesAtIndex(m_ref, attr_idx, refs.data());
    std::vector<LLVMAttributeWrapper> result;
    result.reserve(count);
    for (auto ref : refs) {
      result.emplace_back(ref, m_context_token);
    }
    return result;
  }

  std::optional<LLVMAttributeWrapper>
  get_string_attribute(int idx, const std::string &key) const {
    check_valid();
    unsigned attr_idx = normalize_attribute_index("get_string_attribute", idx);
    LLVMAttributeRef ref = LLVMGetStringAttributeAtIndex(
        m_ref, attr_idx, key.c_str(), key.size());
    if (!ref)
      return std::nullopt;
    return LLVMAttributeWrapper(ref, m_context_token);
  }

  void remove_enum_attribute(int idx, unsigned kind_id) {
    check_valid();
    unsigned attr_idx =
        normalize_attribute_index("remove_enum_attribute", idx);
    LLVMRemoveEnumAttributeAtIndex(m_ref, attr_idx, kind_id);
  }

  void remove_string_attribute(int idx, const std::string &key) {
    check_valid();
    unsigned attr_idx =
        normalize_attribute_index("remove_string_attribute", idx);
    LLVMRemoveStringAttributeAtIndex(m_ref, attr_idx, key.c_str(), key.size());
  }

  void add_target_attribute(const std::string &key, const std::string &value) {
    check_valid();
    LLVMAddTargetDependentFunctionAttr(m_ref, key.c_str(), value.c_str());
  }

  // =========================================================================
  // Function API Refactor: Methods moved from global functions
  // =========================================================================

  // subprogram property (write-only setter)
  // Declared here, implemented after LLVMMetadataWrapper is defined
  void set_subprogram(const LLVMMetadataWrapper &sp);

  // =========================================================================
  // Parent navigation: function -> module -> context
  // =========================================================================

  // Get the module this function belongs to
  LLVMModuleWrapper *module() const;

  // Get the context this function belongs to (derived via module)
  LLVMContextWrapper *context() const;

  // =========================================================================
  // Function verification (Analysis.h)
  // =========================================================================

  /// Verify this function.
  /// Returns true if the function is valid, false otherwise.
  /// Wraps LLVMVerifyFunction with LLVMReturnStatusAction.
  bool verify() const {
    check_valid();
    return !LLVMVerifyFunction(m_ref, LLVMReturnStatusAction);
  }

  /// Verify this function and print any errors to stderr.
  /// Wraps LLVMVerifyFunction with LLVMPrintMessageAction.
  bool verify_and_print() const {
    check_valid();
    return !LLVMVerifyFunction(m_ref, LLVMPrintMessageAction);
  }

  // =========================================================================
  // Intrinsic functions
  // =========================================================================

  /// Get the intrinsic ID for this function.
  /// Returns 0 if the function is not an intrinsic.
  unsigned intrinsic_id() const {
    check_valid();
    return LLVMGetIntrinsicID(m_ref);
  }

  /// Check if this function is an intrinsic.
  bool is_intrinsic() const { return intrinsic_id() != 0; }

  // =========================================================================
  // Personality function (for exception handling)
  // =========================================================================

  /// Check if this function has a personality function.
  bool has_personality_fn() const {
    check_valid();
    return LLVMHasPersonalityFn(m_ref);
  }

  /// Get the personality function.
  LLVMValueWrapper get_personality_fn() const {
    check_valid();
    if (!has_personality_fn()) {
      throw LLVMAssertionError(
          "get_personality_fn requires a personality function "
          "(check has_personality_fn first)");
    }
    LLVMValueRef fn = LLVMGetPersonalityFn(m_ref);
    if (!fn) {
      throw LLVMAssertionError(
          "get_personality_fn: personality function is unexpectedly null");
    }
    return LLVMValueWrapper(fn, m_context_token);
  }

  /// Set the personality function.
  void set_personality_fn(const LLVMValueWrapper &fn) {
    check_valid();
    fn.check_valid();
    LLVMSetPersonalityFn(m_ref, fn.m_ref);
  }

  // =========================================================================
  // GC name
  // =========================================================================

  /// Get the GC name for this function.
  std::optional<std::string> get_gc() const {
    check_valid();
    const char *gc = LLVMGetGC(m_ref);
    if (!gc)
      return std::nullopt;
    return std::string(gc);
  }

  /// Set the GC name for this function.
  void set_gc(const std::string &gc) {
    check_valid();
    LLVMSetGC(m_ref, gc.c_str());
  }

  /// Create a BlockAddress constant for a basic block in this function.
  LLVMValueWrapper block_address(const LLVMBasicBlockWrapper &bb) const {
    check_valid();
    bb.check_valid();
    if (LLVMGetBasicBlockParent(bb.m_ref) != m_ref) {
      throw LLVMAssertionError(
          "block_address requires a basic block owned by this function");
    }
    return LLVMValueWrapper(LLVMBlockAddress(m_ref, bb.m_ref),
                            m_context_token);
  }
};

// =============================================================================
// Implementation of LLVMTypeWrapper constant creation methods
// These need LLVMValueWrapper which is now defined
// =============================================================================

inline LLVMValueWrapper LLVMTypeWrapper::constant(long long val,
                                                  bool sign_extend) const {
  check_valid();
  if (!is_integer())
    throw LLVMAssertionError("constant() requires integer type");
  return LLVMValueWrapper(LLVMConstInt(m_ref, val, sign_extend),
                          m_context_token);
}

inline LLVMValueWrapper LLVMTypeWrapper::real_constant(double val) const {
  check_valid();
  if (!is_float())
    throw LLVMAssertionError("real_constant() requires floating-point type");
  return LLVMValueWrapper(LLVMConstReal(m_ref, val), m_context_token);
}

inline LLVMValueWrapper
LLVMTypeWrapper::constant_from_string(const std::string &text,
                                      unsigned radix) const {
  check_valid();
  if (!is_integer())
    throw LLVMAssertionError("constant_from_string() requires integer type");
  if (radix < 2 || radix > 36)
    throw LLVMAssertionError("radix must be between 2 and 36");
  return LLVMValueWrapper(
      LLVMConstIntOfStringAndSize(m_ref, text.c_str(), text.size(), radix),
      m_context_token);
}

inline LLVMValueWrapper
LLVMTypeWrapper::real_constant_from_string(const std::string &text) const {
  check_valid();
  if (!is_float())
    throw LLVMAssertionError(
        "real_constant_from_string() requires floating-point type");
  return LLVMValueWrapper(
      LLVMConstRealOfStringAndSize(m_ref, text.c_str(), text.size()),
      m_context_token);
}

inline LLVMValueWrapper LLVMTypeWrapper::null() const {
  check_valid();
  return LLVMValueWrapper(LLVMConstNull(m_ref), m_context_token);
}

inline LLVMValueWrapper LLVMTypeWrapper::all_ones() const {
  check_valid();
  return LLVMValueWrapper(LLVMConstAllOnes(m_ref), m_context_token);
}

inline LLVMValueWrapper LLVMTypeWrapper::undef() const {
  check_valid();
  return LLVMValueWrapper(LLVMGetUndef(m_ref), m_context_token);
}

inline LLVMValueWrapper LLVMTypeWrapper::poison() const {
  check_valid();
  return LLVMValueWrapper(LLVMGetPoison(m_ref), m_context_token);
}

// Implementation of LLVMUseWrapper methods - need LLVMValueWrapper
inline LLVMValueWrapper LLVMUseWrapper::get_user() const {
  check_valid();
  LLVMValueRef user = LLVMGetUser(m_ref);
  if (!user)
    throw LLVMAssertionError("Use has no user");
  return LLVMValueWrapper(user, m_context_token);
}

inline LLVMValueWrapper LLVMUseWrapper::get_used_value() const {
  check_valid();
  LLVMValueRef used = LLVMGetUsedValue(m_ref);
  if (!used)
    throw LLVMAssertionError("Use has no used value");
  return LLVMValueWrapper(used, m_context_token);
}

// Implementation of LLVMUseWrapper::get_operand_index
inline unsigned LLVMUseWrapper::get_operand_index() const {
  check_valid();
  LLVMValueRef user = LLVMGetUser(m_ref);
  if (!user)
    throw LLVMAssertionError("Use has no user");
  unsigned num_ops = LLVMGetNumOperands(user);
  for (unsigned i = 0; i < num_ops; ++i) {
    if (LLVMGetOperandUse(user, i) == m_ref) {
      return i;
    }
  }
  throw LLVMAssertionError("Could not find operand index for this use");
}

// Implementation of LLVMValueWrapper::get_operand_use
inline LLVMUseWrapper LLVMValueWrapper::get_operand_use(unsigned index) const {
  check_valid();
  if (index >= static_cast<unsigned>(LLVMGetNumOperands(m_ref)))
    throw LLVMAssertionError("Invalid operand index");
  LLVMUseRef use = LLVMGetOperandUse(m_ref, index);
  if (!use)
    throw LLVMAssertionError("No use at this operand index");
  return LLVMUseWrapper(use, m_context_token);
}

// Implementation of LLVMBasicBlockWrapper::function() - needs
// LLVMFunctionWrapper
inline LLVMFunctionWrapper LLVMBasicBlockWrapper::function() const {
  check_valid();
  LLVMValueRef parent_fn = LLVMGetBasicBlockParent(m_ref);
  if (!parent_fn)
    throw LLVMAssertionError("BasicBlock has no parent function");
  return LLVMFunctionWrapper(parent_fn, m_context_token);
}

// Note: LLVMBasicBlockWrapper::module() and ::context() are defined after
// LLVMModuleWrapper and LLVMContextWrapper are fully defined

// Implementation of LLVMValueWrapper::get_incoming_block() - needs
// LLVMBasicBlockWrapper
inline LLVMBasicBlockWrapper
LLVMValueWrapper::get_incoming_block(unsigned index) const {
  check_valid();
  require_phi_instruction("get_incoming_block");
  unsigned count = LLVMCountIncoming(m_ref);
  if (index >= count) {
    throw LLVMAssertionError(
        "get_incoming_block: incoming block index " + std::to_string(index) +
        " out of range (num_incoming=" + std::to_string(count) + ")");
  }
  LLVMBasicBlockRef bb = LLVMGetIncomingBlock(m_ref, index);
  if (!bb) {
    throw LLVMAssertionError("get_incoming_block: incoming block at index " +
                             std::to_string(index) + " is null");
  }
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

// Implementation of LLVMOperandBundleWrapper::get_arg_at_index - needs
// LLVMValueWrapper
inline LLVMValueWrapper
LLVMOperandBundleWrapper::get_arg_at_index(unsigned index) const {
  check_valid();
  unsigned count = LLVMGetNumOperandBundleArgs(m_ref);
  if (index >= count) {
    throw LLVMAssertionError("get_arg_at_index: operand bundle arg index " +
                             std::to_string(index) +
                             " out of range (num_args=" +
                             std::to_string(count) + ")");
  }
  LLVMValueRef arg = LLVMGetOperandBundleArgAtIndex(m_ref, index);
  if (!arg)
    throw LLVMAssertionError("Invalid operand bundle argument index");
  return LLVMValueWrapper(arg, m_context_token);
}

// Implementation of LLVMValueWrapper BasicBlock methods - need
// LLVMBasicBlockWrapper
inline LLVMBasicBlockWrapper LLVMValueWrapper::block() const {
  check_valid();
  if (!is_a_instruction())
    throw LLVMAssertionError("Cannot get block: value is not an instruction");
  LLVMBasicBlockRef bb = LLVMGetInstructionParent(m_ref);
  if (!bb)
    throw LLVMAssertionError("Instruction has no parent basic block");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

// Note: LLVMValueWrapper::get_function(), get_module(), get_context() are
// defined after LLVMModuleWrapper and LLVMContextWrapper are fully defined

inline LLVMBasicBlockWrapper LLVMValueWrapper::get_normal_dest() const {
  check_valid();
  require_invoke_instruction("normal_dest");
  LLVMBasicBlockRef bb = LLVMGetNormalDest(m_ref);
  if (!bb)
    throw LLVMAssertionError("Invoke instruction has no normal dest");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline std::optional<LLVMBasicBlockWrapper>
LLVMValueWrapper::get_unwind_dest() const {
  check_valid();
  require_terminator_instruction("unwind_dest");
  LLVMBasicBlockRef bb = LLVMGetUnwindDest(m_ref);
  // Unwind dest can be null for cleanupret and catchswitch
  if (!bb)
    return std::nullopt;
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline LLVMBasicBlockWrapper
LLVMValueWrapper::get_successor(unsigned index) const {
  check_valid();
  require_terminator_instruction("get_successor");
  LLVMBasicBlockRef bb = LLVMGetSuccessor(m_ref, index);
  if (!bb)
    throw LLVMAssertionError("Invalid successor index");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline std::vector<LLVMBasicBlockWrapper> LLVMValueWrapper::successors() const {
  check_valid();
  require_terminator_instruction("successors");
  unsigned num = LLVMGetNumSuccessors(m_ref);
  std::vector<LLVMBasicBlockWrapper> result;
  result.reserve(num);
  for (unsigned i = 0; i < num; ++i) {
    result.emplace_back(LLVMGetSuccessor(m_ref, i), m_context_token);
  }
  return result;
}

inline LLVMBasicBlockWrapper LLVMValueWrapper::get_callbr_default_dest() const {
  check_valid();
  require_callbr_instruction("callbr_default_dest");
  LLVMBasicBlockRef bb = LLVMGetCallBrDefaultDest(m_ref);
  if (!bb)
    throw LLVMAssertionError("CallBr has no default dest");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline unsigned LLVMValueWrapper::get_callbr_num_indirect_dests() const {
  check_valid();
  require_callbr_instruction("callbr_num_indirect_dests");
  return LLVMGetCallBrNumIndirectDests(m_ref);
}

inline LLVMBasicBlockWrapper
LLVMValueWrapper::get_callbr_indirect_dest(unsigned index) const {
  check_valid();
  require_callbr_instruction("get_callbr_indirect_dest");
  LLVMBasicBlockRef bb = LLVMGetCallBrIndirectDest(m_ref, index);
  if (!bb)
    throw LLVMAssertionError("Invalid callbr indirect dest index");
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline LLVMBasicBlockWrapper LLVMValueWrapper::value_as_basic_block() const {
  check_valid();
  if (!value_is_basic_block())
    throw LLVMAssertionError("Value is not a basic block");
  LLVMBasicBlockRef bb = LLVMValueAsBasicBlock(m_ref);
  return LLVMBasicBlockWrapper(bb, m_context_token);
}

inline std::vector<LLVMBasicBlockWrapper>
LLVMValueWrapper::get_handlers() const {
  check_valid();
  require_catchswitch_instruction("handlers");
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

inline void
LLVMValueWrapper::add_handler(const LLVMBasicBlockWrapper &handler) {
  check_valid();
  handler.check_valid();
  require_catchswitch_instruction("add_handler");
  LLVMAddHandler(m_ref, handler.m_ref);
}

// =============================================================================
// Builder Wrapper
// =============================================================================

struct LLVMBuilderWrapper : NoMoveCopy {
  LLVMBuilderRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;
  std::shared_ptr<ValidityToken> m_token;

  // Constructor with basic block (position at end)
  LLVMBuilderWrapper(LLVMContextRef ctx,
                     std::shared_ptr<ValidityToken> context_token,
                     LLVMBasicBlockRef bb)
      : m_context_token(std::move(context_token)),
        m_token(std::make_shared<ValidityToken>()) {
    m_ref = LLVMCreateBuilderInContext(ctx);
    LLVMPositionBuilderAtEnd(m_ref, bb);
  }

  // Constructor with instruction (position before/after debug records)
  LLVMBuilderWrapper(LLVMContextRef ctx,
                     std::shared_ptr<ValidityToken> context_token,
                     LLVMValueRef inst, bool before_dbg)
      : m_context_token(std::move(context_token)),
        m_token(std::make_shared<ValidityToken>()) {
    if (!LLVMIsAInstruction(inst))
      throw LLVMAssertionError("Builder position requires an instruction");
    if (!LLVMGetInstructionParent(inst))
      throw LLVMAssertionError(
          "Builder position instruction must belong to a basic block");
    m_ref = LLVMCreateBuilderInContext(ctx);
    if (before_dbg) {
      // Position before instruction and its debug records
      LLVMPositionBuilderBeforeInstrAndDbgRecords(m_ref, inst);
    } else {
      // Position before instruction but after debug records
      LLVMPositionBuilderBefore(m_ref, inst);
    }
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
      throw LLVMMemoryError("Builder has been disposed");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Builder used after context was destroyed");
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
  void position_at_end(const LLVMBasicBlockWrapper &bb, bool before_dbg) {
    check_valid();
    bb.check_valid();
    if (before_dbg) {
      LLVMPositionBuilderBeforeDbgRecords(m_ref, bb.m_ref, nullptr);
    } else {
      LLVMPositionBuilderAtEnd(m_ref, bb.m_ref);
    }
  }

  void position_before(const LLVMValueWrapper &inst, bool before_dbg) {
    check_valid();
    inst.check_valid();
    if (!inst.is_a_instruction())
      throw LLVMAssertionError("position_before requires an instruction value");
    if (!LLVMGetInstructionParent(inst.m_ref))
      throw LLVMAssertionError(
          "position_before requires an instruction in a basic block");
    if (before_dbg) {
      LLVMPositionBuilderBeforeInstrAndDbgRecords(m_ref, inst.m_ref);
    } else {
      LLVMPositionBuilderBefore(m_ref, inst.m_ref);
    }
  }

  std::optional<LLVMBasicBlockWrapper> insert_block() const {
    check_valid();
    LLVMBasicBlockRef bb = LLVMGetInsertBlock(m_ref);
    if (!bb)
      return std::nullopt;
    return LLVMBasicBlockWrapper(bb, m_context_token);
  }

  void position_at(const LLVMBasicBlockWrapper &bb,
                   const LLVMValueWrapper &inst) {
    check_valid();
    bb.check_valid();
    inst.check_valid();
    if (!inst.is_a_instruction())
      throw LLVMAssertionError("position_at requires an instruction value");
    if (LLVMGetInstructionParent(inst.m_ref) != bb.m_ref) {
      throw LLVMAssertionError(
          "position_at requires inst to belong to the provided basic block");
    }
    LLVMPositionBuilder(m_ref, bb.m_ref, inst.m_ref);
  }

  void clear_insertion_position() {
    check_valid();
    LLVMClearInsertionPosition(m_ref);
  }

  // Arithmetic operations
  LLVMValueWrapper add(const LLVMValueWrapper &lhs, const LLVMValueWrapper &rhs,
                       const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildAdd(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
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
    return LLVMValueWrapper(
        LLVMBuildSub(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
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
    return LLVMValueWrapper(
        LLVMBuildMul(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
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
    return LLVMValueWrapper(
        LLVMBuildSDiv(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper udiv(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildUDiv(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
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

  LLVMValueWrapper exact_udiv(const LLVMValueWrapper &lhs,
                              const LLVMValueWrapper &rhs,
                              const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildExactUDiv(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper srem(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildSRem(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper urem(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildURem(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  // Floating point arithmetic
  LLVMValueWrapper fadd(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFAdd(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fsub(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFSub(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fmul(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFMul(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fdiv(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFDiv(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper frem(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFRem(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
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
    return LLVMValueWrapper(
        LLVMBuildShl(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper lshr(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildLShr(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper ashr(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildAShr(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper and_(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildAnd(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper or_(const LLVMValueWrapper &lhs, const LLVMValueWrapper &rhs,
                       const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildOr(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper xor_(const LLVMValueWrapper &lhs,
                        const LLVMValueWrapper &rhs,
                        const std::string &name = "") {
    check_valid();
    lhs.check_valid();
    rhs.check_valid();
    return LLVMValueWrapper(
        LLVMBuildXor(m_ref, lhs.m_ref, rhs.m_ref, name.c_str()),
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

  /// Cast a pointer to a different address space.
  LLVMValueWrapper addr_space_cast(const LLVMValueWrapper &val,
                                   const LLVMTypeWrapper &ty,
                                   const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildAddrSpaceCast(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  // Convenience cast operations
  LLVMValueWrapper zext_or_bitcast(const LLVMValueWrapper &val,
                                   const LLVMTypeWrapper &ty,
                                   const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildZExtOrBitCast(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper sext_or_bitcast(const LLVMValueWrapper &val,
                                   const LLVMTypeWrapper &ty,
                                   const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildSExtOrBitCast(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper trunc_or_bitcast(const LLVMValueWrapper &val,
                                    const LLVMTypeWrapper &ty,
                                    const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildTruncOrBitCast(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper pointer_cast(const LLVMValueWrapper &val,
                                const LLVMTypeWrapper &ty,
                                const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildPointerCast(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper fp_cast(const LLVMValueWrapper &val,
                           const LLVMTypeWrapper &ty,
                           const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildFPCast(m_ref, val.m_ref, ty.m_ref, name.c_str()),
        m_context_token);
  }

  LLVMValueWrapper cast(LLVMOpcode op, const LLVMValueWrapper &val,
                        const LLVMTypeWrapper &ty,
                        const std::string &name = "") {
    check_valid();
    val.check_valid();
    ty.check_valid();
    return LLVMValueWrapper(
        LLVMBuildCast(m_ref, op, val.m_ref, ty.m_ref, name.c_str()),
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

  LLVMValueWrapper indirect_br(const LLVMValueWrapper &addr,
                               unsigned num_dests) {
    check_valid();
    addr.check_valid();
    return LLVMValueWrapper(LLVMBuildIndirectBr(m_ref, addr.m_ref, num_dests),
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
    if (!func_ty.is_function()) {
      throw LLVMAssertionError("Attempting to pass non-function type to call");
    }
    if (func_ty.get_return_type().is_void() && !name.empty()) {
      throw LLVMAssertionError("Cannot name call to function returning void");
    }
    return LLVMValueWrapper(
        LLVMBuildCall2(m_ref, func_ty.m_ref, func.m_ref, arg_refs.data(),
                       static_cast<unsigned>(arg_refs.size()), name.c_str()),
        m_context_token);
  }

  // Convenience overload: infer function type from the callee value.
  // Use the explicit-type version for indirect calls through raw pointers.
  LLVMValueWrapper call_infer(const LLVMValueWrapper &func,
                              const std::vector<LLVMValueWrapper> &args,
                              const std::string &name = "") {
    check_valid();
    func.check_valid();
    if (!func.is_a_function()) {
      throw LLVMAssertionError(
          "call(func, args): func must be a Function. "
          "For indirect calls use call(func_ty, func, args).");
    }
    LLVMTypeRef func_ty = LLVMGlobalGetValueType(func.m_ref);
    std::vector<LLVMValueRef> arg_refs;
    arg_refs.reserve(args.size());
    for (const auto &arg : args) {
      arg.check_valid();
      arg_refs.push_back(arg.m_ref);
    }
    LLVMTypeRef ret_ty = LLVMGetReturnType(func_ty);
    if (LLVMGetTypeKind(ret_ty) == LLVMVoidTypeKind && !name.empty()) {
      throw LLVMAssertionError("Cannot name call to function returning void");
    }
    return LLVMValueWrapper(
        LLVMBuildCall2(m_ref, func_ty, func.m_ref, arg_refs.data(),
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
  LLVMValueWrapper
  invoke_with_operand_bundles(const LLVMTypeWrapper &fn_ty,
                              const LLVMValueWrapper &fn,
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
  LLVMValueWrapper
  call_with_operand_bundles(const LLVMTypeWrapper &fn_ty,
                            const LLVMValueWrapper &fn,
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

    if (fn_ty.get_return_type().is_void() && !name.empty()) {
      throw LLVMAssertionError("Cannot name call to function returning void");
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
  LLVMValueWrapper
  callbr(const LLVMTypeWrapper &fn_ty, const LLVMValueWrapper &fn,
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
                        dest_refs.data(),
                        static_cast<unsigned>(dest_refs.size()),
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
  LLVMValueWrapper landing_pad(const LLVMTypeWrapper &ty, unsigned num_clauses,
                               const std::string &name = "") {
    check_valid();
    ty.check_valid();
    return LLVMValueWrapper(LLVMBuildLandingPad(m_ref, ty.m_ref, nullptr,
                                                num_clauses, name.c_str()),
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
    return LLVMValueWrapper(LLVMBuildCatchRet(m_ref, catch_pad.m_ref, bb.m_ref),
                            m_context_token);
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
                            static_cast<unsigned>(arg_refs.size()),
                            name.c_str()),
        m_context_token);
  }

  // Catch switch
  LLVMValueWrapper catch_switch(const LLVMValueWrapper &parent_pad,
                                std::optional<LLVMBasicBlockWrapper> unwind_bb,
                                unsigned num_handlers,
                                const std::string &name = "") {
    check_valid();
    parent_pad.check_valid();
    LLVMBasicBlockRef unwind_ref =
        unwind_bb.has_value() ? unwind_bb->m_ref : nullptr;
    return LLVMValueWrapper(LLVMBuildCatchSwitch(m_ref, parent_pad.m_ref,
                                                 unwind_ref, num_handlers,
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
    return LLVMValueWrapper(LLVMBuildInsertElement(m_ref, vec.m_ref, val.m_ref,
                                                   index.m_ref, name.c_str()),
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
    return LLVMValueWrapper(LLVMBuildShuffleVector(m_ref, v1.m_ref, v2.m_ref,
                                                   mask.m_ref, name.c_str()),
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
  LLVMValueWrapper
  gep_with_no_wrap_flags(const LLVMTypeWrapper &ty, const LLVMValueWrapper &ptr,
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

  // Atomic RMW (single-thread version)
  LLVMValueWrapper atomic_rmw(LLVMAtomicRMWBinOp op,
                              const LLVMValueWrapper &ptr,
                              const LLVMValueWrapper &val,
                              LLVMAtomicOrdering ordering,
                              bool single_thread = false) {
    check_valid();
    ptr.check_valid();
    val.check_valid();
    return LLVMValueWrapper(LLVMBuildAtomicRMW(m_ref, op, ptr.m_ref, val.m_ref,
                                               ordering, single_thread),
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
    return LLVMValueWrapper(LLVMBuildAtomicRMWSyncScope(m_ref, op, ptr.m_ref,
                                                        val.m_ref, ordering,
                                                        sync_scope_id),
                            m_context_token);
  }

  // Atomic CmpXchg (single-thread version)
  LLVMValueWrapper atomic_cmpxchg(const LLVMValueWrapper &ptr,
                                  const LLVMValueWrapper &cmp,
                                  const LLVMValueWrapper &new_val,
                                  LLVMAtomicOrdering success_ordering,
                                  LLVMAtomicOrdering failure_ordering,
                                  bool single_thread = false) {
    check_valid();
    ptr.check_valid();
    cmp.check_valid();
    new_val.check_valid();
    return LLVMValueWrapper(
        LLVMBuildAtomicCmpXchg(m_ref, ptr.m_ref, cmp.m_ref, new_val.m_ref,
                               success_ordering, failure_ordering,
                               single_thread),
        m_context_token);
  }

  // Atomic CmpXchg with sync scope
  LLVMValueWrapper atomic_cmpxchg_sync_scope(
      const LLVMValueWrapper &ptr, const LLVMValueWrapper &cmp,
      const LLVMValueWrapper &new_val, LLVMAtomicOrdering success_ordering,
      LLVMAtomicOrdering failure_ordering, unsigned sync_scope_id) {
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

  /// Build a memory fence instruction.
  LLVMValueWrapper fence(LLVMAtomicOrdering ordering,
                         bool single_thread = false,
                         const std::string &name = "") {
    check_valid();
    return LLVMValueWrapper(
        LLVMBuildFence(m_ref, ordering, single_thread, name.c_str()),
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
    if (!instr.is_a_instruction()) {
      throw LLVMAssertionError(
          "insert_into_builder_with_name requires an instruction value");
    }
    LLVMInsertIntoBuilderWithName(m_ref, instr.m_ref, name.c_str());
  }

  // Add metadata to instruction (from builder's metadata attachments)
  void add_metadata_to_inst(const LLVMValueWrapper &instr) {
    check_valid();
    instr.check_valid();
    if (!instr.is_a_instruction())
      throw LLVMAssertionError("add_metadata_to_inst requires an instruction");
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
  bool m_borrowed = false; // True if we don't own the module (non-owning ref)

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
        m_token(std::make_shared<ValidityToken>()), m_ctx_ref(ctx) {}

  // Constructor for non-owning (borrowed) reference to an existing module.
  // Used by function.module to return the function's parent module.
  // Borrowed wrappers don't dispose the module when destroyed.
  static LLVMModuleWrapper *
  create_borrowed(LLVMModuleRef mod, LLVMContextRef ctx,
                  std::shared_ptr<ValidityToken> context_token) {
    auto *wrapper = new LLVMModuleWrapper();
    wrapper->m_ref = mod;
    wrapper->m_context_token = std::move(context_token);
    wrapper->m_token = std::make_shared<ValidityToken>();
    wrapper->m_ctx_ref = ctx;
    wrapper->m_borrowed = true;
    return wrapper;
  }

  ~LLVMModuleWrapper() {
    if (m_ref && !m_borrowed) {
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
        fprintf(stderr, "Warning: LLVM Module outlived its Context. "
                        "This may cause a memory leak. "
                        "Ensure modules are deleted before their context.\n");
      }
      m_ref = nullptr;
    }
    // Only invalidate the token if we own it (not borrowed)
    if (m_token && !m_borrowed) {
      m_token->invalidate();
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMMemoryError("Module has been disposed");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Module used after context was destroyed");
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
    return LLVMValueWrapper(LLVMAddGlobalInAddressSpace(
                                m_ref, ty.m_ref, name.c_str(), address_space),
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
    LLVMBool failed = LLVMVerifyModule(m_ref, LLVMReturnStatusAction, &error);
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

  // Verify the module, throwing LLVMError if verification fails.
  void verify_or_raise() const {
    check_valid();
    char *error = nullptr;
    LLVMBool failed =
        LLVMVerifyModule(m_ref, LLVMReturnStatusAction, &error);
    if (failed) {
      std::string msg = error ? std::string(error) : "Verification failed";
      if (error)
        LLVMDisposeMessage(error);
      throw LLVMError(msg);
    }
    if (error)
      LLVMDisposeMessage(error);
  }

  // ==========================================================================
  // BitWriter - Bitcode writing functionality
  // ==========================================================================

  /// Write bitcode to file.
  void write_bitcode_to_file(const std::string &path) {
    check_valid();
    if (LLVMWriteBitcodeToFile(m_ref, path.c_str())) {
      throw LLVMError("Failed to write bitcode to file: " + path);
    }
  }

  /// Write bitcode to memory and return as bytes.
  nb::bytes write_bitcode_to_memory_buffer() {
    check_valid();
    LLVMMemoryBufferRef buf = LLVMWriteBitcodeToMemoryBuffer(m_ref);
    if (!buf) {
      throw LLVMError("Failed to write bitcode to memory buffer");
    }
    const char *data = LLVMGetBufferStart(buf);
    size_t size = LLVMGetBufferSize(buf);
    nb::bytes result(data, size);
    LLVMDisposeMemoryBuffer(buf);
    return result;
  }

  // ==========================================================================
  // Linker - Module linking functionality
  // ==========================================================================

  /// Link another module into this module.
  /// The source module is destroyed after linking.
  void link_module(LLVMModuleWrapper &src) {
    check_valid();
    src.check_valid();
    if (LLVMLinkModules2(m_ref, src.m_ref)) {
      throw LLVMError("Failed to link modules");
    }
    // LLVMLinkModules2 destroys the source module, so mark it as disposed
    src.m_ref = nullptr;
    if (src.m_token) {
      src.m_token->invalidate();
    }
  }

  // ==========================================================================
  // COMDAT support (for Windows/COFF linking)
  // ==========================================================================

  /// Get or insert a COMDAT section with the given name.
  LLVMComdatWrapper get_or_insert_comdat(const std::string &name) {
    check_valid();
    LLVMComdatRef comdat = LLVMGetOrInsertComdat(m_ref, name.c_str());
    return LLVMComdatWrapper(comdat, m_token);
  }

  // ==========================================================================
  // Module printing to file
  // ==========================================================================

  /// Print the module IR to a file.
  void print_to_file(const std::string &filename) {
    check_valid();
    char *error = nullptr;
    if (LLVMPrintModuleToFile(m_ref, filename.c_str(), &error)) {
      std::string msg = error ? error : "Unknown error";
      if (error)
        LLVMDisposeMessage(error);
      throw LLVMError("Failed to print module to file: " + msg);
    }
    if (error)
      LLVMDisposeMessage(error);
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

  std::optional<LLVMValueWrapper>
  get_named_global_alias(const std::string &name) {
    check_valid();
    LLVMValueRef alias =
        LLVMGetNamedGlobalAlias(m_ref, name.c_str(), name.size());
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
    return LLVMValueWrapper(LLVMAddAlias2(m_ref, value_ty.m_ref, addr_space,
                                          aliasee.m_ref, name.c_str()),
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

  std::optional<LLVMValueWrapper>
  get_named_global_ifunc(const std::string &name) {
    check_valid();
    LLVMValueRef ifunc =
        LLVMGetNamedGlobalIFunc(m_ref, name.c_str(), name.size());
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
    return LLVMValueWrapper(LLVMAddGlobalIFunc(m_ref, name.c_str(), name.size(),
                                               ty.m_ref, addr_space,
                                               resolver.m_ref),
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

  std::optional<LLVMNamedMDNodeWrapper>
  get_named_metadata(const std::string &name) {
    check_valid();
    LLVMNamedMDNodeRef md =
        LLVMGetNamedMetadata(m_ref, name.c_str(), name.size());
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

  std::vector<LLVMValueWrapper>
  get_named_metadata_operands(const std::string &name) {
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

  // Add named metadata operand - takes LLVMMetadataWrapper
  // Declared here, implemented after LLVMMetadataWrapper
  void add_named_metadata_operand(const std::string &name,
                                  const LLVMMetadataWrapper &md);

  // =========================================================================
  // Module Flags API
  // =========================================================================

  /// Add a module-level flag.
  void add_module_flag(LLVMModuleFlagBehavior behavior, const std::string &key,
                       const LLVMMetadataWrapper &val);

  /// Get a module-level flag by key.
  std::optional<LLVMMetadataWrapper> get_module_flag(const std::string &key);

  // =========================================================================
  // Module API Refactor: Methods moved from global functions
  // =========================================================================

  // Context property (read-only)
  // Declared here, implemented after LLVMContextWrapper is defined
  LLVMContextWrapper *get_context() const;

  // is_new_dbg_info_format property (read/write)
  bool is_new_dbg_info_format() const {
    check_valid();
    return LLVMIsNewDbgInfoFormat(m_ref);
  }

  void set_is_new_dbg_info_format(bool use_new_format) {
    check_valid();
    LLVMSetIsNewDbgInfoFormat(m_ref, use_new_format);
  }

  // get_intrinsic_declaration method
  LLVMValueWrapper
  get_intrinsic_declaration(unsigned id,
                            const std::vector<LLVMTypeWrapper> &param_types) {
    check_valid();
    std::vector<LLVMTypeRef> type_refs;
    type_refs.reserve(param_types.size());
    for (const auto &ty : param_types) {
      ty.check_valid();
      type_refs.push_back(ty.m_ref);
    }
    return LLVMValueWrapper(LLVMGetIntrinsicDeclaration(
                                m_ref, id, type_refs.data(), type_refs.size()),
                            m_context_token);
  }

  // create_dibuilder method - returns a DIBuilderManager
  // Declared here, implemented after LLVMDIBuilderManager is defined
  LLVMDIBuilderManager *create_dibuilder();

  // Clone - returns a ModuleManager that must be used with 'with' or .dispose()
  LLVMModuleManager *clone() const;

private:
  // Private constructor for borrowing
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
  bool m_borrowed = false; // True if we don't own the context (non-owning ref)

  explicit LLVMContextWrapper(bool global = false)
      : m_token(std::make_shared<ValidityToken>()), m_global(global) {
    if (global) {
      m_ref = LLVMGetGlobalContext();
    } else {
      m_ref = LLVMContextCreate();
    }

    // Install diagnostic handler that uses the global registry.
    // We pass the context ref as the user data so the handler can look up
    // the correct diagnostics storage without needing a pointer to `this`.
    LLVMContextSetDiagnosticHandler(
        m_ref,
        [](LLVMDiagnosticInfoRef info, void *ctx_ref_ptr) {
          auto ctx_ref = static_cast<LLVMContextRef>(ctx_ref_ptr);
          auto severity = LLVMGetDiagInfoSeverity(info);
          char *desc = LLVMGetDiagInfoDescription(info);

          std::string severity_str;
          switch (severity) {
          case LLVMDSError:
            severity_str = "error";
            break;
          case LLVMDSWarning:
            severity_str = "warning";
            break;
          case LLVMDSRemark:
            severity_str = "remark";
            break;
          case LLVMDSNote:
            severity_str = "note";
            break;
          default:
            severity_str = "unknown";
            break;
          }

          DiagnosticRegistry::instance().add_diagnostic(
              ctx_ref, {severity_str, desc ? std::string(desc) : "",
                        std::nullopt, std::nullopt});

          if (desc) {
            LLVMDisposeMessage(desc);
          }
        },
        m_ref);
  }

  // Constructor for non-owning (borrowed) reference to an existing context.
  // Used by get_module_context to return the module's context.
  // Borrowed wrappers share the same diagnostic storage via the global
  // registry.
  LLVMContextWrapper(LLVMContextRef ref, std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_token(std::move(token)), m_global(false),
        m_borrowed(true) {
    // Don't install diagnostic handler - the owning context wrapper has it
  }

  ~LLVMContextWrapper() {
    if (m_ref && !m_global && !m_borrowed) {
      // Clean up diagnostics from the global registry
      DiagnosticRegistry::instance().remove_context(m_ref);
      LLVMContextDispose(m_ref);
      m_ref = nullptr;
    }
    // Only invalidate the token if we own it (not borrowed)
    if (m_token && !m_borrowed) {
      m_token->invalidate();
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMMemoryError("Context has been disposed");
    if (!m_token || !m_token->is_valid())
      throw LLVMMemoryError("Context is no longer valid");
  }

  void dispose() {
    if (m_ref && !m_global && !m_borrowed) {
      // Clean up diagnostics from the global registry
      DiagnosticRegistry::instance().remove_context(m_ref);
      LLVMContextDispose(m_ref);
      m_ref = nullptr;
    }
    // Only invalidate the token if we own it (not borrowed)
    if (m_token && !m_borrowed) {
      m_token->invalidate();
    }
  }

  std::vector<Diagnostic> get_diagnostics() const {
    return DiagnosticRegistry::instance().get_diagnostics(m_ref);
  }

  void clear_diagnostics() {
    DiagnosticRegistry::instance().clear_diagnostics(m_ref);
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

  // New property-based type factory
  LLVMTypeFactoryWrapper get_types() {
    check_valid();
    return LLVMTypeFactoryWrapper(m_ref, m_token);
  }

  // Type factory methods (legacy - will be removed)
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

  LLVMTypeWrapper
  target_ext_type(const std::string &name,
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

  // Attribute creation method (moved from global function)
  LLVMAttributeWrapper create_enum_attribute(unsigned kind_id, uint64_t val) {
    check_valid();
    LLVMAttributeRef ref = LLVMCreateEnumAttribute(m_ref, kind_id, val);
    return LLVMAttributeWrapper(ref, m_token);
  }

  /// Create a string attribute.
  LLVMAttributeWrapper create_string_attribute(const std::string &key,
                                               const std::string &value) {
    check_valid();
    LLVMAttributeRef ref = LLVMCreateStringAttribute(
        m_ref, key.c_str(), key.size(), value.c_str(), value.size());
    return LLVMAttributeWrapper(ref, m_token);
  }

  /// Create a type attribute.
  LLVMAttributeWrapper create_type_attribute(unsigned kind_id,
                                             const LLVMTypeWrapper &type_ref);

  // Get metadata kind ID for a named metadata kind
  unsigned get_md_kind_id(const std::string &name) {
    check_valid();
    return LLVMGetMDKindIDInContext(m_ref, name.c_str(), name.size());
  }

  // Get sync scope ID for a named synchronization scope
  unsigned get_sync_scope_id(const std::string &name) {
    check_valid();
    return LLVMGetSyncScopeID(m_ref, name.c_str(), name.size());
  }

  // Create a string constant in this context.
  LLVMValueWrapper const_string(const std::string &str,
                                bool dont_null_terminate = false) {
    check_valid();
    return LLVMValueWrapper(
        LLVMConstStringInContext2(m_ref, str.c_str(), str.size(),
                                  dont_null_terminate),
        m_token);
  }

  // Create a raw bytes constant in this context without UTF-8 encoding.
  LLVMValueWrapper const_string(const nb::bytes &data,
                                bool dont_null_terminate = false) {
    check_valid();
    return LLVMValueWrapper(
        LLVMConstStringInContext2(m_ref, data.c_str(), data.size(),
                                  dont_null_terminate),
        m_token);
  }

  // Metadata creation methods - declared here, implemented after
  // LLVMMetadataWrapper
  LLVMMetadataWrapper md_string(const std::string &str);
  LLVMMetadataWrapper md_node(const std::vector<LLVMMetadataWrapper> &mds);

  // Module creation (returns context manager) - defined after LLVMModuleManager
  LLVMModuleManager *create_module(const std::string &name);

  // Builder creation (returns context manager) - defined after
  // LLVMBuilderManager. Must be created with a position.
  LLVMBuilderManager *create_builder(const LLVMBasicBlockWrapper &bb);
  LLVMBuilderManager *create_builder(const LLVMValueWrapper &inst,
                                     bool before_dbg);
  LLVMModuleManager *parse_bitcode_from_file(const fs::path &filename,
                                             bool lazy);
  LLVMModuleManager *parse_bitcode_from_bytes(nb::bytes data, bool lazy);
  LLVMModuleManager *parse_ir(const std::string &source,
                              const std::string &mod_name);
};

// =============================================================================
// Context Manager for Python with statement
// =============================================================================

struct LLVMContextManager : NoMoveCopy {
  std::unique_ptr<LLVMContextWrapper> m_context;

  LLVMContextWrapper &enter() {
    if (m_context)
      throw LLVMMemoryError("Context manager already entered");
    m_context = std::make_unique<LLVMContextWrapper>();
    return *m_context;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (!m_context)
      throw LLVMMemoryError("Context manager not entered");
    m_context.reset();
  }
};

// =============================================================================
// Module Manager for Python with statement
// =============================================================================

struct LLVMModuleManager : NoMoveCopy {
  std::string m_name;
  LLVMContextWrapper *m_context = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;
  std::unique_ptr<LLVMModuleWrapper> m_module;
  bool m_entered = false;
  bool m_disposed = false;
  bool m_from_clone = false; // True if this manager owns a cloned module

  // Constructor for ctx.create_module("name")
  LLVMModuleManager(std::string name, LLVMContextWrapper *context)
      : m_name(std::move(name)), m_context(context),
        m_context_token(context ? context->m_token : nullptr) {}

  // Constructor for mod.clone() - takes ownership of pre-created module
  explicit LLVMModuleManager(std::unique_ptr<LLVMModuleWrapper> cloned_module)
      : m_module(std::move(cloned_module)), m_from_clone(true) {}

  LLVMModuleWrapper &enter() {
    if (m_disposed)
      throw LLVMMemoryError("Module has been disposed");
    if (m_entered)
      throw LLVMMemoryError("Module manager already entered");

    m_entered = true;

    if (m_from_clone) {
      // Already have a module from clone - just validate and return it
      m_module->check_valid();
      return *m_module;
    }

    // Create new module
    if (!m_context)
      throw LLVMMemoryError("No context provided");
    // Check context token validity before dereferencing m_context
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Module's context has been destroyed");
    m_context->check_valid();
    m_module = std::make_unique<LLVMModuleWrapper>(m_name, m_context->m_ref,
                                                   m_context->m_token);
    return *m_module;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (m_disposed)
      throw LLVMMemoryError("Module has already been disposed");
    if (!m_entered)
      throw LLVMMemoryError("Module manager was not entered");
    m_module.reset();
    m_disposed = true;
  }

  void dispose() {
    if (m_disposed)
      throw LLVMMemoryError("Module has already been disposed");
    if (m_entered)
      throw LLVMMemoryError(
          "Cannot call dispose() after __enter__; use __exit__ "
          "or 'with' statement");
    if (!m_from_clone && !m_module)
      throw LLVMMemoryError("Module has not been created");
    m_module.reset();
    m_disposed = true;
  }
};

// =============================================================================
// Builder Manager for Python with statement
// =============================================================================

struct LLVMBuilderManager : NoMoveCopy {
  LLVMContextWrapper *m_context = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;
  std::unique_ptr<LLVMBuilderWrapper> m_builder;
  bool m_entered = false;
  bool m_disposed = false;

  // Position: exactly one must be set
  std::optional<LLVMBasicBlockRef> m_initial_bb;
  std::optional<LLVMValueRef> m_initial_inst;
  bool m_before_dbg = true; // Only used with m_initial_inst

  explicit LLVMBuilderManager(LLVMContextWrapper *context)
      : m_context(context),
        m_context_token(context ? context->m_token : nullptr) {}

  LLVMBuilderWrapper &enter() {
    if (m_disposed)
      throw LLVMMemoryError("Builder has been disposed");
    if (m_entered)
      throw LLVMMemoryError("Builder manager already entered");
    if (!m_context)
      throw LLVMMemoryError("No context provided");
    // Check context token validity before dereferencing m_context
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Builder's context has been destroyed");
    m_context->check_valid();

    // Create builder with position
    if (m_initial_bb) {
      m_builder = std::make_unique<LLVMBuilderWrapper>(
          m_context->m_ref, m_context->m_token, *m_initial_bb);
    } else if (m_initial_inst) {
      m_builder = std::make_unique<LLVMBuilderWrapper>(
          m_context->m_ref, m_context->m_token, *m_initial_inst, m_before_dbg);
    } else {
      throw LLVMMemoryError(
          "Builder must be created with a position (BasicBlock or "
          "Instruction). "
          "Use ctx.create_builder(bb), ctx.create_builder(inst), "
          "bb.create_builder(), or inst.create_builder().");
    }

    m_entered = true;
    return *m_builder;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (m_disposed)
      throw LLVMMemoryError("Builder has already been disposed");
    if (!m_entered)
      throw LLVMMemoryError("Builder manager was not entered");
    m_builder.reset();
    m_disposed = true;
  }

  void dispose() {
    if (m_disposed)
      throw LLVMMemoryError("Builder has already been disposed");
    if (m_entered)
      throw LLVMMemoryError(
          "Cannot call dispose() after __enter__; use __exit__ "
          "or 'with' statement");
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

LLVMBuilderManager *
LLVMContextWrapper::create_builder(const LLVMBasicBlockWrapper &bb) {
  check_valid();
  bb.check_valid();
  auto manager = new LLVMBuilderManager(this);
  manager->m_initial_bb = bb.m_ref;
  return manager;
}

LLVMBuilderManager *
LLVMContextWrapper::create_builder(const LLVMValueWrapper &inst,
                                   bool before_dbg) {
  check_valid();
  inst.check_valid();
  if (!inst.is_a_instruction())
    throw LLVMAssertionError("create_builder requires an instruction value");
  if (!LLVMGetInstructionParent(inst.m_ref))
    throw LLVMAssertionError(
        "create_builder requires an instruction in a basic block");
  auto manager = new LLVMBuilderManager(this);
  manager->m_initial_inst = inst.m_ref;
  manager->m_before_dbg = before_dbg;
  return manager;
}

// =============================================================================
// Convenience method implementations for BasicBlock and Value
// =============================================================================

LLVMBuilderManager *LLVMBasicBlockWrapper::create_builder(
    bool first_non_phi) const {
  check_valid();
  // Get the context for this basic block via function -> module -> context
  // Note: context() returns a new LLVMContextWrapper that we don't own
  // The builder manager will hold a non-owning pointer
  LLVMContextWrapper *ctx =
      const_cast<LLVMBasicBlockWrapper *>(this)->context();
  auto manager = new LLVMBuilderManager(ctx);

  if (first_non_phi) {
    LLVMValueRef inst = LLVMGetFirstInstruction(m_ref);
    while (inst && LLVMGetInstructionOpcode(inst) == LLVMPHI) {
      inst = LLVMGetNextInstruction(inst);
    }
    if (inst) {
      manager->m_initial_inst = inst;
      manager->m_before_dbg = true;
    } else {
      manager->m_initial_bb = m_ref;
    }
  } else {
    manager->m_initial_bb = m_ref;
  }
  return manager;
}

LLVMBuilderManager *LLVMValueWrapper::create_builder(bool before_dbg) const {
  check_valid();
  if (!is_a_instruction())
    throw LLVMAssertionError("create_builder requires an instruction value");
  if (!LLVMGetInstructionParent(m_ref))
    throw LLVMAssertionError(
        "create_builder requires an instruction in a basic block");
  // Get the context for this value
  LLVMContextWrapper *ctx = get_context();
  auto manager = new LLVMBuilderManager(ctx);
  manager->m_initial_inst = m_ref;
  manager->m_before_dbg = before_dbg;
  return manager;
}

LLVMModuleManager *
LLVMContextWrapper::parse_bitcode_from_file(const fs::path &filename,
                                            bool lazy) {
  check_valid();
  clear_diagnostics();

  // Create memory buffer from file
  LLVMMemoryBufferRef buf;
  char *error_msg = nullptr;
  if (LLVMCreateMemoryBufferWithContentsOfFile(
          (const char *)filename.u8string().data(), &buf, &error_msg)) {
    std::string err = error_msg ? error_msg : "Unknown error";
    if (error_msg)
      LLVMDisposeMessage(error_msg);
    throw LLVMError("Failed to read file: " + err);
  }

  // Parse bitcode
  LLVMModuleRef mod_ref;
  LLVMBool failed;
  if (lazy) {
    failed = LLVMGetBitcodeModuleInContext2(m_ref, buf, &mod_ref);
  } else {
    failed = LLVMParseBitcodeInContext2(m_ref, buf, &mod_ref);
  }

  if (failed) {
    LLVMDisposeMemoryBuffer(buf); // Dispose on failure!
    throw LLVMParseError(get_diagnostics());
  }

  // Create module wrapper
  auto mod = std::make_unique<LLVMModuleWrapper>(mod_ref, m_ref, m_token);

  // For lazy loading, LLVM's Module takes ownership and stores the buffer
  // internally For eager loading, LLVM consumed the buffer during parsing, so
  // we dispose it
  if (!lazy) {
    LLVMDisposeMemoryBuffer(buf);
  }
  // Note: We do NOT store buf in m_memory_buffer for lazy modules because
  // LLVM's Module already owns it. Storing it would cause a double-free.

  return new LLVMModuleManager(std::move(mod));
}

LLVMModuleManager *LLVMContextWrapper::parse_bitcode_from_bytes(nb::bytes data,
                                                                bool lazy) {
  check_valid();
  clear_diagnostics();

  // Create memory buffer with a copy (LLVM will own it on success)
  auto buf = LLVMCreateMemoryBufferWithMemoryRangeCopy(data.c_str(),
                                                       data.size(), "<bytes>");

  // Parse bitcode
  LLVMModuleRef mod_ref;
  LLVMBool failed;
  if (lazy) {
    failed = LLVMGetBitcodeModuleInContext2(m_ref, buf, &mod_ref);
  } else {
    failed = LLVMParseBitcodeInContext2(m_ref, buf, &mod_ref);
  }

  if (failed) {
    // On failure, we must dispose the buffer ourselves
    LLVMDisposeMemoryBuffer(buf);
    throw LLVMParseError(get_diagnostics());
  }

  // Create module wrapper (no buffer ownership - LLVM internals own it)
  auto mod = std::make_unique<LLVMModuleWrapper>(mod_ref, m_ref, m_token);

  // For lazy loading, LLVM's Module takes ownership and stores the buffer
  // internally. For eager loading, LLVM consumed the buffer during parsing,
  // so we dispose it.
  if (!lazy) {
    LLVMDisposeMemoryBuffer(buf);
  }

  return new LLVMModuleManager(std::move(mod));
}

LLVMModuleManager *LLVMContextWrapper::parse_ir(const std::string &source,
                                                const std::string &mod_name) {
  check_valid();
  clear_diagnostics();

  // Create memory buffer with a copy
  auto buf = LLVMCreateMemoryBufferWithMemoryRangeCopy(
      source.c_str(), source.size(), "<source>");

  // Parse IR (always eager)
  LLVMModuleRef mod_ref;
  char *error_msg = nullptr;
  auto failed = LLVMParseIRInContext(m_ref, buf, &mod_ref, &error_msg);

  if (failed) {
    // On failure, dispose buffer ourselves
    LLVMDisposeMemoryBuffer(buf);

    std::string err = error_msg ? error_msg : "Unknown error";
    if (error_msg)
      LLVMDisposeMessage(error_msg);
    // LLVMParseIRInContext doesn't use diagnostic handler, so we create
    // a diagnostic manually
    if (!err.empty()) {
      DiagnosticRegistry::instance().add_diagnostic(
          m_ref, {"error", err, std::nullopt, std::nullopt});
    }
    throw LLVMParseError(get_diagnostics());
  }

  // On success, LLVM took ownership of buf, don't dispose it
  auto mod = std::make_unique<LLVMModuleWrapper>(mod_ref, m_ref, m_token);
  return new LLVMModuleManager(std::move(mod));
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
    throw LLVMAssertionError("Cannot create empty vector constant");
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
  return LLVMValueWrapper(LLVMConstStringInContext2(ctx->m_ref, str.c_str(),
                                                    str.size(),
                                                    dont_null_terminate),
                          ctx->m_token);
}

LLVMValueWrapper const_string(LLVMContextWrapper *ctx, const nb::bytes &data,
                              bool dont_null_terminate = false) {
  ctx->check_valid();
  return LLVMValueWrapper(LLVMConstStringInContext2(ctx->m_ref, data.c_str(),
                                                    data.size(),
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

LLVMValueWrapper
const_int_of_arbitrary_precision(const LLVMTypeWrapper &ty,
                                 const std::vector<uint64_t> &words) {
  ty.check_valid();
  return LLVMValueWrapper(
      LLVMConstIntOfArbitraryPrecision(
          ty.m_ref, static_cast<unsigned>(words.size()), words.data()),
      ty.m_context_token);
}

// BlockAddress - create a constant that is the address of a basic block
LLVMValueWrapper block_address(LLVMFunctionWrapper &fn,
                               LLVMBasicBlockWrapper &bb) {
  fn.check_valid();
  bb.check_valid();
  if (LLVMGetBasicBlockParent(bb.m_ref) != fn.m_ref) {
    throw LLVMAssertionError(
        "block_address requires a basic block owned by the function");
  }
  return LLVMValueWrapper(LLVMBlockAddress(fn.m_ref, bb.m_ref),
                          fn.m_context_token);
}

// Constant creation functions for echo command
LLVMValueWrapper const_data_array(const LLVMTypeWrapper &elem_ty,
                                  const std::string &data) {
  elem_ty.check_valid();
  return LLVMValueWrapper(
      LLVMConstDataArray(elem_ty.m_ref, data.c_str(), data.size()),
      elem_ty.m_context_token);
}

LLVMValueWrapper const_data_array(const LLVMTypeWrapper &elem_ty,
                                  const nb::bytes &data) {
  elem_ty.check_valid();
  return LLVMValueWrapper(
      LLVMConstDataArray(elem_ty.m_ref, data.c_str(), data.size()),
      elem_ty.m_context_token);
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
  return LLVMValueWrapper(LLVMConstantPtrAuth(ptr.m_ref, key.m_ref,
                                              discriminator.m_ref,
                                              addr_discriminator.m_ref),
                          ptr.m_context_token);
}

// Intrinsic support functions
bool intrinsic_is_overloaded(unsigned id) {
  return LLVMIntrinsicIsOverloaded(id);
}

// Get the type of an intrinsic function given its ID and parameter types
LLVMTypeWrapper
intrinsic_get_type(LLVMContextWrapper *ctx, unsigned id,
                   const std::vector<LLVMTypeWrapper> &param_types) {
  ctx->check_valid();
  std::vector<LLVMTypeRef> type_refs;
  type_refs.reserve(param_types.size());
  for (const auto &ty : param_types) {
    ty.check_valid();
    type_refs.push_back(ty.m_ref);
  }
  return LLVMTypeWrapper(
      LLVMIntrinsicGetType(ctx->m_ref, id, type_refs.data(), type_refs.size()),
      ctx->m_token);
}

// Get the opcode needed to cast from one type to another
LLVMOpcode get_cast_opcode(const LLVMValueWrapper &src, bool src_is_signed,
                           const LLVMTypeWrapper &dest_ty,
                           bool dest_is_signed) {
  src.check_valid();
  dest_ty.check_valid();
  return LLVMGetCastOpcode(src.m_ref, src_is_signed, dest_ty.m_ref,
                           dest_is_signed);
}

// Forward declaration - implemented after LLVMMetadataWrapper is defined
void replace_md_node_operand_with(LLVMValueWrapper &val, unsigned index,
                                  const LLVMMetadataWrapper &replacement);

LLVMValueWrapper
get_intrinsic_declaration(LLVMModuleWrapper *mod, unsigned id,
                          const std::vector<LLVMTypeWrapper> &param_types) {
  mod->check_valid();
  std::vector<LLVMTypeRef> type_refs;
  type_refs.reserve(param_types.size());
  for (const auto &ty : param_types) {
    ty.check_valid();
    type_refs.push_back(ty.m_ref);
  }
  return LLVMValueWrapper(LLVMGetIntrinsicDeclaration(mod->m_ref, id,
                                                      type_refs.data(),
                                                      type_refs.size()),
                          mod->m_context_token);
}

// Helper for PHI nodes (with runtime assertions)
void phi_add_incoming(LLVMValueWrapper &phi, const LLVMValueWrapper &val,
                      const LLVMBasicBlockWrapper &bb) {
  phi.check_valid();
  val.check_valid();
  bb.check_valid();

  // Assert this is a PHI node
  if (!LLVMIsAPHINode(phi.m_ref))
    throw LLVMAssertionError("add_incoming() requires a PHI node");

  // Assert type compatibility
  LLVMTypeRef phi_type = LLVMTypeOf(phi.m_ref);
  LLVMTypeRef val_type = LLVMTypeOf(val.m_ref);
  if (phi_type != val_type)
    throw LLVMAssertionError("PHI incoming value type mismatch");

  LLVMValueRef vals[] = {val.m_ref};
  LLVMBasicBlockRef bbs[] = {bb.m_ref};
  LLVMAddIncoming(phi.m_ref, vals, bbs, 1);
}

// Helper for switch (with runtime assertions)
void switch_add_case(LLVMValueWrapper &switch_inst, const LLVMValueWrapper &val,
                     const LLVMBasicBlockWrapper &bb) {
  switch_inst.check_valid();
  val.check_valid();
  bb.check_valid();

  // Assert this is a switch instruction
  if (LLVMGetInstructionOpcode(switch_inst.m_ref) != LLVMSwitch)
    throw LLVMAssertionError("add_case() requires a switch instruction");

  // Assert case value is constant
  if (!LLVMIsConstant(val.m_ref))
    throw LLVMAssertionError("Switch case value must be constant");

  LLVMAddCase(switch_inst.m_ref, val.m_ref, bb.m_ref);
}

// Helper for indirect branch
void indirect_br_add_destination(LLVMValueWrapper &indirect_br,
                                 const LLVMBasicBlockWrapper &dest) {
  indirect_br.check_valid();
  dest.check_valid();

  // Assert this is an indirect branch instruction
  if (LLVMGetInstructionOpcode(indirect_br.m_ref) != LLVMIndirectBr)
    throw LLVMAssertionError(
        "add_destination() requires an indirect branch instruction");

  LLVMAddDestination(indirect_br.m_ref, dest.m_ref);
}

// Helper for globals
void global_set_initializer(LLVMValueWrapper &global,
                            const LLVMValueWrapper &init) {
  global.check_valid();
  init.check_valid();
  global.require_global_variable("initializer");
  LLVMSetInitializer(global.m_ref, init.m_ref);
}

void global_set_constant(LLVMValueWrapper &global, bool is_const) {
  global.check_valid();
  global.require_global_variable("set_constant");
  LLVMSetGlobalConstant(global.m_ref, is_const);
}

void global_set_linkage(LLVMValueWrapper &global, LLVMLinkage linkage) {
  global.check_valid();
  global.require_global_value("linkage");
  LLVMSetLinkage(global.m_ref, linkage);
}

// Removed duplicate global_set_alignment and global_get_alignment
// - these are now available directly on LLVMValueWrapper

bool global_is_constant(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_variable("is_global_constant");
  return LLVMIsGlobalConstant(global.m_ref);
}

LLVMLinkage global_get_linkage(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_value("linkage");
  return LLVMGetLinkage(global.m_ref);
}

void global_set_visibility(LLVMValueWrapper &global, LLVMVisibility vis) {
  global.check_valid();
  global.require_global_value("visibility");
  LLVMSetVisibility(global.m_ref, vis);
}

LLVMVisibility global_get_visibility(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_value("visibility");
  return LLVMGetVisibility(global.m_ref);
}

LLVMDLLStorageClass
global_get_dll_storage_class(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_value("dll_storage_class");
  return LLVMGetDLLStorageClass(global.m_ref);
}

void global_set_dll_storage_class(LLVMValueWrapper &global,
                                  LLVMDLLStorageClass storage_class) {
  global.check_valid();
  global.require_global_value("dll_storage_class");
  LLVMSetDLLStorageClass(global.m_ref, storage_class);
}

// Comdat getter/setter for global objects (functions and global variables)
std::optional<LLVMComdatWrapper>
global_get_comdat(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_object("comdat");
  LLVMComdatRef comdat = LLVMGetComdat(global.m_ref);
  if (!comdat)
    return std::nullopt;
  return LLVMComdatWrapper(comdat, global.m_context_token);
}

void global_set_comdat(LLVMValueWrapper &global,
                       const LLVMComdatWrapper &comdat) {
  global.check_valid();
  comdat.check_valid();
  global.require_global_object("set_comdat");
  LLVMSetComdat(global.m_ref, comdat.m_ref);
}

void global_set_section(LLVMValueWrapper &global, const std::string &section) {
  global.check_valid();
  global.require_global_object("section");
  LLVMSetSection(global.m_ref, section.c_str());
}

std::string global_get_section(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_object("section");
  const char *section = LLVMGetSection(global.m_ref);
  return section ? std::string(section) : "";
}

void global_set_thread_local(LLVMValueWrapper &global, bool is_tls) {
  global.check_valid();
  global.require_global_variable("set_thread_local");
  LLVMSetThreadLocal(global.m_ref, is_tls);
}

bool global_is_thread_local(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_variable("is_thread_local");
  return LLVMIsThreadLocal(global.m_ref);
}

void global_set_externally_initialized(LLVMValueWrapper &global, bool is_ext) {
  global.check_valid();
  global.require_global_variable("set_externally_initialized");
  LLVMSetExternallyInitialized(global.m_ref, is_ext);
}

bool global_is_externally_initialized(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_variable("is_externally_initialized");
  return LLVMIsExternallyInitialized(global.m_ref);
}

std::optional<LLVMValueWrapper>
global_get_initializer(const LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_variable("initializer");
  LLVMValueRef init = LLVMGetInitializer(global.m_ref);
  if (!init)
    return std::nullopt;
  return LLVMValueWrapper(init, global.m_context_token);
}

void global_delete(LLVMValueWrapper &global) {
  global.check_valid();
  global.require_global_variable("delete_global");
  LLVMDeleteGlobal(global.m_ref);
  global.m_ref = nullptr;
}

// PHI node helpers
// Removed duplicate phi_count_incoming - now available directly as
// LLVMValueWrapper::count_incoming()

LLVMBasicBlockWrapper phi_get_incoming_block(const LLVMValueWrapper &phi,
                                             unsigned index) {
  phi.check_valid();
  phi.require_phi_instruction("get_incoming_block");
  unsigned count = LLVMCountIncoming(phi.m_ref);
  if (index >= count) {
    throw LLVMAssertionError(
        "get_incoming_block: incoming block index " + std::to_string(index) +
        " out of range (num_incoming=" + std::to_string(count) + ")");
  }
  LLVMBasicBlockRef bb = LLVMGetIncomingBlock(phi.m_ref, index);
  if (!bb) {
    throw LLVMAssertionError("get_incoming_block: incoming block at index " +
                             std::to_string(index) + " is null");
  }
  return LLVMBasicBlockWrapper(bb, phi.m_context_token);
}

// Instruction property helpers

// Type helpers
unsigned type_count_struct_element_types(const LLVMTypeWrapper &ty) {
  ty.check_valid();
  if (!ty.is_struct())
    throw LLVMAssertionError("struct_element_count requires a struct type");
  return LLVMCountStructElementTypes(ty.m_ref);
}

// Helper for struct types
void struct_set_body(LLVMTypeWrapper &struct_ty,
                     const std::vector<LLVMTypeWrapper> &elem_types,
                     bool packed) {
  struct_ty.check_valid();
  if (!struct_ty.is_struct())
    throw LLVMAssertionError("set_body requires a struct type");
  if (struct_ty.is_literal_struct()) {
    throw LLVMAssertionError(
        "set_body requires an identified (named) struct type");
  }
  if (!struct_ty.is_opaque_struct()) {
    throw LLVMAssertionError("set_body requires an opaque struct type");
  }
  std::vector<LLVMTypeRef> elems;
  elems.reserve(elem_types.size());
  for (const auto &e : elem_types) {
    e.check_valid();
    elems.push_back(e.m_ref);
  }
  // blahblah
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
      throw LLVMMemoryError("Target is null");
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

// Convenience: initialize all targets, MCs, asm printers, and asm parsers
void initialize_all() {
  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargets();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllAsmPrinters();
  LLVMInitializeAllAsmParsers();
}

std::optional<LLVMTargetWrapper> get_first_target() {
  LLVMTargetRef ref = LLVMGetFirstTarget();
  if (ref)
    return LLVMTargetWrapper(ref);
  return std::nullopt;
}

// Get target from triple
std::optional<LLVMTargetWrapper>
get_target_from_triple(const std::string &triple) {
  LLVMTargetRef ref = nullptr;
  char *error = nullptr;
  if (LLVMGetTargetFromTriple(triple.c_str(), &ref, &error)) {
    std::string msg = error ? error : "Unknown error";
    if (error)
      LLVMDisposeMessage(error);
    throw LLVMError("Failed to get target from triple: " + msg);
  }
  if (error)
    LLVMDisposeMessage(error);
  if (ref)
    return LLVMTargetWrapper(ref);
  return std::nullopt;
}

// Get target from name
std::optional<LLVMTargetWrapper> get_target_from_name(const std::string &name) {
  LLVMTargetRef ref = LLVMGetTargetFromName(name.c_str());
  if (ref)
    return LLVMTargetWrapper(ref);
  return std::nullopt;
}

// Host target queries
std::string get_default_target_triple() {
  char *triple = LLVMGetDefaultTargetTriple();
  std::string result(triple);
  LLVMDisposeMessage(triple);
  return result;
}

std::string normalize_target_triple(const std::string &triple) {
  char *normalized = LLVMNormalizeTargetTriple(triple.c_str());
  std::string result(normalized);
  LLVMDisposeMessage(normalized);
  return result;
}

std::string get_host_cpu_name() {
  char *cpu = LLVMGetHostCPUName();
  std::string result(cpu);
  LLVMDisposeMessage(cpu);
  return result;
}

std::string get_host_cpu_features() {
  char *features = LLVMGetHostCPUFeatures();
  std::string result(features);
  LLVMDisposeMessage(features);
  return result;
}

// Native target initialization
bool initialize_native_target() { return LLVMInitializeNativeTarget() == 0; }

bool initialize_native_asm_printer() {
  return LLVMInitializeNativeAsmPrinter() == 0;
}

bool initialize_native_asm_parser() {
  return LLVMInitializeNativeAsmParser() == 0;
}

bool initialize_native_disassembler() {
  return LLVMInitializeNativeDisassembler() == 0;
}

// =============================================================================
// Target Data Wrapper
// =============================================================================

struct LLVMTargetDataWrapper : NoMoveCopy {
  LLVMTargetDataRef m_ref = nullptr;
  bool m_owned = true;

  LLVMTargetDataWrapper() = default;
  explicit LLVMTargetDataWrapper(LLVMTargetDataRef ref, bool owned = true)
      : m_ref(ref), m_owned(owned) {}

  ~LLVMTargetDataWrapper() {
    if (m_ref && m_owned) {
      LLVMDisposeTargetData(m_ref);
      m_ref = nullptr;
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMMemoryError("TargetData is null");
  }

  std::string to_string() const {
    check_valid();
    char *str = LLVMCopyStringRepOfTargetData(m_ref);
    std::string result(str);
    LLVMDisposeMessage(str);
    return result;
  }

  LLVMByteOrdering byte_order() const {
    check_valid();
    return LLVMByteOrder(m_ref);
  }

  unsigned pointer_size(unsigned address_space = 0) const {
    check_valid();
    return LLVMPointerSizeForAS(m_ref, address_space);
  }

  unsigned long long size_of_type_in_bits(const LLVMTypeWrapper &ty) const {
    check_valid();
    ty.check_valid();
    return LLVMSizeOfTypeInBits(m_ref, ty.m_ref);
  }

  unsigned long long store_size_of_type(const LLVMTypeWrapper &ty) const {
    check_valid();
    ty.check_valid();
    return LLVMStoreSizeOfType(m_ref, ty.m_ref);
  }

  unsigned long long abi_size_of_type(const LLVMTypeWrapper &ty) const {
    check_valid();
    ty.check_valid();
    return LLVMABISizeOfType(m_ref, ty.m_ref);
  }

  unsigned abi_alignment_of_type(const LLVMTypeWrapper &ty) const {
    check_valid();
    ty.check_valid();
    return LLVMABIAlignmentOfType(m_ref, ty.m_ref);
  }

  unsigned call_frame_alignment_of_type(const LLVMTypeWrapper &ty) const {
    check_valid();
    ty.check_valid();
    return LLVMCallFrameAlignmentOfType(m_ref, ty.m_ref);
  }

  unsigned preferred_alignment_of_type(const LLVMTypeWrapper &ty) const {
    check_valid();
    ty.check_valid();
    return LLVMPreferredAlignmentOfType(m_ref, ty.m_ref);
  }

  unsigned preferred_alignment_of_global(const LLVMValueWrapper &gv) const {
    check_valid();
    gv.check_valid();
    return LLVMPreferredAlignmentOfGlobal(m_ref, gv.m_ref);
  }

  unsigned element_at_offset(const LLVMTypeWrapper &struct_ty,
                             unsigned long long offset) const {
    check_valid();
    struct_ty.check_valid();
    return LLVMElementAtOffset(m_ref, struct_ty.m_ref, offset);
  }

  unsigned long long offset_of_element(const LLVMTypeWrapper &struct_ty,
                                       unsigned elem) const {
    check_valid();
    struct_ty.check_valid();
    return LLVMOffsetOfElement(m_ref, struct_ty.m_ref, elem);
  }

  LLVMTypeWrapper *int_ptr_type(const LLVMContextWrapper &ctx,
                                unsigned address_space = 0) const {
    check_valid();
    ctx.check_valid();
    LLVMTypeRef ty =
        LLVMIntPtrTypeForASInContext(ctx.m_ref, m_ref, address_space);
    return new LLVMTypeWrapper(ty, ctx.m_token);
  }
};

LLVMTargetDataWrapper *create_target_data(const std::string &string_rep) {
  return new LLVMTargetDataWrapper(LLVMCreateTargetData(string_rep.c_str()));
}

// =============================================================================
// Target Machine Wrapper
// =============================================================================

struct LLVMTargetMachineWrapper : NoMoveCopy {
  LLVMTargetMachineRef m_ref = nullptr;

  LLVMTargetMachineWrapper() = default;
  explicit LLVMTargetMachineWrapper(LLVMTargetMachineRef ref) : m_ref(ref) {}

  ~LLVMTargetMachineWrapper() {
    if (m_ref) {
      LLVMDisposeTargetMachine(m_ref);
      m_ref = nullptr;
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMMemoryError("TargetMachine is null");
  }

  LLVMTargetWrapper get_target() const {
    check_valid();
    return LLVMTargetWrapper(LLVMGetTargetMachineTarget(m_ref));
  }

  std::string get_triple() const {
    check_valid();
    char *triple = LLVMGetTargetMachineTriple(m_ref);
    std::string result(triple);
    LLVMDisposeMessage(triple);
    return result;
  }

  std::string get_cpu() const {
    check_valid();
    char *cpu = LLVMGetTargetMachineCPU(m_ref);
    std::string result(cpu);
    LLVMDisposeMessage(cpu);
    return result;
  }

  std::string get_feature_string() const {
    check_valid();
    char *features = LLVMGetTargetMachineFeatureString(m_ref);
    std::string result(features);
    LLVMDisposeMessage(features);
    return result;
  }

  LLVMTargetDataWrapper *create_data_layout() const {
    check_valid();
    return new LLVMTargetDataWrapper(LLVMCreateTargetDataLayout(m_ref));
  }

  void set_asm_verbosity(bool verbose) {
    check_valid();
    LLVMSetTargetMachineAsmVerbosity(m_ref, verbose);
  }

  void set_fast_isel(bool enable) {
    check_valid();
    LLVMSetTargetMachineFastISel(m_ref, enable);
  }

  void set_global_isel(bool enable) {
    check_valid();
    LLVMSetTargetMachineGlobalISel(m_ref, enable);
  }

  void set_global_isel_abort(LLVMGlobalISelAbortMode mode) {
    check_valid();
    LLVMSetTargetMachineGlobalISelAbort(m_ref, mode);
  }

  void set_machine_outliner(bool enable) {
    check_valid();
    LLVMSetTargetMachineMachineOutliner(m_ref, enable);
  }

  // Emit to file
  void emit_to_file(LLVMModuleWrapper &mod, const std::string &filename,
                    LLVMCodeGenFileType file_type) {
    check_valid();
    mod.check_valid();
    char *error = nullptr;
    if (LLVMTargetMachineEmitToFile(m_ref, mod.m_ref, filename.c_str(),
                                    file_type, &error)) {
      std::string msg = error ? error : "Unknown error";
      if (error)
        LLVMDisposeMessage(error);
      throw LLVMError("Failed to emit to file: " + msg);
    }
    if (error)
      LLVMDisposeMessage(error);
  }

  // Emit to memory buffer - returns bytes
  nb::bytes emit_to_memory_buffer(LLVMModuleWrapper &mod,
                                  LLVMCodeGenFileType file_type) {
    check_valid();
    mod.check_valid();
    LLVMMemoryBufferRef buf = nullptr;
    char *error = nullptr;
    if (LLVMTargetMachineEmitToMemoryBuffer(m_ref, mod.m_ref, file_type, &error,
                                            &buf)) {
      std::string msg = error ? error : "Unknown error";
      if (error)
        LLVMDisposeMessage(error);
      throw LLVMError("Failed to emit to memory buffer: " + msg);
    }
    if (error)
      LLVMDisposeMessage(error);

    const char *data = LLVMGetBufferStart(buf);
    size_t size = LLVMGetBufferSize(buf);
    nb::bytes result(data, size);
    LLVMDisposeMemoryBuffer(buf);
    return result;
  }
};

// Create target machine from target
LLVMTargetMachineWrapper *create_target_machine(const LLVMTargetWrapper &target,
                                                const std::string &triple,
                                                const std::string &cpu,
                                                const std::string &features,
                                                LLVMCodeGenOptLevel opt_level,
                                                LLVMRelocMode reloc_mode,
                                                LLVMCodeModel code_model) {
  target.check_valid();
  LLVMTargetMachineRef ref = LLVMCreateTargetMachine(
      target.m_ref, triple.c_str(), cpu.c_str(), features.c_str(), opt_level,
      reloc_mode, code_model);
  if (!ref) {
    throw LLVMError("Failed to create target machine");
  }
  return new LLVMTargetMachineWrapper(ref);
}

// =============================================================================
// Pass Builder Options Wrapper
// =============================================================================

struct LLVMPassBuilderOptionsWrapper : NoMoveCopy {
  LLVMPassBuilderOptionsRef m_ref = nullptr;

  LLVMPassBuilderOptionsWrapper() { m_ref = LLVMCreatePassBuilderOptions(); }

  ~LLVMPassBuilderOptionsWrapper() {
    if (m_ref) {
      LLVMDisposePassBuilderOptions(m_ref);
      m_ref = nullptr;
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMMemoryError("PassBuilderOptions is null");
  }

  void set_verify_each(bool verify) {
    check_valid();
    LLVMPassBuilderOptionsSetVerifyEach(m_ref, verify);
  }

  void set_debug_logging(bool enable) {
    check_valid();
    LLVMPassBuilderOptionsSetDebugLogging(m_ref, enable);
  }

  void set_loop_interleaving(bool enable) {
    check_valid();
    LLVMPassBuilderOptionsSetLoopInterleaving(m_ref, enable);
  }

  void set_loop_vectorization(bool enable) {
    check_valid();
    LLVMPassBuilderOptionsSetLoopVectorization(m_ref, enable);
  }

  void set_slp_vectorization(bool enable) {
    check_valid();
    LLVMPassBuilderOptionsSetSLPVectorization(m_ref, enable);
  }

  void set_loop_unrolling(bool enable) {
    check_valid();
    LLVMPassBuilderOptionsSetLoopUnrolling(m_ref, enable);
  }

  void set_forget_all_scev_in_loop_unroll(bool forget) {
    check_valid();
    LLVMPassBuilderOptionsSetForgetAllSCEVInLoopUnroll(m_ref, forget);
  }

  void set_licm_mssa_opt_cap(unsigned cap) {
    check_valid();
    LLVMPassBuilderOptionsSetLicmMssaOptCap(m_ref, cap);
  }

  void set_licm_mssa_no_acc_for_promotion_cap(unsigned cap) {
    check_valid();
    LLVMPassBuilderOptionsSetLicmMssaNoAccForPromotionCap(m_ref, cap);
  }

  void set_call_graph_profile(bool enable) {
    check_valid();
    LLVMPassBuilderOptionsSetCallGraphProfile(m_ref, enable);
  }

  void set_merge_functions(bool enable) {
    check_valid();
    LLVMPassBuilderOptionsSetMergeFunctions(m_ref, enable);
  }

  void set_inliner_threshold(int threshold) {
    check_valid();
    LLVMPassBuilderOptionsSetInlinerThreshold(m_ref, threshold);
  }
};

// Run passes on module
void run_passes(LLVMModuleWrapper &mod, const std::string &passes,
                LLVMTargetMachineWrapper *tm,
                LLVMPassBuilderOptionsWrapper *opts) {
  mod.check_valid();
  LLVMTargetMachineRef tm_ref = nullptr;
  if (tm) {
    tm->check_valid();
    tm_ref = tm->m_ref;
  }
  LLVMPassBuilderOptionsRef opts_ref = nullptr;
  if (opts) {
    opts->check_valid();
    opts_ref = opts->m_ref;
  }

  LLVMErrorRef err = LLVMRunPasses(mod.m_ref, passes.c_str(), tm_ref, opts_ref);
  if (err) {
    char *msg = LLVMGetErrorMessage(err);
    std::string error_msg = msg ? msg : "Unknown error";
    LLVMDisposeErrorMessage(msg);
    throw LLVMError("Failed to run passes: " + error_msg);
  }
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
      throw LLVMMemoryError("MemoryBuffer is null");
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

// LLVMMemoryBufferWrapper is kept internal for module parsing - not exposed to
// Python

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
      throw LLVMMemoryError("DisasmContext is null or invalid");
  }

  // Disassemble a single instruction
  // Returns (bytes_consumed, disassembly_string)
  std::pair<size_t, std::string>
  disasm_instruction(const std::vector<uint8_t> &bytes, size_t offset,
                     uint64_t pc) {
    check_valid();
    if (offset >= bytes.size())
      return {0, ""};

    char outline[1024];
    size_t consumed = LLVMDisasmInstruction(
        m_ref, const_cast<uint8_t *>(bytes.data() + offset),
        bytes.size() - offset, pc, outline, sizeof(outline));

    if (consumed == 0)
      return {0, ""};
    return {consumed, std::string(outline)};
  }

  /// Set disassembler options.
  /// Returns true on success (1 on success, 0 on failure in C API).
  bool set_options(uint64_t options) {
    check_valid();
    return LLVMSetDisasmOptions(m_ref, options) != 0;
  }
};

// Create disassembler with triple, cpu, and features
LLVMDisasmContextWrapper *
create_disasm_cpu_features(const std::string &triple, const std::string &cpu,
                           const std::string &features) {
  LLVMDisasmContextRef ref =
      LLVMCreateDisasmCPUFeatures(triple.c_str(), cpu.c_str(), features.c_str(),
                                  nullptr, 0, nullptr, nullptr);

  return new LLVMDisasmContextWrapper(ref);
}

// =============================================================================
// Object File Wrappers
// =============================================================================

// Forward declarations
struct LLVMBinaryWrapper;
struct LLVMBinaryManager;
struct LLVMSectionIteratorWrapper;
struct LLVMSymbolIteratorWrapper;
struct LLVMRelocationIteratorWrapper;

// Binary wrapper - owns the binary and its backing memory buffer
struct LLVMBinaryWrapper : NoMoveCopy {
  LLVMBinaryRef m_ref = nullptr;
  LLVMMemoryBufferRef m_buf =
      nullptr; // Owned - must stay alive while binary exists
  std::shared_ptr<ValidityToken> m_token;

  LLVMBinaryWrapper() = default;
  LLVMBinaryWrapper(LLVMBinaryRef ref, LLVMMemoryBufferRef buf)
      : m_ref(ref), m_buf(buf), m_token(std::make_shared<ValidityToken>()) {}

  ~LLVMBinaryWrapper() { dispose_internal(); }

  void dispose_internal() {
    if (m_token)
      m_token->invalidate();
    if (m_ref) {
      LLVMDisposeBinary(m_ref);
      m_ref = nullptr;
    }
    // Dispose buffer AFTER binary since binary references it
    if (m_buf) {
      LLVMDisposeMemoryBuffer(m_buf);
      m_buf = nullptr;
    }
  }

  void check_valid() const {
    if (!m_ref)
      throw LLVMMemoryError("Binary has been disposed");
  }

  LLVMBinaryType get_type() const {
    check_valid();
    return LLVMBinaryGetType(m_ref);
  }

  /// Copy the binary's memory buffer content.
  /// Returns a copy of the binary's backing memory buffer as bytes.
  nb::bytes copy_to_memory_buffer() const {
    check_valid();
    LLVMMemoryBufferRef buf = LLVMBinaryCopyMemoryBuffer(m_ref);
    const char *start = LLVMGetBufferStart(buf);
    size_t size = LLVMGetBufferSize(buf);
    nb::bytes result(start, size);
    LLVMDisposeMemoryBuffer(buf);
    return result;
  }
};

inline const char *binary_type_name(LLVMBinaryType type) {
  switch (type) {
  case LLVMBinaryTypeArchive:
    return "archive";
  case LLVMBinaryTypeMachOUniversalBinary:
    return "mach-o universal binary";
  case LLVMBinaryTypeCOFFImportFile:
    return "coff import file";
  case LLVMBinaryTypeIR:
    return "ir/bitcode";
  case LLVMBinaryTypeWinRes:
    return "windows resource";
  case LLVMBinaryTypeCOFF:
    return "coff object";
  case LLVMBinaryTypeELF32L:
    return "elf32 little-endian object";
  case LLVMBinaryTypeELF32B:
    return "elf32 big-endian object";
  case LLVMBinaryTypeELF64L:
    return "elf64 little-endian object";
  case LLVMBinaryTypeELF64B:
    return "elf64 big-endian object";
  case LLVMBinaryTypeMachO32L:
    return "mach-o32 little-endian object";
  case LLVMBinaryTypeMachO32B:
    return "mach-o32 big-endian object";
  case LLVMBinaryTypeMachO64L:
    return "mach-o64 little-endian object";
  case LLVMBinaryTypeMachO64B:
    return "mach-o64 big-endian object";
  case LLVMBinaryTypeWasm:
    return "wasm object";
  case LLVMBinaryTypeOffload:
    return "offload";
  default:
    return "unknown";
  }
}

inline bool binary_supports_object_iterators(LLVMBinaryType type) {
  switch (type) {
  case LLVMBinaryTypeCOFF:
  case LLVMBinaryTypeELF32L:
  case LLVMBinaryTypeELF32B:
  case LLVMBinaryTypeELF64L:
  case LLVMBinaryTypeELF64B:
  case LLVMBinaryTypeMachO32L:
  case LLVMBinaryTypeMachO32B:
  case LLVMBinaryTypeMachO64L:
  case LLVMBinaryTypeMachO64B:
  case LLVMBinaryTypeWasm:
    return true;
  default:
    return false;
  }
}

// Section iterator - holds validity token from binary
struct LLVMSectionIteratorWrapper : NoMoveCopy {
  LLVMSectionIteratorRef m_ref = nullptr;
  LLVMBinaryRef m_binary_ref = nullptr; // For is_at_end check
  std::shared_ptr<ValidityToken> m_binary_token;
  // Python iterator state: __next__ returns current item, advances on
  // subsequent calls. This avoids exposing end-iterator data to Python code.
  bool m_python_iter_started = false;

  LLVMSectionIteratorWrapper() = default;
  LLVMSectionIteratorWrapper(LLVMSectionIteratorRef ref,
                             LLVMBinaryRef binary_ref,
                             std::shared_ptr<ValidityToken> binary_token)
      : m_ref(ref), m_binary_ref(binary_ref),
        m_binary_token(std::move(binary_token)) {}

  ~LLVMSectionIteratorWrapper() {
    if (m_ref) {
      LLVMDisposeSectionIterator(m_ref);
      m_ref = nullptr;
    }
  }

  void check_valid() const {
    if (!m_binary_token || !m_binary_token->is_valid())
      throw LLVMMemoryError("Section iterator used after binary was disposed");
    if (!m_ref)
      throw LLVMMemoryError("Section iterator is null");
  }

  bool is_at_end() const {
    check_valid();
    return LLVMObjectFileIsSectionIteratorAtEnd(m_binary_ref, m_ref);
  }

  void check_not_at_end(const char *api_name) const {
    if (is_at_end()) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires iterator not at end");
    }
  }

  void move_next() {
    check_valid();
    LLVMMoveToNextSection(m_ref);
  }

  std::string get_name() const {
    check_valid();
    check_not_at_end("SectionIterator.name");
    const char *name = LLVMGetSectionName(m_ref);
    return name ? std::string(name) : std::string();
  }

  uint64_t get_address() const {
    check_valid();
    check_not_at_end("SectionIterator.address");
    return LLVMGetSectionAddress(m_ref);
  }

  uint64_t get_size() const {
    check_valid();
    check_not_at_end("SectionIterator.size");
    return LLVMGetSectionSize(m_ref);
  }

  nb::bytes get_contents() const {
    check_valid();
    check_not_at_end("SectionIterator.contents");
    const char *contents = LLVMGetSectionContents(m_ref);
    size_t size = LLVMGetSectionSize(m_ref);
    return nb::bytes(contents, size);
  }

  bool contains_symbol(const LLVMSymbolIteratorWrapper &sym) const;

  // For Python iteration - returns self
  LLVMSectionIteratorWrapper &iter() {
    check_valid();
    return *this;
  }

  // For Python iteration - returns next section or throws StopIteration
  LLVMSectionIteratorWrapper &next() {
    check_valid();
    if (is_at_end()) {
      throw nb::stop_iteration();
    }
    // We return self since Python iteration expects the iterator to advance
    // and return the current value. We'll advance after returning.
    return *this;
  }
};

// Symbol iterator - holds validity token from binary
struct LLVMSymbolIteratorWrapper : NoMoveCopy {
  LLVMSymbolIteratorRef m_ref = nullptr;
  LLVMBinaryRef m_binary_ref = nullptr;
  std::shared_ptr<ValidityToken> m_binary_token;
  bool m_python_iter_started = false;

  LLVMSymbolIteratorWrapper() = default;
  LLVMSymbolIteratorWrapper(LLVMSymbolIteratorRef ref, LLVMBinaryRef binary_ref,
                            std::shared_ptr<ValidityToken> binary_token)
      : m_ref(ref), m_binary_ref(binary_ref),
        m_binary_token(std::move(binary_token)) {}

  ~LLVMSymbolIteratorWrapper() {
    if (m_ref) {
      LLVMDisposeSymbolIterator(m_ref);
      m_ref = nullptr;
    }
  }

  void check_valid() const {
    if (!m_binary_token || !m_binary_token->is_valid())
      throw LLVMMemoryError("Symbol iterator used after binary was disposed");
    if (!m_ref)
      throw LLVMMemoryError("Symbol iterator is null");
  }

  bool is_at_end() const {
    check_valid();
    return LLVMObjectFileIsSymbolIteratorAtEnd(m_binary_ref, m_ref);
  }

  void check_not_at_end(const char *api_name) const {
    if (is_at_end()) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires iterator not at end");
    }
  }

  void move_next() {
    check_valid();
    LLVMMoveToNextSymbol(m_ref);
  }

  std::string get_name() const {
    check_valid();
    check_not_at_end("SymbolIterator.name");
    const char *name = LLVMGetSymbolName(m_ref);
    return name ? std::string(name) : std::string();
  }

  uint64_t get_address() const {
    check_valid();
    check_not_at_end("SymbolIterator.address");
    return LLVMGetSymbolAddress(m_ref);
  }

  uint64_t get_size() const {
    check_valid();
    check_not_at_end("SymbolIterator.size");

    // Some symbol kinds (e.g. file symbols) have no containing section.
    // LLVMGetSymbolSize can assert on those in debug builds.
    LLVMSectionIteratorRef sect = LLVMObjectFileCopySectionIterator(m_binary_ref);
    if (!sect)
      return 0;

    LLVMMoveToContainingSection(sect, m_ref);
    bool HasContainingSection =
        !LLVMObjectFileIsSectionIteratorAtEnd(m_binary_ref, sect);
    LLVMDisposeSectionIterator(sect);

    if (!HasContainingSection)
      return 0;

    return LLVMGetSymbolSize(m_ref);
  }

  // For Python iteration
  LLVMSymbolIteratorWrapper &iter() {
    check_valid();
    return *this;
  }

  LLVMSymbolIteratorWrapper &next() {
    check_valid();
    if (is_at_end()) {
      throw nb::stop_iteration();
    }
    return *this;
  }
};

// Relocation iterator - holds validity token from binary
struct LLVMRelocationIteratorWrapper : NoMoveCopy {
  LLVMRelocationIteratorRef m_ref = nullptr;
  LLVMSectionIteratorRef m_section_ref = nullptr; // For is_at_end check
  std::shared_ptr<ValidityToken> m_binary_token;
  bool m_python_iter_started = false;

  LLVMRelocationIteratorWrapper() = default;
  LLVMRelocationIteratorWrapper(LLVMRelocationIteratorRef ref,
                                LLVMSectionIteratorRef section_ref,
                                std::shared_ptr<ValidityToken> binary_token)
      : m_ref(ref), m_section_ref(section_ref),
        m_binary_token(std::move(binary_token)) {}

  ~LLVMRelocationIteratorWrapper() {
    if (m_ref) {
      LLVMDisposeRelocationIterator(m_ref);
      m_ref = nullptr;
    }
  }

  void check_valid() const {
    if (!m_binary_token || !m_binary_token->is_valid())
      throw LLVMMemoryError(
          "Relocation iterator used after binary was disposed");
    if (!m_ref)
      throw LLVMMemoryError("Relocation iterator is null");
  }

  bool is_at_end() const {
    check_valid();
    return LLVMIsRelocationIteratorAtEnd(m_section_ref, m_ref);
  }

  void check_not_at_end(const char *api_name) const {
    if (is_at_end()) {
      throw LLVMAssertionError(std::string(api_name) +
                               " requires iterator not at end");
    }
  }

  void move_next() {
    check_valid();
    LLVMMoveToNextRelocation(m_ref);
  }

  uint64_t get_offset() const {
    check_valid();
    check_not_at_end("RelocationIterator.offset");
    return LLVMGetRelocationOffset(m_ref);
  }

  uint64_t get_type() const {
    check_valid();
    check_not_at_end("RelocationIterator.type");
    return LLVMGetRelocationType(m_ref);
  }

  std::string get_type_name() const {
    check_valid();
    check_not_at_end("RelocationIterator.type_name");
    const char *name = LLVMGetRelocationTypeName(m_ref);
    std::string result = name ? std::string(name) : std::string();
    // Caller owns the string, so we need to free it
    if (name)
      LLVMDisposeMessage(const_cast<char *>(name));
    return result;
  }

  std::string get_value_string() const {
    check_valid();
    check_not_at_end("RelocationIterator.value_string");
    const char *value = LLVMGetRelocationValueString(m_ref);
    std::string result = value ? std::string(value) : std::string();
    if (value)
      LLVMDisposeMessage(const_cast<char *>(value));
    return result;
  }

  // For Python iteration
  LLVMRelocationIteratorWrapper &iter() {
    check_valid();
    return *this;
  }

  LLVMRelocationIteratorWrapper &next() {
    check_valid();
    if (is_at_end()) {
      throw nb::stop_iteration();
    }
    return *this;
  }
};

// Implement contains_symbol after LLVMSymbolIteratorWrapper is defined
bool LLVMSectionIteratorWrapper::contains_symbol(
    const LLVMSymbolIteratorWrapper &sym) const {
  check_valid();
  sym.check_valid();
  if (is_at_end()) {
    throw LLVMAssertionError(
        "SectionIterator.contains_symbol requires section iterator not at end");
  }
  if (sym.is_at_end()) {
    throw LLVMAssertionError(
        "SectionIterator.contains_symbol requires symbol iterator not at end");
  }
  return LLVMGetSectionContainsSymbol(m_ref, sym.m_ref);
}

// Binary manager for Python 'with' statement
struct LLVMBinaryManager : NoMoveCopy {
  std::unique_ptr<LLVMBinaryWrapper> m_binary;
  bool m_entered = false;
  bool m_disposed = false;

  // For deferred creation from bytes
  std::optional<std::string> m_bytes_data; // Store as string to own the data

  // For deferred creation from file
  std::optional<fs::path> m_file_path;

  // Constructor for from_bytes
  explicit LLVMBinaryManager(nb::bytes data)
      : m_bytes_data(std::string(data.c_str(), data.size())) {}

  // Constructor for from_file
  explicit LLVMBinaryManager(fs::path path) : m_file_path(std::move(path)) {}

  LLVMBinaryWrapper &enter() {
    if (m_disposed)
      throw LLVMMemoryError("Binary has been disposed");
    if (m_entered)
      throw LLVMMemoryError("Binary manager already entered");

    m_entered = true;

    if (m_bytes_data) {
      m_binary = create_from_bytes(*m_bytes_data);
    } else if (m_file_path) {
      m_binary = create_from_file(*m_file_path);
    } else {
      throw LLVMMemoryError("No data source for binary");
    }

    return *m_binary;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (m_disposed)
      throw LLVMMemoryError("Binary has already been disposed");
    if (!m_entered)
      throw LLVMMemoryError("Binary manager was not entered");
    m_binary.reset();
    m_disposed = true;
  }

  void dispose() {
    if (m_disposed)
      throw LLVMMemoryError("Binary has already been disposed");
    if (m_entered)
      throw LLVMMemoryError("Cannot call dispose() after __enter__; use "
                            "__exit__ or 'with' statement");
    m_disposed = true;
  }

private:
  static std::unique_ptr<LLVMBinaryWrapper>
  create_from_bytes(const std::string &data) {
    LLVMMemoryBufferRef buf = LLVMCreateMemoryBufferWithMemoryRangeCopy(
        data.c_str(), data.size(), "<bytes>");
    if (!buf) {
      throw LLVMError("Failed to create memory buffer");
    }

    char *error_msg = nullptr;
    LLVMBinaryRef ref =
        LLVMCreateBinary(buf, LLVMGetGlobalContext(), &error_msg);

    if (!ref || error_msg) {
      std::string err =
          error_msg ? std::string(error_msg) : "Unknown error creating binary";
      if (error_msg)
        LLVMDisposeMessage(error_msg);
      LLVMDisposeMemoryBuffer(buf);
      throw LLVMError("Error creating binary: " + err);
    }

    return std::make_unique<LLVMBinaryWrapper>(ref, buf);
  }

  static std::unique_ptr<LLVMBinaryWrapper>
  create_from_file(const fs::path &path) {
    LLVMMemoryBufferRef buf;
    char *error_msg = nullptr;

    if (LLVMCreateMemoryBufferWithContentsOfFile(path.string().c_str(), &buf,
                                                 &error_msg)) {
      std::string err =
          error_msg ? std::string(error_msg) : "Unknown error reading file";
      if (error_msg)
        LLVMDisposeMessage(error_msg);
      throw LLVMError("Error reading file: " + err);
    }

    char *binary_err = nullptr;
    LLVMBinaryRef ref =
        LLVMCreateBinary(buf, LLVMGetGlobalContext(), &binary_err);

    if (!ref || binary_err) {
      std::string err = binary_err ? std::string(binary_err)
                                   : "Unknown error creating binary";
      if (binary_err)
        LLVMDisposeMessage(binary_err);
      LLVMDisposeMemoryBuffer(buf);
      throw LLVMError("Error creating binary: " + err);
    }

    return std::make_unique<LLVMBinaryWrapper>(ref, buf);
  }
};

// Factory functions
LLVMBinaryManager *create_binary_from_bytes(nb::bytes data) {
  return new LLVMBinaryManager(std::move(data));
}

LLVMBinaryManager *create_binary_from_file(const fs::path &path) {
  return new LLVMBinaryManager(path);
}

// =============================================================================
// BitReader Functions
// =============================================================================

// Parse bitcode (legacy API with error message)
// =============================================================================
// DIBuilder Wrapper
// =============================================================================

struct LLVMMetadataWrapper;

struct LLVMDIBuilderWrapper : NoMoveCopy {
  LLVMDIBuilderRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_module_token;

  LLVMDIBuilderWrapper(LLVMModuleRef mod,
                       std::shared_ptr<ValidityToken> module_token)
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
      throw LLVMMemoryError("DIBuilder is null");
    if (!m_module_token || !m_module_token->is_valid())
      throw LLVMMemoryError("DIBuilder used after module was destroyed");
  }

  void finalize() {
    check_valid();
    LLVMDIBuilderFinalize(m_ref);
  }

  // =========================================================================
  // File & Scope Creation Methods
  // =========================================================================

  LLVMMetadataWrapper create_file(const std::string &filename,
                                  const std::string &directory);

  LLVMMetadataWrapper create_compile_unit(
      int lang, const LLVMMetadataWrapper &file, const std::string &producer,
      bool is_optimized, const std::string &flags, unsigned runtime_ver,
      const std::string &split_name, unsigned kind, unsigned dwo_id,
      bool split_debug_inlining, bool debug_info_for_profiling,
      const std::string &sys_root, const std::string &sdk);

  LLVMMetadataWrapper create_module(const LLVMMetadataWrapper &parent_scope,
                                    const std::string &name,
                                    const std::string &config_macros,
                                    const std::string &include_path,
                                    const std::string &api_notes_file);

  LLVMMetadataWrapper create_namespace(const LLVMMetadataWrapper &parent_scope,
                                       const std::string &name,
                                       bool export_symbols);

  LLVMMetadataWrapper create_lexical_block(const LLVMMetadataWrapper &scope,
                                           const LLVMMetadataWrapper &file,
                                           unsigned line, unsigned column);

  // =========================================================================
  // Function & Subroutine Creation Methods
  // =========================================================================

  LLVMMetadataWrapper
  create_function(const LLVMMetadataWrapper &scope, const std::string &name,
                  const std::string &linkage_name,
                  const LLVMMetadataWrapper &file, unsigned line_no,
                  const LLVMMetadataWrapper *subroutine_type,
                  bool is_local_to_unit, bool is_definition,
                  unsigned scope_line, unsigned flags, bool is_optimized);

  LLVMMetadataWrapper
  create_subroutine_type(const LLVMMetadataWrapper &file,
                         const std::vector<LLVMMetadataWrapper> &param_types,
                         unsigned flags);

  // =========================================================================
  // Type Creation Methods
  // =========================================================================

  LLVMMetadataWrapper create_basic_type(const std::string &name,
                                        uint64_t size_in_bits,
                                        unsigned encoding, unsigned flags);

  LLVMMetadataWrapper create_pointer_type(const LLVMMetadataWrapper &pointee,
                                          uint64_t size_in_bits,
                                          uint32_t align_in_bits,
                                          unsigned address_space,
                                          const std::string &name);

  LLVMMetadataWrapper
  create_vector_type(uint64_t size_in_bits, uint32_t align_in_bits,
                     const LLVMMetadataWrapper &element_type,
                     const std::vector<LLVMMetadataWrapper> &subscripts);

  LLVMMetadataWrapper
  create_typedef(const LLVMMetadataWrapper &type, const std::string &name,
                 const LLVMMetadataWrapper &file, unsigned line_no,
                 const LLVMMetadataWrapper &scope, uint32_t align_in_bits);

  LLVMMetadataWrapper create_struct_type(
      const LLVMMetadataWrapper &scope, const std::string &name,
      const LLVMMetadataWrapper &file, unsigned line_number,
      uint64_t size_in_bits, uint32_t align_in_bits, unsigned flags,
      const LLVMMetadataWrapper *derived_from,
      const std::vector<LLVMMetadataWrapper> &elements, unsigned runtime_lang,
      const LLVMMetadataWrapper *vtable_holder, const std::string &unique_id);

  LLVMMetadataWrapper
  create_enumeration_type(const LLVMMetadataWrapper &scope,
                          const std::string &name,
                          const LLVMMetadataWrapper &file, unsigned line_number,
                          uint64_t size_in_bits, uint32_t align_in_bits,
                          const std::vector<LLVMMetadataWrapper> &elements,
                          const LLVMMetadataWrapper &underlying_type);

  LLVMMetadataWrapper create_forward_decl(unsigned tag, const std::string &name,
                                          const LLVMMetadataWrapper &scope,
                                          const LLVMMetadataWrapper &file,
                                          unsigned line, unsigned runtime_lang,
                                          uint64_t size_in_bits,
                                          uint32_t align_in_bits,
                                          const std::string &unique_id);

  LLVMMetadataWrapper create_replaceable_composite_type(
      unsigned tag, const std::string &name, const LLVMMetadataWrapper &scope,
      const LLVMMetadataWrapper &file, unsigned line, unsigned runtime_lang,
      uint64_t size_in_bits, uint32_t align_in_bits, unsigned flags,
      const std::string &unique_id);

  LLVMMetadataWrapper create_subrange_type(
      const LLVMMetadataWrapper &scope, const std::string &name, unsigned line,
      const LLVMMetadataWrapper &file, uint64_t size_in_bits,
      uint32_t align_in_bits, unsigned flags,
      const LLVMMetadataWrapper &element_type,
      const LLVMMetadataWrapper *lower_bound,
      const LLVMMetadataWrapper *upper_bound, const LLVMMetadataWrapper *stride,
      const LLVMMetadataWrapper *bias);

  LLVMMetadataWrapper create_set_type(const LLVMMetadataWrapper &scope,
                                      const std::string &name,
                                      const LLVMMetadataWrapper &file,
                                      unsigned line, uint64_t size_in_bits,
                                      uint32_t align_in_bits,
                                      const LLVMMetadataWrapper &base_type);

  LLVMMetadataWrapper create_dynamic_array_type(
      const LLVMMetadataWrapper &scope, const std::string &name, unsigned line,
      const LLVMMetadataWrapper &file, uint64_t size_in_bits,
      uint32_t align_in_bits, const LLVMMetadataWrapper &element_type,
      const std::vector<LLVMMetadataWrapper> &subscripts,
      const LLVMMetadataWrapper &data_location,
      const LLVMMetadataWrapper *associated,
      const LLVMMetadataWrapper *allocated, const LLVMMetadataWrapper *rank,
      const LLVMMetadataWrapper *bit_stride);

  // =========================================================================
  // Variable & Expression Methods
  // =========================================================================

  LLVMMetadataWrapper create_parameter_variable(
      const LLVMMetadataWrapper &scope, const std::string &name,
      unsigned arg_no, const LLVMMetadataWrapper &file, unsigned line_no,
      const LLVMMetadataWrapper &type, bool always_preserve, unsigned flags);

  LLVMMetadataWrapper create_auto_variable(const LLVMMetadataWrapper &scope,
                                           const std::string &name,
                                           const LLVMMetadataWrapper &file,
                                           unsigned line_no,
                                           const LLVMMetadataWrapper &type,
                                           bool always_preserve, unsigned flags,
                                           uint32_t align_in_bits);

  LLVMMetadataWrapper create_global_variable_expression(
      const LLVMMetadataWrapper &scope, const std::string &name,
      const std::string &linkage, const LLVMMetadataWrapper &file,
      unsigned line_no, const LLVMMetadataWrapper &type, bool is_local_to_unit,
      const LLVMMetadataWrapper &expr, const LLVMMetadataWrapper *decl,
      uint32_t align_in_bits);

  LLVMMetadataWrapper create_expression(const std::vector<uint64_t> &addr);

  LLVMMetadataWrapper create_constant_value_expression(uint64_t value);

  // =========================================================================
  // Label Methods
  // =========================================================================

  LLVMMetadataWrapper create_label(const LLVMMetadataWrapper &scope,
                                   const std::string &name,
                                   const LLVMMetadataWrapper &file,
                                   unsigned line_no, bool always_preserve);

  void insert_label_at_end(const LLVMMetadataWrapper &label_info,
                           const LLVMMetadataWrapper &debug_loc,
                           LLVMBasicBlockWrapper &block);

  void insert_label_before(const LLVMMetadataWrapper &label_info,
                           const LLVMMetadataWrapper &debug_loc,
                           LLVMValueWrapper &insert_before);

  // =========================================================================
  // Debug Record Insertion Methods
  // =========================================================================

  void insert_declare_record_at_end(LLVMValueWrapper &storage,
                                    const LLVMMetadataWrapper &var_info,
                                    const LLVMMetadataWrapper &expr,
                                    const LLVMMetadataWrapper &debug_loc,
                                    LLVMBasicBlockWrapper &block);

  void insert_dbg_value_record_at_end(LLVMValueWrapper &val,
                                      const LLVMMetadataWrapper &var_info,
                                      const LLVMMetadataWrapper &expr,
                                      const LLVMMetadataWrapper &debug_loc,
                                      LLVMBasicBlockWrapper &block);

  // =========================================================================
  // Array & Subrange Methods
  // =========================================================================

  LLVMMetadataWrapper get_or_create_subrange(int64_t lo, int64_t count);

  LLVMMetadataWrapper
  get_or_create_array(const std::vector<LLVMMetadataWrapper> &elements);

  // =========================================================================
  // Enumerator Methods
  // =========================================================================

  LLVMMetadataWrapper create_enumerator(const std::string &name, int64_t value,
                                        bool is_unsigned);

  LLVMMetadataWrapper
  create_enumerator_of_arbitrary_precision(const std::string &name,
                                           const std::vector<uint64_t> &value,
                                           bool is_unsigned);

  // =========================================================================
  // ObjC & Inheritance Methods
  // =========================================================================

  LLVMMetadataWrapper create_objc_property(const std::string &name,
                                           const LLVMMetadataWrapper &file,
                                           unsigned line_no,
                                           const std::string &getter_name,
                                           const std::string &setter_name,
                                           unsigned property_attributes,
                                           const LLVMMetadataWrapper &type);

  LLVMMetadataWrapper create_objc_ivar(const std::string &name,
                                       const LLVMMetadataWrapper &file,
                                       unsigned line_no, uint64_t size_in_bits,
                                       uint32_t align_in_bits,
                                       uint64_t offset_in_bits, unsigned flags,
                                       const LLVMMetadataWrapper &type,
                                       const LLVMMetadataWrapper &property);

  LLVMMetadataWrapper
  create_inheritance(const LLVMMetadataWrapper &derived_type,
                     const LLVMMetadataWrapper &base_type,
                     uint64_t offset_in_bits, uint32_t v_bptr_offset,
                     unsigned flags);

  // =========================================================================
  // Import & Macro Methods
  // =========================================================================

  LLVMMetadataWrapper create_imported_module_from_module(
      const LLVMMetadataWrapper &scope,
      const LLVMMetadataWrapper &import_module, const LLVMMetadataWrapper &file,
      unsigned line, const std::vector<LLVMMetadataWrapper> &elements);

  LLVMMetadataWrapper create_imported_module_from_alias(
      const LLVMMetadataWrapper &scope,
      const LLVMMetadataWrapper &imported_entity,
      const LLVMMetadataWrapper &file, unsigned line,
      const std::vector<LLVMMetadataWrapper> &elements);

  LLVMMetadataWrapper
  create_temp_macro_file(const LLVMMetadataWrapper *parent_macro_file,
                         unsigned line, const LLVMMetadataWrapper &file);

  LLVMMetadataWrapper create_macro(const LLVMMetadataWrapper &parent_macro_file,
                                   unsigned line, unsigned macro_type,
                                   const std::string &name,
                                   const std::string &value);

  // =========================================================================
  // Missing DIBuilder Methods (from debuginfo.md TODO)
  // =========================================================================

  void finalize_subprogram(const LLVMMetadataWrapper &subprogram);

  LLVMMetadataWrapper
  create_member_type(const LLVMMetadataWrapper &scope, const std::string &name,
                     const LLVMMetadataWrapper &file, unsigned line_no,
                     uint64_t size_in_bits, uint32_t align_in_bits,
                     uint64_t offset_in_bits, unsigned flags,
                     const LLVMMetadataWrapper &type);

  LLVMMetadataWrapper
  create_union_type(const LLVMMetadataWrapper &scope, const std::string &name,
                    const LLVMMetadataWrapper &file, unsigned line_number,
                    uint64_t size_in_bits, uint32_t align_in_bits,
                    unsigned flags,
                    const std::vector<LLVMMetadataWrapper> &elements,
                    unsigned runtime_lang, const std::string &unique_id);

  LLVMMetadataWrapper
  create_array_type(uint64_t size_in_bits, uint32_t align_in_bits,
                    const LLVMMetadataWrapper &element_type,
                    const std::vector<LLVMMetadataWrapper> &subscripts);

  LLVMMetadataWrapper create_qualified_type(unsigned tag,
                                            const LLVMMetadataWrapper &type);

  LLVMMetadataWrapper create_reference_type(unsigned tag,
                                            const LLVMMetadataWrapper &type);

  LLVMMetadataWrapper create_null_ptr_type();

  LLVMMetadataWrapper create_bit_field_member_type(
      const LLVMMetadataWrapper &scope, const std::string &name,
      const LLVMMetadataWrapper &file, unsigned line_no, uint64_t size_in_bits,
      uint64_t offset_in_bits, uint64_t storage_offset_in_bits, unsigned flags,
      const LLVMMetadataWrapper &type);

  LLVMMetadataWrapper create_artificial_type(const LLVMMetadataWrapper &type);

  LLVMMetadataWrapper
  get_or_create_type_array(const std::vector<LLVMMetadataWrapper> &types);

  LLVMMetadataWrapper
  create_lexical_block_file(const LLVMMetadataWrapper &scope,
                            const LLVMMetadataWrapper &file,
                            unsigned discriminator);

  LLVMMetadataWrapper create_imported_declaration(
      const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper &decl,
      const LLVMMetadataWrapper &file, unsigned line, const std::string &name,
      const std::vector<LLVMMetadataWrapper> &elements);

  LLVMMetadataWrapper create_imported_module_from_namespace(
      const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper &ns,
      const LLVMMetadataWrapper &file, unsigned line);

  // C++ class type
  LLVMMetadataWrapper create_class_type(
      const LLVMMetadataWrapper &scope, const std::string &name,
      const LLVMMetadataWrapper &file, unsigned line_number,
      uint64_t size_in_bits, uint32_t align_in_bits, uint64_t offset_in_bits,
      unsigned flags, const LLVMMetadataWrapper *derived_from,
      const std::vector<LLVMMetadataWrapper> &elements,
      const LLVMMetadataWrapper *vtable_holder,
      const LLVMMetadataWrapper *template_params, const std::string &unique_id);

  // Static member type (C++ static class member)
  LLVMMetadataWrapper create_static_member_type(
      const LLVMMetadataWrapper &scope, const std::string &name,
      const LLVMMetadataWrapper &file, unsigned line_no,
      const LLVMMetadataWrapper &type, unsigned flags,
      const LLVMValueWrapper *const_val, uint32_t align_in_bits);

  // Member pointer type (C++ pointer-to-member)
  LLVMMetadataWrapper
  create_member_pointer_type(const LLVMMetadataWrapper &pointee_type,
                             const LLVMMetadataWrapper &class_type,
                             uint64_t size_in_bits, uint32_t align_in_bits,
                             unsigned flags);

  // Insert declare record before an instruction
  void insert_declare_record_before(LLVMValueWrapper &storage,
                                    const LLVMMetadataWrapper &var_info,
                                    const LLVMMetadataWrapper &expr,
                                    const LLVMMetadataWrapper &debug_loc,
                                    LLVMValueWrapper &insert_before);

  // Insert dbg value record before an instruction
  void insert_dbg_value_record_before(LLVMValueWrapper &val,
                                      const LLVMMetadataWrapper &var_info,
                                      const LLVMMetadataWrapper &expr,
                                      const LLVMMetadataWrapper &debug_loc,
                                      LLVMValueWrapper &insert_before);

  // =========================================================================
  // Utility Methods
  // =========================================================================

  void replace_arrays(std::vector<LLVMMetadataWrapper> &composite_types,
                      std::vector<LLVMMetadataWrapper> &arrays);
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
      throw LLVMMemoryError("Metadata is null");
    if (!m_context_token || !m_context_token->is_valid())
      throw LLVMMemoryError("Metadata used after context was destroyed");
  }

  // Convert metadata to value (requires context)
  LLVMValueWrapper as_value(LLVMContextWrapper &ctx) const {
    check_valid();
    ctx.check_valid();
    return LLVMValueWrapper(LLVMMetadataAsValue(ctx.m_ref, m_ref), ctx.m_token);
  }

  // Replace all uses of this temporary metadata with another
  void replace_all_uses_with(const LLVMMetadataWrapper &md) {
    check_valid();
    md.check_valid();
    LLVMMetadataReplaceAllUsesWith(m_ref, md.m_ref);
  }
};

// Implementation of get_metadata for ValueMetadataEntriesWrapper
inline LLVMMetadataWrapper LLVMValueMetadataEntriesWrapper_get_metadata(
    const LLVMValueMetadataEntriesWrapper &entries, unsigned index) {
  entries.check_valid();
  if (index >= entries.m_count)
    throw std::out_of_range("Index out of range");
  return LLVMMetadataWrapper(
      LLVMValueMetadataEntriesGetMetadata(entries.m_entries, index),
      entries.m_context_token);
}

// Implementation of LLVMValueWrapper::as_metadata() - needs LLVMMetadataWrapper
inline LLVMMetadataWrapper LLVMValueWrapper::as_metadata() const {
  check_valid();
  return LLVMMetadataWrapper(LLVMValueAsMetadata(m_ref), m_context_token);
}

// Implementation of replace_md_node_operand_with - needs LLVMMetadataWrapper
inline void
replace_md_node_operand_with(LLVMValueWrapper &val, unsigned index,
                             const LLVMMetadataWrapper &replacement) {
  val.check_valid();
  replacement.check_valid();
  LLVMReplaceMDNodeOperandWith(val.m_ref, index, replacement.m_ref);
}

// Implementation of LLVMValueWrapper::set_metadata() - unified for instructions
// and globals. Takes a context wrapper to convert metadata to value when
// needed.
inline void LLVMValueWrapper::set_metadata(unsigned kind,
                                           const LLVMMetadataWrapper &md,
                                           LLVMContextWrapper &ctx) {
  check_valid();
  md.check_valid();
  ctx.check_valid();
  // Check if this is a global value or an instruction
  if (LLVMIsAGlobalValue(m_ref)) {
    LLVMGlobalSetMetadata(m_ref, kind, md.m_ref);
  } else {
    // For instructions, convert metadata to value using provided context
    LLVMValueRef md_val = LLVMMetadataAsValue(ctx.m_ref, md.m_ref);
    LLVMSetMetadata(m_ref, kind, md_val);
  }
}

// Implementation of LLVMContextWrapper::md_string() - needs LLVMMetadataWrapper
inline LLVMMetadataWrapper
LLVMContextWrapper::md_string(const std::string &str) {
  check_valid();
  return LLVMMetadataWrapper(
      LLVMMDStringInContext2(m_ref, str.c_str(), str.size()), m_token);
}

// Implementation of LLVMContextWrapper::md_node() - needs LLVMMetadataWrapper
inline LLVMMetadataWrapper
LLVMContextWrapper::md_node(const std::vector<LLVMMetadataWrapper> &mds) {
  check_valid();
  std::vector<LLVMMetadataRef> refs;
  refs.reserve(mds.size());
  for (const auto &md : mds) {
    md.check_valid();
    refs.push_back(md.m_ref);
  }
  return LLVMMetadataWrapper(
      LLVMMDNodeInContext2(m_ref, refs.data(), refs.size()), m_token);
}

// Implementation of LLVMContextWrapper::create_type_attribute() - needs
// LLVMTypeWrapper
inline LLVMAttributeWrapper
LLVMContextWrapper::create_type_attribute(unsigned kind_id,
                                          const LLVMTypeWrapper &type_ref) {
  check_valid();
  type_ref.check_valid();
  LLVMAttributeRef ref =
      LLVMCreateTypeAttribute(m_ref, kind_id, type_ref.m_ref);
  return LLVMAttributeWrapper(ref, m_token);
}

// Implementation of LLVMModuleWrapper::add_named_metadata_operand() - needs
// LLVMMetadataWrapper
inline void
LLVMModuleWrapper::add_named_metadata_operand(const std::string &name,
                                              const LLVMMetadataWrapper &md) {
  check_valid();
  md.check_valid();
  // Need to convert metadata to value first for LLVMAddNamedMetadataOperand
  LLVMValueRef val = LLVMMetadataAsValue(m_ctx_ref, md.m_ref);
  LLVMAddNamedMetadataOperand(m_ref, name.c_str(), val);
}

// Implementation of LLVMModuleWrapper::add_module_flag() - needs
// LLVMMetadataWrapper
inline void LLVMModuleWrapper::add_module_flag(LLVMModuleFlagBehavior behavior,
                                               const std::string &key,
                                               const LLVMMetadataWrapper &val) {
  check_valid();
  val.check_valid();
  LLVMAddModuleFlag(m_ref, behavior, key.c_str(), key.size(), val.m_ref);
}

// Implementation of LLVMModuleWrapper::get_module_flag() - needs
// LLVMMetadataWrapper
inline std::optional<LLVMMetadataWrapper>
LLVMModuleWrapper::get_module_flag(const std::string &key) {
  check_valid();
  LLVMMetadataRef md = LLVMGetModuleFlag(m_ref, key.c_str(), key.size());
  if (!md)
    return std::nullopt;
  return LLVMMetadataWrapper(md, m_context_token);
}

// Implementation of LLVMFunctionWrapper::set_subprogram() - needs
// LLVMMetadataWrapper
inline void LLVMFunctionWrapper::set_subprogram(const LLVMMetadataWrapper &sp) {
  check_valid();
  sp.check_valid();
  LLVMSetSubprogram(m_ref, sp.m_ref);
}

// =============================================================================
// DIBuilder Method Implementations
// =============================================================================

// File & Scope Creation
inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_file(const std::string &filename,
                                  const std::string &directory) {
  check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateFile(m_ref, filename.c_str(), filename.size(),
                              directory.c_str(), directory.size()),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_compile_unit(
    int lang, const LLVMMetadataWrapper &file, const std::string &producer,
    bool is_optimized, const std::string &flags, unsigned runtime_ver,
    const std::string &split_name, unsigned kind, unsigned dwo_id,
    bool split_debug_inlining, bool debug_info_for_profiling,
    const std::string &sys_root, const std::string &sdk) {
  check_valid();
  file.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateCompileUnit(
          m_ref, (LLVMDWARFSourceLanguage)lang, file.m_ref, producer.c_str(),
          producer.size(), is_optimized, flags.c_str(), flags.size(),
          runtime_ver, split_name.c_str(), split_name.size(),
          (LLVMDWARFEmissionKind)kind, dwo_id, split_debug_inlining,
          debug_info_for_profiling, sys_root.c_str(), sys_root.size(),
          sdk.c_str(), sdk.size()),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_module(
    const LLVMMetadataWrapper &parent_scope, const std::string &name,
    const std::string &config_macros, const std::string &include_path,
    const std::string &api_notes_file) {
  check_valid();
  parent_scope.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateModule(
          m_ref, parent_scope.m_ref, name.c_str(), name.size(),
          config_macros.c_str(), config_macros.size(), include_path.c_str(),
          include_path.size(), api_notes_file.c_str(), api_notes_file.size()),
      m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_namespace(const LLVMMetadataWrapper &parent_scope,
                                       const std::string &name,
                                       bool export_symbols) {
  check_valid();
  parent_scope.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateNameSpace(m_ref, parent_scope.m_ref, name.c_str(),
                                   name.size(), export_symbols),
      m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_lexical_block(const LLVMMetadataWrapper &scope,
                                           const LLVMMetadataWrapper &file,
                                           unsigned line, unsigned column) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  return LLVMMetadataWrapper(LLVMDIBuilderCreateLexicalBlock(
                                 m_ref, scope.m_ref, file.m_ref, line, column),
                             m_module_token);
}

// Function & Subroutine Creation
inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_function(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const std::string &linkage_name, const LLVMMetadataWrapper &file,
    unsigned line_no, const LLVMMetadataWrapper *subroutine_type,
    bool is_local_to_unit, bool is_definition, unsigned scope_line,
    unsigned flags, bool is_optimized) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  LLVMMetadataRef type = subroutine_type ? subroutine_type->m_ref : nullptr;
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateFunction(
          m_ref, scope.m_ref, name.c_str(), name.size(), linkage_name.c_str(),
          linkage_name.size(), file.m_ref, line_no, type, is_local_to_unit,
          is_definition, scope_line, (LLVMDIFlags)flags, is_optimized),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_subroutine_type(
    const LLVMMetadataWrapper &file,
    const std::vector<LLVMMetadataWrapper> &param_types, unsigned flags) {
  check_valid();
  file.check_valid();
  std::vector<LLVMMetadataRef> param_refs;
  param_refs.reserve(param_types.size());
  for (const auto &p : param_types) {
    p.check_valid();
    param_refs.push_back(p.m_ref);
  }
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateSubroutineType(m_ref, file.m_ref, param_refs.data(),
                                        param_refs.size(), (LLVMDIFlags)flags),
      m_module_token);
}

// Type Creation
inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_basic_type(const std::string &name,
                                        uint64_t size_in_bits,
                                        unsigned encoding, unsigned flags) {
  check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateBasicType(m_ref, name.c_str(), name.size(),
                                   size_in_bits, encoding, (LLVMDIFlags)flags),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_pointer_type(
    const LLVMMetadataWrapper &pointee, uint64_t size_in_bits,
    uint32_t align_in_bits, unsigned address_space, const std::string &name) {
  check_valid();
  pointee.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreatePointerType(m_ref, pointee.m_ref, size_in_bits,
                                     align_in_bits, address_space, name.c_str(),
                                     name.size()),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_vector_type(
    uint64_t size_in_bits, uint32_t align_in_bits,
    const LLVMMetadataWrapper &element_type,
    const std::vector<LLVMMetadataWrapper> &subscripts) {
  check_valid();
  element_type.check_valid();
  std::vector<LLVMMetadataRef> sub_refs;
  sub_refs.reserve(subscripts.size());
  for (const auto &s : subscripts) {
    s.check_valid();
    sub_refs.push_back(s.m_ref);
  }
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateVectorType(m_ref, size_in_bits, align_in_bits,
                                    element_type.m_ref, sub_refs.data(),
                                    sub_refs.size()),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_typedef(
    const LLVMMetadataWrapper &type, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_no,
    const LLVMMetadataWrapper &scope, uint32_t align_in_bits) {
  check_valid();
  type.check_valid();
  file.check_valid();
  scope.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateTypedef(m_ref, type.m_ref, name.c_str(), name.size(),
                                 file.m_ref, line_no, scope.m_ref,
                                 align_in_bits),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_struct_type(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_number,
    uint64_t size_in_bits, uint32_t align_in_bits, unsigned flags,
    const LLVMMetadataWrapper *derived_from,
    const std::vector<LLVMMetadataWrapper> &elements, unsigned runtime_lang,
    const LLVMMetadataWrapper *vtable_holder, const std::string &unique_id) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  std::vector<LLVMMetadataRef> elem_refs;
  elem_refs.reserve(elements.size());
  for (const auto &e : elements) {
    e.check_valid();
    elem_refs.push_back(e.m_ref);
  }
  LLVMMetadataRef derived = derived_from ? derived_from->m_ref : nullptr;
  LLVMMetadataRef vtable = vtable_holder ? vtable_holder->m_ref : nullptr;
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateStructType(
          m_ref, scope.m_ref, name.c_str(), name.size(), file.m_ref,
          line_number, size_in_bits, align_in_bits, (LLVMDIFlags)flags, derived,
          elem_refs.empty() ? nullptr : elem_refs.data(),
          static_cast<unsigned>(elem_refs.size()), runtime_lang, vtable,
          unique_id.c_str(), unique_id.size()),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_enumeration_type(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_number,
    uint64_t size_in_bits, uint32_t align_in_bits,
    const std::vector<LLVMMetadataWrapper> &elements,
    const LLVMMetadataWrapper &underlying_type) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  underlying_type.check_valid();
  std::vector<LLVMMetadataRef> elem_refs;
  elem_refs.reserve(elements.size());
  for (const auto &e : elements) {
    e.check_valid();
    elem_refs.push_back(e.m_ref);
  }
  return LLVMMetadataWrapper(LLVMDIBuilderCreateEnumerationType(
                                 m_ref, scope.m_ref, name.c_str(), name.size(),
                                 file.m_ref, line_number, size_in_bits,
                                 align_in_bits, elem_refs.data(),
                                 elem_refs.size(), underlying_type.m_ref),
                             m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_forward_decl(
    unsigned tag, const std::string &name, const LLVMMetadataWrapper &scope,
    const LLVMMetadataWrapper &file, unsigned line, unsigned runtime_lang,
    uint64_t size_in_bits, uint32_t align_in_bits,
    const std::string &unique_id) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateForwardDecl(m_ref, tag, name.c_str(), name.size(),
                                     scope.m_ref, file.m_ref, line,
                                     runtime_lang, size_in_bits, align_in_bits,
                                     unique_id.c_str(), unique_id.size()),
      m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_replaceable_composite_type(
    unsigned tag, const std::string &name, const LLVMMetadataWrapper &scope,
    const LLVMMetadataWrapper &file, unsigned line, unsigned runtime_lang,
    uint64_t size_in_bits, uint32_t align_in_bits, unsigned flags,
    const std::string &unique_id) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateReplaceableCompositeType(
          m_ref, tag, name.c_str(), name.size(), scope.m_ref, file.m_ref, line,
          runtime_lang, size_in_bits, align_in_bits, (LLVMDIFlags)flags,
          unique_id.c_str(), unique_id.size()),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_subrange_type(
    const LLVMMetadataWrapper &scope, const std::string &name, unsigned line,
    const LLVMMetadataWrapper &file, uint64_t size_in_bits,
    uint32_t align_in_bits, unsigned flags,
    const LLVMMetadataWrapper &element_type,
    const LLVMMetadataWrapper *lower_bound,
    const LLVMMetadataWrapper *upper_bound, const LLVMMetadataWrapper *stride,
    const LLVMMetadataWrapper *bias) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  element_type.check_valid();
  LLVMMetadataRef lb = lower_bound ? lower_bound->m_ref : nullptr;
  LLVMMetadataRef ub = upper_bound ? upper_bound->m_ref : nullptr;
  LLVMMetadataRef st = stride ? stride->m_ref : nullptr;
  LLVMMetadataRef bi = bias ? bias->m_ref : nullptr;
  return LLVMMetadataWrapper(LLVMDIBuilderCreateSubrangeType(
                                 m_ref, scope.m_ref, name.c_str(), name.size(),
                                 line, file.m_ref, size_in_bits, align_in_bits,
                                 (LLVMDIFlags)flags, element_type.m_ref, lb, ub,
                                 st, bi),
                             m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_set_type(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line, uint64_t size_in_bits,
    uint32_t align_in_bits, const LLVMMetadataWrapper &base_type) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  base_type.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateSetType(m_ref, scope.m_ref, name.c_str(), name.size(),
                                 file.m_ref, line, size_in_bits, align_in_bits,
                                 base_type.m_ref),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_dynamic_array_type(
    const LLVMMetadataWrapper &scope, const std::string &name, unsigned line,
    const LLVMMetadataWrapper &file, uint64_t size_in_bits,
    uint32_t align_in_bits, const LLVMMetadataWrapper &element_type,
    const std::vector<LLVMMetadataWrapper> &subscripts,
    const LLVMMetadataWrapper &data_location,
    const LLVMMetadataWrapper *associated, const LLVMMetadataWrapper *allocated,
    const LLVMMetadataWrapper *rank, const LLVMMetadataWrapper *bit_stride) {
  check_valid();
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
  LLVMMetadataRef stride_ref = bit_stride ? bit_stride->m_ref : nullptr;
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateDynamicArrayType(
          m_ref, scope.m_ref, name.c_str(), name.size(), line, file.m_ref,
          size_in_bits, align_in_bits, element_type.m_ref, sub_refs.data(),
          sub_refs.size(), data_location.m_ref, assoc, alloc, rnk, stride_ref),
      m_module_token);
}

// Variable & Expression
inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_parameter_variable(
    const LLVMMetadataWrapper &scope, const std::string &name, unsigned arg_no,
    const LLVMMetadataWrapper &file, unsigned line_no,
    const LLVMMetadataWrapper &type, bool always_preserve, unsigned flags) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  type.check_valid();
  return LLVMMetadataWrapper(LLVMDIBuilderCreateParameterVariable(
                                 m_ref, scope.m_ref, name.c_str(), name.size(),
                                 arg_no, file.m_ref, line_no, type.m_ref,
                                 always_preserve, (LLVMDIFlags)flags),
                             m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_auto_variable(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_no,
    const LLVMMetadataWrapper &type, bool always_preserve, unsigned flags,
    uint32_t align_in_bits) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  type.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateAutoVariable(
          m_ref, scope.m_ref, name.c_str(), name.size(), file.m_ref, line_no,
          type.m_ref, always_preserve, (LLVMDIFlags)flags, align_in_bits),
      m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_global_variable_expression(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const std::string &linkage, const LLVMMetadataWrapper &file,
    unsigned line_no, const LLVMMetadataWrapper &type, bool is_local_to_unit,
    const LLVMMetadataWrapper &expr, const LLVMMetadataWrapper *decl,
    uint32_t align_in_bits) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  type.check_valid();
  expr.check_valid();
  LLVMMetadataRef decl_ref = decl ? decl->m_ref : nullptr;
  return LLVMMetadataWrapper(LLVMDIBuilderCreateGlobalVariableExpression(
                                 m_ref, scope.m_ref, name.c_str(), name.size(),
                                 linkage.c_str(), linkage.size(), file.m_ref,
                                 line_no, type.m_ref, is_local_to_unit,
                                 expr.m_ref, decl_ref, align_in_bits),
                             m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_expression(const std::vector<uint64_t> &addr) {
  check_valid();
  std::vector<uint64_t> addr_copy = addr;
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateExpression(m_ref, addr_copy.data(), addr_copy.size()),
      m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_constant_value_expression(uint64_t value) {
  check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateConstantValueExpression(m_ref, value), m_module_token);
}

// Label Methods
inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_label(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_no, bool always_preserve) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateLabel(m_ref, scope.m_ref, name.c_str(), name.size(),
                               file.m_ref, line_no, always_preserve),
      m_module_token);
}

inline void
LLVMDIBuilderWrapper::insert_label_at_end(const LLVMMetadataWrapper &label_info,
                                          const LLVMMetadataWrapper &debug_loc,
                                          LLVMBasicBlockWrapper &block) {
  check_valid();
  label_info.check_valid();
  debug_loc.check_valid();
  block.check_valid();
  LLVMDIBuilderInsertLabelAtEnd(m_ref, label_info.m_ref, debug_loc.m_ref,
                                block.m_ref);
}

inline void
LLVMDIBuilderWrapper::insert_label_before(const LLVMMetadataWrapper &label_info,
                                          const LLVMMetadataWrapper &debug_loc,
                                          LLVMValueWrapper &insert_before) {
  check_valid();
  label_info.check_valid();
  debug_loc.check_valid();
  insert_before.check_valid();
  LLVMDIBuilderInsertLabelBefore(m_ref, label_info.m_ref, debug_loc.m_ref,
                                 insert_before.m_ref);
}

// Debug Record Insertion
inline void LLVMDIBuilderWrapper::insert_declare_record_at_end(
    LLVMValueWrapper &storage, const LLVMMetadataWrapper &var_info,
    const LLVMMetadataWrapper &expr, const LLVMMetadataWrapper &debug_loc,
    LLVMBasicBlockWrapper &block) {
  check_valid();
  storage.check_valid();
  var_info.check_valid();
  expr.check_valid();
  debug_loc.check_valid();
  block.check_valid();
  LLVMDIBuilderInsertDeclareRecordAtEnd(m_ref, storage.m_ref, var_info.m_ref,
                                        expr.m_ref, debug_loc.m_ref,
                                        block.m_ref);
}

inline void LLVMDIBuilderWrapper::insert_dbg_value_record_at_end(
    LLVMValueWrapper &val, const LLVMMetadataWrapper &var_info,
    const LLVMMetadataWrapper &expr, const LLVMMetadataWrapper &debug_loc,
    LLVMBasicBlockWrapper &block) {
  check_valid();
  val.check_valid();
  var_info.check_valid();
  expr.check_valid();
  debug_loc.check_valid();
  block.check_valid();
  LLVMDIBuilderInsertDbgValueRecordAtEnd(m_ref, val.m_ref, var_info.m_ref,
                                         expr.m_ref, debug_loc.m_ref,
                                         block.m_ref);
}

// Array & Subrange
inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::get_or_create_subrange(int64_t lo, int64_t count) {
  check_valid();
  return LLVMMetadataWrapper(LLVMDIBuilderGetOrCreateSubrange(m_ref, lo, count),
                             m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::get_or_create_array(
    const std::vector<LLVMMetadataWrapper> &elements) {
  check_valid();
  std::vector<LLVMMetadataRef> elem_refs;
  elem_refs.reserve(elements.size());
  for (const auto &e : elements) {
    e.check_valid();
    elem_refs.push_back(e.m_ref);
  }
  return LLVMMetadataWrapper(
      LLVMDIBuilderGetOrCreateArray(m_ref, elem_refs.data(), elem_refs.size()),
      m_module_token);
}

// Enumerator Methods
inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_enumerator(const std::string &name, int64_t value,
                                        bool is_unsigned) {
  check_valid();
  return LLVMMetadataWrapper(LLVMDIBuilderCreateEnumerator(m_ref, name.c_str(),
                                                           name.size(), value,
                                                           is_unsigned),
                             m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_enumerator_of_arbitrary_precision(
    const std::string &name, const std::vector<uint64_t> &value,
    bool is_unsigned) {
  check_valid();
  return LLVMMetadataWrapper(LLVMDIBuilderCreateEnumeratorOfArbitraryPrecision(
                                 m_ref, name.c_str(), name.size(),
                                 value.size() * 64, value.data(), is_unsigned),
                             m_module_token);
}

// ObjC & Inheritance Methods
inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_objc_property(
    const std::string &name, const LLVMMetadataWrapper &file, unsigned line_no,
    const std::string &getter_name, const std::string &setter_name,
    unsigned property_attributes, const LLVMMetadataWrapper &type) {
  check_valid();
  file.check_valid();
  type.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateObjCProperty(
          m_ref, name.c_str(), name.size(), file.m_ref, line_no,
          getter_name.c_str(), getter_name.size(), setter_name.c_str(),
          setter_name.size(), property_attributes, type.m_ref),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_objc_ivar(
    const std::string &name, const LLVMMetadataWrapper &file, unsigned line_no,
    uint64_t size_in_bits, uint32_t align_in_bits, uint64_t offset_in_bits,
    unsigned flags, const LLVMMetadataWrapper &type,
    const LLVMMetadataWrapper &property) {
  check_valid();
  file.check_valid();
  type.check_valid();
  property.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateObjCIVar(m_ref, name.c_str(), name.size(), file.m_ref,
                                  line_no, size_in_bits, align_in_bits,
                                  offset_in_bits, (LLVMDIFlags)flags,
                                  type.m_ref, property.m_ref),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_inheritance(
    const LLVMMetadataWrapper &derived_type,
    const LLVMMetadataWrapper &base_type, uint64_t offset_in_bits,
    uint32_t v_bptr_offset, unsigned flags) {
  check_valid();
  derived_type.check_valid();
  base_type.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateInheritance(m_ref, derived_type.m_ref, base_type.m_ref,
                                     offset_in_bits, v_bptr_offset,
                                     (LLVMDIFlags)flags),
      m_module_token);
}

// Import & Macro Methods
inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_imported_module_from_module(
    const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper &import_module,
    const LLVMMetadataWrapper &file, unsigned line,
    const std::vector<LLVMMetadataWrapper> &elements) {
  check_valid();
  scope.check_valid();
  import_module.check_valid();
  file.check_valid();
  std::vector<LLVMMetadataRef> elem_refs;
  elem_refs.reserve(elements.size());
  for (const auto &e : elements) {
    e.check_valid();
    elem_refs.push_back(e.m_ref);
  }
  return LLVMMetadataWrapper(LLVMDIBuilderCreateImportedModuleFromModule(
                                 m_ref, scope.m_ref, import_module.m_ref,
                                 file.m_ref, line, elem_refs.data(),
                                 elem_refs.size()),
                             m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_imported_module_from_alias(
    const LLVMMetadataWrapper &scope,
    const LLVMMetadataWrapper &imported_entity, const LLVMMetadataWrapper &file,
    unsigned line, const std::vector<LLVMMetadataWrapper> &elements) {
  check_valid();
  scope.check_valid();
  imported_entity.check_valid();
  file.check_valid();
  std::vector<LLVMMetadataRef> elem_refs;
  elem_refs.reserve(elements.size());
  for (const auto &e : elements) {
    e.check_valid();
    elem_refs.push_back(e.m_ref);
  }
  return LLVMMetadataWrapper(LLVMDIBuilderCreateImportedModuleFromAlias(
                                 m_ref, scope.m_ref, imported_entity.m_ref,
                                 file.m_ref, line, elem_refs.data(),
                                 elem_refs.size()),
                             m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_temp_macro_file(
    const LLVMMetadataWrapper *parent_macro_file, unsigned line,
    const LLVMMetadataWrapper &file) {
  check_valid();
  file.check_valid();
  LLVMMetadataRef parent =
      parent_macro_file ? parent_macro_file->m_ref : nullptr;
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateTempMacroFile(m_ref, parent, line, file.m_ref),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_macro(
    const LLVMMetadataWrapper &parent_macro_file, unsigned line,
    unsigned macro_type, const std::string &name, const std::string &value) {
  check_valid();
  parent_macro_file.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateMacro(m_ref, parent_macro_file.m_ref, line,
                               (LLVMDWARFMacinfoRecordType)macro_type,
                               name.c_str(), name.size(), value.c_str(),
                               value.size()),
      m_module_token);
}

// Missing DIBuilder Method Implementations

inline void LLVMDIBuilderWrapper::finalize_subprogram(
    const LLVMMetadataWrapper &subprogram) {
  check_valid();
  subprogram.check_valid();
  LLVMDIBuilderFinalizeSubprogram(m_ref, subprogram.m_ref);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_member_type(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_no, uint64_t size_in_bits,
    uint32_t align_in_bits, uint64_t offset_in_bits, unsigned flags,
    const LLVMMetadataWrapper &type) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  type.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateMemberType(m_ref, scope.m_ref, name.c_str(),
                                    name.size(), file.m_ref, line_no,
                                    size_in_bits, align_in_bits, offset_in_bits,
                                    (LLVMDIFlags)flags, type.m_ref),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_union_type(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_number,
    uint64_t size_in_bits, uint32_t align_in_bits, unsigned flags,
    const std::vector<LLVMMetadataWrapper> &elements, unsigned runtime_lang,
    const std::string &unique_id) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  std::vector<LLVMMetadataRef> elem_refs;
  elem_refs.reserve(elements.size());
  for (const auto &e : elements) {
    e.check_valid();
    elem_refs.push_back(e.m_ref);
  }
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateUnionType(
          m_ref, scope.m_ref, name.c_str(), name.size(), file.m_ref,
          line_number, size_in_bits, align_in_bits, (LLVMDIFlags)flags,
          elem_refs.data(), elem_refs.size(), runtime_lang, unique_id.c_str(),
          unique_id.size()),
      m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_array_type(
    uint64_t size_in_bits, uint32_t align_in_bits,
    const LLVMMetadataWrapper &element_type,
    const std::vector<LLVMMetadataWrapper> &subscripts) {
  check_valid();
  element_type.check_valid();
  std::vector<LLVMMetadataRef> sub_refs;
  sub_refs.reserve(subscripts.size());
  for (const auto &s : subscripts) {
    s.check_valid();
    sub_refs.push_back(s.m_ref);
  }
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateArrayType(m_ref, size_in_bits, align_in_bits,
                                   element_type.m_ref, sub_refs.data(),
                                   sub_refs.size()),
      m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_qualified_type(unsigned tag,
                                            const LLVMMetadataWrapper &type) {
  check_valid();
  type.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateQualifiedType(m_ref, tag, type.m_ref), m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_reference_type(unsigned tag,
                                            const LLVMMetadataWrapper &type) {
  check_valid();
  type.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateReferenceType(m_ref, tag, type.m_ref), m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_null_ptr_type() {
  check_valid();
  return LLVMMetadataWrapper(LLVMDIBuilderCreateNullPtrType(m_ref),
                             m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_bit_field_member_type(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_no, uint64_t size_in_bits,
    uint64_t offset_in_bits, uint64_t storage_offset_in_bits, unsigned flags,
    const LLVMMetadataWrapper &type) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  type.check_valid();
  return LLVMMetadataWrapper(LLVMDIBuilderCreateBitFieldMemberType(
                                 m_ref, scope.m_ref, name.c_str(), name.size(),
                                 file.m_ref, line_no, size_in_bits,
                                 offset_in_bits, storage_offset_in_bits,
                                 (LLVMDIFlags)flags, type.m_ref),
                             m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_artificial_type(const LLVMMetadataWrapper &type) {
  check_valid();
  type.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateArtificialType(m_ref, type.m_ref), m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::get_or_create_type_array(
    const std::vector<LLVMMetadataWrapper> &types) {
  check_valid();
  std::vector<LLVMMetadataRef> type_refs;
  type_refs.reserve(types.size());
  for (const auto &t : types) {
    t.check_valid();
    type_refs.push_back(t.m_ref);
  }
  return LLVMMetadataWrapper(LLVMDIBuilderGetOrCreateTypeArray(
                                 m_ref, type_refs.data(), type_refs.size()),
                             m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_lexical_block_file(
    const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper &file,
    unsigned discriminator) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  return LLVMMetadataWrapper(LLVMDIBuilderCreateLexicalBlockFile(
                                 m_ref, scope.m_ref, file.m_ref, discriminator),
                             m_module_token);
}

inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_imported_declaration(
    const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper &decl,
    const LLVMMetadataWrapper &file, unsigned line, const std::string &name,
    const std::vector<LLVMMetadataWrapper> &elements) {
  check_valid();
  scope.check_valid();
  decl.check_valid();
  file.check_valid();
  std::vector<LLVMMetadataRef> elem_refs;
  elem_refs.reserve(elements.size());
  for (const auto &e : elements) {
    e.check_valid();
    elem_refs.push_back(e.m_ref);
  }
  return LLVMMetadataWrapper(LLVMDIBuilderCreateImportedDeclaration(
                                 m_ref, scope.m_ref, decl.m_ref, file.m_ref,
                                 line, name.c_str(), name.size(),
                                 elem_refs.data(), elem_refs.size()),
                             m_module_token);
}

inline LLVMMetadataWrapper
LLVMDIBuilderWrapper::create_imported_module_from_namespace(
    const LLVMMetadataWrapper &scope, const LLVMMetadataWrapper &ns,
    const LLVMMetadataWrapper &file, unsigned line) {
  check_valid();
  scope.check_valid();
  ns.check_valid();
  file.check_valid();
  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateImportedModuleFromNamespace(
          m_ref, scope.m_ref, ns.m_ref, file.m_ref, line),
      m_module_token);
}

// Utility Methods
inline void LLVMDIBuilderWrapper::replace_arrays(
    std::vector<LLVMMetadataWrapper> &composite_types,
    std::vector<LLVMMetadataWrapper> &arrays) {
  check_valid();
  if (composite_types.size() != 1 || arrays.size() != 1) {
    throw std::invalid_argument(
        "Currently only supports single composite type and array");
  }
  composite_types[0].check_valid();
  arrays[0].check_valid();
  LLVMMetadataRef ct_ref = composite_types[0].m_ref;
  LLVMMetadataRef ar_ref = arrays[0].m_ref;
  LLVMReplaceArrays(m_ref, &ct_ref, &ar_ref, 1);
}

// Create C++ class type debug info
inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_class_type(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_number,
    uint64_t size_in_bits, uint32_t align_in_bits, uint64_t offset_in_bits,
    unsigned flags, const LLVMMetadataWrapper *derived_from,
    const std::vector<LLVMMetadataWrapper> &elements,
    const LLVMMetadataWrapper *vtable_holder,
    const LLVMMetadataWrapper *template_params, const std::string &unique_id) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  if (derived_from)
    derived_from->check_valid();
  if (vtable_holder)
    vtable_holder->check_valid();
  if (template_params)
    template_params->check_valid();

  std::vector<LLVMMetadataRef> elem_refs;
  elem_refs.reserve(elements.size());
  for (const auto &e : elements) {
    e.check_valid();
    elem_refs.push_back(e.m_ref);
  }

  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateClassType(
          m_ref, scope.m_ref, name.c_str(), name.size(), file.m_ref,
          line_number, size_in_bits, align_in_bits, offset_in_bits,
          (LLVMDIFlags)flags, derived_from ? derived_from->m_ref : nullptr,
          elem_refs.data(), static_cast<unsigned>(elem_refs.size()),
          vtable_holder ? vtable_holder->m_ref : nullptr,
          template_params ? template_params->m_ref : nullptr, unique_id.c_str(),
          unique_id.size()),
      m_module_token);
}

// Create static member type debug info
inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_static_member_type(
    const LLVMMetadataWrapper &scope, const std::string &name,
    const LLVMMetadataWrapper &file, unsigned line_no,
    const LLVMMetadataWrapper &type, unsigned flags,
    const LLVMValueWrapper *const_val, uint32_t align_in_bits) {
  check_valid();
  scope.check_valid();
  file.check_valid();
  type.check_valid();
  if (const_val)
    const_val->check_valid();

  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateStaticMemberType(
          m_ref, scope.m_ref, name.c_str(), name.size(), file.m_ref, line_no,
          type.m_ref, (LLVMDIFlags)flags,
          const_val ? const_val->m_ref : nullptr, align_in_bits),
      m_module_token);
}

// Create member pointer type debug info (C++ pointer-to-member)
inline LLVMMetadataWrapper LLVMDIBuilderWrapper::create_member_pointer_type(
    const LLVMMetadataWrapper &pointee_type,
    const LLVMMetadataWrapper &class_type, uint64_t size_in_bits,
    uint32_t align_in_bits, unsigned flags) {
  check_valid();
  pointee_type.check_valid();
  class_type.check_valid();

  return LLVMMetadataWrapper(
      LLVMDIBuilderCreateMemberPointerType(m_ref, pointee_type.m_ref,
                                           class_type.m_ref, size_in_bits,
                                           align_in_bits, (LLVMDIFlags)flags),
      m_module_token);
}

// Insert declare record before an instruction
inline void LLVMDIBuilderWrapper::insert_declare_record_before(
    LLVMValueWrapper &storage, const LLVMMetadataWrapper &var_info,
    const LLVMMetadataWrapper &expr, const LLVMMetadataWrapper &debug_loc,
    LLVMValueWrapper &insert_before) {
  check_valid();
  storage.check_valid();
  var_info.check_valid();
  expr.check_valid();
  debug_loc.check_valid();
  insert_before.check_valid();

  LLVMDIBuilderInsertDeclareRecordBefore(m_ref, storage.m_ref, var_info.m_ref,
                                         expr.m_ref, debug_loc.m_ref,
                                         insert_before.m_ref);
}

// Insert dbg value record before an instruction
inline void LLVMDIBuilderWrapper::insert_dbg_value_record_before(
    LLVMValueWrapper &val, const LLVMMetadataWrapper &var_info,
    const LLVMMetadataWrapper &expr, const LLVMMetadataWrapper &debug_loc,
    LLVMValueWrapper &insert_before) {
  check_valid();
  val.check_valid();
  var_info.check_valid();
  expr.check_valid();
  debug_loc.check_valid();
  insert_before.check_valid();

  LLVMDIBuilderInsertDbgValueRecordBefore(m_ref, val.m_ref, var_info.m_ref,
                                          expr.m_ref, debug_loc.m_ref,
                                          insert_before.m_ref);
}

// =============================================================================
// DIBuilder Manager (context manager for `with` statement)
// =============================================================================

struct LLVMDIBuilderManager : NoMoveCopy {
  LLVMModuleWrapper *m_module = nullptr;
  std::shared_ptr<ValidityToken> m_module_token;
  std::unique_ptr<LLVMDIBuilderWrapper> m_dibuilder;
  bool m_entered = false;
  bool m_disposed = false;

  explicit LLVMDIBuilderManager(LLVMModuleWrapper *module)
      : m_module(module), m_module_token(module ? module->m_token : nullptr) {}

  LLVMDIBuilderWrapper &enter() {
    if (m_disposed)
      throw LLVMMemoryError("DIBuilder has been disposed");
    if (m_entered)
      throw LLVMMemoryError("DIBuilder manager already entered");
    if (!m_module)
      throw LLVMMemoryError("No module provided");
    // Check module token validity before dereferencing m_module
    if (!m_module_token || !m_module_token->is_valid())
      throw LLVMMemoryError("DIBuilder's module has been destroyed");
    m_module->check_valid();
    m_dibuilder = std::make_unique<LLVMDIBuilderWrapper>(m_module->m_ref,
                                                         m_module->m_token);
    m_entered = true;
    return *m_dibuilder;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (m_disposed)
      throw LLVMMemoryError("DIBuilder has already been disposed");
    if (!m_entered)
      throw LLVMMemoryError("DIBuilder manager was not entered");
    // Finalize is typically called explicitly, but we could auto-finalize here
    // For now, just clean up - user should call finalize() before exiting
    m_dibuilder.reset();
    m_disposed = true;
  }

  void dispose() {
    if (m_disposed)
      throw LLVMMemoryError("DIBuilder has already been disposed");
    if (m_entered)
      throw LLVMMemoryError(
          "Cannot call dispose() after __enter__; use __exit__ "
          "or 'with' statement");
    // For DIBuilder, there's nothing to dispose if not entered
    m_disposed = true;
  }
};

// =============================================================================
// Implementation of LLVMModuleWrapper methods that need LLVMContextWrapper or
// LLVMDIBuilderManager
// =============================================================================

inline LLVMContextWrapper *LLVMModuleWrapper::get_context() const {
  check_valid();
  // Return a non-owning wrapper for the module's context.
  return new LLVMContextWrapper(m_ctx_ref, m_context_token);
}

inline LLVMDIBuilderManager *LLVMModuleWrapper::create_dibuilder() {
  check_valid();
  // Using const_cast because we need a non-const pointer for the manager
  // but the method is logically const (creates a new object, doesn't modify
  // module)
  return new LLVMDIBuilderManager(const_cast<LLVMModuleWrapper *>(this));
}

// =============================================================================
// Implementation of LLVMTypeWrapper methods that need LLVMContextWrapper
// =============================================================================

inline LLVMContextWrapper *LLVMTypeWrapper::context() const {
  check_valid();
  LLVMContextRef ctx_ref = LLVMGetTypeContext(m_ref);
  // Return a non-owning (borrowed) wrapper for the type's context
  return new LLVMContextWrapper(ctx_ref, m_context_token);
}

// =============================================================================
// Implementation of LLVMBasicBlockWrapper methods that need
// LLVMModuleWrapper/LLVMContextWrapper
// =============================================================================

inline LLVMModuleWrapper *LLVMBasicBlockWrapper::module() const {
  check_valid();
  LLVMValueRef parent_fn = LLVMGetBasicBlockParent(m_ref);
  if (!parent_fn)
    throw LLVMAssertionError("BasicBlock has no parent function");
  LLVMModuleRef mod_ref = LLVMGetGlobalParent(parent_fn);
  if (!mod_ref)
    throw LLVMAssertionError("BasicBlock's function has no parent module");
  LLVMContextRef ctx_ref = LLVMGetModuleContext(mod_ref);
  return LLVMModuleWrapper::create_borrowed(mod_ref, ctx_ref, m_context_token);
}

inline LLVMContextWrapper *LLVMBasicBlockWrapper::context() const {
  check_valid();
  LLVMValueRef parent_fn = LLVMGetBasicBlockParent(m_ref);
  if (!parent_fn)
    throw LLVMAssertionError("BasicBlock has no parent function");
  LLVMModuleRef mod_ref = LLVMGetGlobalParent(parent_fn);
  if (!mod_ref)
    throw LLVMAssertionError("BasicBlock's function has no parent module");
  LLVMContextRef ctx_ref = LLVMGetModuleContext(mod_ref);
  return new LLVMContextWrapper(ctx_ref, m_context_token);
}

// =============================================================================
// Implementation of LLVMFunctionWrapper parent navigation methods
// =============================================================================

inline LLVMModuleWrapper *LLVMFunctionWrapper::module() const {
  check_valid();
  LLVMModuleRef mod_ref = LLVMGetGlobalParent(m_ref);
  if (!mod_ref)
    throw LLVMAssertionError("Function has no parent module");
  LLVMContextRef ctx_ref = LLVMGetModuleContext(mod_ref);
  // Return a non-owning (borrowed) wrapper for the function's module
  return LLVMModuleWrapper::create_borrowed(mod_ref, ctx_ref, m_context_token);
}

inline LLVMContextWrapper *LLVMFunctionWrapper::context() const {
  check_valid();
  LLVMModuleRef mod_ref = LLVMGetGlobalParent(m_ref);
  if (!mod_ref)
    throw LLVMAssertionError("Function has no parent module");
  LLVMContextRef ctx_ref = LLVMGetModuleContext(mod_ref);
  // Return a non-owning (borrowed) wrapper for the function's context
  return new LLVMContextWrapper(ctx_ref, m_context_token);
}

// =============================================================================
// Implementation of LLVMValueWrapper parent navigation methods
// =============================================================================

inline LLVMFunctionWrapper LLVMValueWrapper::get_function() const {
  check_valid();
  // For instructions: get function via block
  if (is_a_instruction()) {
    LLVMBasicBlockRef bb = LLVMGetInstructionParent(m_ref);
    if (!bb)
      throw LLVMAssertionError("Instruction has no parent basic block");
    LLVMValueRef fn = LLVMGetBasicBlockParent(bb);
    if (!fn)
      throw LLVMAssertionError("BasicBlock has no parent function");
    return LLVMFunctionWrapper(fn, m_context_token);
  }
  // For parameters: use LLVMGetParamParent
  if (is_a_argument()) {
    LLVMValueRef fn = LLVMGetParamParent(m_ref);
    if (!fn)
      throw LLVMAssertionError("Parameter has no parent function");
    return LLVMFunctionWrapper(fn, m_context_token);
  }
  // For globals: throw - they don't have a function parent
  throw LLVMAssertionError(
      "Cannot get function: value is not an instruction or parameter");
}

inline LLVMModuleWrapper *LLVMValueWrapper::get_module() const {
  check_valid();
  LLVMModuleRef mod_ref = nullptr;

  // For instructions: get module via block -> function
  if (is_a_instruction()) {
    LLVMBasicBlockRef bb = LLVMGetInstructionParent(m_ref);
    if (!bb)
      throw LLVMAssertionError("Instruction has no parent basic block");
    LLVMValueRef fn = LLVMGetBasicBlockParent(bb);
    if (!fn)
      throw LLVMAssertionError("BasicBlock has no parent function");
    mod_ref = LLVMGetGlobalParent(fn);
  }
  // For parameters: get module via function
  else if (is_a_argument()) {
    LLVMValueRef fn = LLVMGetParamParent(m_ref);
    if (!fn)
      throw LLVMAssertionError("Parameter has no parent function");
    mod_ref = LLVMGetGlobalParent(fn);
  }
  // For globals: use LLVMGetGlobalParent directly
  else if (is_a_global_value()) {
    mod_ref = LLVMGetGlobalParent(m_ref);
  } else {
    throw LLVMAssertionError(
        "Cannot get module: value is not an instruction, parameter, or global");
  }

  if (!mod_ref)
    throw LLVMAssertionError("Value has no parent module");
  LLVMContextRef ctx_ref = LLVMGetModuleContext(mod_ref);
  return LLVMModuleWrapper::create_borrowed(mod_ref, ctx_ref, m_context_token);
}

inline LLVMContextWrapper *LLVMValueWrapper::get_context() const {
  check_valid();
  LLVMModuleRef mod_ref = nullptr;

  // For instructions: get module via block -> function
  if (is_a_instruction()) {
    LLVMBasicBlockRef bb = LLVMGetInstructionParent(m_ref);
    if (!bb)
      throw LLVMAssertionError("Instruction has no parent basic block");
    LLVMValueRef fn = LLVMGetBasicBlockParent(bb);
    if (!fn)
      throw LLVMAssertionError("BasicBlock has no parent function");
    mod_ref = LLVMGetGlobalParent(fn);
  }
  // For parameters: get module via function
  else if (is_a_argument()) {
    LLVMValueRef fn = LLVMGetParamParent(m_ref);
    if (!fn)
      throw LLVMAssertionError("Parameter has no parent function");
    mod_ref = LLVMGetGlobalParent(fn);
  }
  // For globals: use LLVMGetGlobalParent directly
  else if (is_a_global_value()) {
    mod_ref = LLVMGetGlobalParent(m_ref);
  } else {
    throw LLVMAssertionError("Cannot get context: value is not an instruction, "
                             "parameter, or global");
  }

  if (!mod_ref)
    throw LLVMAssertionError("Value has no parent module");
  LLVMContextRef ctx_ref = LLVMGetModuleContext(mod_ref);
  return new LLVMContextWrapper(ctx_ref, m_context_token);
}

// =============================================================================
// Module Registration
// =============================================================================

NB_MODULE(llvm, m) {
  // Register exceptions
  auto exc_error = nb::exception<LLVMError>(m, "LLVMError");
  auto exc_memory =
      nb::exception<LLVMMemoryError>(m, "LLVMMemoryError", PyExc_SystemExit);
  auto exc_assertion = nb::exception<LLVMAssertionError>(
      m, "LLVMAssertionError", PyExc_AssertionError);
  auto exc_parse = nb::exception<LLVMParseError>(m, "LLVMParseError");

  // Set docstrings on exception classes
  exc_error.doc() =
      "Recoverable LLVM error.\n\n"
      "Raised for runtime errors that can be caught and handled, such as:\n"
      "- I/O errors when reading files\n"
      "- Bitcode/IR parsing failures\n"
      "- Binary creation errors\n\n"
      "These errors derive from Exception and can be caught normally.";

  exc_memory.doc() =
      "Memory/lifetime error - derives from SystemExit.\n\n"
      "Raised for memory safety violations and lifetime issues:\n"
      "- Accessing objects after context was destroyed\n"
      "- Using disposed modules or builders\n"
      "- Context manager state errors\n\n"
      "WARNING: Derives from SystemExit, NOT Exception.\n"
      "Cannot be caught with 'except Exception'. Use 'except SystemExit' "
      "or 'except LLVMMemoryError' explicitly.\n\n"
      "This design prevents accidental continuation after memory safety "
      "violations.";

  exc_assertion.doc() =
      "Programming error - derives from AssertionError.\n\n"
      "Raised for logic errors unrelated to object lifetimes:\n"
      "- Type mismatches: calling int_width on a float type\n"
      "- Invalid indices: parameter index out of range\n"
      "- Invalid operations: value is not inline assembly\n\n"
      "These indicate bugs in your code but are recoverable.";

  exc_parse.doc() = "LLVM IR/bitcode parsing error with diagnostics.\n\n"
                    "Raised when parsing LLVM IR or bitcode fails. Use "
                    "ctx.get_diagnostics()\n"
                    "to retrieve detailed diagnostic information after "
                    "catching this exception.\n\n"
                    "Example:\n"
                    "    try:\n"
                    "        mod = ctx.parse_ir('invalid')\n"
                    "    except LLVMParseError as e:\n"
                    "        print(f'Parse failed: {e}')\n"
                    "        for diag in ctx.get_diagnostics():\n"
                    "            print(f'{diag.severity}: {diag.message}')";

  // Diagnostic class
  nb::class_<Diagnostic>(m, "Diagnostic")
      .def_ro("severity", &Diagnostic::severity)
      .def_ro("message", &Diagnostic::message)
      .def_ro("line", &Diagnostic::line)
      .def_ro("column", &Diagnostic::column);

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
      .export_values()
      .def("__str__", [](LLVMLinkage v) {
        switch (v) {
        case LLVMExternalLinkage: return "external";
        case LLVMAvailableExternallyLinkage: return "available_externally";
        case LLVMLinkOnceAnyLinkage: return "linkonce";
        case LLVMLinkOnceODRLinkage: return "linkonce_odr";
        case LLVMWeakAnyLinkage: return "weak";
        case LLVMWeakODRLinkage: return "weak_odr";
        case LLVMAppendingLinkage: return "appending";
        case LLVMInternalLinkage: return "internal";
        case LLVMPrivateLinkage: return "private";
        case LLVMExternalWeakLinkage: return "extern_weak";
        case LLVMCommonLinkage: return "common";
        default: return "unknown";
        }
      });

  nb::enum_<LLVMModuleFlagBehavior>(
      m, "ModuleFlagBehavior",
      R"(Module flag merge behavior for LLVMAddModuleFlag.)")
      .value("Error", LLVMModuleFlagBehaviorError,
             "Emit an error if two values disagree, otherwise use the operand.")
      .value("Warning", LLVMModuleFlagBehaviorWarning,
             "Emit a warning if two values disagree.")
      .value("Require", LLVMModuleFlagBehaviorRequire,
             "Adds a requirement that another module flag be present and have "
             "a specified value.")
      .value("Override", LLVMModuleFlagBehaviorOverride,
             "Uses the specified value, regardless of the behavior or value of "
             "the other module.")
      .value("Append", LLVMModuleFlagBehaviorAppend,
             "Appends the two values, which are required to be metadata nodes.")
      .value(
          "AppendUnique", LLVMModuleFlagBehaviorAppendUnique,
          "Appends the two values, dropping duplicates from the second list.")
      .export_values();

  nb::enum_<LLVMVisibility>(m, "Visibility")
      .value("Default", LLVMDefaultVisibility)
      .value("Hidden", LLVMHiddenVisibility)
      .value("Protected", LLVMProtectedVisibility)
      .export_values()
      .def("__str__", [](LLVMVisibility v) {
        switch (v) {
        case LLVMDefaultVisibility: return "default";
        case LLVMHiddenVisibility: return "hidden";
        case LLVMProtectedVisibility: return "protected";
        default: return "unknown";
        }
      });

  nb::enum_<LLVMUnnamedAddr>(m, "UnnamedAddr")
      .value("No", LLVMNoUnnamedAddr)
      .value("Local", LLVMLocalUnnamedAddr)
      .value("Global", LLVMGlobalUnnamedAddr)
      .export_values();

  nb::enum_<LLVMDLLStorageClass>(
      m, "DLLStorageClass", "DLL storage class for Windows PE/COFF targets.")
      .value("Default", LLVMDefaultStorageClass)
      .value("DLLImport", LLVMDLLImportStorageClass,
             "Function/variable to be imported from DLL.")
      .value("DLLExport", LLVMDLLExportStorageClass,
             "Function/variable to be accessible from DLL.")
      .export_values();

  nb::enum_<LLVMComdatSelectionKind>(
      m, "ComdatSelectionKind",
      "Selection kind for COMDAT sections (Windows/COFF linking).")
      .value("Any", LLVMAnyComdatSelectionKind,
             "The linker may choose any COMDAT.")
      .value("ExactMatch", LLVMExactMatchComdatSelectionKind,
             "The data referenced by the COMDAT must be the same.")
      .value("Largest", LLVMLargestComdatSelectionKind,
             "The linker will choose the largest COMDAT.")
      .value("NoDeduplicate", LLVMNoDeduplicateComdatSelectionKind,
             "No deduplication is performed.")
      .value("SameSize", LLVMSameSizeComdatSelectionKind,
             "The data referenced by the COMDAT must be the same size.")
      .export_values();

  nb::enum_<LLVMCallConv>(m, "CallConv")
      .value("C", LLVMCCallConv)
      .value("Fast", LLVMFastCallConv)
      .value("Cold", LLVMColdCallConv)
      .value("X86Stdcall", LLVMX86StdcallCallConv)
      .value("X86Fastcall", LLVMX86FastcallCallConv)
      .value("GHC", LLVMGHCCallConv)
      .value("HiPE", LLVMHiPECallConv)
      .value("PreserveMost", LLVMPreserveMostCallConv)
      .value("PreserveAll", LLVMPreserveAllCallConv)
      .value("Swift", LLVMSwiftCallConv)
      .value("CXX_FAST_TLS", LLVMCXXFASTTLSCallConv)
      .value("X86ThisCall", LLVMX86ThisCallCallConv)
      .value("X86_64_SysV", LLVMX8664SysVCallConv)
      .value("Win64", LLVMWin64CallConv)
      .value("X86VectorCall", LLVMX86VectorCallCallConv)
      .value("X86RegCall", LLVMX86RegCallCallConv)
      .export_values()
      .def("__str__", [](LLVMCallConv v) {
        switch (v) {
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
      });

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
      .export_values()
      .def("__str__", [](LLVMIntPredicate v) {
        switch (v) {
        case LLVMIntEQ: return "eq";
        case LLVMIntNE: return "ne";
        case LLVMIntUGT: return "ugt";
        case LLVMIntUGE: return "uge";
        case LLVMIntULT: return "ult";
        case LLVMIntULE: return "ule";
        case LLVMIntSGT: return "sgt";
        case LLVMIntSGE: return "sge";
        case LLVMIntSLT: return "slt";
        case LLVMIntSLE: return "sle";
        default: return "unknown";
        }
      });

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
      .export_values()
      .def("__str__", [](LLVMRealPredicate v) {
        switch (v) {
        case LLVMRealPredicateFalse: return "false";
        case LLVMRealOEQ: return "oeq";
        case LLVMRealOGT: return "ogt";
        case LLVMRealOGE: return "oge";
        case LLVMRealOLT: return "olt";
        case LLVMRealOLE: return "ole";
        case LLVMRealONE: return "one";
        case LLVMRealORD: return "ord";
        case LLVMRealUNO: return "uno";
        case LLVMRealUEQ: return "ueq";
        case LLVMRealUGT: return "ugt";
        case LLVMRealUGE: return "uge";
        case LLVMRealULT: return "ult";
        case LLVMRealULE: return "ule";
        case LLVMRealUNE: return "une";
        case LLVMRealPredicateTrue: return "true";
        default: return "unknown";
        }
      });

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
      .export_values()
      .def("__str__", [](LLVMTypeKind v) {
        switch (v) {
        case LLVMVoidTypeKind: return "void";
        case LLVMHalfTypeKind: return "half";
        case LLVMBFloatTypeKind: return "bfloat";
        case LLVMFloatTypeKind: return "float";
        case LLVMDoubleTypeKind: return "double";
        case LLVMX86_FP80TypeKind: return "x86_fp80";
        case LLVMFP128TypeKind: return "fp128";
        case LLVMPPC_FP128TypeKind: return "ppc_fp128";
        case LLVMLabelTypeKind: return "label";
        case LLVMIntegerTypeKind: return "integer";
        case LLVMFunctionTypeKind: return "function";
        case LLVMStructTypeKind: return "struct";
        case LLVMArrayTypeKind: return "array";
        case LLVMPointerTypeKind: return "pointer";
        case LLVMVectorTypeKind: return "vector";
        case LLVMMetadataTypeKind: return "metadata";
        case LLVMX86_AMXTypeKind: return "x86_amx";
        case LLVMTokenTypeKind: return "token";
        case LLVMScalableVectorTypeKind: return "scalablevector";
        case LLVMTargetExtTypeKind: return "targetext";
        default: return "unknown";
        }
      });

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
      .export_values()
      .def("__str__", [](LLVMOpcode op) {
        switch (op) {
        case LLVMRet: return "ret";
        case LLVMBr: return "br";
        case LLVMSwitch: return "switch";
        case LLVMIndirectBr: return "indirectbr";
        case LLVMInvoke: return "invoke";
        case LLVMUnreachable: return "unreachable";
        case LLVMCallBr: return "callbr";
        case LLVMFNeg: return "fneg";
        case LLVMAdd: return "add";
        case LLVMFAdd: return "fadd";
        case LLVMSub: return "sub";
        case LLVMFSub: return "fsub";
        case LLVMMul: return "mul";
        case LLVMFMul: return "fmul";
        case LLVMUDiv: return "udiv";
        case LLVMSDiv: return "sdiv";
        case LLVMFDiv: return "fdiv";
        case LLVMURem: return "urem";
        case LLVMSRem: return "srem";
        case LLVMFRem: return "frem";
        case LLVMShl: return "shl";
        case LLVMLShr: return "lshr";
        case LLVMAShr: return "ashr";
        case LLVMAnd: return "and";
        case LLVMOr: return "or";
        case LLVMXor: return "xor";
        case LLVMAlloca: return "alloca";
        case LLVMLoad: return "load";
        case LLVMStore: return "store";
        case LLVMGetElementPtr: return "getelementptr";
        case LLVMTrunc: return "trunc";
        case LLVMZExt: return "zext";
        case LLVMSExt: return "sext";
        case LLVMFPToUI: return "fptoui";
        case LLVMFPToSI: return "fptosi";
        case LLVMUIToFP: return "uitofp";
        case LLVMSIToFP: return "sitofp";
        case LLVMFPTrunc: return "fptrunc";
        case LLVMFPExt: return "fpext";
        case LLVMPtrToInt: return "ptrtoint";
        case LLVMIntToPtr: return "inttoptr";
        case LLVMBitCast: return "bitcast";
        case LLVMAddrSpaceCast: return "addrspacecast";
        case LLVMICmp: return "icmp";
        case LLVMFCmp: return "fcmp";
        case LLVMPHI: return "phi";
        case LLVMCall: return "call";
        case LLVMSelect: return "select";
        case LLVMUserOp1: return "userop1";
        case LLVMUserOp2: return "userop2";
        case LLVMVAArg: return "va_arg";
        case LLVMExtractElement: return "extractelement";
        case LLVMInsertElement: return "insertelement";
        case LLVMShuffleVector: return "shufflevector";
        case LLVMExtractValue: return "extractvalue";
        case LLVMInsertValue: return "insertvalue";
        case LLVMFreeze: return "freeze";
        case LLVMFence: return "fence";
        case LLVMAtomicCmpXchg: return "cmpxchg";
        case LLVMAtomicRMW: return "atomicrmw";
        case LLVMResume: return "resume";
        case LLVMLandingPad: return "landingpad";
        case LLVMCleanupRet: return "cleanupret";
        case LLVMCatchRet: return "catchret";
        case LLVMCatchPad: return "catchpad";
        case LLVMCleanupPad: return "cleanuppad";
        case LLVMCatchSwitch: return "catchswitch";
        default: return "unknown";
        }
      });

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
      .def("__eq__", [](const LLVMTypeWrapper &a,
                        const LLVMTypeWrapper &b) { return a == b; })
      .def("__ne__", [](const LLVMTypeWrapper &a,
                        const LLVMTypeWrapper &b) { return a != b; })
      .def("__hash__",
           [](const LLVMTypeWrapper &v) {
             return std::hash<LLVMTypeRef>{}(v.m_ref);
           })
      .def_prop_ro("kind", &LLVMTypeWrapper::kind,
                   R"(Get the kind of this type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def("__str__", &LLVMTypeWrapper::to_string)
      .def("__repr__", &LLVMTypeWrapper::to_string)
      .def_prop_ro("is_void", &LLVMTypeWrapper::is_void,
                   R"(Check if this is a void type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def_prop_ro("is_integer", &LLVMTypeWrapper::is_integer,
                   R"(Check if this is an integer type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def_prop_ro("is_float", &LLVMTypeWrapper::is_float,
                   R"(Check if this is a floating-point type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def_prop_ro("is_pointer", &LLVMTypeWrapper::is_pointer,
                   R"(Check if this is a pointer type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def_prop_ro("is_function", &LLVMTypeWrapper::is_function,
                   R"(Check if this is a function type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def_prop_ro("is_struct", &LLVMTypeWrapper::is_struct,
                   R"(Check if this is a struct type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def_prop_ro("is_array", &LLVMTypeWrapper::is_array,
                   R"(Check if this is an array type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def_prop_ro("is_vector", &LLVMTypeWrapper::is_vector,
                   R"(Check if this is a vector type.

<sub>C API: LLVMGetTypeKind</sub>)")
      .def_prop_ro("int_width", &LLVMTypeWrapper::get_int_width,
                   R"(Get the bit width of an integer type.

<sub>C API: LLVMGetIntTypeWidth</sub>)")
      .def_prop_ro("is_sized", &LLVMTypeWrapper::is_sized,
                   R"(Check if this type has a known size.

<sub>C API: LLVMTypeIsSized</sub>)")
      .def_prop_ro("is_packed_struct", &LLVMTypeWrapper::is_packed_struct,
                   R"(Check if this struct is packed.

<sub>C API: LLVMIsPackedStruct</sub>)")
      .def_prop_ro("is_opaque_struct", &LLVMTypeWrapper::is_opaque_struct,
                   R"(Check if this struct is opaque.

<sub>C API: LLVMIsOpaqueStruct</sub>)")
      .def_prop_ro("is_literal_struct", &LLVMTypeWrapper::is_literal_struct,
                   R"(Check if this is a literal (unnamed) struct type.

<sub>C API: LLVMIsLiteralStruct</sub>)")
      .def_prop_ro("struct_name", &LLVMTypeWrapper::get_struct_name,
                   R"(Get the name of a struct type.

<sub>C API: LLVMGetStructName</sub>)")
      .def_prop_ro("is_vararg", &LLVMTypeWrapper::is_vararg_function,
                   R"(Check if this function type is variadic.

<sub>C API: LLVMIsFunctionVarArg</sub>)")
      .def("get_struct_element_type", &LLVMTypeWrapper::get_struct_element_type,
           "index"_a,
           R"(Get the element type at the given index.

Valid when:
  - this type is a struct
  - 0 <= index < struct_element_count

<sub>C API: LLVMStructGetTypeAtIndex</sub>)")
      .def_prop_ro("is_opaque_pointer", &LLVMTypeWrapper::is_opaque_pointer,
                   R"(Check if this is an opaque pointer.

<sub>C API: LLVMPointerTypeIsOpaque</sub>)")
      .def_prop_ro("element_type", &LLVMTypeWrapper::get_element_type,
                   R"(Get the element type (for arrays, vectors).

<sub>C API: LLVMGetElementType</sub>)")
      .def_prop_ro("array_length", &LLVMTypeWrapper::get_array_length,
                   R"(Get the length of an array type.

<sub>C API: LLVMGetArrayLength2</sub>)")
      .def_prop_ro("vector_size", &LLVMTypeWrapper::get_vector_size,
                   R"(Get the number of elements in a vector type.

<sub>C API: LLVMGetVectorSize</sub>)")
      .def_prop_ro("pointer_address_space",
                   &LLVMTypeWrapper::get_pointer_address_space,
                   R"(Get the address space of a pointer type.

<sub>C API: LLVMGetPointerAddressSpace</sub>)")
      .def_prop_ro("return_type", &LLVMTypeWrapper::get_return_type,
                   R"(Get the return type of a function type.

<sub>C API: LLVMGetReturnType</sub>)")
      .def_prop_ro("param_count", &LLVMTypeWrapper::count_param_types,
                   R"(Get the number of parameters in a function type.

<sub>C API: LLVMCountParamTypes</sub>)")
      .def_prop_ro("param_types", &LLVMTypeWrapper::get_param_types,
                   R"(Get the parameter types of a function type.

<sub>C API: LLVMGetParamTypes</sub>)")
      .def_prop_ro("target_ext_type_name",
                   &LLVMTypeWrapper::get_target_ext_type_name,
                   R"(Get the name of a target extension type.

<sub>C API: LLVMGetTargetExtTypeName</sub>)")
      .def_prop_ro(
          "target_ext_type_num_type_params",
          &LLVMTypeWrapper::get_target_ext_type_num_type_params,
          R"(Get number of type parameters for this target extension type.

<sub>C API: LLVMGetTargetExtTypeNumTypeParams</sub>)")
      .def_prop_ro(
          "target_ext_type_num_int_params",
          &LLVMTypeWrapper::get_target_ext_type_num_int_params,
          R"(Get number of integer parameters for this target extension type.

<sub>C API: LLVMGetTargetExtTypeNumIntParams</sub>)")
      .def("get_target_ext_type_type_param",
           &LLVMTypeWrapper::get_target_ext_type_type_param, "index"_a,
           R"(Get a type parameter of this target extension type by index.

Valid when:
  - this type is a target extension type
  - 0 <= index < target_ext_type_num_type_params

<sub>C API: LLVMGetTargetExtTypeTypeParam</sub>)")
      .def("get_target_ext_type_int_param",
           &LLVMTypeWrapper::get_target_ext_type_int_param, "index"_a,
           R"(Get an integer parameter of this target extension type by index.

Valid when:
  - this type is a target extension type
  - 0 <= index < target_ext_type_num_int_params

<sub>C API: LLVMGetTargetExtTypeIntParam</sub>)")
      .def("set_body", &struct_set_body, "elem_types"_a, "packed"_a = false,
           R"(Set the body of an opaque struct.

Valid when:
  - this type is a struct
  - this type is identified (named), not literal
  - this type is currently opaque

<sub>C API: LLVMStructSetBody</sub>)")
      .def_prop_ro("struct_element_count", &type_count_struct_element_types,
                   R"(Get number of struct elements.

Valid when:
  - this type is a struct

<sub>C API: LLVMCountStructElementTypes</sub>)")
      // Phase 2: Type-based constant creation
      .def("constant", &LLVMTypeWrapper::constant, "val"_a,
           "sign_extend"_a = false,
           R"(Create an integer constant of this type.

Valid when:
  - this type is an integer type

<sub>C API: LLVMConstInt</sub>)")
      .def("constant_from_string", &LLVMTypeWrapper::constant_from_string,
           "text"_a, "radix"_a = 10,
           R"(Create an integer constant from a string.

Useful for large integers that don't fit in Python int.

Valid when:
  - this type is an integer type
  - 2 <= radix <= 36

Args:
    text: The number as a string (e.g., "12345678901234567890")
    radix: The radix (base), 2-36. Default is 10.

<sub>C API: LLVMConstIntOfStringAndSize</sub>)")
      .def("real_constant", &LLVMTypeWrapper::real_constant, "val"_a,
           R"(Create a floating-point constant of this type.

Valid when:
  - this type is a floating-point type

<sub>C API: LLVMConstReal</sub>)")
      .def("real_constant_from_string",
           &LLVMTypeWrapper::real_constant_from_string, "text"_a,
           R"(Create a floating-point constant from a string.

Useful for precise floating-point values.

Valid when:
  - this type is a floating-point type

Args:
    text: The number as a string (e.g., "3.14159265358979323846")

<sub>C API: LLVMConstRealOfStringAndSize</sub>)")
      .def("null", &LLVMTypeWrapper::null,
           R"(Create a null value of this type.

<sub>C API: LLVMConstNull</sub>)")
      .def("all_ones", &LLVMTypeWrapper::all_ones,
           R"(Create an all-ones constant of this type.

<sub>C API: LLVMConstAllOnes</sub>)")
      .def("undef", &LLVMTypeWrapper::undef,
           R"(Create an undef value of this type.

<sub>C API: LLVMGetUndef</sub>)")
      .def("poison", &LLVMTypeWrapper::poison,
           R"(Create a poison value of this type.

<sub>C API: LLVMGetPoison</sub>)")
      // Phase 2: Composite type factory methods
      .def("array", &LLVMTypeWrapper::array, "count"_a,
           R"(Create an array type with this element type.

<sub>C API: LLVMArrayType2</sub>)")
      .def("vector", &LLVMTypeWrapper::vector, "count"_a,
           R"(Create a vector type with this element type.

<sub>C API: LLVMVectorType</sub>)")
      .def("pointer", &LLVMTypeWrapper::pointer, "address_space"_a = 0,
           R"(Create a pointer type in this type's context.

<sub>C API: LLVMPointerType</sub>)")
      // Constant creation from type
      .def("const_array", [](const LLVMTypeWrapper &self,
                             const std::vector<LLVMValueWrapper> &vals) {
        self.check_valid();
        std::vector<LLVMValueRef> refs;
        refs.reserve(vals.size());
        for (const auto &v : vals) {
          v.check_valid();
          refs.push_back(v.m_ref);
        }
        return LLVMValueWrapper(
            LLVMConstArray2(self.m_ref, refs.data(), refs.size()),
            self.m_context_token);
      }, "vals"_a,
           R"(Create an array constant of this element type.

<sub>C API: LLVMConstArray2</sub>)")
      .def("const_named_struct", [](const LLVMTypeWrapper &self,
                                    const std::vector<LLVMValueWrapper> &vals) {
        self.check_valid();
        std::vector<LLVMValueRef> refs;
        refs.reserve(vals.size());
        for (const auto &v : vals) {
          v.check_valid();
          refs.push_back(v.m_ref);
        }
        return LLVMValueWrapper(
            LLVMConstNamedStruct(self.m_ref, refs.data(), refs.size()),
            self.m_context_token);
      }, "vals"_a,
           R"(Create a named struct constant of this struct type.

<sub>C API: LLVMConstNamedStruct</sub>)")
      .def("const_data_array", [](const LLVMTypeWrapper &self,
                                  const std::string &data) {
        self.check_valid();
        return LLVMValueWrapper(
            LLVMConstDataArray(self.m_ref, data.data(), data.size()),
            self.m_context_token);
      }, "data"_a,
           R"(Create a data array constant of this element type from a string.

<sub>C API: LLVMConstDataArray</sub>)")
      .def("const_data_array", [](const LLVMTypeWrapper &self,
                                  const nb::bytes &data) {
        self.check_valid();
        return LLVMValueWrapper(
            LLVMConstDataArray(self.m_ref, data.c_str(), data.size()),
            self.m_context_token);
      }, "data"_a,
           R"(Create a data array constant of this element type from raw bytes.

<sub>C API: LLVMConstDataArray</sub>)")
      // Parent navigation
      .def_prop_ro("context", &LLVMTypeWrapper::context,
                   R"(Get the context this type belongs to.

<sub>C API: LLVMGetTypeContext</sub>)",
                   nb::rv_policy::take_ownership);

  // Use wrapper (represents a use edge in the use-def chain)
  nb::class_<LLVMUseWrapper>(
      m, "Use",
      "A use is a `user` which contains a reference to the `used_value`.")
      .def("__eq__", [](const LLVMUseWrapper &a,
                        const LLVMUseWrapper &b) { return a.m_ref == b.m_ref; })
      .def("__ne__", [](const LLVMUseWrapper &a,
                        const LLVMUseWrapper &b) { return a.m_ref != b.m_ref; })
      .def("__hash__",
           [](const LLVMUseWrapper &v) {
             return std::hash<LLVMUseRef>{}(v.m_ref);
           })
      .def_prop_ro("user", &LLVMUseWrapper::get_user,
                   "Obtain the user value for a user.\n\n@api "
                   "llvm::Use::getUser(), LLVMGetUser")
      .def_prop_ro("used_value", &LLVMUseWrapper::get_used_value,
                   "Obtain the value this use corresponds to.\n\n@api "
                   "llvm::Use::get(), LLVMGetUsedValue")
      .def_prop_ro("operand_index", &LLVMUseWrapper::get_operand_index,
                   R"(Get the operand index of this use within the user instruction.

<sub>C API: LLVMGetOperandUse</sub>)");

  // Value wrapper
  nb::class_<LLVMValueWrapper>(m, "Value")
      .def("__eq__", [](const LLVMValueWrapper &a,
                        const LLVMValueWrapper &b) { return a == b; })
      .def("__ne__", [](const LLVMValueWrapper &a,
                        const LLVMValueWrapper &b) { return a != b; })
      .def("__hash__",
           [](const LLVMValueWrapper &v) {
             return std::hash<LLVMValueRef>{}(v.m_ref);
           })
      .def_prop_ro("type", &LLVMValueWrapper::type,
                   R"(Get the type of this value.

<sub>C API: LLVMTypeOf</sub>)")
      .def_prop_rw("name", &LLVMValueWrapper::get_name,
                   &LLVMValueWrapper::set_name,
                   R"(Get or set the name.

<sub>C API: LLVMGetValueName2, LLVMSetValueName2</sub>)")
      .def("__str__", &LLVMValueWrapper::to_string)
      .def("__repr__", &LLVMValueWrapper::to_string)
      .def_prop_ro("is_constant", &LLVMValueWrapper::is_constant,
                   R"(Check if this is a constant.

<sub>C API: LLVMIsConstant</sub>)")
      .def_prop_ro("is_undef", &LLVMValueWrapper::is_undef,
                   R"(Check if this is undef.

<sub>C API: LLVMIsUndef</sub>)")
      .def_prop_ro("is_poison", &LLVMValueWrapper::is_poison,
                   R"(Check if this is poison.

<sub>C API: LLVMIsPoison</sub>)")
      .def_prop_ro("uses", &LLVMValueWrapper::uses,
                   R"(Get all uses of this value.

<sub>C API: LLVMGetFirstUse, LLVMGetNextUse</sub>)")
      .def_prop_ro("users", &LLVMValueWrapper::users,
                   R"(Get all users of this value.

<sub>C API: LLVMGetFirstUse, LLVMGetUser</sub>)")
      .def_prop_ro("next_global", &LLVMValueWrapper::next_global,
                   R"(Get the next global.

<sub>C API: LLVMGetNextGlobal</sub>)")
      .def_prop_ro("prev_global", &LLVMValueWrapper::prev_global,
                   R"(Get the previous global.

<sub>C API: LLVMGetPreviousGlobal</sub>)")
      .def("add_incoming", &phi_add_incoming, "val"_a, "bb"_a,
           R"(Add an incoming value to a PHI node.

<sub>C API: LLVMAddIncoming</sub>)")
      .def("add_case", &switch_add_case, "val"_a, "bb"_a,
           R"(Add a case to a switch instruction.

<sub>C API: LLVMAddCase</sub>)")
      .def("add_destination", &indirect_br_add_destination, "dest"_a,
           R"(Add a destination to an indirect branch.

<sub>C API: LLVMAddDestination</sub>)")
      .def_prop_rw("initializer", &global_get_initializer,
                   &global_set_initializer,
                   R"(Get or set the initializer.

<sub>C API: LLVMGetInitializer, LLVMSetInitializer</sub>)")
      .def("set_constant", &global_set_constant, "is_const"_a,
           R"(Set whether this global is constant.

<sub>C API: LLVMSetGlobalConstant</sub>)")
      .def_prop_rw("is_global_constant", &global_is_constant,
                   &global_set_constant,
                   R"(Get or set whether this global is constant.

<sub>C API: LLVMIsGlobalConstant, LLVMSetGlobalConstant</sub>)")
      .def_prop_rw("linkage", &global_get_linkage, &global_set_linkage,
                   R"(Get or set linkage.

<sub>C API: LLVMGetLinkage, LLVMSetLinkage</sub>)")
      .def_prop_rw("visibility", &global_get_visibility, &global_set_visibility,
                   R"(Get or set visibility.

<sub>C API: LLVMGetVisibility, LLVMSetVisibility</sub>)")
      .def_prop_rw("dll_storage_class", &global_get_dll_storage_class,
                   &global_set_dll_storage_class,
                   R"(DLL storage class for Windows PE/COFF targets.

<sub>C API: LLVMGetDLLStorageClass, LLVMSetDLLStorageClass</sub>)")
      .def_prop_ro("comdat", &global_get_comdat,
                   R"(Get the COMDAT section for this global (None if not set).

<sub>C API: LLVMGetComdat</sub>)")
      .def("set_comdat", &global_set_comdat, "comdat"_a,
           R"(Set the COMDAT section for this global.

<sub>C API: LLVMSetComdat</sub>)")
      .def_prop_rw("alignment", &LLVMValueWrapper::get_alignment,
                   &LLVMValueWrapper::set_alignment,
                   R"(Get or set alignment.

<sub>C API: LLVMGetAlignment, LLVMSetAlignment</sub>)")
      .def_prop_rw("section", &global_get_section, &global_set_section,
                   R"(Get or set section.

<sub>C API: LLVMGetSection, LLVMSetSection</sub>)")
      .def("set_thread_local", &global_set_thread_local, "is_tls"_a,
           R"(Set whether this global is thread-local.

<sub>C API: LLVMSetThreadLocal</sub>)")
      .def_prop_rw("is_thread_local", &global_is_thread_local,
                   &global_set_thread_local,
                   R"(Get or set whether this global is thread-local.

<sub>C API: LLVMIsThreadLocal, LLVMSetThreadLocal</sub>)")
      .def("set_externally_initialized", &global_set_externally_initialized,
           "is_ext"_a,
           R"(Set whether this global is initialized externally.

<sub>C API: LLVMSetExternallyInitialized</sub>)")
      .def_prop_rw("is_externally_initialized",
                   &global_is_externally_initialized,
                   &global_set_externally_initialized,
                   R"(Get or set whether this global is initialized externally.

<sub>C API: LLVMIsExternallyInitialized, LLVMSetExternallyInitialized</sub>)")
      .def("delete_global", &global_delete,
           R"(Delete this global.

<sub>C API: LLVMDeleteGlobal</sub>)")
      .def("delete", &global_delete,
           R"(Delete this global.

<sub>C API: LLVMDeleteGlobal</sub>)")
      // PHI helpers
      .def_prop_ro("num_incoming", &LLVMValueWrapper::count_incoming,
                   R"(Get number of incoming values.

<sub>C API: LLVMCountIncoming</sub>)")
      .def("get_incoming_value", &LLVMValueWrapper::get_incoming_value,
           "index"_a,
           R"(Get incoming value at index.

<sub>C API: LLVMGetIncomingValue</sub>)")
      .def("get_incoming_block", &phi_get_incoming_block, "index"_a,
           R"(Get incoming block at index.

<sub>C API: LLVMGetIncomingBlock</sub>)")
      .def_prop_ro(
          "incoming",
          [](const LLVMValueWrapper &phi) {
            phi.check_valid();
            phi.require_phi_instruction("incoming");
            unsigned n = LLVMCountIncoming(phi.m_ref);
            nb::list result;
            for (unsigned i = 0; i < n; ++i) {
              auto val = LLVMValueWrapper(LLVMGetIncomingValue(phi.m_ref, i),
                                          phi.m_context_token);
              auto bb = LLVMBasicBlockWrapper(
                  LLVMGetIncomingBlock(phi.m_ref, i), phi.m_context_token);
              result.append(nb::make_tuple(val, bb));
            }
            return result;
          },
          R"(Get all incoming (value, block) pairs as a list of tuples.)")
      // Branch instruction helpers
      .def_prop_ro("is_conditional", &LLVMValueWrapper::is_conditional,
                   R"(Check if conditional branch.

<sub>C API: LLVMIsConditional</sub>)")
      .def_prop_ro("condition", &LLVMValueWrapper::get_condition,
                   R"(Get branch condition.

<sub>C API: LLVMGetCondition</sub>)")
      .def_prop_ro("num_successors", &LLVMValueWrapper::get_num_successors,
                   R"(Get number of successors.

<sub>C API: LLVMGetNumSuccessors</sub>)")
      .def("get_successor", &LLVMValueWrapper::get_successor, "index"_a,
           R"(Get successor at index.

For conditional `br` instructions, successor indexing does not match raw
operand indexing:
  - `get_successor(0)` is the true destination, matching `get_operand(2)`
  - `get_successor(1)` is the false destination, matching `get_operand(1)`

<sub>C API: LLVMGetSuccessor</sub>)")
      .def_prop_ro("successors", &LLVMValueWrapper::successors,
                   R"(Get all successors.

For conditional `br`, this list is `[true_dest, false_dest]`, while raw branch
operands are `[cond, false_dest, true_dest]`.

<sub>C API: LLVMGetNumSuccessors, LLVMGetSuccessor</sub>)")
      // Load/Store helpers
      .def("set_volatile", &LLVMValueWrapper::set_volatile, "is_volatile"_a,
           R"(Set volatile flag.

<sub>C API: LLVMSetVolatile</sub>)")
      .def_prop_rw("is_volatile", &LLVMValueWrapper::get_volatile,
                   &LLVMValueWrapper::set_volatile,
                   R"(Get or set volatile flag.

<sub>C API: LLVMGetVolatile, LLVMSetVolatile</sub>)")
      .def("set_inst_alignment", &LLVMValueWrapper::set_alignment, "align"_a,
           R"(Set instruction alignment.

<sub>C API: LLVMSetAlignment</sub>)")
      .def_prop_rw("inst_alignment", &LLVMValueWrapper::get_alignment,
                   &LLVMValueWrapper::set_alignment,
                   R"(Get or set instruction alignment.

<sub>C API: LLVMGetAlignment, LLVMSetAlignment</sub>)")
      // Comparison helpers
      .def_prop_ro("icmp_predicate", &LLVMValueWrapper::get_icmp_predicate,
                   R"(Get integer comparison predicate.

<sub>C API: LLVMGetICmpPredicate</sub>)")
      .def_prop_ro("fcmp_predicate", &LLVMValueWrapper::get_fcmp_predicate,
                   R"(Get float comparison predicate.

<sub>C API: LLVMGetFCmpPredicate</sub>)")
      // Instruction iteration
      .def_prop_ro("next_instruction", &LLVMValueWrapper::next_instruction,
                   R"(Get next instruction.

<sub>C API: LLVMGetNextInstruction</sub>)")
      .def_prop_ro("prev_instruction", &LLVMValueWrapper::prev_instruction,
                   R"(Get previous instruction.

<sub>C API: LLVMGetPreviousInstruction</sub>)")
      // Instruction predicates
      .def_prop_ro("is_call_inst", &LLVMValueWrapper::is_a_call_inst,
                   R"(Check if this is a call.

<sub>C API: LLVMIsACallInst</sub>)")
      .def_prop_ro("is_declaration", &LLVMValueWrapper::is_declaration,
                   R"(Check if this is a declaration.

<sub>C API: LLVMIsDeclaration</sub>)")
      // Operand access
      .def_prop_ro("operands", &LLVMValueWrapper::operands,
                   R"(Get all raw operands as a list.

Warning:
  - Raw operand order is instruction-specific and may differ from printed IR.
  - Prefer semantic accessors when available (e.g. `condition`,
    `get_successor`/`successors`, `called_value`, `get_arg_operand`,
    `incoming`, `handlers`).
  - See `devdocs/operands.md` for known layouts and gotchas.
)")
      .def_prop_ro("has_uses", &LLVMValueWrapper::has_uses,
                   R"(Check if this value has any uses.)")
      .def_prop_ro("num_operands", &LLVMValueWrapper::get_num_operands,
                   R"(Get number of operands.

<sub>C API: LLVMGetNumOperands</sub>)")
      .def("get_operand", &LLVMValueWrapper::get_operand, "index"_a,
           R"(Get raw operand at index.

Warning:
  - Raw operand indices are instruction-specific implementation details.
  - Prefer semantic accessors when available.
  - See `devdocs/operands.md` for layout details and gotchas.

<sub>C API: LLVMGetOperand</sub>)")
      .def("set_operand", &LLVMValueWrapper::set_operand, "index"_a, "val"_a,
           R"(Set a raw operand at the given index.

Warning:
  - Raw operand indices are instruction-specific and easy to misuse.
  - Prefer semantic mutators/accessors when available.
  - See `devdocs/operands.md` for layout details and gotchas.

<sub>C API: LLVMSetOperand</sub>)")
      .def("get_operand_use", &LLVMValueWrapper::get_operand_use, "index"_a,
           R"(Get the Use object for a raw operand index.

Warning:
  - Raw operand indices are instruction-specific implementation details.
  - Prefer semantic accessors when available.
  - See `devdocs/operands.md` for layout details and gotchas.

<sub>C API: LLVMGetOperandUse</sub>)")
      // Constant type checking
      .def_prop_ro("is_global_value", &LLVMValueWrapper::is_a_global_value,
                   R"(Check if global value.

<sub>C API: LLVMIsAGlobalValue</sub>)")
      .def_prop_ro("is_function", &LLVMValueWrapper::is_a_function,
                   R"(Check if function.

<sub>C API: LLVMIsAFunction</sub>)")
      .def_prop_ro("is_global_variable",
                   &LLVMValueWrapper::is_a_global_variable,
                   R"(Check if global variable.

<sub>C API: LLVMIsAGlobalVariable</sub>)")
      .def_prop_ro("is_global_alias", &LLVMValueWrapper::is_a_global_alias,
                   R"(Check if global alias.

<sub>C API: LLVMIsAGlobalAlias</sub>)")
      .def_prop_ro("is_constant_int", &LLVMValueWrapper::is_a_constant_int,
                   R"(Check if constant int.

<sub>C API: LLVMIsAConstantInt</sub>)")
      .def_prop_ro("is_constant_fp", &LLVMValueWrapper::is_a_constant_fp,
                   R"(Check if constant FP.

<sub>C API: LLVMIsAConstantFP</sub>)")
      .def_prop_ro("is_constant_aggregate_zero",
                   &LLVMValueWrapper::is_a_constant_aggregate_zero,
                   R"(Check if constant aggregate zero.

<sub>C API: LLVMIsAConstantAggregateZero</sub>)")
      .def_prop_ro("is_constant_data_array",
                   &LLVMValueWrapper::is_a_constant_data_array,
                   R"(Check if constant data array.

<sub>C API: LLVMIsAConstantDataArray</sub>)")
      .def_prop_ro("is_constant_array", &LLVMValueWrapper::is_a_constant_array,
                   R"(Check if constant array.

<sub>C API: LLVMIsAConstantArray</sub>)")
      .def_prop_ro("is_constant_struct",
                   &LLVMValueWrapper::is_a_constant_struct,
                   R"(Check if constant struct.

<sub>C API: LLVMIsAConstantStruct</sub>)")
      .def_prop_ro("is_constant_pointer_null",
                   &LLVMValueWrapper::is_a_constant_pointer_null,
                   R"(Check if constant pointer null.

<sub>C API: LLVMIsAConstantPointerNull</sub>)")
      .def_prop_ro("is_constant_vector",
                   &LLVMValueWrapper::is_a_constant_vector,
                   R"(Check if constant vector.

<sub>C API: LLVMIsAConstantVector</sub>)")
      .def_prop_ro("is_constant_data_vector",
                   &LLVMValueWrapper::is_a_constant_data_vector,
                   R"(Check if constant data vector.

<sub>C API: LLVMIsAConstantDataVector</sub>)")
      .def_prop_ro("is_constant_expr", &LLVMValueWrapper::is_a_constant_expr,
                   R"(Check if constant expr.

<sub>C API: LLVMIsAConstantExpr</sub>)")
      .def_prop_ro(
          "is_constant_ptr_auth", &LLVMValueWrapper::is_a_constant_ptr_auth,
          R"(Check if this is a constant pointer authentication expression.

<sub>C API: LLVMIsAConstantPtrAuth</sub>)")
      .def_prop_ro("is_null", &LLVMValueWrapper::is_null,
                   R"(Check if null.

<sub>C API: LLVMIsNull</sub>)")
      // Constant integer value access
      .def_prop_ro("const_zext_value", &LLVMValueWrapper::const_zext_value,
                   R"(Get zero-extended value.

<sub>C API: LLVMConstIntGetZExtValue</sub>)")
      .def_prop_ro("const_sext_value", &LLVMValueWrapper::const_sext_value,
                   R"(Get sign-extended value.

<sub>C API: LLVMConstIntGetSExtValue</sub>)")
      .def_prop_ro("const_real_double", &LLVMValueWrapper::const_real_double,
                   R"(Get floating-point constant value as a double.

<sub>C API: LLVMConstRealGetDouble</sub>)")
      // Value as metadata check
      .def_prop_ro("is_value_as_metadata",
                   &LLVMValueWrapper::is_value_as_metadata,
                   R"(Check if this is a ValueAsMetadata wrapper.

<sub>C API: LLVMIsAValueAsMetadata</sub>)")
      // Intrinsic support
      .def_prop_ro(
          "intrinsic_id", &LLVMValueWrapper::get_intrinsic_id,
          R"(Get intrinsic ID if this is a call to an LLVM intrinsic (0 if not).

<sub>C API: LLVMGetIntrinsicID</sub>)")
      // Constant data access
      .def_prop_ro(
          "raw_data_values", &LLVMValueWrapper::get_raw_data_values,
          R"(Get elements of a ConstantDataSequential as a list of floats.

<sub>C API: LLVMGetConstantDataSequentialElementAsDouble</sub>)")
      .def(
          "get_aggregate_element", &LLVMValueWrapper::get_aggregate_element,
          "index"_a,
          R"(Get element of a constant aggregate (struct, array, vector) at index.

<sub>C API: LLVMGetAggregateElement</sub>)")
      // Constant expression support
      .def_prop_ro("const_opcode", &LLVMValueWrapper::get_const_opcode,
                   R"(Get the opcode for a constant expression.

<sub>C API: LLVMGetConstOpcode</sub>)")
      .def_prop_ro(
          "gep_source_element_type",
          &LLVMValueWrapper::get_gep_source_element_type,
          R"(Get the source element type of this GEP (GetElementPtr) instruction.

<sub>C API: LLVMGetGEPSourceElementType</sub>)")
      .def_prop_ro(
          "num_indices", &LLVMValueWrapper::get_num_indices,
          R"(Get the number of indices in this GEP or ExtractValue instruction.

<sub>C API: LLVMGetNumIndices</sub>)")
      .def_prop_ro(
          "gep_no_wrap_flags", &LLVMValueWrapper::get_gep_no_wrap_flags,
          R"(Get the no-wrap flags (nuw/nusw/inbounds) for this GEP instruction.

<sub>C API: LLVMGEPGetNoWrapFlags</sub>)")
      // Pointer auth constant support
      .def_prop_ro(
          "constant_ptr_auth_pointer",
          &LLVMValueWrapper::get_constant_ptr_auth_pointer,
          R"(Get the pointer from a constant pointer authentication expression.

<sub>C API: LLVMGetConstantPtrAuthPointer</sub>)")
      .def_prop_ro(
          "constant_ptr_auth_key", &LLVMValueWrapper::get_constant_ptr_auth_key,
          R"(Get the key from a constant pointer authentication expression.

<sub>C API: LLVMGetConstantPtrAuthKey</sub>)")
      .def_prop_ro(
          "constant_ptr_auth_discriminator",
          &LLVMValueWrapper::get_constant_ptr_auth_discriminator,
          R"(Get the integer discriminator from a constant pointer authentication expression.

<sub>C API: LLVMGetConstantPtrAuthAddrDiscriminator</sub>)")
      .def_prop_ro(
          "constant_ptr_auth_addr_discriminator",
          &LLVMValueWrapper::get_constant_ptr_auth_addr_discriminator,
          R"(Get the address discriminator from a constant pointer authentication expression.

<sub>C API: LLVMGetConstantPtrAuthAddrDiscriminator</sub>)")
      // Parameter iteration
      .def_prop_ro("next_param", &LLVMValueWrapper::next_param,
                   R"(Get next parameter.

<sub>C API: LLVMGetNextParam</sub>)")
      .def_prop_ro("prev_param", &LLVMValueWrapper::prev_param,
                   R"(Get previous parameter.

<sub>C API: LLVMGetPreviousParam</sub>)")
      // Global alias iteration
      .def_prop_ro("next_global_alias", &LLVMValueWrapper::next_global_alias,
                   R"(Get next global alias.

<sub>C API: LLVMGetNextGlobalAlias</sub>)")
      .def_prop_ro("prev_global_alias", &LLVMValueWrapper::prev_global_alias,
                   R"(Get previous global alias.

<sub>C API: LLVMGetPreviousGlobalAlias</sub>)")
      .def_prop_ro("aliasee", &LLVMValueWrapper::alias_get_aliasee,
                   R"(Get the value this alias points to.

<sub>C API: LLVMAliasGetAliasee</sub>)")
      .def("alias_set_aliasee", &LLVMValueWrapper::alias_set_aliasee,
           "aliasee"_a, R"(Set the value this alias points to.

<sub>C API: LLVMAliasSetAliasee</sub>)")
      // Global IFunc iteration
      .def_prop_ro("next_global_ifunc", &LLVMValueWrapper::next_global_ifunc,
                   R"(Get the next indirect function (IFunc) in the module.

<sub>C API: LLVMGetNextGlobalIFunc</sub>)")
      .def_prop_ro("prev_global_ifunc", &LLVMValueWrapper::prev_global_ifunc,
                   R"(Get the previous indirect function (IFunc) in the module.

<sub>C API: LLVMGetPreviousGlobalIFunc</sub>)")
      .def_prop_ro("global_ifunc_resolver",
                   &LLVMValueWrapper::get_global_ifunc_resolver,
                   R"(Get the resolver function for this indirect function.

<sub>C API: LLVMGetGlobalIFuncResolver</sub>)")
      .def("set_global_ifunc_resolver",
           &LLVMValueWrapper::set_global_ifunc_resolver, "resolver"_a,
           R"(Set the resolver function for this indirect function.

<sub>C API: LLVMSetGlobalIFuncResolver</sub>)")
      .def("erase_from_parent_ifunc",
           &LLVMValueWrapper::erase_from_parent_ifunc,
           R"(Erase this global IFunc from its parent module and delete it.

After calling this, the IFunc value is no longer valid and should not be used.

<sub>C API: LLVMEraseGlobalIFunc</sub>)")
      .def("remove_from_parent_ifunc",
           &LLVMValueWrapper::remove_from_parent_ifunc,
           R"(Remove this global IFunc from its parent module but keep it alive.

WARNING: This is a low-level API for advanced use cases like moving an IFunc
between modules. The removed IFunc will still hold references to values in the
original module. If those values are deleted before the IFunc, LLVM will crash.
Prefer using erase_from_parent_ifunc() for most use cases.

<sub>C API: LLVMRemoveGlobalIFunc</sub>)")
      // Global properties
      .def_prop_ro("global_value_type",
                   &LLVMValueWrapper::global_get_value_type,
                   R"(Get the value type of this global variable or function.

<sub>C API: LLVMGlobalGetValueType</sub>)")
      .def_prop_rw(
          "unnamed_address", &LLVMValueWrapper::get_unnamed_address,
          &LLVMValueWrapper::set_unnamed_address,
          R"(Unnamed address attribute (controls whether address is significant).

<sub>C API: LLVMGetUnnamedAddress, LLVMSetUnnamedAddress</sub>)")
      .def_prop_ro(
          "has_personality_fn", &LLVMValueWrapper::has_personality_fn,
          R"(Check if this function has an exception handling personality function.

<sub>C API: LLVMHasPersonalityFn</sub>)")
      .def_prop_ro("personality_fn", &LLVMValueWrapper::get_personality_fn,
                   R"(Get the exception handling personality function.

Valid when:
  - has_personality_fn is True

<sub>C API: LLVMGetPersonalityFn</sub>)")
      .def("set_personality_fn", &LLVMValueWrapper::set_personality_fn, "fn"_a,
           R"(Set the exception handling personality function.

<sub>C API: LLVMSetPersonalityFn</sub>)")
      .def_prop_ro(
          "has_prefix_data", &LLVMValueWrapper::has_prefix_data,
          R"(Check if this function has prefix data (data before function entry).

<sub>C API: LLVMHasPrefixData</sub>)")
      .def_prop_ro("prefix_data", &LLVMValueWrapper::get_prefix_data,
                   R"(Get prefix data (data before function entry).

Valid when:
  - has_prefix_data is True

<sub>C API: LLVMGetPrefixData</sub>)")
      .def("set_prefix_data", &LLVMValueWrapper::set_prefix_data, "data"_a,
           R"(Set prefix data (data before function entry).

<sub>C API: LLVMSetPrefixData</sub>)")
      .def_prop_ro(
          "has_prologue_data", &LLVMValueWrapper::has_prologue_data,
          R"(Check if this function has prologue data (data at function entry).

<sub>C API: LLVMHasPrologueData</sub>)")
      .def_prop_ro("prologue_data", &LLVMValueWrapper::get_prologue_data,
                   R"(Get prologue data (data at function entry).

Valid when:
  - has_prologue_data is True

<sub>C API: LLVMGetPrologueData</sub>)")
      .def("set_prologue_data", &LLVMValueWrapper::set_prologue_data, "data"_a,
           R"(Set prologue data (data at function entry).

<sub>C API: LLVMSetPrologueData</sub>)")
      // Instruction properties
      .def_prop_ro("opcode", &LLVMValueWrapper::get_instruction_opcode,
                   R"(Get instruction opcode.

<sub>C API: LLVMGetInstructionOpcode</sub>)")
      .def_prop_ro("opcode_name", &LLVMValueWrapper::get_opcode_name,
                   R"(Get the mnemonic string for this instruction's opcode (e.g. "add", "br", "call").)")
      // Instruction flags
      .def_prop_ro("nsw", &LLVMValueWrapper::get_nsw,
                   R"(Get NSW flag.

<sub>C API: LLVMGetNSW</sub>)")
      .def_prop_ro("nuw", &LLVMValueWrapper::get_nuw,
                   R"(Get NUW flag.

<sub>C API: LLVMGetNUW</sub>)")
      .def_prop_ro("exact", &LLVMValueWrapper::get_exact,
                   R"(Get exact flag.

<sub>C API: LLVMGetExact</sub>)")
      .def_prop_ro("nneg", &LLVMValueWrapper::get_nneg,
                   R"(Get nneg flag.

<sub>C API: LLVMGetNNeg</sub>)")
      // Memory access properties
      .def_prop_ro("ordering", &LLVMValueWrapper::get_ordering,
                   R"(Get atomic ordering.

<sub>C API: LLVMGetOrdering</sub>)")
      // Call/invoke properties
      .def_prop_ro("num_arg_operands", &LLVMValueWrapper::get_num_arg_operands,
                   R"(Get number of arg operands.

<sub>C API: LLVMGetNumArgOperands</sub>)")
      // Alloca properties
      .def_prop_ro("allocated_type", &LLVMValueWrapper::get_allocated_type,
                   R"(Get allocated type.

<sub>C API: LLVMGetAllocatedType</sub>)")
      .def_prop_ro("function_type", &LLVMValueWrapper::get_function_type,
                   R"(Get function type.
          
<sub>C API: LLVMGlobalGetValueType</sub>)")
      .def_prop_ro("value_kind", &LLVMValueWrapper::value_kind,
                   R"(Get value kind.

<sub>C API: LLVMGetValueKind</sub>)")
      // Operand bundle support
      .def_prop_ro("num_operand_bundles",
                   &LLVMValueWrapper::get_num_operand_bundles,
                   R"(Get number of operand bundles.

<sub>C API: LLVMGetNumOperandBundles</sub>)")
      // Inline assembly support
      .def_prop_ro("is_inline_asm", &LLVMValueWrapper::is_a_inline_asm,
                   R"(Check if this is an inline assembly value.

<sub>C API: LLVMIsAInlineAsm</sub>)")
      .def_prop_ro("inline_asm_asm_string",
                   &LLVMValueWrapper::get_inline_asm_asm_string,
                   R"(Get the assembly string from an inline asm value.

<sub>C API: LLVMGetInlineAsmAsmString</sub>)")
      .def_prop_ro("inline_asm_constraint_string",
                   &LLVMValueWrapper::get_inline_asm_constraint_string,
                   R"(Get the constraint string from an inline asm value.

<sub>C API: LLVMGetInlineAsmConstraintString</sub>)")
      .def_prop_ro("inline_asm_dialect",
                   &LLVMValueWrapper::get_inline_asm_dialect,
                   R"(Get the dialect (AT&T or Intel) of an inline asm value.

<sub>C API: LLVMGetInlineAsmDialect</sub>)")
      .def_prop_ro("inline_asm_function_type",
                   &LLVMValueWrapper::get_inline_asm_function_type,
                   R"(Get the function type of an inline asm value.

<sub>C API: LLVMGetInlineAsmFunctionType</sub>)")
      .def_prop_ro("inline_asm_has_side_effects",
                   &LLVMValueWrapper::get_inline_asm_has_side_effects,
                   R"(Check if this inline assembly has side effects.

<sub>C API: LLVMGetInlineAsmHasSideEffects</sub>)")
      .def_prop_ro("inline_asm_needs_aligned_stack",
                   &LLVMValueWrapper::get_inline_asm_needs_aligned_stack,
                   R"(Check if this inline assembly requires an aligned stack.

<sub>C API: LLVMGetInlineAsmNeedsAlignedStack</sub>)")
      .def_prop_ro("inline_asm_can_unwind",
                   &LLVMValueWrapper::get_inline_asm_can_unwind,
                   R"(Check if inline assembly can unwind the stack.

<sub>C API: LLVMGetInlineAsmCanUnwind</sub>)")
      // Flag setters
      .def("set_nsw", &LLVMValueWrapper::set_nsw, "nsw"_a,
           R"(Set NSW flag.

<sub>C API: LLVMSetNSW</sub>)")
      .def("set_nuw", &LLVMValueWrapper::set_nuw, "nuw"_a,
           R"(Set NUW flag.

<sub>C API: LLVMSetNUW</sub>)")
      .def("set_exact", &LLVMValueWrapper::set_exact, "exact"_a,
           R"(Set exact flag.

<sub>C API: LLVMSetExact</sub>)")
      .def("set_nneg", &LLVMValueWrapper::set_nneg, "nneg"_a,
           R"(Set nneg flag.

<sub>C API: LLVMSetNNeg</sub>)")
      .def_prop_ro("is_disjoint", &LLVMValueWrapper::get_is_disjoint,
                   R"(Get disjoint flag.

<sub>C API: LLVMGetIsDisjoint</sub>)")
      .def("set_is_disjoint", &LLVMValueWrapper::set_is_disjoint,
           "is_disjoint"_a, R"(Set disjoint flag.

<sub>C API: LLVMSetIsDisjoint</sub>)")
      .def_prop_ro("icmp_same_sign", &LLVMValueWrapper::get_icmp_same_sign,
                   R"(Get same sign flag.

<sub>C API: LLVMGetICmpSameSign</sub>)")
      .def("set_icmp_same_sign", &LLVMValueWrapper::set_icmp_same_sign,
           "same_sign"_a, R"(Set same sign flag.

<sub>C API: LLVMSetICmpSameSign</sub>)")
      .def("set_ordering", &LLVMValueWrapper::set_ordering, "ordering"_a,
           R"(Set atomic ordering.

<sub>C API: LLVMSetOrdering</sub>)")
      .def("set_volatile", &LLVMValueWrapper::set_volatile, "is_volatile"_a,
           R"(Set volatile flag.

<sub>C API: LLVMSetVolatile</sub>)")
      // Atomic properties
      .def_prop_ro("is_atomic", &LLVMValueWrapper::is_atomic,
                   R"(Check if this atomic operation uses singlethread ordering.

<sub>C API: LLVMIsAtomicSingleThread</sub>)")
      .def_prop_ro("atomic_sync_scope_id",
                   &LLVMValueWrapper::get_atomic_sync_scope_id,
                   R"(Get sync scope ID.

<sub>C API: LLVMGetAtomicSyncScopeID</sub>)")
      .def("set_atomic_sync_scope_id",
           &LLVMValueWrapper::set_atomic_sync_scope_id, "scope_id"_a,
           R"(Set sync scope ID.

<sub>C API: LLVMSetAtomicSyncScopeID</sub>)")
      .def_prop_ro(
          "atomic_rmw_bin_op", &LLVMValueWrapper::get_atomic_rmw_bin_op,
          R"(Get the operation kind for an atomic read-modify-write instruction.

<sub>C API: LLVMGetAtomicRMWBinOp</sub>)")
      .def_prop_ro(
          "cmpxchg_success_ordering",
          &LLVMValueWrapper::get_cmpxchg_success_ordering,
          R"(Get the memory ordering on success for a compare-exchange instruction.

<sub>C API: LLVMGetCmpXchgSuccessOrdering</sub>)")
      .def_prop_ro(
          "cmpxchg_failure_ordering",
          &LLVMValueWrapper::get_cmpxchg_failure_ordering,
          R"(Get the memory ordering on failure for a compare-exchange instruction.

<sub>C API: LLVMGetCmpXchgFailureOrdering</sub>)")
      .def_prop_ro("weak", &LLVMValueWrapper::get_weak,
                   R"(Get weak flag.

<sub>C API: LLVMGetWeak</sub>)")
      .def("set_weak", &LLVMValueWrapper::set_weak, "is_weak"_a,
           R"(Set weak flag.

<sub>C API: LLVMSetWeak</sub>)")
      // Tail call kind
      .def_prop_ro("tail_call_kind", &LLVMValueWrapper::get_tail_call_kind,
                   R"(Get tail call kind.

<sub>C API: LLVMGetTailCallKind</sub>)")
      .def("set_tail_call_kind", &LLVMValueWrapper::set_tail_call_kind,
           "kind"_a, R"(Set tail call kind.

<sub>C API: LLVMSetTailCallKind</sub>)")
      // Called function
      .def_prop_ro("called_function_type",
                   &LLVMValueWrapper::get_called_function_type,
                   R"(Get called function type.

<sub>C API: LLVMGetCalledFunctionType</sub>)")
      .def_prop_ro("called_value", &LLVMValueWrapper::get_called_value,
                   R"(Get called value.

<sub>C API: LLVMGetCalledValue</sub>)")
      .def("set_called_operand", &LLVMValueWrapper::set_called_operand,
           "val"_a,
           R"(Set the called operand (callee) of a call/invoke instruction.
The callee is the last operand of a CallBase instruction.

<sub>C API: LLVMSetOperand</sub>)")
      .def_prop_rw("instruction_call_conv",
          &LLVMValueWrapper::get_instruction_call_conv,
          &LLVMValueWrapper::set_instruction_call_conv,
          R"(Get/set the calling convention of a call/invoke instruction.

<sub>C API: LLVMGetInstructionCallConv, LLVMSetInstructionCallConv</sub>)")
      // Landing pad properties
      .def_prop_ro("num_clauses", &LLVMValueWrapper::get_num_clauses,
                   R"(Get number of clauses.

<sub>C API: LLVMGetNumClauses</sub>)")
      .def("get_clause", &LLVMValueWrapper::get_clause, "index"_a,
           R"(Get clause at index.

<sub>C API: LLVMGetClause</sub>)")
      .def_prop_ro("is_cleanup", &LLVMValueWrapper::is_cleanup,
                   R"(Check if cleanup.

<sub>C API: LLVMIsCleanup</sub>)")
      .def("set_cleanup", &LLVMValueWrapper::set_cleanup, "is_cleanup"_a,
           R"(Set cleanup flag.

<sub>C API: LLVMSetCleanup</sub>)")
      // Catch switch/pad properties
      .def_prop_ro("parent_catch_switch",
                   &LLVMValueWrapper::get_parent_catch_switch,
                   R"(Get parent catch switch.

<sub>C API: LLVMGetParentCatchSwitch</sub>)")
      .def_prop_ro("num_handlers", &LLVMValueWrapper::get_num_handlers,
                   R"(Get number of handlers.

<sub>C API: LLVMGetNumHandlers</sub>)")
      // Shuffle vector mask
      .def_prop_ro("num_mask_elements",
                   &LLVMValueWrapper::get_num_mask_elements,
                   R"(Get number of mask elements.

<sub>C API: LLVMGetNumMaskElements</sub>)")
      .def("get_mask_value", &LLVMValueWrapper::get_mask_value, "index"_a,
           R"(Get mask value at index.

<sub>C API: LLVMGetMaskValue</sub>)")
      // Fast-math flags
      .def_prop_ro("can_use_fast_math_flags",
                   &LLVMValueWrapper::can_use_fast_math_flags,
                   R"(Can use fast-math flags.

<sub>C API: LLVMCanValueUseFastMathFlags</sub>)")
      .def_prop_ro("fast_math_flags", &LLVMValueWrapper::get_fast_math_flags,
                   R"(Get fast-math flags.

<sub>C API: LLVMGetFastMathFlags</sub>)")
      .def("set_fast_math_flags", &LLVMValueWrapper::set_fast_math_flags,
           "flags"_a, R"(Set fast-math flags.

<sub>C API: LLVMSetFastMathFlags</sub>)")
      // Call instruction arg operand
      .def("get_arg_operand", &LLVMValueWrapper::get_arg_operand, "index"_a,
           R"(Get arg operand at index.

<sub>C API: LLVMGetArgOperand</sub>)")
      // Instruction manipulation
      .def("remove_from_parent", &LLVMValueWrapper::remove_from_parent,
           R"(Remove instruction from parent.

<sub>C API: LLVMInstructionRemoveFromParent</sub>)")
      .def("move_before", &LLVMValueWrapper::move_before, "other"_a,
           "preserve"_a = false,
           R"(Move this instruction before another instruction.

<sub>C API: LLVMInstructionRemoveFromParent, LLVMInsertIntoBuilder</sub>)")
      .def("move_after", &LLVMValueWrapper::move_after, "other"_a,
           "preserve"_a = false,
           R"(Move this instruction after another instruction.

<sub>C API: LLVMInstructionRemoveFromParent, LLVMInsertIntoBuilder</sub>)")
      .def_prop_ro("is_instruction", &LLVMValueWrapper::is_a_instruction,
                   R"(Check if instruction.

<sub>C API: LLVMIsAInstruction</sub>)")
      .def_prop_ro("is_terminator",
                   &LLVMValueWrapper::is_a_terminator_inst,
                   R"(Check if terminator.

<sub>C API: LLVMIsATerminatorInst</sub>)")
      .def_prop_ro("is_argument", &LLVMValueWrapper::is_a_argument,
                   R"(Check if argument.

<sub>C API: LLVMIsAArgument</sub>)")
      // Parent navigation: value -> block -> function -> module -> context
      .def_prop_ro("block", &LLVMValueWrapper::block,
                   R"(Get the basic block this instruction belongs to.

<sub>C API: LLVMGetInstructionParent</sub>)")
      .def_prop_ro("parent", &LLVMValueWrapper::block,
                   R"(Get the parent basic block this instruction belongs to.

Alias for `.block`.

<sub>C API: LLVMGetInstructionParent</sub>)")
      .def_prop_ro("function", &LLVMValueWrapper::get_function,
                   R"(Get the function this value belongs to.

<sub>C API: LLVMGetBasicBlockParent, LLVMGetParamParent</sub>)")
      .def_prop_ro("module", &LLVMValueWrapper::get_module,
                   R"(Get the module this value belongs to.

<sub>C API: LLVMGetGlobalParent</sub>)",
                   nb::rv_policy::take_ownership)
      .def_prop_ro("context", &LLVMValueWrapper::get_context,
                   R"(Get the context this value belongs to.

<sub>C API: LLVMGetTypeContext</sub>)",
                   nb::rv_policy::take_ownership)
      // BasicBlock properties
      .def_prop_ro("normal_dest", &LLVMValueWrapper::get_normal_dest,
                   R"(Get normal destination.

<sub>C API: LLVMGetNormalDest</sub>)")
      .def_prop_ro("unwind_dest", &LLVMValueWrapper::get_unwind_dest,
                   R"(Get unwind destination.

<sub>C API: LLVMGetUnwindDest</sub>)")
      .def_prop_ro("callbr_default_dest",
                   &LLVMValueWrapper::get_callbr_default_dest,
                   R"(Get callbr default dest.

<sub>C API: LLVMGetCallBrDefaultDest</sub>)")
      .def_prop_ro("callbr_num_indirect_dests",
                   &LLVMValueWrapper::get_callbr_num_indirect_dests,
                   R"(Get callbr indirect dest count.

<sub>C API: LLVMGetCallBrNumIndirectDests</sub>)")
      .def("get_callbr_indirect_dest",
           &LLVMValueWrapper::get_callbr_indirect_dest, "index"_a,
           R"(Get callbr indirect dest.

<sub>C API: LLVMGetCallBrIndirectDest</sub>)")
      // Value/BasicBlock conversion
      .def_prop_ro("value_is_basic_block",
                   &LLVMValueWrapper::value_is_basic_block,
                   R"(Check if value is basic block.

<sub>C API: LLVMValueIsBasicBlock</sub>)")
      .def("value_as_basic_block", &LLVMValueWrapper::value_as_basic_block,
           R"(Convert value to basic block.

<sub>C API: LLVMValueAsBasicBlock</sub>)")
      // Landing pad and catch switch operations
      .def("add_clause", &LLVMValueWrapper::add_clause, "clause_val"_a,
           R"(Add landing pad clause.

<sub>C API: LLVMAddClause</sub>)")
      .def("add_handler", &LLVMValueWrapper::add_handler, "handler"_a,
           R"(Add catch switch handler.

<sub>C API: LLVMAddHandler</sub>)")
      .def_prop_ro("handlers", &LLVMValueWrapper::get_handlers,
                   R"(Get all handlers.

<sub>C API: LLVMGetHandlers</sub>)")
      .def("get_operand_bundle_at_index",
           &LLVMValueWrapper::get_operand_bundle_at_index, "index"_a,
           R"(Get operand bundle at index.

<sub>C API: LLVMGetOperandBundleAtIndex</sub>)")
      .def_prop_ro(
          "indices", &LLVMValueWrapper::get_indices,
          R"(Get the indices for GEP or ExtractValue/InsertValue instructions.

<sub>C API: LLVMGetIndices</sub>)")
      // Global/instruction metadata
      .def("global_copy_all_metadata",
           &LLVMValueWrapper::global_copy_all_metadata,
           R"(Copy all metadata.

<sub>C API: LLVMGlobalCopyAllMetadata</sub>)")
      .def("instruction_get_all_metadata_other_than_debug_loc",
           &LLVMValueWrapper::instruction_get_all_metadata_other_than_debug_loc,
           R"(Get all metadata except debug loc.

<sub>C API: LLVMInstructionGetAllMetadataOtherThanDebugLoc</sub>)")
      // Value methods
      .def("const_bitcast", &LLVMValueWrapper::const_bitcast, "type"_a,
           R"(Create constant bitcast.

<sub>C API: LLVMConstBitCast</sub>)")
      .def("as_metadata", &LLVMValueWrapper::as_metadata,
           R"(Convert to metadata.

<sub>C API: LLVMValueAsMetadata</sub>)")
      .def("delete_instruction", &LLVMValueWrapper::delete_instruction,
           R"(Delete this instruction from its parent block.

<sub>C API: LLVMDeleteInstruction</sub>)")
      .def("erase_from_parent", &LLVMValueWrapper::erase_from_parent,
           R"(Remove this instruction from its parent block and delete it.
Combines remove_from_parent() + delete_instruction() atomically.

<sub>C API: LLVMInstructionEraseFromParent</sub>)")
      .def("instruction_clone", &LLVMValueWrapper::instruction_clone,
           R"(Clone an instruction. The clone has no parent and no name.

<sub>C API: LLVMInstructionClone</sub>)")
      .def("replace_all_uses_with",
           &LLVMValueWrapper::replace_all_uses_with, "new_value"_a,
           R"(Replace all uses of this value with another value.

<sub>C API: LLVMReplaceAllUsesWith</sub>)")
      // Callsite attribute methods (for call/invoke instructions)
      .def("get_callsite_attribute_count",
           &LLVMValueWrapper::get_callsite_attribute_count, "idx"_a,
           R"(Get the number of attributes at a call site index.

Valid when:
  - value is a call/invoke/callbr instruction
  - -1 <= idx <= num_arg_operands

<sub>C API: LLVMGetCallSiteAttributeCount</sub>)")
      .def("get_callsite_enum_attribute",
           &LLVMValueWrapper::get_callsite_enum_attribute, "idx"_a, "kind_id"_a,
           R"(Get an enum attribute at a call site index (None if not found).

Valid when:
  - value is a call/invoke/callbr instruction
  - -1 <= idx <= num_arg_operands

<sub>C API: LLVMGetCallSiteEnumAttribute</sub>)")
      .def("add_callsite_attribute", &LLVMValueWrapper::add_callsite_attribute,
           "idx"_a, "attr"_a,
           R"(Add an attribute to a call site at the given index.

Valid when:
  - value is a call/invoke/callbr instruction
  - -1 <= idx <= num_arg_operands

<sub>C API: LLVMAddCallSiteAttribute</sub>)")
      // Unified metadata method
      .def("set_metadata", &LLVMValueWrapper::set_metadata, "kind"_a, "md"_a,
           "ctx"_a,
           R"(Set metadata on value.

<sub>C API: LLVMSetMetadata, LLVMGlobalSetMetadata</sub>)")
      // Builder creation for instructions (TODO: add to module too)
      .def("create_builder", &LLVMValueWrapper::create_builder, nb::kw_only(),
           "before_dbg"_a = false, nb::rv_policy::take_ownership,
           R"(Create a Builder positioned before this Instruction.

Valid when:
  - this value is an instruction
  - the instruction belongs to a basic block (is inserted)

Args:
  before_dbg: If True, insert before debug records.
              If False, insert after debug records but before the instruction.

Returns:
  A BuilderManager for use with Python's 'with' statement.

<sub>C API: LLVMCreateBuilderInContext, LLVMPositionBuilderBefore</sub>)");

  // BasicBlock wrapper
  nb::class_<LLVMBasicBlockWrapper>(m, "BasicBlock")
      .def("__eq__", [](const LLVMBasicBlockWrapper &a,
                        const LLVMBasicBlockWrapper &b) { return a == b; })
      .def("__ne__", [](const LLVMBasicBlockWrapper &a,
                        const LLVMBasicBlockWrapper &b) { return a != b; })
      .def("__hash__",
           [](const LLVMBasicBlockWrapper &v) {
             return std::hash<LLVMBasicBlockRef>{}(v.m_ref);
           })
      .def_prop_ro("name", &LLVMBasicBlockWrapper::get_name,
                   R"(Block name.

<sub>C API: LLVMGetBasicBlockName</sub>)")
      .def("as_value", &LLVMBasicBlockWrapper::as_value,
           R"(Get as value.

<sub>C API: LLVMBasicBlockAsValue</sub>)")
      .def("__str__", &LLVMBasicBlockWrapper::to_string)
      .def("__repr__", &LLVMBasicBlockWrapper::to_string)
      .def_prop_ro("next_block", &LLVMBasicBlockWrapper::next_block,
                   R"(Next block.

<sub>C API: LLVMGetNextBasicBlock</sub>)")
      .def_prop_ro("prev_block", &LLVMBasicBlockWrapper::prev_block,
                   R"(Previous block.

<sub>C API: LLVMGetPreviousBasicBlock</sub>)")
      .def_prop_ro("has_terminator", &LLVMBasicBlockWrapper::has_terminator,
                   R"(Check if this basic block has a terminator instruction.

<sub>C API: LLVMGetBasicBlockTerminator</sub>)")
      .def_prop_ro("terminator", &LLVMBasicBlockWrapper::terminator,
                   R"(Get terminator.

<sub>C API: LLVMGetBasicBlockTerminator</sub>)")
      .def_prop_ro("phis", &LLVMBasicBlockWrapper::phis,
                   R"(Get all PHI instructions at the beginning of this block.)")
      .def_prop_ro("uses", &LLVMBasicBlockWrapper::uses,
                   R"(Get all uses of this basic block.)")
      .def_prop_ro("users", &LLVMBasicBlockWrapper::users,
                   R"(Get all users of this basic block.)")
      .def_prop_ro("first_non_phi", &LLVMBasicBlockWrapper::first_non_phi,
                   R"(Get the first instruction that is not a PHI node.

Returns None when the block has no non-PHI instruction.)")
      .def_prop_ro("first_instruction",
                   &LLVMBasicBlockWrapper::first_instruction,
                   R"(First instruction.

<sub>C API: LLVMGetFirstInstruction</sub>)")
      .def_prop_ro("last_instruction", &LLVMBasicBlockWrapper::last_instruction,
                   R"(Last instruction.

<sub>C API: LLVMGetLastInstruction</sub>)")
      .def_prop_ro("instructions", &LLVMBasicBlockWrapper::instructions,
                   R"(All instructions.

<sub>C API: LLVMGetFirstInstruction, LLVMGetNextInstruction</sub>)")
      // Parent navigation
      .def_prop_ro("function", &LLVMBasicBlockWrapper::function,
                   R"(Parent function.

<sub>C API: LLVMGetBasicBlockParent</sub>)")
      .def_prop_ro("module", &LLVMBasicBlockWrapper::module,
                   R"(Parent module.

<sub>C API: LLVMGetGlobalParent</sub>)",
                   nb::rv_policy::take_ownership)
      .def_prop_ro("context", &LLVMBasicBlockWrapper::context,
                   R"(Parent context.

<sub>C API: LLVMGetTypeContext</sub>)",
                   nb::rv_policy::take_ownership)
      .def_prop_ro("successors", &LLVMBasicBlockWrapper::successors,
                   R"(Successor blocks.

<sub>C API: LLVMGetNumSuccessors, LLVMGetSuccessor</sub>)")
      .def_prop_ro("predecessors", &LLVMBasicBlockWrapper::predecessors,
                   R"(Predecessor blocks.

<sub>C API: LLVMGetNumPredecessors</sub>)")
      .def("split_basic_block", &LLVMBasicBlockWrapper::split_basic_block,
           "instruction"_a, "name"_a = "",
           R"(Split this basic block at the given instruction.

The `instruction` itself is included in the moved range.
All instructions from `instruction` through the original terminator are
moved into a new successor block.

The original block is terminated with an unconditional branch to the
new block. PHI nodes in successor blocks are updated to reference the
new block as predecessor where needed.

Use `split_basic_block_before()` when you want a new predecessor block
and want `instruction` to remain in the original block.

Returns the new basic block.

<sub>C++ API: BasicBlock::splitBasicBlock</sub>)")
      .def("split_basic_block_before",
           &LLVMBasicBlockWrapper::split_basic_block_before, "instruction"_a,
           "name"_a = "",
           R"(Split this basic block before the given instruction.

The `instruction` itself stays in the original block.
All instructions before `instruction` are moved into a new predecessor
block.

Existing predecessors are redirected to the new block, and the new
block unconditionally branches to this block.

Use `split_basic_block()` when you want `instruction` included in the
moved range and a new successor block.

Returns the new predecessor basic block.

<sub>C++ API: BasicBlock::splitBasicBlockBefore</sub>)")
      .def("move_before", &LLVMBasicBlockWrapper::move_before, "other"_a,
           R"(Move before block.

<sub>C API: LLVMMoveBasicBlockBefore</sub>)")
      .def("move_after", &LLVMBasicBlockWrapper::move_after, "other"_a,
           R"(Move after block.

<sub>C API: LLVMMoveBasicBlockAfter</sub>)")
      .def(
          "create_builder", &LLVMBasicBlockWrapper::create_builder,
          nb::kw_only(), "first_non_phi"_a = false,
          nb::rv_policy::take_ownership,
          R"(Create a Builder in this BasicBlock.

By default, positions at block end.

With `first_non_phi=True`, positions before the first non-PHI instruction.
If the block has no non-PHI instruction (empty block or PHI-only block),
the builder is positioned at the end of the block.

Returns a BuilderManager for use with Python's 'with' statement.

<sub>C API: LLVMCreateBuilderInContext</sub>)");

  // Function wrapper
  nb::class_<LLVMFunctionWrapper, LLVMValueWrapper>(m, "Function")
      .def_prop_ro("param_count", &LLVMFunctionWrapper::param_count,
                   R"(Parameter count.

<sub>C API: LLVMCountParams</sub>)")
      .def("get_param", &LLVMFunctionWrapper::get_param, "index"_a,
           R"(Get parameter.

<sub>C API: LLVMGetParam</sub>)")
      .def_prop_ro("params", &LLVMFunctionWrapper::params,
                   R"(All parameters.

<sub>C API: LLVMGetParams</sub>)")
      .def_prop_rw("linkage", &LLVMFunctionWrapper::get_linkage,
                   &LLVMFunctionWrapper::set_linkage,
                   R"(Linkage.

<sub>C API: LLVMGetLinkage, LLVMSetLinkage</sub>)")
      .def_prop_rw("calling_conv", &LLVMFunctionWrapper::get_calling_conv,
                   &LLVMFunctionWrapper::set_calling_conv,
                   R"(Calling convention.

<sub>C API: LLVMGetFunctionCallConv, LLVMSetFunctionCallConv</sub>)")
      .def(
          "append_basic_block",
          [](LLVMFunctionWrapper &self, const std::string &name) {
            return self.append_basic_block(name);
          },
          "name"_a = "",
          R"(Append basic block.

<sub>C API: LLVMAppendBasicBlockInContext</sub>)")
      .def_prop_ro("entry_block", &LLVMFunctionWrapper::entry_block,
                   R"(Entry block.

Valid when:
  - is_declaration is False

<sub>C API: LLVMGetEntryBasicBlock</sub>)")
      .def_prop_ro("basic_block_count", &LLVMFunctionWrapper::basic_block_count,
                   R"(Block count.

<sub>C API: LLVMCountBasicBlocks</sub>)")
      .def_prop_ro("first_basic_block", &LLVMFunctionWrapper::first_basic_block,
                   R"(First block.

Valid when:
  - basic_block_count > 0

<sub>C API: LLVMGetFirstBasicBlock</sub>)")
      .def_prop_ro("last_basic_block", &LLVMFunctionWrapper::last_basic_block,
                   R"(Last block.

Valid when:
  - basic_block_count > 0

<sub>C API: LLVMGetLastBasicBlock</sub>)")
      .def_prop_ro("basic_blocks", &LLVMFunctionWrapper::basic_blocks,
                   R"(All blocks.

<sub>C API: LLVMGetBasicBlocks</sub>)")
      .def("append_existing_basic_block",
           &LLVMFunctionWrapper::append_existing_basic_block, "bb"_a,
           R"(Append existing block.

Valid when:
  - bb is unattached (has no parent function)

<sub>C API: LLVMAppendExistingBasicBlock</sub>)")
      .def("erase", &LLVMFunctionWrapper::erase,
           R"(Erase function.

<sub>C API: LLVMDeleteFunction</sub>)")
      .def("delete", &LLVMFunctionWrapper::erase,
           R"(Delete function (alias for erase).

<sub>C API: LLVMDeleteFunction</sub>)")
      // Parameter iteration
      .def("first_param", &LLVMFunctionWrapper::first_param,
           R"(First parameter.

<sub>C API: LLVMGetFirstParam</sub>)")
      .def("last_param", &LLVMFunctionWrapper::last_param,
           R"(Last parameter.

<sub>C API: LLVMGetLastParam</sub>)")
      // Function iteration
      .def_prop_ro("next_function", &LLVMFunctionWrapper::next_function,
                   R"(Next function.

<sub>C API: LLVMGetNextFunction</sub>)")
      .def_prop_ro("prev_function", &LLVMFunctionWrapper::prev_function,
                   R"(Previous function.

<sub>C API: LLVMGetPreviousFunction</sub>)")
      // Attribute methods
      .def("get_attribute_count", &LLVMFunctionWrapper::get_attribute_count,
           "idx"_a, R"(Attribute count.

Valid when:
  - -1 <= idx <= param_count
  - idx=-1 is function attrs, idx=0 is return attrs, idx>=1 are parameter attrs

<sub>C API: LLVMGetAttributeCountAtIndex</sub>)")
      .def("get_enum_attribute", &LLVMFunctionWrapper::get_enum_attribute,
           "idx"_a, "kind_id"_a,
           R"(Get enum attribute.

Valid when:
  - -1 <= idx <= param_count
  - idx=-1 is function attrs, idx=0 is return attrs, idx>=1 are parameter attrs

<sub>C API: LLVMGetEnumAttributeAtIndex</sub>)")
      .def("add_attribute", &LLVMFunctionWrapper::add_attribute, "idx"_a,
           "attr"_a, R"(Add attribute.

Valid when:
  - -1 <= idx <= param_count
  - idx=-1 is function attrs, idx=0 is return attrs, idx>=1 are parameter attrs

<sub>C API: LLVMAddAttributeAtIndex</sub>)")
      .def("get_attributes", &LLVMFunctionWrapper::get_attributes, "idx"_a,
           R"(Get all attributes.

Valid when:
  - -1 <= idx <= param_count
  - idx=-1 is function attrs, idx=0 is return attrs, idx>=1 are parameter attrs

<sub>C API: LLVMGetAttributesAtIndex</sub>)")
      .def("get_string_attribute", &LLVMFunctionWrapper::get_string_attribute,
           "idx"_a, "key"_a,
           R"(Get string attribute.

Valid when:
  - -1 <= idx <= param_count
  - idx=-1 is function attrs, idx=0 is return attrs, idx>=1 are parameter attrs

<sub>C API: LLVMGetStringAttributeAtIndex</sub>)")
      .def("remove_enum_attribute", &LLVMFunctionWrapper::remove_enum_attribute,
           "idx"_a, "kind_id"_a,
           R"(Remove enum attribute.

Valid when:
  - -1 <= idx <= param_count
  - idx=-1 is function attrs, idx=0 is return attrs, idx>=1 are parameter attrs

<sub>C API: LLVMRemoveEnumAttributeAtIndex</sub>)")
      .def("remove_string_attribute",
           &LLVMFunctionWrapper::remove_string_attribute, "idx"_a, "key"_a,
           R"(Remove string attribute.

Valid when:
  - -1 <= idx <= param_count
  - idx=-1 is function attrs, idx=0 is return attrs, idx>=1 are parameter attrs

<sub>C API: LLVMRemoveStringAttributeAtIndex</sub>)")
      .def("add_target_attribute", &LLVMFunctionWrapper::add_target_attribute,
           "key"_a, "value"_a,
           R"(Add target attribute.

<sub>C API: LLVMAddTargetDependentFunctionAttr</sub>)")
      // Debug info
      .def("set_subprogram", &LLVMFunctionWrapper::set_subprogram, "sp"_a,
           R"(Set debug info subprogram metadata for this function.

<sub>C API: LLVMSetSubprogram</sub>)")
      // Parent navigation
      .def_prop_ro("module", &LLVMFunctionWrapper::module,
                   R"(Parent module.

<sub>C API: LLVMGetGlobalParent</sub>)",
                   nb::rv_policy::take_ownership)
      .def_prop_ro("context", &LLVMFunctionWrapper::context,
                   R"(Parent context.

<sub>C API: LLVMGetTypeContext</sub>)",
                   nb::rv_policy::take_ownership)
      // Verification
      .def("verify", &LLVMFunctionWrapper::verify,
           R"(Verify function.

<sub>C API: LLVMVerifyFunction</sub>
           
           Returns True if the function is valid, False otherwise.
           <sub>C API: LLVMVerifyFunction</sub>)")
      .def("verify_and_print", &LLVMFunctionWrapper::verify_and_print,
           R"(Verify function and print errors to stderr.

<sub>C API: LLVMVerifyFunction</sub>)")
      // =====================================================================
      // Intrinsic functions
      // =====================================================================
      .def_prop_ro("intrinsic_id", &LLVMFunctionWrapper::intrinsic_id,
                   R"(Get the intrinsic ID for this function.
           
           Returns 0 if the function is not an intrinsic.

<sub>C API: LLVMGetIntrinsicID</sub>)")
      .def_prop_ro("is_intrinsic", &LLVMFunctionWrapper::is_intrinsic,
                   R"(Check if intrinsic.

<sub>C API: LLVMGetIntrinsicID</sub>)")
      // =====================================================================
      // Personality function (for exception handling)
      // =====================================================================
      .def_prop_ro("has_personality_fn",
                   &LLVMFunctionWrapper::has_personality_fn,
                   R"(Check if this function has a personality function.

<sub>C API: LLVMHasPersonalityFn</sub>)")
      .def("get_personality_fn", &LLVMFunctionWrapper::get_personality_fn,
           R"(Get the personality function.

Valid when:
  - has_personality_fn is True

<sub>C API: LLVMGetPersonalityFn</sub>)")
      .def("set_personality_fn", &LLVMFunctionWrapper::set_personality_fn,
           "fn"_a,
           R"(Set the personality function.

<sub>C API: LLVMSetPersonalityFn</sub>)")
      // =====================================================================
      // GC name
      // =====================================================================
      .def("get_gc", &LLVMFunctionWrapper::get_gc,
           R"(Get the GC name for this function.
           
           Returns None if no GC is set.

<sub>C API: LLVMGetGC</sub>)")
      .def("set_gc", &LLVMFunctionWrapper::set_gc, "gc"_a,
           R"(Set the GC name for this function.

<sub>C API: LLVMSetGC</sub>)")
      .def("block_address", &LLVMFunctionWrapper::block_address, "bb"_a,
           R"(Create a BlockAddress constant for a basic block in this function.
Used for computed goto (indirect branch) support.

Valid when:
  - bb is owned by this function

<sub>C API: LLVMBlockAddress</sub>)");

  // Builder wrapper
  nb::class_<LLVMBuilderWrapper>(m, "Builder")
      .def("position_at_end", &LLVMBuilderWrapper::position_at_end, "bb"_a,
           nb::kw_only(), "before_dbg"_a = false,
           R"(Position at end of block.

<sub>C API: LLVMPositionBuilderAtEnd</sub>)")
      .def("position_before", &LLVMBuilderWrapper::position_before, "inst"_a,
           nb::kw_only(), "before_dbg"_a = false,
           R"(Position before instruction.

Valid when:
  - inst is an instruction value
  - inst belongs to a basic block

<sub>C API: LLVMPositionBuilderBefore</sub>)")
      .def("position_at", &LLVMBuilderWrapper::position_at, "bb"_a, "inst"_a,
           R"(Position at instruction.

Valid when:
  - inst is an instruction value
  - inst belongs to bb

<sub>C API: LLVMPositionBuilder</sub>)")
      .def("clear_insertion_position",
           &LLVMBuilderWrapper::clear_insertion_position,
           R"(Clear insertion position.

<sub>C API: LLVMClearInsertionPosition</sub>)")
      .def_prop_ro("insert_block", &LLVMBuilderWrapper::insert_block,
                   R"(Get current insert block.

<sub>C API: LLVMGetInsertBlock</sub>)")
      // Arithmetic
      .def("add", &LLVMBuilderWrapper::add, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build add.

<sub>C API: LLVMBuildAdd</sub>)")
      .def("nsw_add", &LLVMBuilderWrapper::nsw_add, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build nsw add.

<sub>C API: LLVMBuildNSWAdd</sub>)")
      .def("nuw_add", &LLVMBuilderWrapper::nuw_add, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build nuw add.

<sub>C API: LLVMBuildNUWAdd</sub>)")
      .def("sub", &LLVMBuilderWrapper::sub, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build sub.

<sub>C API: LLVMBuildSub</sub>)")
      .def("nsw_sub", &LLVMBuilderWrapper::nsw_sub, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build nsw sub.

<sub>C API: LLVMBuildNSWSub</sub>)")
      .def("nuw_sub", &LLVMBuilderWrapper::nuw_sub, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build nuw sub.

<sub>C API: LLVMBuildNUWSub</sub>)")
      .def("mul", &LLVMBuilderWrapper::mul, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build mul.

<sub>C API: LLVMBuildMul</sub>)")
      .def("nsw_mul", &LLVMBuilderWrapper::nsw_mul, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build nsw mul.

<sub>C API: LLVMBuildNSWMul</sub>)")
      .def("nuw_mul", &LLVMBuilderWrapper::nuw_mul, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build nuw mul.

<sub>C API: LLVMBuildNUWMul</sub>)")
      .def("sdiv", &LLVMBuilderWrapper::sdiv, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build sdiv.

<sub>C API: LLVMBuildSDiv</sub>)")
      .def("udiv", &LLVMBuilderWrapper::udiv, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build udiv.

<sub>C API: LLVMBuildUDiv</sub>)")
      .def("exact_sdiv", &LLVMBuilderWrapper::exact_sdiv, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build exact sdiv.

<sub>C API: LLVMBuildExactSDiv</sub>)")
      .def("exact_udiv", &LLVMBuilderWrapper::exact_udiv, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build exact udiv.

<sub>C API: LLVMBuildExactUDiv</sub>)")
      .def("srem", &LLVMBuilderWrapper::srem, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build srem.

<sub>C API: LLVMBuildSRem</sub>)")
      .def("urem", &LLVMBuilderWrapper::urem, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build urem.

<sub>C API: LLVMBuildURem</sub>)")
      // Float arithmetic
      .def("fadd", &LLVMBuilderWrapper::fadd, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build fadd.

<sub>C API: LLVMBuildFAdd</sub>)")
      .def("fsub", &LLVMBuilderWrapper::fsub, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build fsub.

<sub>C API: LLVMBuildFSub</sub>)")
      .def("fmul", &LLVMBuilderWrapper::fmul, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build fmul.

<sub>C API: LLVMBuildFMul</sub>)")
      .def("fdiv", &LLVMBuilderWrapper::fdiv, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build fdiv.

<sub>C API: LLVMBuildFDiv</sub>)")
      .def("frem", &LLVMBuilderWrapper::frem, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build frem.

<sub>C API: LLVMBuildFRem</sub>)")
      // Unary
      .def("neg", &LLVMBuilderWrapper::neg, "val"_a, "name"_a = "",
           R"(Build neg.

<sub>C API: LLVMBuildNeg</sub>)")
      .def("nsw_neg", &LLVMBuilderWrapper::nsw_neg, "val"_a, "name"_a = "",
           R"(Build nsw neg.

<sub>C API: LLVMBuildNSWNeg</sub>)")
      .def("fneg", &LLVMBuilderWrapper::fneg, "val"_a, "name"_a = "",
           R"(Build fneg.

<sub>C API: LLVMBuildFNeg</sub>)")
      .def("not_", &LLVMBuilderWrapper::not_, "val"_a, "name"_a = "",
           R"(Build not.

<sub>C API: LLVMBuildNot</sub>)")
      // Bitwise
      .def("shl", &LLVMBuilderWrapper::shl, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build shl.

<sub>C API: LLVMBuildShl</sub>)")
      .def("lshr", &LLVMBuilderWrapper::lshr, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build lshr.

<sub>C API: LLVMBuildLShr</sub>)")
      .def("ashr", &LLVMBuilderWrapper::ashr, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build ashr.

<sub>C API: LLVMBuildAShr</sub>)")
      .def("and_", &LLVMBuilderWrapper::and_, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build and.

<sub>C API: LLVMBuildAnd</sub>)")
      .def("or_", &LLVMBuilderWrapper::or_, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build or.

<sub>C API: LLVMBuildOr</sub>)")
      .def("xor", &LLVMBuilderWrapper::xor_, "lhs"_a, "rhs"_a, "name"_a = "",
           R"(Build xor.

<sub>C API: LLVMBuildXor</sub>)")
      .def("binop", &LLVMBuilderWrapper::binop, "opcode"_a, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build binary op.

<sub>C API: LLVMBuildBinOp</sub>)")
      // Memory
      .def("alloca", &LLVMBuilderWrapper::build_alloca, "ty"_a, "name"_a = "",
           R"(Build alloca.

<sub>C API: LLVMBuildAlloca</sub>)")
      .def("array_alloca", &LLVMBuilderWrapper::build_array_alloca, "ty"_a,
           "size"_a, "name"_a = "",
           R"(Build array alloca.

<sub>C API: LLVMBuildArrayAlloca</sub>)")
      .def("load", &LLVMBuilderWrapper::load, "ty"_a, "ptr"_a, "name"_a = "",
           R"(Build load.

<sub>C API: LLVMBuildLoad2</sub>)")
      .def("store", &LLVMBuilderWrapper::store, "val"_a, "ptr"_a,
           R"(Build store.

<sub>C API: LLVMBuildStore</sub>)")
      .def("gep", &LLVMBuilderWrapper::gep, "ty"_a, "ptr"_a, "indices"_a,
           "name"_a = "",
           R"(Build a GetElementPtr instruction for pointer arithmetic.

<sub>C API: LLVMBuildGEP2</sub>)")
      .def(
          "inbounds_gep", &LLVMBuilderWrapper::inbounds_gep, "ty"_a, "ptr"_a,
          "indices"_a, "name"_a = "",
          R"(Build an inbounds GetElementPtr instruction (asserts pointer stays in bounds).

<sub>C API: LLVMBuildInBoundsGEP2</sub>)")
      .def("struct_gep", &LLVMBuilderWrapper::struct_gep, "ty"_a, "ptr"_a,
           "idx"_a, "name"_a = "",
           R"(Build a GEP to access a struct field by index.

<sub>C API: LLVMBuildStructGEP2</sub>)")
      // Comparisons
      .def("icmp", &LLVMBuilderWrapper::icmp, "pred"_a, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build icmp.

<sub>C API: LLVMBuildICmp</sub>)")
      .def("fcmp", &LLVMBuilderWrapper::fcmp, "pred"_a, "lhs"_a, "rhs"_a,
           "name"_a = "", R"(Build fcmp.

<sub>C API: LLVMBuildFCmp</sub>)")
      .def("select", &LLVMBuilderWrapper::select, "cond"_a, "then_val"_a,
           "else_val"_a, "name"_a = "",
           R"(Build select.

<sub>C API: LLVMBuildSelect</sub>)")
      // Casts
      .def("trunc", &LLVMBuilderWrapper::trunc, "val"_a, "ty"_a, "name"_a = "",
           R"(Build trunc.

<sub>C API: LLVMBuildTrunc</sub>)")
      .def("zext", &LLVMBuilderWrapper::zext, "val"_a, "ty"_a, "name"_a = "",
           R"(Build zext.

<sub>C API: LLVMBuildZExt</sub>)")
      .def("sext", &LLVMBuilderWrapper::sext, "val"_a, "ty"_a, "name"_a = "",
           R"(Build sext.

<sub>C API: LLVMBuildSExt</sub>)")
      .def("fptrunc", &LLVMBuilderWrapper::fptrunc, "val"_a, "ty"_a,
           "name"_a = "", R"(Build fptrunc.

<sub>C API: LLVMBuildFPTrunc</sub>)")
      .def("fpext", &LLVMBuilderWrapper::fpext, "val"_a, "ty"_a, "name"_a = "",
           R"(Build fpext.

<sub>C API: LLVMBuildFPExt</sub>)")
      .def("fptosi", &LLVMBuilderWrapper::fptosi, "val"_a, "ty"_a,
           "name"_a = "", R"(Build fptosi.

<sub>C API: LLVMBuildFPToSI</sub>)")
      .def("fptoui", &LLVMBuilderWrapper::fptoui, "val"_a, "ty"_a,
           "name"_a = "", R"(Build fptoui.

<sub>C API: LLVMBuildFPToUI</sub>)")
      .def("sitofp", &LLVMBuilderWrapper::sitofp, "val"_a, "ty"_a,
           "name"_a = "", R"(Build sitofp.

<sub>C API: LLVMBuildSIToFP</sub>)")
      .def("uitofp", &LLVMBuilderWrapper::uitofp, "val"_a, "ty"_a,
           "name"_a = "", R"(Build uitofp.

<sub>C API: LLVMBuildUIToFP</sub>)")
      .def("ptrtoint", &LLVMBuilderWrapper::ptrtoint, "val"_a, "ty"_a,
           "name"_a = "", R"(Build ptrtoint.

<sub>C API: LLVMBuildPtrToInt</sub>)")
      .def("inttoptr", &LLVMBuilderWrapper::inttoptr, "val"_a, "ty"_a,
           "name"_a = "", R"(Build inttoptr.

<sub>C API: LLVMBuildIntToPtr</sub>)")
      .def("bitcast", &LLVMBuilderWrapper::bitcast, "val"_a, "ty"_a,
           "name"_a = "", R"(Build bitcast.

<sub>C API: LLVMBuildBitCast</sub>)")
      .def("int_cast2", &LLVMBuilderWrapper::int_cast2, "val"_a, "ty"_a,
           "is_signed"_a, "name"_a = "",
           R"(Build int cast.

<sub>C API: LLVMBuildIntCast2</sub>)")
      .def("addr_space_cast", &LLVMBuilderWrapper::addr_space_cast, "val"_a,
           "ty"_a, "name"_a = "",
           R"(Cast a pointer to a different address space.

<sub>C API: LLVMBuildAddrSpaceCast</sub>)")
      // Convenience casts
      .def("zext_or_bitcast", &LLVMBuilderWrapper::zext_or_bitcast, "val"_a,
           "ty"_a, "name"_a = "",
           R"(Zero extend or bitcast.

<sub>C API: LLVMBuildZExtOrBitCast</sub>)")
      .def("sext_or_bitcast", &LLVMBuilderWrapper::sext_or_bitcast, "val"_a,
           "ty"_a, "name"_a = "",
           R"(Sign extend or bitcast.

<sub>C API: LLVMBuildSExtOrBitCast</sub>)")
      .def("trunc_or_bitcast", &LLVMBuilderWrapper::trunc_or_bitcast, "val"_a,
           "ty"_a, "name"_a = "",
           R"(Truncate or bitcast.

<sub>C API: LLVMBuildTruncOrBitCast</sub>)")
      .def("pointer_cast", &LLVMBuilderWrapper::pointer_cast, "val"_a, "ty"_a,
           "name"_a = "", R"(Pointer cast.

<sub>C API: LLVMBuildPointerCast</sub>)")
      .def("fp_cast", &LLVMBuilderWrapper::fp_cast, "val"_a, "ty"_a,
           "name"_a = "", R"(FP cast.

<sub>C API: LLVMBuildFPCast</sub>)")
      .def("cast", &LLVMBuilderWrapper::cast, "op"_a, "val"_a, "ty"_a,
           "name"_a = "", R"(Generic cast.

<sub>C API: LLVMBuildCast</sub>)")
      // Control flow
      .def("ret", &LLVMBuilderWrapper::ret, "val"_a,
           R"(Build ret.

<sub>C API: LLVMBuildRet</sub>)")
      .def("ret_void", &LLVMBuilderWrapper::ret_void,
           R"(Build ret void.

<sub>C API: LLVMBuildRetVoid</sub>)")
      .def("br", &LLVMBuilderWrapper::br, "dest"_a,
           R"(Build br.

<sub>C API: LLVMBuildBr</sub>)")
      .def("cond_br", &LLVMBuilderWrapper::cond_br, "cond"_a, "then_bb"_a,
           "else_bb"_a, R"(Build cond_br.

<sub>C API: LLVMBuildCondBr</sub>)")
      .def("switch_", &LLVMBuilderWrapper::switch_, "val"_a, "else_bb"_a,
           "num_cases"_a, R"(Build switch.

<sub>C API: LLVMBuildSwitch</sub>)")
      .def("indirect_br", &LLVMBuilderWrapper::indirect_br, "addr"_a,
           "num_dests"_a, R"(Build indirect br.

<sub>C API: LLVMBuildIndirectBr</sub>)")
      .def("call", &LLVMBuilderWrapper::call_infer, "func"_a, "args"_a,
           "name"_a = "",
           R"(Build a call instruction, inferring the function type from the callee.
For indirect calls through raw pointers, use the explicit-type overload.

<sub>C API: LLVMBuildCall2</sub>)")
      .def("call", &LLVMBuilderWrapper::call, "func_ty"_a, "func"_a, "args"_a,
           "name"_a = "",
           R"(Build a call instruction with an explicit function type.
Prefer the 2-arg form call(func, args) for direct calls.

<sub>C API: LLVMBuildCall2</sub>)")
      .def("unreachable", &LLVMBuilderWrapper::unreachable,
           R"(Build unreachable.

<sub>C API: LLVMBuildUnreachable</sub>)")
      .def("phi", &LLVMBuilderWrapper::phi, "ty"_a, "name"_a = "",
           R"(Build phi.

<sub>C API: LLVMBuildPhi</sub>)")
      // Additional instruction builders
      .def("resume", &LLVMBuilderWrapper::resume, "exn"_a,
           R"(Build resume.

<sub>C API: LLVMBuildResume</sub>)")
      .def("landing_pad", &LLVMBuilderWrapper::landing_pad, "ty"_a,
           "num_clauses"_a, "name"_a = "",
           R"(Build landing_pad.

<sub>C API: LLVMBuildLandingPad</sub>)")
      .def("catch_ret", &LLVMBuilderWrapper::catch_ret, "catch_pad"_a, "bb"_a,
           R"(Build catch_ret.

<sub>C API: LLVMBuildCatchRet</sub>)")
      .def("catch_pad", &LLVMBuilderWrapper::catch_pad, "parent_pad"_a,
           "args"_a, "name"_a = "",
           R"(Build catch_pad.

<sub>C API: LLVMBuildCatchPad</sub>)")
      .def("cleanup_pad", &LLVMBuilderWrapper::cleanup_pad, "parent_pad"_a,
           "args"_a, "name"_a = "",
           R"(Build cleanup_pad.

<sub>C API: LLVMBuildCleanupPad</sub>)")
      .def("extract_value", &LLVMBuilderWrapper::extract_value, "agg"_a,
           "index"_a, "name"_a = "",
           R"(Build extract_value.

<sub>C API: LLVMBuildExtractValue</sub>)")
      .def("insert_value", &LLVMBuilderWrapper::insert_value, "agg"_a, "val"_a,
           "index"_a, "name"_a = "",
           R"(Build insert_value.

<sub>C API: LLVMBuildInsertValue</sub>)")
      .def("extract_element", &LLVMBuilderWrapper::extract_element, "vec"_a,
           "index"_a, "name"_a = "",
           R"(Build extract_element.

<sub>C API: LLVMBuildExtractElement</sub>)")
      .def("insert_element", &LLVMBuilderWrapper::insert_element, "vec"_a,
           "val"_a, "index"_a, "name"_a = "",
           R"(Build insert_element.

<sub>C API: LLVMBuildInsertElement</sub>)")
      .def("shuffle_vector", &LLVMBuilderWrapper::shuffle_vector, "v1"_a,
           "v2"_a, "mask"_a, "name"_a = "",
           R"(Build shuffle_vector.

<sub>C API: LLVMBuildShuffleVector</sub>)")
      .def("freeze", &LLVMBuilderWrapper::freeze, "val"_a, "name"_a = "",
           R"(Build freeze.

<sub>C API: LLVMBuildFreeze</sub>)")
      .def("gep_with_no_wrap_flags",
           &LLVMBuilderWrapper::gep_with_no_wrap_flags, "ty"_a, "ptr"_a,
           "indices"_a, "flags"_a, "name"_a = "",
           R"(Build a GEP with explicit no-wrap flags (nuw/nusw).

<sub>C API: LLVMBuildGEPWithNoWrapFlags</sub>)")
      .def("atomic_rmw", &LLVMBuilderWrapper::atomic_rmw, "op"_a, "ptr"_a,
           "val"_a, "ordering"_a, "single_thread"_a = false,
           R"(Build an atomic read-modify-write instruction.

<sub>C API: LLVMBuildAtomicRMW</sub>)")
      .def("atomic_rmw_sync_scope", &LLVMBuilderWrapper::atomic_rmw_sync_scope,
           "op"_a, "ptr"_a, "val"_a, "ordering"_a, "sync_scope_id"_a,
           R"(Build an atomic RMW with explicit sync scope.

<sub>C API: LLVMBuildAtomicRMWSyncScope</sub>)")
      .def("atomic_cmpxchg", &LLVMBuilderWrapper::atomic_cmpxchg, "ptr"_a,
           "cmp"_a, "new_val"_a, "success_ordering"_a, "failure_ordering"_a,
           "single_thread"_a = false,
           R"(Build an atomic compare-exchange instruction.

<sub>C API: LLVMBuildAtomicCmpXchg</sub>)")
      .def("atomic_cmpxchg_sync_scope",
           &LLVMBuilderWrapper::atomic_cmpxchg_sync_scope, "ptr"_a, "cmp"_a,
           "new_val"_a, "success_ordering"_a, "failure_ordering"_a,
           "sync_scope_id"_a,
           R"(Build an atomic cmpxchg with explicit sync scope.

<sub>C API: LLVMBuildAtomicCmpXchgSyncScope</sub>)")
      .def("fence", &LLVMBuilderWrapper::fence, "ordering"_a,
           "single_thread"_a = false, "name"_a = "",
           R"(Build a memory fence instruction.

Args:
    ordering: The memory ordering constraint (AtomicOrdering enum)
    single_thread: If true, only synchronizes with this thread (default false)
    name: Optional name for the instruction

<sub>C API: LLVMBuildFence</sub>)")
      .def("fence_sync_scope", &LLVMBuilderWrapper::fence_sync_scope,
           "ordering"_a, "sync_scope_id"_a, "name"_a = "",
           R"(Build fence with scope.

<sub>C API: LLVMBuildFenceSyncScope</sub>)")
      .def("insert_into_builder_with_name",
           &LLVMBuilderWrapper::insert_into_builder_with_name, "instr"_a,
           "name"_a,
           R"(Insert instruction.

Valid when:
  - instr is an instruction value

<sub>C API: LLVMInsertIntoBuilderWithName</sub>)")
      .def("add_metadata_to_inst", &LLVMBuilderWrapper::add_metadata_to_inst,
           "instr"_a,
           R"(Add metadata to instruction.

Valid when:
  - instr is an instruction value

<sub>C API: LLVMAddMetadataToInst</sub>)")
      // Missing builders for echo command - wrapped to convert
      // OperandBundleWrapper
      .def(
          "invoke_with_operand_bundles",
          [](LLVMBuilderWrapper &self, const LLVMTypeWrapper &fn_ty,
             const LLVMValueWrapper &fn,
             const std::vector<LLVMValueWrapper> &args,
             const LLVMBasicBlockWrapper &then_bb,
             const LLVMBasicBlockWrapper &catch_bb,
             const std::vector<LLVMOperandBundleWrapper *> &bundles,
             const std::string &name) {
            std::vector<LLVMOperandBundleRef> bundle_refs;
            for (auto *b : bundles)
              bundle_refs.push_back(b->m_ref);
            return self.invoke_with_operand_bundles(
                fn_ty, fn, args, then_bb, catch_bb, bundle_refs, name);
          },
          "fn_ty"_a, "fn"_a, "args"_a, "then_bb"_a, "catch_bb"_a, "bundles"_a,
          "name"_a = "",
          R"(Build invoke with bundles.

<sub>C API: LLVMBuildInvokeWithOperandBundles</sub>)")
      .def(
          "call_with_operand_bundles",
          [](LLVMBuilderWrapper &self, const LLVMTypeWrapper &fn_ty,
             const LLVMValueWrapper &fn,
             const std::vector<LLVMValueWrapper> &args,
             const std::vector<LLVMOperandBundleWrapper *> &bundles,
             const std::string &name) {
            std::vector<LLVMOperandBundleRef> bundle_refs;
            for (auto *b : bundles)
              bundle_refs.push_back(b->m_ref);
            return self.call_with_operand_bundles(fn_ty, fn, args, bundle_refs,
                                                  name);
          },
          "fn_ty"_a, "fn"_a, "args"_a, "bundles"_a, "name"_a = "",
          R"(Build call with bundles.

<sub>C API: LLVMBuildCallWithOperandBundles</sub>)")
      .def(
          "callbr",
          [](LLVMBuilderWrapper &self, const LLVMTypeWrapper &fn_ty,
             const LLVMValueWrapper &fn,
             const LLVMBasicBlockWrapper &default_dest,
             const std::vector<LLVMBasicBlockWrapper> &indirect_dests,
             const std::vector<LLVMValueWrapper> &args,
             const std::vector<LLVMOperandBundleWrapper *> &bundles,
             const std::string &name) {
            std::vector<LLVMOperandBundleRef> bundle_refs;
            for (auto *b : bundles)
              bundle_refs.push_back(b->m_ref);
            return self.callbr(fn_ty, fn, default_dest, indirect_dests, args,
                               bundle_refs, name);
          },
          "fn_ty"_a, "fn"_a, "default_dest"_a, "indirect_dests"_a, "args"_a,
          "bundles"_a, "name"_a = "",
          R"(Build callbr.

<sub>C API: LLVMBuildCallBr</sub>)")
      .def("catch_switch", &LLVMBuilderWrapper::catch_switch, "parent_pad"_a,
           "unwind_bb"_a = nb::none(), "num_handlers"_a = 0, "name"_a = "",
           R"(Build catch_switch.

<sub>C API: LLVMBuildCatchSwitch</sub>)")
      .def("cleanup_ret", &LLVMBuilderWrapper::cleanup_ret, "catch_pad"_a,
           "bb"_a = nb::none(),
           R"(Build cleanup_ret.

<sub>C API: LLVMBuildCleanupRet</sub>)");

  // Operand Bundle wrapper
  nb::class_<LLVMOperandBundleWrapper>(m, "OperandBundle")
      .def_prop_ro("tag", &LLVMOperandBundleWrapper::get_tag,
                   R"(Get operand bundle tag.

<sub>C API: LLVMGetOperandBundleTag</sub>)")
      .def_prop_ro("num_args", &LLVMOperandBundleWrapper::get_num_args,
                   R"(Get number of args.

<sub>C API: LLVMGetNumOperandBundleArgs</sub>)")
      .def("get_arg_at_index", &LLVMOperandBundleWrapper::get_arg_at_index,
           "index"_a,
           R"(Get arg at index.

Valid when:
  - 0 <= index < num_args

<sub>C API: LLVMGetOperandBundleArgAtIndex</sub>)");

  // Attribute wrapper
  nb::class_<LLVMAttributeWrapper>(m, "Attribute")
      .def_prop_ro("is_valid", &LLVMAttributeWrapper::is_valid,
                   R"(Check if attribute is valid.

<sub>C API: LLVMIsEnumAttribute</sub>)")
      .def_prop_ro("kind", &LLVMAttributeWrapper::get_kind,
                   R"(Get kind ID.

<sub>C API: LLVMGetEnumAttributeKind</sub>)")
      .def_prop_ro("value", &LLVMAttributeWrapper::get_value,
                   R"(Get enum attribute value.

<sub>C API: LLVMGetEnumAttributeValue</sub>)")
      .def_prop_ro("is_enum_attribute",
                   &LLVMAttributeWrapper::is_enum_attribute,
                   R"(Check if enum attribute.

<sub>C API: LLVMIsEnumAttribute</sub>)")
      .def_prop_ro("is_string_attribute",
                   &LLVMAttributeWrapper::is_string_attribute,
                   R"(Check if string attribute.

<sub>C API: LLVMIsStringAttribute</sub>)")
      .def_prop_ro("is_type_attribute",
                   &LLVMAttributeWrapper::is_type_attribute,
                   R"(Check if type attribute.

<sub>C API: LLVMIsTypeAttribute</sub>)")
      .def_prop_ro("string_kind", &LLVMAttributeWrapper::get_string_kind,
                   R"(Get string key.

<sub>C API: LLVMGetStringAttributeKind</sub>)")
      .def_prop_ro("string_value", &LLVMAttributeWrapper::get_string_value,
                   R"(Get string value.

<sub>C API: LLVMGetStringAttributeValue</sub>)")
      .def_prop_ro("type_value", &LLVMAttributeWrapper::get_type_value,
                   R"(Get type value.

<sub>C API: LLVMGetTypeAttributeValue</sub>)");

  // Comdat wrapper (for Windows/COFF COMDAT sections)
  nb::class_<LLVMComdatWrapper>(m, "Comdat",
                                "COMDAT section for Windows/COFF linking.")
      .def_prop_ro("is_valid", &LLVMComdatWrapper::is_valid,
                   R"(Check if Comdat is valid.

<sub>C API: LLVMGetComdatSelectionKind</sub>)")
      .def_prop_rw("selection_kind", &LLVMComdatWrapper::get_selection_kind,
                   &LLVMComdatWrapper::set_selection_kind,
                   R"(Get or set the selection kind for this COMDAT.

<sub>C API: LLVMGetComdatSelectionKind, LLVMSetComdatSelectionKind</sub>)");

  // Value metadata entries wrapper (for global/instruction metadata copying)
  nb::class_<LLVMValueMetadataEntriesWrapper>(m, "ValueMetadataEntries")
      .def("__len__", &LLVMValueMetadataEntriesWrapper::size)
      .def("get_kind", &LLVMValueMetadataEntriesWrapper::get_kind, "index"_a,
           R"(Get metadata kind at index.

<sub>C API: LLVMValueMetadataEntriesGetKind</sub>)")
      .def("get_metadata", &LLVMValueMetadataEntriesWrapper_get_metadata,
           "index"_a,
           R"(Get metadata at index.

<sub>C API: LLVMValueMetadataEntriesGetMetadata</sub>)");

  // Named metadata node wrapper
  nb::class_<LLVMNamedMDNodeWrapper>(m, "NamedMDNode")
      .def("__eq__", [](const LLVMNamedMDNodeWrapper &a,
                        const LLVMNamedMDNodeWrapper &b) { return a == b; })
      .def("__ne__", [](const LLVMNamedMDNodeWrapper &a,
                        const LLVMNamedMDNodeWrapper &b) { return a != b; })
      .def("__hash__",
           [](const LLVMNamedMDNodeWrapper &v) {
             return std::hash<LLVMNamedMDNodeRef>{}(v.m_ref);
           })
      .def_prop_ro("name", &LLVMNamedMDNodeWrapper::get_name,
                   R"(Get name.

<sub>C API: LLVMGetNamedMetadataName</sub>)")
      .def_prop_ro("next", &LLVMNamedMDNodeWrapper::next,
                   R"(Get next.

<sub>C API: LLVMGetNextNamedMetadata</sub>)")
      .def_prop_ro("prev", &LLVMNamedMDNodeWrapper::prev,
                   R"(Get previous.

<sub>C API: LLVMGetPreviousNamedMetadata</sub>)");

  // Module wrapper
  nb::class_<LLVMModuleWrapper>(m, "Module")
      .def("__eq__", [](const LLVMModuleWrapper &a,
                        const LLVMModuleWrapper &b) { return a.m_ref == b.m_ref; })
      .def("__ne__", [](const LLVMModuleWrapper &a,
                        const LLVMModuleWrapper &b) { return a.m_ref != b.m_ref; })
      .def("__hash__",
           [](const LLVMModuleWrapper &v) {
             return std::hash<LLVMModuleRef>{}(v.m_ref);
           })
      .def("__repr__", [](const LLVMModuleWrapper &v) {
        v.check_valid();
        std::string name = v.get_name();
        return "<Module '" + name + "'>";
      })
      .def_prop_rw("name", &LLVMModuleWrapper::get_name,
                   &LLVMModuleWrapper::set_name,
                   R"(Module identifier.

<sub>C API: LLVMGetModuleIdentifier, LLVMSetModuleIdentifier</sub>)")
      .def_prop_rw("source_filename", &LLVMModuleWrapper::get_source_filename,
                   &LLVMModuleWrapper::set_source_filename,
                   R"(Source filename.

<sub>C API: LLVMGetSourceFileName, LLVMSetSourceFileName</sub>)")
      .def_prop_rw("data_layout", &LLVMModuleWrapper::get_data_layout,
                   &LLVMModuleWrapper::set_data_layout,
                   R"(Data layout string.

<sub>C API: LLVMGetDataLayoutStr, LLVMSetDataLayout</sub>)")
      .def_prop_rw("target_triple", &LLVMModuleWrapper::get_target_triple,
                   &LLVMModuleWrapper::set_target_triple,
                   R"(Target triple.

<sub>C API: LLVMGetTarget, LLVMSetTarget</sub>)")
      .def("add_function", &LLVMModuleWrapper::add_function, "name"_a,
           "func_ty"_a, R"(Add a function.

<sub>C API: LLVMAddFunction</sub>)")
      .def("get_function", &LLVMModuleWrapper::get_function, "name"_a,
           R"(Get a function by name.

<sub>C API: LLVMGetNamedFunction</sub>)")
      .def("add_global", &LLVMModuleWrapper::add_global, "ty"_a, "name"_a,
           R"(Add a global variable.

<sub>C API: LLVMAddGlobal</sub>)")
      .def("add_global_in_address_space",
           &LLVMModuleWrapper::add_global_in_address_space, "ty"_a, "name"_a,
           "address_space"_a,
           R"(Add global in address space.

<sub>C API: LLVMAddGlobalInAddressSpace</sub>)")
      .def("get_global", &LLVMModuleWrapper::get_global, "name"_a,
           R"(Get a global by name.

<sub>C API: LLVMGetNamedGlobal</sub>)")
      .def_prop_ro("first_global", &LLVMModuleWrapper::first_global,
                   R"(First global.

<sub>C API: LLVMGetFirstGlobal</sub>)")
      .def_prop_ro("last_global", &LLVMModuleWrapper::last_global,
                   R"(Last global.

<sub>C API: LLVMGetLastGlobal</sub>)")
      .def_prop_ro("globals", &LLVMModuleWrapper::globals,
                   R"(All globals.

<sub>C API: LLVMGetFirstGlobal, LLVMGetNextGlobal</sub>)")
      .def_prop_ro("functions", &LLVMModuleWrapper::functions,
                   R"(All functions.

<sub>C API: LLVMGetFirstFunction, LLVMGetNextFunction</sub>)")
      .def_prop_ro("first_function", &LLVMModuleWrapper::first_function,
                   R"(First function.

<sub>C API: LLVMGetFirstFunction</sub>)")
      .def_prop_ro("last_function", &LLVMModuleWrapper::last_function,
                   R"(Last function.

<sub>C API: LLVMGetLastFunction</sub>)")
      .def("__str__", &LLVMModuleWrapper::to_string)
      .def("to_string", &LLVMModuleWrapper::to_string,
           R"(Get module as IR string.

<sub>C API: LLVMPrintModuleToString</sub>)")
      .def("verify", &LLVMModuleWrapper::verify,
           R"(Verify the module.

<sub>C API: LLVMVerifyModule</sub>)")
      .def("get_verification_error", &LLVMModuleWrapper::get_verification_error,
           R"(Get verification error message.

<sub>C API: LLVMVerifyModule</sub>)")
      .def("verify_or_raise", &LLVMModuleWrapper::verify_or_raise,
           R"(Verify the module, raising LLVMError if verification fails.

<sub>C API: LLVMVerifyModule</sub>)")
      .def_prop_ro("debug_metadata_version", [](const LLVMModuleWrapper &self) -> unsigned {
        self.check_valid();
        return LLVMGetModuleDebugMetadataVersion(self.m_ref);
      }, R"(Get the debug metadata version from this module.

<sub>C API: LLVMGetModuleDebugMetadataVersion</sub>)")
      .def("strip_debug_info", [](LLVMModuleWrapper &self) -> bool {
        self.check_valid();
        return LLVMStripModuleDebugInfo(self.m_ref);
      }, R"(Strip debug info from this module. Returns true if changed.

<sub>C API: LLVMStripModuleDebugInfo</sub>)")
      // Global alias support
      .def_prop_ro("first_global_alias", &LLVMModuleWrapper::first_global_alias,
                   R"(First global alias.

<sub>C API: LLVMGetFirstGlobalAlias</sub>)")
      .def_prop_ro("last_global_alias", &LLVMModuleWrapper::last_global_alias,
                   R"(Last global alias.

<sub>C API: LLVMGetLastGlobalAlias</sub>)")
      .def("get_named_global_alias", &LLVMModuleWrapper::get_named_global_alias,
           "name"_a, R"(Get named global alias.

<sub>C API: LLVMGetNamedGlobalAlias</sub>)")
      .def("add_alias", &LLVMModuleWrapper::add_alias, "value_ty"_a,
           "addr_space"_a, "aliasee"_a, "name"_a,
           R"(Add a global alias.

<sub>C API: LLVMAddAlias2</sub>)")
      // Global IFunc support
      .def_prop_ro("first_global_ifunc", &LLVMModuleWrapper::first_global_ifunc,
                   R"(Get the first indirect function (IFunc) in the module.

<sub>C API: LLVMGetFirstGlobalIFunc</sub>)")
      .def_prop_ro("last_global_ifunc", &LLVMModuleWrapper::last_global_ifunc,
                   R"(Get the last indirect function (IFunc) in the module.

<sub>C API: LLVMGetLastGlobalIFunc</sub>)")
      .def("get_named_global_ifunc", &LLVMModuleWrapper::get_named_global_ifunc,
           "name"_a, R"(Get an indirect function by name.

<sub>C API: LLVMGetNamedGlobalIFunc</sub>)")
      .def("add_global_ifunc", &LLVMModuleWrapper::add_global_ifunc, "name"_a,
           "ty"_a, "addr_space"_a, "resolver"_a,
           R"(Add an indirect function (IFunc) to the module.

<sub>C API: LLVMAddGlobalIFunc</sub>)")
      // Named metadata support
      .def_prop_ro("first_named_metadata",
                   &LLVMModuleWrapper::first_named_metadata,
                   R"(First named metadata.

<sub>C API: LLVMGetFirstNamedMetadata</sub>)")
      .def_prop_ro("last_named_metadata",
                   &LLVMModuleWrapper::last_named_metadata,
                   R"(Last named metadata.

<sub>C API: LLVMGetLastNamedMetadata</sub>)")
      .def("get_named_metadata", &LLVMModuleWrapper::get_named_metadata,
           "name"_a, R"(Get named metadata.

<sub>C API: LLVMGetNamedMetadata</sub>)")
      .def("get_or_insert_named_metadata",
           &LLVMModuleWrapper::get_or_insert_named_metadata, "name"_a,
           R"(Get or insert named metadata.

<sub>C API: LLVMGetOrInsertNamedMetadata</sub>)")
      .def("get_named_metadata_num_operands",
           &LLVMModuleWrapper::get_named_metadata_num_operands, "name"_a,
           R"(Get operand count.

<sub>C API: LLVMGetNamedMetadataNumOperands</sub>)")
      .def("get_named_metadata_operands",
           &LLVMModuleWrapper::get_named_metadata_operands, "name"_a,
           R"(Get operands.

<sub>C API: LLVMGetNamedMetadataOperands</sub>)")
      // Inline assembly support
      .def_prop_rw("inline_asm", &LLVMModuleWrapper::get_inline_asm,
                   &LLVMModuleWrapper::set_inline_asm,
                   R"(Module inline ASM.

<sub>C API: LLVMGetModuleInlineAsm, LLVMSetModuleInlineAsm2</sub>)")
      .def("clone", &LLVMModuleWrapper::clone, nb::rv_policy::take_ownership,
           R"(Create a copy of this module.

<sub>C API: LLVMCloneModule</sub>)")
      // BitWriter methods
      .def("write_bitcode_to_file", &LLVMModuleWrapper::write_bitcode_to_file,
           "path"_a,
           R"(Write the module as bitcode to a file.
           
           Args:
               path: Output file path

<sub>C API: LLVMWriteBitcodeToFile</sub>)")
      .def("write_bitcode_to_memory_buffer",
           &LLVMModuleWrapper::write_bitcode_to_memory_buffer,
           R"(Write the module as bitcode to a bytes object.
           
           Returns:
               bytes: The bitcode data.

<sub>C API: LLVMWriteBitcodeToMemoryBuffer</sub>)")
      // Linker methods
      .def("link_module", &LLVMModuleWrapper::link_module, "src"_a,
           R"(Link another module into this module.
           
           The source module is destroyed after linking.
           
           Args:
               src: Source module to link in

<sub>C API: LLVMLinkModules2</sub>)")
      // COMDAT support
      .def("get_or_insert_comdat", &LLVMModuleWrapper::get_or_insert_comdat,
           "name"_a,
           R"(Get or insert a COMDAT section with the given name.
           
           COMDAT sections are used on Windows/COFF targets for symbol
           deduplication and merging.
           
           Args:
               name: The COMDAT name
               
           Returns:
               A Comdat object for the named section.

<sub>C API: LLVMGetOrInsertComdat</sub>)")
      // Module printing to file
      .def("print_to_file", &LLVMModuleWrapper::print_to_file, "filename"_a,
           R"(Print the module IR to a file.
           
           Args:
               filename: Output file path

<sub>C API: LLVMPrintModuleToFile</sub>)")
      // Named metadata operand
      .def("add_named_metadata_operand",
           &LLVMModuleWrapper::add_named_metadata_operand, "name"_a, "md"_a,
           R"(Add operand.

<sub>C API: LLVMAddNamedMetadataOperand</sub>)")
      // =====================================================================
      // Module Flags API
      // =====================================================================
      .def("add_module_flag", &LLVMModuleWrapper::add_module_flag, "behavior"_a,
           "key"_a, "val"_a,
           R"(Add a module-level flag.

Args:
    behavior: The merge behavior (ModuleFlagBehavior enum)
    key: The flag name
    val: The metadata value

<sub>C API: LLVMAddModuleFlag</sub>)")
      .def("get_module_flag", &LLVMModuleWrapper::get_module_flag, "key"_a,
           R"(Get a module-level flag by key.

Args:
    key: The flag name

Returns:
    The metadata value, or None if not found.

<sub>C API: LLVMGetModuleFlag</sub>)")
      // Module methods
      .def_prop_ro("context", &LLVMModuleWrapper::get_context,
                   nb::rv_policy::take_ownership,
                   R"(Get the context for this module.

<sub>C API: LLVMGetModuleContext</sub>)")
      .def_prop_rw("is_new_dbg_info_format",
                   &LLVMModuleWrapper::is_new_dbg_info_format,
                   &LLVMModuleWrapper::set_is_new_dbg_info_format,
                   R"(Whether to use new debug info format.

<sub>C API: LLVMIsNewDbgInfoFormat, LLVMSetIsNewDbgInfoFormat</sub>)")
      .def("get_intrinsic_declaration",
           &LLVMModuleWrapper::get_intrinsic_declaration, "id"_a,
           "param_types"_a,
           R"(Get intrinsic declaration for this module.

<sub>C API: LLVMGetIntrinsicDeclaration</sub>)")
      .def("create_dibuilder", &LLVMModuleWrapper::create_dibuilder,
           nb::rv_policy::take_ownership,
           R"(Create a debug info builder for this module.

Use with 'with' statement:
    with mod.create_dibuilder() as dib:
        file = dib.create_file("foo.c", ".")
        # ... create debug info ...
        dib.finalize()

<sub>C API: LLVMCreateDIBuilder</sub>)");

  // TypeFactory wrapper (property-based type namespace)
  nb::class_<LLVMTypeFactoryWrapper>(m, "TypeFactory")
      // Fixed-width integer types
      .def_prop_ro("i1", &LLVMTypeFactoryWrapper::i1,
                   R"(1-bit int.

<sub>C API: LLVMInt1TypeInContext</sub>)")
      .def_prop_ro("i8", &LLVMTypeFactoryWrapper::i8,
                   R"(8-bit int.

<sub>C API: LLVMInt8TypeInContext</sub>)")
      .def_prop_ro("i16", &LLVMTypeFactoryWrapper::i16,
                   R"(16-bit int.

<sub>C API: LLVMInt16TypeInContext</sub>)")
      .def_prop_ro("i32", &LLVMTypeFactoryWrapper::i32,
                   R"(32-bit int.

<sub>C API: LLVMInt32TypeInContext</sub>)")
      .def_prop_ro("i64", &LLVMTypeFactoryWrapper::i64,
                   R"(64-bit int.

<sub>C API: LLVMInt64TypeInContext</sub>)")
      .def_prop_ro("i128", &LLVMTypeFactoryWrapper::i128,
                   R"(128-bit int.

<sub>C API: LLVMInt128TypeInContext</sub>)")
      // Floating-point types
      .def_prop_ro("f16", &LLVMTypeFactoryWrapper::f16,
                   R"(Half type.

<sub>C API: LLVMHalfTypeInContext</sub>)")
      .def_prop_ro("bf16", &LLVMTypeFactoryWrapper::bf16,
                   R"(BFloat16 type.

<sub>C API: LLVMBFloatTypeInContext</sub>)")
      .def_prop_ro("f32", &LLVMTypeFactoryWrapper::f32,
                   R"(Float type.

<sub>C API: LLVMFloatTypeInContext</sub>)")
      .def_prop_ro("f64", &LLVMTypeFactoryWrapper::f64,
                   R"(Double type.

<sub>C API: LLVMDoubleTypeInContext</sub>)")
      .def_prop_ro("x86_fp80", &LLVMTypeFactoryWrapper::x86_fp80,
                   R"(X86 FP80 type.

<sub>C API: LLVMX86FP80TypeInContext</sub>)")
      .def_prop_ro("fp128", &LLVMTypeFactoryWrapper::fp128,
                   R"(FP128 type.

<sub>C API: LLVMFP128TypeInContext</sub>)")
      .def_prop_ro("ppc_fp128", &LLVMTypeFactoryWrapper::ppc_fp128,
                   R"(PPC FP128 type.

<sub>C API: LLVMPPCFP128TypeInContext</sub>)")
      // Other types
      .def_prop_ro("void", &LLVMTypeFactoryWrapper::void_,
                   R"(Void type.

<sub>C API: LLVMVoidTypeInContext</sub>)")
      .def_prop_ro("label", &LLVMTypeFactoryWrapper::label,
                   R"(Label type.

<sub>C API: LLVMLabelTypeInContext</sub>)")
      .def_prop_ro("metadata", &LLVMTypeFactoryWrapper::metadata,
                   R"(Metadata type.

<sub>C API: LLVMMetadataTypeInContext</sub>)")
      .def_prop_ro("token", &LLVMTypeFactoryWrapper::token,
                   R"(Token type.

<sub>C API: LLVMTokenTypeInContext</sub>)")
      .def_prop_ro("x86_amx", &LLVMTypeFactoryWrapper::x86_amx,
                   R"(X86 AMX type.

<sub>C API: LLVMX86AMXTypeInContext</sub>)")
      // Pointer type (property for default, method for custom address space)
      .def_prop_ro("ptr", &LLVMTypeFactoryWrapper::ptr_default,
                   R"(Opaque pointer type (address space 0).

<sub>C API: LLVMPointerTypeInContext</sub>)")
      .def("addrspace_ptr", &LLVMTypeFactoryWrapper::addrspace_ptr, "address_space"_a,
           R"(Pointer type in a specific address space.

<sub>C API: LLVMPointerTypeInContext</sub>)")
      .def("int_n", &LLVMTypeFactoryWrapper::int_n, "bits"_a,
           R"(N-bit int.

<sub>C API: LLVMIntTypeInContext</sub>)")
      .def("function", &LLVMTypeFactoryWrapper::function, "ret_ty"_a,
           "param_types"_a, "vararg"_a = false,
           R"(Function type.

<sub>C API: LLVMFunctionType</sub>)")
      .def("struct", &LLVMTypeFactoryWrapper::struct_, "elem_types"_a,
           "packed"_a = false, "name"_a = "",
           R"(Struct type.

<sub>C API: LLVMStructTypeInContext, LLVMStructCreateNamed</sub>)")
      .def("opaque_struct", &LLVMTypeFactoryWrapper::opaque_struct, "name"_a,
           R"(Opaque struct type.

<sub>C API: LLVMStructCreateNamed</sub>)")
      .def("array", &LLVMTypeFactoryWrapper::array, "elem_ty"_a, "count"_a,
           R"(Array type.

<sub>C API: LLVMArrayType2</sub>)")
      .def("vector", &LLVMTypeFactoryWrapper::vector, "elem_ty"_a,
           "elem_count"_a, R"(Vector type.

<sub>C API: LLVMVectorType</sub>)")
      .def("scalable_vector", &LLVMTypeFactoryWrapper::scalable_vector,
           "elem_ty"_a, "elem_count"_a,
           R"(Scalable vector type.

<sub>C API: LLVMScalableVectorType</sub>)")
      .def("target_ext", &LLVMTypeFactoryWrapper::target_ext, "name"_a,
           "type_params"_a, "int_params"_a,
           R"(Target extension type.

<sub>C API: LLVMTargetExtTypeInContext</sub>)")
      .def("get", &LLVMTypeFactoryWrapper::get, "name"_a,
           R"(Get named struct type.

<sub>C API: LLVMGetTypeByName2</sub>)");

  // Context wrapper
  nb::class_<LLVMContextWrapper>(m, "Context")
      .def_prop_rw("discard_value_names",
                   &LLVMContextWrapper::get_discard_value_names,
                   &LLVMContextWrapper::set_discard_value_names,
                   R"(Discard value names.

<sub>C API: LLVMContextShouldDiscardValueNames, LLVMContextSetDiscardValueNames</sub>)")
      // Property-based type factory
      .def_prop_ro("types", &LLVMContextWrapper::get_types,
                   R"(Type factory for this context.

<sub>C API: LLVMInt</sub>*TypeInContext)")
      // Module/Builder creation
      .def("create_module", &LLVMContextWrapper::create_module, "name"_a,
           nb::rv_policy::take_ownership,
           R"(Create a module.

<sub>C API: LLVMModuleCreateWithNameInContext</sub>)")
      .def("create_builder",
           nb::overload_cast<const LLVMBasicBlockWrapper &>(
               &LLVMContextWrapper::create_builder),
           "bb"_a, nb::rv_policy::take_ownership,
           R"(Create a Builder positioned at the end of a BasicBlock.

Returns a BuilderManager for use with Python's 'with' statement.

<sub>C API: LLVMCreateBuilderInContext, LLVMPositionBuilderAtEnd</sub>)")
      .def("create_builder",
           nb::overload_cast<const LLVMValueWrapper &, bool>(
               &LLVMContextWrapper::create_builder),
           "inst"_a, nb::kw_only(), "before_dbg"_a = false,
           nb::rv_policy::take_ownership,
           R"(Create a Builder positioned before an Instruction.

Valid when:
  - inst is an instruction value
  - inst belongs to a basic block

Returns a BuilderManager for use with Python's 'with' statement.

<sub>C API: LLVMCreateBuilderInContext, LLVMPositionBuilderBefore</sub>)")
      .def("create_basic_block", &LLVMContextWrapper::create_basic_block,
           "name"_a,
           R"(Create orphan basic block.

<sub>C API: LLVMCreateBasicBlockInContext</sub>)")
      // Parsing methods
      .def("parse_bitcode_from_file",
           &LLVMContextWrapper::parse_bitcode_from_file, "filename"_a,
           "lazy"_a = false, nb::rv_policy::take_ownership,
           R"(Parse bitcode from file.

<sub>C API: LLVMParseBitcodeInContext2</sub>)")
      .def("parse_bitcode_from_bytes",
           &LLVMContextWrapper::parse_bitcode_from_bytes, "data"_a,
           "lazy"_a = false, nb::rv_policy::take_ownership,
           R"(Parse bitcode from bytes.

<sub>C API: LLVMParseBitcodeInContext2</sub>)")
      .def("parse_ir", &LLVMContextWrapper::parse_ir, "source"_a,
           "mod_name"_a = "<source>", nb::rv_policy::take_ownership,
           R"(Parse IR from string.

<sub>C API: LLVMParseIRInContext</sub>)")
      // Diagnostics
      .def("get_diagnostics", &LLVMContextWrapper::get_diagnostics,
           R"(Get accumulated diagnostics.

<sub>C API: LLVMContextSetDiagnosticHandler</sub>)")
      .def("clear_diagnostics", &LLVMContextWrapper::clear_diagnostics,
           R"(Clear accumulated diagnostics.

<sub>C API: LLVMContextSetDiagnosticHandler</sub>)")
      // Attribute creation methods
      .def("create_enum_attribute", &LLVMContextWrapper::create_enum_attribute,
           "kind_id"_a, "val"_a,
           R"(Create enum attribute.

<sub>C API: LLVMCreateEnumAttribute</sub>)")
      .def("create_string_attribute",
           &LLVMContextWrapper::create_string_attribute, "key"_a, "value"_a,
           R"(Create a string attribute.

Args:
    key: The attribute key (e.g., "target-cpu")
    value: The attribute value (e.g., "skylake")

Returns:
    A new string attribute.

<sub>C API: LLVMCreateStringAttribute</sub>)")
      .def("create_type_attribute", &LLVMContextWrapper::create_type_attribute,
           "kind_id"_a, "type"_a,
           R"(Create a type attribute.

Args:
    kind_id: The attribute kind ID
    type: The type to attach to the attribute

Returns:
    A new type attribute.

<sub>C API: LLVMCreateTypeAttribute</sub>)")
      .def("const_string",
           nb::overload_cast<const std::string &, bool>(
               &LLVMContextWrapper::const_string),
           "str"_a, "dont_null_terminate"_a = false,
           R"(Create a string constant in this context.

<sub>C API: LLVMConstStringInContext2</sub>)")
      .def("const_string",
           nb::overload_cast<const nb::bytes &, bool>(
               &LLVMContextWrapper::const_string),
           "data"_a, "dont_null_terminate"_a = false,
           R"(Create a raw-bytes constant in this context.

<sub>C API: LLVMConstStringInContext2</sub>)")
      .def("const_struct", [](LLVMContextWrapper &self,
                              const std::vector<LLVMValueWrapper> &vals,
                              bool packed) {
        self.check_valid();
        std::vector<LLVMValueRef> refs;
        refs.reserve(vals.size());
        for (const auto &v : vals) {
          v.check_valid();
          refs.push_back(v.m_ref);
        }
        return LLVMValueWrapper(
            LLVMConstStructInContext(self.m_ref, refs.data(), refs.size(), packed),
            self.m_token);
      }, "vals"_a, "packed"_a = false,
           R"(Create a struct constant in this context.

<sub>C API: LLVMConstStructInContext</sub>)")
      .def("get_md_kind_id", &LLVMContextWrapper::get_md_kind_id, "name"_a,
           R"(Get metadata kind ID.

<sub>C API: LLVMGetMDKindIDInContext</sub>)")
      // Metadata creation methods
      .def("md_string", &LLVMContextWrapper::md_string, "str"_a,
           R"(Create metadata string.

<sub>C API: LLVMMDStringInContext2</sub>)")
      .def("md_node", &LLVMContextWrapper::md_node, "mds"_a,
           R"(Create metadata node.

<sub>C API: LLVMMDNodeInContext2</sub>)")
      // Sync scope
      .def("get_sync_scope_id", &LLVMContextWrapper::get_sync_scope_id,
           "name"_a,
           R"(Get the sync scope ID for a named synchronization scope.

Maps a synchronization scope name to an ID unique within this context.
Common scope names include "singlethread" for thread-local synchronization.

<sub>C API: LLVMGetSyncScopeID</sub>)");

  // Context manager
  nb::class_<LLVMContextManager>(m, "ContextManager")
      .def("__enter__", &LLVMContextManager::enter,
           nb::rv_policy::reference_internal,
           R"(Enter the context manager and create a new Context.

Valid when:
  - this manager has not already been entered
)")
      .def("__exit__", &LLVMContextManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none(),
           R"(Exit the context manager and dispose the Context.

Valid when:
  - this manager was previously entered
)");

  // Module manager
  nb::class_<LLVMModuleManager>(m, "ModuleManager")
      .def("__enter__", &LLVMModuleManager::enter,
           nb::rv_policy::reference_internal,
           R"(Enter the manager and obtain a Module.

Valid when:
  - manager is not disposed
  - manager has not already been entered
)")
      .def("__exit__", &LLVMModuleManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none(),
           R"(Exit the manager and dispose the owned Module.

Valid when:
  - manager is entered
  - manager is not already disposed
)")
      .def(
          "dispose", &LLVMModuleManager::dispose,
          R"(Dispose the module manager without using a 'with' statement.

Valid when:
  - manager is not already disposed
  - manager has not been entered yet
)");

  // Builder manager
  nb::class_<LLVMBuilderManager>(m, "BuilderManager")
      .def("__enter__", &LLVMBuilderManager::enter,
           nb::rv_policy::reference_internal,
           R"(Enter the manager and obtain a Builder.

Valid when:
  - manager is not disposed
  - manager has not already been entered
  - manager has a valid insertion position (basic block or instruction)
)")
      .def("__exit__", &LLVMBuilderManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none(),
           R"(Exit the manager and dispose the owned Builder.

Valid when:
  - manager is entered
  - manager is not already disposed
)")
      .def(
          "dispose", &LLVMBuilderManager::dispose,
          R"(Dispose the builder manager without using a 'with' statement.

Valid when:
  - manager is not already disposed
  - manager has not been entered yet
)");

  // Module-level factory functions
  m.def(
      "create_context", [] { return new LLVMContextManager(); },
      nb::rv_policy::take_ownership,
      R"(Create new LLVM context.

<sub>C API: LLVMContextCreate</sub>)");

  m.def(
      "global_context",
      [] {
        static LLVMContextWrapper context(true);
        return &context;
      },
      nb::rv_policy::reference,
      R"(Get global context.

<sub>C API: LLVMGetGlobalContext</sub>)");

  // Constant creation functions
  // Note: Basic constant creation (const_int, const_real, const_null, etc.)
  // is now available via Type methods: ty.constant(), ty.real_constant(),
  // ty.null() The following are aggregate/advanced constant creation functions
  // still needed:
  // TODO: these need to be refactored to not be in the llvm module as globals
  // they are specific to values and therefore to a context (or even module?)
  m.def("const_array", &const_array, "elem_ty"_a, "vals"_a,
        R"(Create array constant.

<sub>C API: LLVMConstArray2</sub>)");
  m.def("const_struct", &const_struct, "vals"_a, "packed"_a, "ctx"_a,
        R"(Create struct constant.

<sub>C API: LLVMConstStructInContext</sub>)");
  m.def("const_vector", &const_vector, "vals"_a,
        R"(Create vector constant.

<sub>C API: LLVMConstVector</sub>)");
  m.def("const_string",
        static_cast<LLVMValueWrapper (*)(LLVMContextWrapper *,
                                         const std::string &, bool)>(
            &const_string),
        "ctx"_a, "str"_a,
        "dont_null_terminate"_a = false,
        R"(Create string constant.

<sub>C API: LLVMConstStringInContext2</sub>)");
  m.def("const_string",
        static_cast<LLVMValueWrapper (*)(LLVMContextWrapper *,
                                         const nb::bytes &, bool)>(
            &const_string),
        "ctx"_a, "data"_a, "dont_null_terminate"_a = false,
        R"(Create raw-bytes constant.

<sub>C API: LLVMConstStringInContext2</sub>)");
  m.def("const_named_struct", &const_named_struct, "struct_ty"_a, "vals"_a,
        R"(Create named struct constant.

<sub>C API: LLVMConstNamedStruct</sub>)");
  // Advanced constant creation
  m.def("const_int_of_arbitrary_precision", &const_int_of_arbitrary_precision,
        "ty"_a, "words"_a,
        R"(Arbitrary precision int.

<sub>C API: LLVMConstIntOfArbitraryPrecision</sub>)");
  m.def("const_data_array",
        static_cast<LLVMValueWrapper (*)(const LLVMTypeWrapper &,
                                         const std::string &)>(
            &const_data_array),
        "elem_ty"_a, "data"_a,
        R"(Create data array.

<sub>C API: LLVMConstDataArray</sub>)");
  m.def("const_data_array",
        static_cast<LLVMValueWrapper (*)(const LLVMTypeWrapper &,
                                         const nb::bytes &)>(
            &const_data_array),
        "elem_ty"_a, "data"_a,
        R"(Create raw-bytes data array.

<sub>C API: LLVMConstDataArray</sub>)");
  m.def("const_gep_with_no_wrap_flags", &const_gep_with_no_wrap_flags, "ty"_a,
        "ptr"_a, "indices"_a, "no_wrap_flags"_a,
        R"(Create a constant GEP expression with explicit no-wrap flags.

<sub>C API: LLVMConstGEPWithNoWrapFlags</sub>)");
  m.def("const_ptr_auth", &const_ptr_auth, "ptr"_a, "key"_a, "discriminator"_a,
        "addr_discriminator"_a,
        R"(Create a constant pointer authentication expression for ARM64e.

<sub>C API: LLVMConstantPtrAuth</sub>)");
  m.def("block_address", &block_address, "fn"_a, "bb"_a,
        R"(Create a BlockAddress constant that is the address of a basic block.

This is used for computed goto (indirect branch) support.

Valid when:
  - bb is owned by fn

<sub>C API: LLVMBlockAddress</sub>)");
  m.def("intrinsic_is_overloaded", &intrinsic_is_overloaded, "id"_a,
        R"(Check if intrinsic is overloaded.

<sub>C API: LLVMIntrinsicIsOverloaded</sub>)");
  // NOTE: get_intrinsic_declaration has been moved to
  // Module.get_intrinsic_declaration() method

  // Operand bundle creation
  m.def(
      "create_operand_bundle",
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
      R"(Create operand bundle.

<sub>C API: LLVMCreateOperandBundle</sub>)");

  // Get undef mask element constant
  m.def(
      "get_undef_mask_elem", []() { return LLVMGetUndefMaskElem(); },
      R"(Get undef mask element.

<sub>C API: LLVMGetUndefMaskElem</sub>)");

  // Get inline assembly
  // TODO: this should be a method on type instead
  m.def(
      "get_inline_asm",
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
      R"(Create inline assembly.

<sub>C API: LLVMGetInlineAsm</sub>)");

  // Target wrapper
  nb::class_<LLVMTargetWrapper>(m, "Target")
      .def_prop_ro("name", &LLVMTargetWrapper::get_name,
                   R"(Target name.

<sub>C API: LLVMGetTargetName</sub>)")
      .def_prop_ro("description", &LLVMTargetWrapper::get_description,
                   R"(Target description.

<sub>C API: LLVMGetTargetDescription</sub>)")
      .def_prop_ro("has_jit", &LLVMTargetWrapper::has_jit,
                   R"(Has JIT support.

<sub>C API: LLVMTargetHasJIT</sub>)")
      .def_prop_ro("has_target_machine", &LLVMTargetWrapper::has_target_machine,
                   R"(Check if this target has a TargetMachine implementation.

<sub>C API: LLVMTargetHasTargetMachine</sub>)")
      .def_prop_ro("has_asm_backend", &LLVMTargetWrapper::has_asm_backend,
                   R"(Has ASM backend.

<sub>C API: LLVMTargetHasAsmBackend</sub>)")
      .def_prop_ro("next", &LLVMTargetWrapper::next,
                   R"(Next target.

<sub>C API: LLVMGetNextTarget</sub>)");

  // Target functions
  m.def("initialize_all", &initialize_all,
        R"(Initialize all targets, MCs, ASM printers, and ASM parsers.
Convenience function that calls initialize_all_target_infos(),
initialize_all_targets(), initialize_all_target_mcs(),
initialize_all_asm_printers(), and initialize_all_asm_parsers().)");
  m.def("initialize_all_target_infos", &initialize_all_target_infos,
        R"(Initialize all target infos.

<sub>C API: LLVMInitializeAllTargetInfos</sub>)");
  m.def("initialize_all_targets", &initialize_all_targets,
        R"(Initialize all targets.

<sub>C API: LLVMInitializeAllTargets</sub>)");
  m.def("initialize_all_target_mcs", &initialize_all_target_mcs,
        R"(Initialize all target MCs.

<sub>C API: LLVMInitializeAllTargetMCs</sub>)");
  m.def("initialize_all_asm_printers", &initialize_all_asm_printers,
        R"(Initialize all ASM printers.

<sub>C API: LLVMInitializeAllAsmPrinters</sub>)");
  m.def("initialize_all_asm_parsers", &initialize_all_asm_parsers,
        R"(Initialize all ASM parsers.

<sub>C API: LLVMInitializeAllAsmParsers</sub>)");
  m.def("initialize_all_disassemblers", &initialize_all_disassemblers,
        R"(Initialize all disassemblers.

<sub>C API: LLVMInitializeAllDisassemblers</sub>)");
  m.def("get_first_target", &get_first_target,
        R"(Get the first registered target (returns None if no targets).

<sub>C API: LLVMGetFirstTarget</sub>)");

  // Native target initialization
  m.def("initialize_native_target", &initialize_native_target,
        R"(Initialize the native target.
        
        Returns True on success, False on failure.

<sub>C API: LLVMInitializeNativeTarget</sub>)");
  m.def("initialize_native_asm_printer", &initialize_native_asm_printer,
        R"(Initialize the native ASM printer.
        
        Returns True on success, False on failure.

<sub>C API: LLVMInitializeNativeAsmPrinter</sub>)");
  m.def("initialize_native_asm_parser", &initialize_native_asm_parser,
        R"(Initialize the native ASM parser.
        
        Returns True on success, False on failure.

<sub>C API: LLVMInitializeNativeAsmParser</sub>)");
  m.def("initialize_native_disassembler", &initialize_native_disassembler,
        R"(Initialize the native disassembler.
        
        Returns True on success, False on failure.

<sub>C API: LLVMInitializeNativeDisassembler</sub>)");

  // Host target queries
  m.def("get_default_target_triple", &get_default_target_triple,
        R"(Get the default target triple for the current host.

<sub>C API: LLVMGetDefaultTargetTriple</sub>)");
  m.def("normalize_target_triple", &normalize_target_triple, "triple"_a,
        R"(Normalize a target triple string.

<sub>C API: LLVMNormalizeTargetTriple</sub>)");
  m.def("get_host_cpu_name", &get_host_cpu_name,
        R"(Get the host CPU name.

<sub>C API: LLVMGetHostCPUName</sub>)");
  m.def("get_host_cpu_features", &get_host_cpu_features,
        R"(Get the host CPU features as a feature string.

<sub>C API: LLVMGetHostCPUFeatures</sub>)");

  // Target lookup
  m.def("get_target_from_triple", &get_target_from_triple, "triple"_a,
        R"(Get a target from a target triple string.
        
        Raises LLVMError if the target is not found.

<sub>C API: LLVMGetTargetFromTriple</sub>)");
  m.def("get_target_from_name", &get_target_from_name, "name"_a,
        R"(Get a target from its name.
        
        Returns None if the target is not found.

<sub>C API: LLVMGetTargetFromName</sub>)");

  // ==========================================================================
  // Target Machine Enums
  // ==========================================================================

  nb::enum_<LLVMCodeGenOptLevel>(m, "CodeGenOptLevel",
                                 R"(Code generation optimization level.)")
      .value("None_", LLVMCodeGenLevelNone, "No optimization")
      .value("Less", LLVMCodeGenLevelLess, "Less optimization (O1)")
      .value("Default", LLVMCodeGenLevelDefault, "Default optimization (O2)")
      .value("Aggressive", LLVMCodeGenLevelAggressive,
             "Aggressive optimization (O3)");

  nb::enum_<LLVMRelocMode>(m, "RelocMode",
                           R"(Relocation model for code generation.)")
      .value("Default", LLVMRelocDefault, "Default relocation model")
      .value("Static", LLVMRelocStatic, "Static relocation model")
      .value("PIC", LLVMRelocPIC, "Position-independent code")
      .value("DynamicNoPic", LLVMRelocDynamicNoPic, "Dynamic, no PIC");

  nb::enum_<LLVMCodeModel>(m, "CodeModel", R"(Code model for code generation.)")
      .value("Default", LLVMCodeModelDefault, "Default code model")
      .value("JITDefault", LLVMCodeModelJITDefault, "JIT default code model")
      .value("Tiny", LLVMCodeModelTiny, "Tiny code model")
      .value("Small", LLVMCodeModelSmall, "Small code model")
      .value("Kernel", LLVMCodeModelKernel, "Kernel code model")
      .value("Medium", LLVMCodeModelMedium, "Medium code model")
      .value("Large", LLVMCodeModelLarge, "Large code model");

  nb::enum_<LLVMCodeGenFileType>(m, "CodeGenFileType",
                                 R"(Type of file to generate.)")
      .value("AssemblyFile", LLVMAssemblyFile, "Assembly file (.s)")
      .value("ObjectFile", LLVMObjectFile, "Object file (.o)");

  nb::enum_<LLVMGlobalISelAbortMode>(
      m, "GlobalISelAbortMode", R"(Global instruction selection abort mode.)")
      .value("Enable", LLVMGlobalISelAbortEnable)
      .value("Disable", LLVMGlobalISelAbortDisable)
      .value("DisableWithDiag", LLVMGlobalISelAbortDisableWithDiag);

  nb::enum_<LLVMByteOrdering>(m, "ByteOrdering",
                              R"(Byte ordering (endianness).)")
      .value("BigEndian", LLVMBigEndian)
      .value("LittleEndian", LLVMLittleEndian);

  // ==========================================================================
  // Target Data Wrapper
  // ==========================================================================

  nb::class_<LLVMTargetDataWrapper>(m, "TargetData",
                                    R"(Target data layout information.
        
        Provides information about type sizes and alignment for a specific target.)")
      .def("__str__", &LLVMTargetDataWrapper::to_string)
      .def_prop_ro("byte_order", &LLVMTargetDataWrapper::byte_order,
                   R"(Get byte order.

<sub>C API: LLVMByteOrder</sub>)")
      .def("pointer_size", &LLVMTargetDataWrapper::pointer_size,
           "address_space"_a = 0,
           R"(Get pointer size.

<sub>C API: LLVMPointerSizeForAS</sub>)")
      .def("size_of_type_in_bits", &LLVMTargetDataWrapper::size_of_type_in_bits,
           "ty"_a, R"(Get size in bits.

<sub>C API: LLVMSizeOfTypeInBits</sub>)")
      .def("store_size_of_type", &LLVMTargetDataWrapper::store_size_of_type,
           "ty"_a, R"(Get store size.

<sub>C API: LLVMStoreSizeOfType</sub>)")
      .def("abi_size_of_type", &LLVMTargetDataWrapper::abi_size_of_type, "ty"_a,
           R"(Get ABI size.

<sub>C API: LLVMABISizeOfType</sub>)")
      .def("abi_alignment_of_type",
           &LLVMTargetDataWrapper::abi_alignment_of_type, "ty"_a,
           R"(Get ABI alignment.

<sub>C API: LLVMABIAlignmentOfType</sub>)")
      .def("call_frame_alignment_of_type",
           &LLVMTargetDataWrapper::call_frame_alignment_of_type, "ty"_a,
           R"(Get call frame alignment.

<sub>C API: LLVMCallFrameAlignmentOfType</sub>)")
      .def("preferred_alignment_of_type",
           &LLVMTargetDataWrapper::preferred_alignment_of_type, "ty"_a,
           R"(Get preferred alignment.

<sub>C API: LLVMPreferredAlignmentOfType</sub>)")
      .def("preferred_alignment_of_global",
           &LLVMTargetDataWrapper::preferred_alignment_of_global, "gv"_a,
           R"(Get global alignment.

<sub>C API: LLVMPreferredAlignmentOfGlobal</sub>)")
      .def("element_at_offset", &LLVMTargetDataWrapper::element_at_offset,
           "struct_ty"_a, "offset"_a,
           R"(Get element at offset.

<sub>C API: LLVMElementAtOffset</sub>)")
      .def("offset_of_element", &LLVMTargetDataWrapper::offset_of_element,
           "struct_ty"_a, "elem"_a,
           R"(Get element offset.

<sub>C API: LLVMOffsetOfElement</sub>)")
      .def(
          "int_ptr_type", &LLVMTargetDataWrapper::int_ptr_type, "ctx"_a,
          "address_space"_a = 0, nb::rv_policy::take_ownership,
          R"(Get the integer type that is the same size as a pointer on this target.

Args:
    ctx: The LLVM context to create the type in.
    address_space: The address space (default 0).

Returns:
    An integer type with the same bit width as a pointer.

<sub>C API: LLVMIntPtrTypeForASInContext</sub>)");

  m.def("create_target_data", &create_target_data, "string_rep"_a,
        nb::rv_policy::take_ownership,
        R"(Create a target data layout from a string representation.

<sub>C API: LLVMCreateTargetData</sub>)");

  // ==========================================================================
  // Target Machine Wrapper
  // ==========================================================================

  nb::class_<LLVMTargetMachineWrapper>(m, "TargetMachine",
                                       R"(Target machine for code generation.)")
      .def_prop_ro("target", &LLVMTargetMachineWrapper::get_target,
                   R"(Get target.

<sub>C API: LLVMGetTargetMachineTarget</sub>)")
      .def_prop_ro("triple", &LLVMTargetMachineWrapper::get_triple,
                   R"(Get triple.

<sub>C API: LLVMGetTargetMachineTriple</sub>)")
      .def_prop_ro("cpu", &LLVMTargetMachineWrapper::get_cpu,
                   R"(Get CPU.

<sub>C API: LLVMGetTargetMachineCPU</sub>)")
      .def_prop_ro("feature_string",
                   &LLVMTargetMachineWrapper::get_feature_string,
                   R"(Get features.

<sub>C API: LLVMGetTargetMachineFeatureString</sub>)")
      .def("create_data_layout", &LLVMTargetMachineWrapper::create_data_layout,
           nb::rv_policy::take_ownership,
           R"(Create data layout.

<sub>C API: LLVMCreateTargetDataLayout</sub>)")
      .def("set_asm_verbosity", &LLVMTargetMachineWrapper::set_asm_verbosity,
           "verbose"_a,
           R"(Set ASM verbosity.

<sub>C API: LLVMSetTargetMachineAsmVerbosity</sub>)")
      .def("set_fast_isel", &LLVMTargetMachineWrapper::set_fast_isel,
           "enable"_a, R"(Enable/disable fast instruction selection.

<sub>C API: LLVMSetTargetMachineFastISel</sub>)")
      .def("set_global_isel", &LLVMTargetMachineWrapper::set_global_isel,
           "enable"_a,
           R"(Enable/disable global instruction selection.

<sub>C API: LLVMSetTargetMachineGlobalISel</sub>)")
      .def("set_global_isel_abort",
           &LLVMTargetMachineWrapper::set_global_isel_abort, "mode"_a,
           R"(Set global instruction selection abort mode.

<sub>C API: LLVMSetTargetMachineGlobalISelAbort</sub>)")
      .def("set_machine_outliner",
           &LLVMTargetMachineWrapper::set_machine_outliner, "enable"_a,
           R"(Enable/disable the machine outliner pass.

<sub>C API: LLVMSetTargetMachineMachineOutliner</sub>)")
      .def("emit_to_file", &LLVMTargetMachineWrapper::emit_to_file, "mod"_a,
           "filename"_a, "file_type"_a,
           R"(Emit the module to a file.
           
           Args:
               mod: Module to emit
               filename: Output filename
               file_type: Type of file (AssemblyFile or ObjectFile)

<sub>C API: LLVMTargetMachineEmitToFile</sub>)")
      .def("emit_to_memory_buffer",
           &LLVMTargetMachineWrapper::emit_to_memory_buffer, "mod"_a,
           "file_type"_a,
           R"(Emit the module to a memory buffer.
           
           Args:
               mod: Module to emit
               file_type: Type of output (AssemblyFile or ObjectFile)
               
           Returns:
               bytes: The generated output.

<sub>C API: LLVMTargetMachineEmitToMemoryBuffer</sub>)");

  m.def("create_target_machine", &create_target_machine, "target"_a, "triple"_a,
        "cpu"_a = "", "features"_a = "",
        "opt_level"_a = LLVMCodeGenLevelDefault,
        "reloc_mode"_a = LLVMRelocDefault,
        "code_model"_a = LLVMCodeModelDefault, nb::rv_policy::take_ownership,
        R"(Create a target machine for code generation.
        
        Args:
            target: Target to create machine for
            triple: Target triple string
            cpu: CPU name (default: "")
            features: Feature string (default: "")
            opt_level: Optimization level (default: Default)
            reloc_mode: Relocation mode (default: Default)
            code_model: Code model (default: Default)
            
        Returns:
            TargetMachine instance

<sub>C API: LLVMCreateTargetMachine</sub>)");

  // ==========================================================================
  // Pass Builder Options Wrapper
  // ==========================================================================

  nb::class_<LLVMPassBuilderOptionsWrapper>(m, "PassBuilderOptions",
                                            R"(Options for the pass builder.
        
        Used to configure optimization passes when calling run_passes().)")
      .def(nb::init<>(), R"(Create options.

<sub>C API: LLVMCreatePassBuilderOptions</sub>)")
      .def("set_verify_each", &LLVMPassBuilderOptionsWrapper::set_verify_each,
           "verify"_a, R"(Verify the module after each pass.

<sub>C API: LLVMPassBuilderOptionsSetVerifyEach</sub>)")
      .def("set_debug_logging",
           &LLVMPassBuilderOptionsWrapper::set_debug_logging, "enable"_a,
           R"(Enable debug logging for passes.

<sub>C API: LLVMPassBuilderOptionsSetDebugLogging</sub>)")
      .def("set_loop_interleaving",
           &LLVMPassBuilderOptionsWrapper::set_loop_interleaving, "enable"_a,
           R"(Enable loop interleaving optimization.

<sub>C API: LLVMPassBuilderOptionsSetLoopInterleaving</sub>)")
      .def("set_loop_vectorization",
           &LLVMPassBuilderOptionsWrapper::set_loop_vectorization, "enable"_a,
           R"(Enable loop vectorization optimization.

<sub>C API: LLVMPassBuilderOptionsSetLoopVectorization</sub>)")
      .def("set_slp_vectorization",
           &LLVMPassBuilderOptionsWrapper::set_slp_vectorization, "enable"_a,
           R"(Enable SLP (superword-level parallelism) vectorization.

<sub>C API: LLVMPassBuilderOptionsSetSLPVectorization</sub>)")
      .def("set_loop_unrolling",
           &LLVMPassBuilderOptionsWrapper::set_loop_unrolling, "enable"_a,
           R"(Enable loop unrolling optimization.

<sub>C API: LLVMPassBuilderOptionsSetLoopUnrolling</sub>)")
      .def("set_forget_all_scev_in_loop_unroll",
           &LLVMPassBuilderOptionsWrapper::set_forget_all_scev_in_loop_unroll,
           "forget"_a,
           R"(Forget all Scalar Evolution analysis during loop unrolling.

<sub>C API: LLVMPassBuilderOptionsSetForgetAllSCEVInLoopUnroll</sub>)")
      .def("set_licm_mssa_opt_cap",
           &LLVMPassBuilderOptionsWrapper::set_licm_mssa_opt_cap, "cap"_a,
           R"(Set LICM (loop-invariant code motion) MSSA optimization cap.

<sub>C API: LLVMPassBuilderOptionsSetLicmMssaOptCap</sub>)")
      .def("set_licm_mssa_no_acc_for_promotion_cap",
           &LLVMPassBuilderOptionsWrapper::
               set_licm_mssa_no_acc_for_promotion_cap,
           "cap"_a,
           R"(Set LICM MSSA no-acc-for-promotion cap.

<sub>C API: LLVMPassBuilderOptionsSetLicmMssaNoAccForPromotionCap</sub>)")
      .def("set_call_graph_profile",
           &LLVMPassBuilderOptionsWrapper::set_call_graph_profile, "enable"_a,
           R"(Enable call graph profile for PGO.

<sub>C API: LLVMPassBuilderOptionsSetCallGraphProfile</sub>)")
      .def("set_merge_functions",
           &LLVMPassBuilderOptionsWrapper::set_merge_functions, "enable"_a,
           R"(Enable function merging optimization.

<sub>C API: LLVMPassBuilderOptionsSetMergeFunctions</sub>)")
      .def("set_inliner_threshold",
           &LLVMPassBuilderOptionsWrapper::set_inliner_threshold, "threshold"_a,
           R"(Set the inliner threshold (higher = more inlining).

<sub>C API: LLVMPassBuilderOptionsSetInlinerThreshold</sub>)");

  m.def("run_passes", &run_passes, "mod"_a, "passes"_a,
        "target_machine"_a.none() = nullptr, "options"_a.none() = nullptr,
        R"doc(Run optimization passes on a module.
        
        Args:
            mod: Module to optimize
            passes: Pass pipeline string (e.g., 'default<O2>', 'function(simplifycfg)')
            target_machine: Optional target machine for target-specific passes
            options: Optional PassBuilderOptions
            
        Common pass pipelines:
            - 'default<O0>': No optimization
            - 'default<O1>': Light optimization
            - 'default<O2>': Standard optimization  
            - 'default<O3>': Aggressive optimization
            - 'default<Os>': Size optimization
            - 'default<Oz>': Aggressive size optimization
            
        <sub>C API: LLVMRunPasses</sub>)doc");

  // Memory buffer is internal only - not exposed to Python

  // =============================================================================
  // Disassembler Bindings
  // =============================================================================
  // Disassembler Bindings
  // =============================================================================

  nb::class_<LLVMDisasmContextWrapper>(m, "DisasmContext")
      .def_prop_ro("is_valid", &LLVMDisasmContextWrapper::is_valid,
                   R"(Check if disassembler context is valid.

<sub>C API: LLVMCreateDisasmCPUFeatures</sub>)")
      .def("disasm_instruction", &LLVMDisasmContextWrapper::disasm_instruction,
           "bytes"_a, "offset"_a, "pc"_a,
           R"(Disassemble a single instruction.
           
           Args:
               bytes: The byte array containing machine code
               offset: Offset into bytes to start disassembling
               pc: Program counter value for the instruction
               
           Returns:
               Tuple of (bytes_consumed, disassembly_string)
               If bytes_consumed is 0, disassembly failed.

<sub>C API: LLVMDisasmInstruction</sub>)")
      .def("set_options", &LLVMDisasmContextWrapper::set_options, "options"_a,
           R"(Set disassembler options.

Args:
    options: Bitmask of disassembler options (use DisasmOption_* constants)

Returns:
    True on success, False on failure.

<sub>C API: LLVMSetDisasmOptions</sub>)");

  // Disassembler option constants
  m.attr("DisasmOption_UseMarkup") = nb::int_(1);
  m.attr("DisasmOption_PrintImmHex") = nb::int_(2);
  m.attr("DisasmOption_AsmPrinterVariant") = nb::int_(4);
  m.attr("DisasmOption_SetInstrComments") = nb::int_(8);
  m.attr("DisasmOption_PrintLatency") = nb::int_(16);

  m.def("create_disasm_cpu_features", &create_disasm_cpu_features, "triple"_a,
        "cpu"_a = "", "features"_a = "", nb::rv_policy::take_ownership,
        R"(Create a disassembler for the given triple, CPU, and features.
        
        Args:
            triple: Target triple string (e.g., "x86_64-linux-unknown")
            cpu: CPU name (can be empty)
            features: Feature string (can be empty or "NULL")
            
        Returns:
            DisasmContext, or one with is_valid=False if creation failed.

<sub>C API: LLVMCreateDisasmCPUFeatures</sub>)");

  // =============================================================================
  // Object File Bindings
  // =============================================================================

  // BinaryType enum
  nb::enum_<LLVMBinaryType>(m, "BinaryType")
      .value("Archive", LLVMBinaryTypeArchive)
      .value("MachOUniversalBinary", LLVMBinaryTypeMachOUniversalBinary)
      .value("COFFImportFile", LLVMBinaryTypeCOFFImportFile)
      .value("IR", LLVMBinaryTypeIR)
      .value("WinRes", LLVMBinaryTypeWinRes)
      .value("COFF", LLVMBinaryTypeCOFF)
      .value("ELF32L", LLVMBinaryTypeELF32L)
      .value("ELF32B", LLVMBinaryTypeELF32B)
      .value("ELF64L", LLVMBinaryTypeELF64L)
      .value("ELF64B", LLVMBinaryTypeELF64B)
      .value("MachO32L", LLVMBinaryTypeMachO32L)
      .value("MachO32B", LLVMBinaryTypeMachO32B)
      .value("MachO64L", LLVMBinaryTypeMachO64L)
      .value("MachO64B", LLVMBinaryTypeMachO64B)
      .value("Wasm", LLVMBinaryTypeWasm)
      .value("Offload", LLVMBinaryTypeOffload);

  // Binary wrapper
  nb::class_<LLVMBinaryWrapper>(m, "Binary")
      .def_prop_ro(
          "type",
          [](const LLVMBinaryWrapper &self) {
            self.check_valid();
            return self.get_type();
          },
          R"(Get binary type.

<sub>C API: LLVMBinaryGetType</sub>)")
      .def_prop_ro(
          "sections",
          [](LLVMBinaryWrapper &self) {
            self.check_valid();
            LLVMBinaryType type = self.get_type();
            if (!binary_supports_object_iterators(type)) {
              throw LLVMAssertionError(
                  std::string("sections requires an object-file binary (got ") +
                  binary_type_name(type) + ")");
            }
            LLVMSectionIteratorRef ref =
                LLVMObjectFileCopySectionIterator(self.m_ref);
            return new LLVMSectionIteratorWrapper(ref, self.m_ref,
                                                  self.m_token);
          },
          nb::rv_policy::take_ownership,
          R"(Section iterator.

Valid when:
  - binary.type is an object-file type (COFF/ELF/MachO/Wasm)

<sub>C API: LLVMObjectFileCopySectionIterator</sub>)")
      .def_prop_ro(
          "symbols",
          [](LLVMBinaryWrapper &self) {
            self.check_valid();
            LLVMBinaryType type = self.get_type();
            if (!binary_supports_object_iterators(type)) {
              throw LLVMAssertionError(
                  std::string("symbols requires an object-file binary (got ") +
                  binary_type_name(type) + ")");
            }
            LLVMSymbolIteratorRef ref =
                LLVMObjectFileCopySymbolIterator(self.m_ref);
            return new LLVMSymbolIteratorWrapper(ref, self.m_ref, self.m_token);
          },
          nb::rv_policy::take_ownership,
          R"(Symbol iterator.

Valid when:
  - binary.type is an object-file type (COFF/ELF/MachO/Wasm)

<sub>C API: LLVMObjectFileCopySymbolIterator</sub>)")
      .def("copy_to_memory_buffer", &LLVMBinaryWrapper::copy_to_memory_buffer,
           R"(Copy the binary's contents to a memory buffer.
           
           Returns a copy of the binary's backing memory buffer as bytes.
           
           Returns:
               bytes: A copy of the binary data.

<sub>C API: LLVMBinaryCopyMemoryBuffer</sub>)");

  // Section iterator
  nb::class_<LLVMSectionIteratorWrapper>(m, "SectionIterator")
      .def("is_at_end", &LLVMSectionIteratorWrapper::is_at_end,
           R"(Check if iterator is at end.

Valid when:
  - parent binary is still valid (not disposed)

<sub>C API: LLVMObjectFileIsSectionIteratorAtEnd</sub>)")
      .def("move_next", &LLVMSectionIteratorWrapper::move_next,
           R"(Move to next section.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMMoveToNextSection</sub>)")
      .def_prop_ro("name", &LLVMSectionIteratorWrapper::get_name,
                   R"(Section name.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetSectionName</sub>)")
      .def_prop_ro("address", &LLVMSectionIteratorWrapper::get_address,
                   R"(Section address.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetSectionAddress</sub>)")
      .def_prop_ro("size", &LLVMSectionIteratorWrapper::get_size,
                   R"(Section size.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetSectionSize</sub>)")
      .def_prop_ro("contents", &LLVMSectionIteratorWrapper::get_contents,
                   R"(Section contents.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetSectionContents</sub>)")
      .def("contains_symbol", &LLVMSectionIteratorWrapper::contains_symbol,
           "symbol"_a, R"(Check if section contains the given symbol.

Valid when:
  - parent binary is still valid (not disposed)
  - section iterator is not at end (`is_at_end() == False`)
  - symbol iterator is not at end (`symbol.is_at_end() == False`)

<sub>C API: LLVMGetSectionContainsSymbol</sub>)")
      .def(
          "move_to_containing_section",
          [](LLVMSectionIteratorWrapper &self,
             const LLVMSymbolIteratorWrapper &sym) {
            self.check_valid();
            sym.check_valid();
            if (sym.is_at_end()) {
              throw LLVMAssertionError(
                  "move_to_containing_section requires symbol iterator not at "
                  "end");
            }
            LLVMMoveToContainingSection(self.m_ref, sym.m_ref);
          },
          "symbol"_a,
          R"(Move to containing section.

Valid when:
  - parent binary is still valid (not disposed)
  - symbol iterator is not at end (`symbol.is_at_end() == False`)

<sub>C API: LLVMMoveToContainingSection</sub>)")
      .def_prop_ro(
          "relocations",
          [](LLVMSectionIteratorWrapper &self) {
            self.check_valid();
            self.check_not_at_end("SectionIterator.relocations");
            LLVMRelocationIteratorRef ref = LLVMGetRelocations(self.m_ref);
            return new LLVMRelocationIteratorWrapper(ref, self.m_ref,
                                                     self.m_binary_token);
          },
          nb::rv_policy::take_ownership,
          R"(Relocation iterator.

Valid when:
  - parent binary is still valid (not disposed)
  - section iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetRelocations</sub>)")
      // Python iteration support
      .def("__iter__", &LLVMSectionIteratorWrapper::iter,
           nb::rv_policy::reference_internal)
      .def(
          "__next__",
          [](LLVMSectionIteratorWrapper &self) -> LLVMSectionIteratorWrapper * {
            self.check_valid();
            if (self.m_python_iter_started) {
              self.move_next();
            } else {
              self.m_python_iter_started = true;
            }
            if (self.is_at_end()) {
              throw nb::stop_iteration();
            }
            return &self;
          },
          nb::rv_policy::reference_internal);

  // Symbol iterator
  nb::class_<LLVMSymbolIteratorWrapper>(m, "SymbolIterator")
      .def("is_at_end", &LLVMSymbolIteratorWrapper::is_at_end,
           R"(Check if iterator is at end.

Valid when:
  - parent binary is still valid (not disposed)

<sub>C API: LLVMObjectFileIsSymbolIteratorAtEnd</sub>)")
      .def("move_next", &LLVMSymbolIteratorWrapper::move_next,
           R"(Move to next symbol.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMMoveToNextSymbol</sub>)")
      .def_prop_ro("name", &LLVMSymbolIteratorWrapper::get_name,
                   R"(Symbol name.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetSymbolName</sub>)")
      .def_prop_ro("address", &LLVMSymbolIteratorWrapper::get_address,
                   R"(Symbol address.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetSymbolAddress</sub>)")
      .def_prop_ro("size", &LLVMSymbolIteratorWrapper::get_size,
                   R"(Symbol size.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetSymbolSize</sub>)")
      // Python iteration support
      .def("__iter__", &LLVMSymbolIteratorWrapper::iter,
           nb::rv_policy::reference_internal)
      .def(
          "__next__",
          [](LLVMSymbolIteratorWrapper &self) -> LLVMSymbolIteratorWrapper * {
            self.check_valid();
            if (self.m_python_iter_started) {
              self.move_next();
            } else {
              self.m_python_iter_started = true;
            }
            if (self.is_at_end()) {
              throw nb::stop_iteration();
            }
            return &self;
          },
          nb::rv_policy::reference_internal);

  // Relocation iterator
  nb::class_<LLVMRelocationIteratorWrapper>(m, "RelocationIterator")
      .def("is_at_end", &LLVMRelocationIteratorWrapper::is_at_end,
           R"(Check if iterator is at end.

Valid when:
  - parent binary is still valid (not disposed)

<sub>C API: LLVMIsRelocationIteratorAtEnd</sub>)")
      .def("move_next", &LLVMRelocationIteratorWrapper::move_next,
           R"(Move to next relocation.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMMoveToNextRelocation</sub>)")
      .def_prop_ro("offset", &LLVMRelocationIteratorWrapper::get_offset,
                   R"(Offset.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetRelocationOffset</sub>)")
      .def_prop_ro("type", &LLVMRelocationIteratorWrapper::get_type,
                   R"(Type.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetRelocationType</sub>)")
      .def_prop_ro("type_name", &LLVMRelocationIteratorWrapper::get_type_name,
                   R"(Type name.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetRelocationTypeName</sub>)")
      .def_prop_ro("value_string",
                   &LLVMRelocationIteratorWrapper::get_value_string,
                   R"(Value string.

Valid when:
  - parent binary is still valid (not disposed)
  - iterator is not at end (`is_at_end() == False`)

<sub>C API: LLVMGetRelocationValueString</sub>)")
      // Python iteration support
      .def("__iter__", &LLVMRelocationIteratorWrapper::iter,
           nb::rv_policy::reference_internal)
      .def(
          "__next__",
          [](LLVMRelocationIteratorWrapper &self)
              -> LLVMRelocationIteratorWrapper * {
            self.check_valid();
            if (self.m_python_iter_started) {
              self.move_next();
            } else {
              self.m_python_iter_started = true;
            }
            if (self.is_at_end()) {
              throw nb::stop_iteration();
            }
            return &self;
          },
          nb::rv_policy::reference_internal);

  // Binary manager for context manager protocol
  nb::class_<LLVMBinaryManager>(m, "BinaryManager")
      .def("__enter__", &LLVMBinaryManager::enter,
           nb::rv_policy::reference_internal,
           R"(Enter the manager and obtain a Binary.

Valid when:
  - manager is not disposed
  - manager has not already been entered
)")
      .def("__exit__", &LLVMBinaryManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none(),
           R"(Exit the manager and dispose the owned Binary.

Valid when:
  - manager is entered
  - manager is not already disposed
)")
      .def(
          "dispose", &LLVMBinaryManager::dispose,
          R"(Dispose the binary without using a 'with' statement.

Valid when:
  - manager is not already disposed
  - manager has not been entered yet
)");

  // Factory functions for creating binaries
  m.def("create_binary_from_bytes", &create_binary_from_bytes, "data"_a,
        nb::rv_policy::take_ownership,
        R"(Create a binary manager from bytes for use with 'with' statement.
        
        Args:
            data: The bytes containing the object file data
            
        Returns:
            BinaryManager for use in a 'with' statement
            
        Example:
            with llvm.create_binary_from_bytes(data) as binary:
                for section in binary.sections:
                    print(section.name)

<sub>C API: LLVMCreateBinary</sub>)");

  m.def("create_binary_from_file", &create_binary_from_file, "path"_a,
        nb::rv_policy::take_ownership,
        R"(Create a binary manager from a file for use with 'with' statement.
        
        Args:
            path: Path to the object file
            
        Returns:
            BinaryManager for use in a 'with' statement
            
        Example:
            with llvm.create_binary_from_file("test.o") as binary:
                for section in binary.sections:
                    print(section.name)

<sub>C API: LLVMCreateBinary</sub>)");

  // BitReader functions

  // Attribute index constants
  m.attr("AttributeReturnIndex") =
      nb::int_(static_cast<int>(LLVMAttributeReturnIndex));
  m.attr("AttributeFunctionIndex") =
      nb::int_(static_cast<int>(LLVMAttributeFunctionIndex));

  // Static attribute function
  m.def(
      "get_last_enum_attribute_kind",
      []() { return LLVMGetLastEnumAttributeKind(); },
      R"(Get last enum attribute kind.

<sub>C API: LLVMGetLastEnumAttributeKind</sub>)");

  // Static metadata function
  m.def(
      "get_md_kind_id",
      [](const std::string &name) {
        return LLVMGetMDKindID(name.c_str(),
                               static_cast<unsigned>(name.size()));
      },
      "name"_a, R"(Get metadata kind ID.

<sub>C API: LLVMGetMDKindID</sub>)");

  // NOTE: get_module_context has been moved to Module.context property

  // ==========================================================================
  // Diagnostic Handler Support
  // ==========================================================================

  nb::enum_<LLVMDiagnosticSeverity>(m, "DiagnosticSeverity")
      .value("Error", LLVMDSError)
      .value("Warning", LLVMDSWarning)
      .value("Remark", LLVMDSRemark)
      .value("Note", LLVMDSNote);

  // Bitcode parsing API that uses LLVMGetBitcodeModule2 (global context)
  m.def(
      "get_bitcode_module_2",
      [](LLVMMemoryBufferWrapper &membuf) -> LLVMModuleWrapper * {
        membuf.check_valid();
        LLVMModuleRef mod = nullptr;

        if (LLVMGetBitcodeModule2(membuf.m_ref, &mod)) {
          throw LLVMError("Failed to parse bitcode");
        }

        // Get global context and create wrapper
        LLVMContextRef global_ctx = LLVMGetGlobalContext();
        static thread_local auto global_token =
            std::make_shared<ValidityToken>();
        return new LLVMModuleWrapper(mod, global_ctx, global_token);
      },
      nb::rv_policy::take_ownership, "membuf"_a,
      R"(Parse bitcode from memory buffer using global context (uses diagnostic handler).)");

  // ==========================================================================
  // Debug Info Builder
  // ==========================================================================

  nb::class_<LLVMDIBuilderWrapper>(m, "DIBuilder")
      .def("finalize", &LLVMDIBuilderWrapper::finalize,
           R"(Finalize the debug info builder and emit debug info.

<sub>C API: LLVMDIBuilderFinalize</sub>)")
      // File & Scope Creation
      .def("create_file", &LLVMDIBuilderWrapper::create_file, "filename"_a,
           "directory"_a, R"(Create file debug info metadata.

<sub>C API: LLVMDIBuilderCreateFile</sub>)")
      .def("create_compile_unit", &LLVMDIBuilderWrapper::create_compile_unit,
           "lang"_a, "file"_a, "producer"_a, "is_optimized"_a, "flags"_a,
           "runtime_ver"_a, "split_name"_a, "kind"_a, "dwo_id"_a,
           "split_debug_inlining"_a, "debug_info_for_profiling"_a, "sys_root"_a,
           "sdk"_a, R"(Create compile unit debug info.

<sub>C API: LLVMDIBuilderCreateCompileUnit</sub>)")
      .def("create_module", &LLVMDIBuilderWrapper::create_module,
           "parent_scope"_a, "name"_a, "config_macros"_a, "include_path"_a,
           "api_notes_file"_a,
           R"(Create module debug info metadata.

<sub>C API: LLVMDIBuilderCreateModule</sub>)")
      .def("create_namespace", &LLVMDIBuilderWrapper::create_namespace,
           "parent_scope"_a, "name"_a, "export_symbols"_a,
           R"(Create namespace debug info metadata.

<sub>C API: LLVMDIBuilderCreateNameSpace</sub>)")
      .def("create_lexical_block", &LLVMDIBuilderWrapper::create_lexical_block,
           "scope"_a, "file"_a, "line"_a, "column"_a,
           R"(Create lexical block debug info for a scope.

<sub>C API: LLVMDIBuilderCreateLexicalBlock</sub>)")
      // Function & Subroutine Creation
      .def("create_function", &LLVMDIBuilderWrapper::create_function, "scope"_a,
           "name"_a, "linkage_name"_a, "file"_a, "line_no"_a,
           "subroutine_type"_a.none(), "is_local_to_unit"_a, "is_definition"_a,
           "scope_line"_a, "flags"_a, "is_optimized"_a,
           R"(Create function (subprogram) debug info.

<sub>C API: LLVMDIBuilderCreateFunction</sub>)")
      .def("create_subroutine_type",
           &LLVMDIBuilderWrapper::create_subroutine_type, "file"_a,
           "param_types"_a, "flags"_a,
           R"(Create subroutine type debug info (function signature).

<sub>C API: LLVMDIBuilderCreateSubroutineType</sub>)")
      // Type Creation
      .def("create_basic_type", &LLVMDIBuilderWrapper::create_basic_type,
           "name"_a, "size_in_bits"_a, "encoding"_a, "flags"_a,
           R"(Create basic type debug info (int, float, etc.).

<sub>C API: LLVMDIBuilderCreateBasicType</sub>)")
      .def("create_pointer_type", &LLVMDIBuilderWrapper::create_pointer_type,
           "pointee_type"_a, "size_in_bits"_a, "align_in_bits"_a,
           "address_space"_a, "name"_a,
           R"(Create pointer type debug info.

<sub>C API: LLVMDIBuilderCreatePointerType</sub>)")
      .def("create_vector_type", &LLVMDIBuilderWrapper::create_vector_type,
           "size_in_bits"_a, "align_in_bits"_a, "element_type"_a,
           "subscripts"_a,
           R"(Create vector type debug info.

<sub>C API: LLVMDIBuilderCreateVectorType</sub>)")
      .def("create_typedef", &LLVMDIBuilderWrapper::create_typedef, "type"_a,
           "name"_a, "file"_a, "line_no"_a, "scope"_a, "align_in_bits"_a,
           R"(Create typedef debug info.

<sub>C API: LLVMDIBuilderCreateTypedef</sub>)")
      .def("create_struct_type", &LLVMDIBuilderWrapper::create_struct_type,
           "scope"_a, "name"_a, "file"_a, "line_number"_a, "size_in_bits"_a,
           "align_in_bits"_a, "flags"_a, "derived_from"_a.none() = nullptr,
           "elements"_a = std::vector<LLVMMetadataWrapper>{},
           "runtime_lang"_a = 0, "vtable_holder"_a.none() = nullptr,
           "unique_id"_a = "",
           R"(Create struct type debug info.

<sub>C API: LLVMDIBuilderCreateStructType</sub>)")
      .def("create_enumeration_type",
           &LLVMDIBuilderWrapper::create_enumeration_type, "scope"_a, "name"_a,
           "file"_a, "line_number"_a, "size_in_bits"_a, "align_in_bits"_a,
           "elements"_a, "underlying_type"_a,
           R"(Create enumeration type debug info.

<sub>C API: LLVMDIBuilderCreateEnumerationType</sub>)")
      .def("create_forward_decl", &LLVMDIBuilderWrapper::create_forward_decl,
           "tag"_a, "name"_a, "scope"_a, "file"_a, "line"_a, "runtime_lang"_a,
           "size_in_bits"_a, "align_in_bits"_a, "unique_id"_a,
           R"(Create forward declaration for a type.

<sub>C API: LLVMDIBuilderCreateForwardDecl</sub>)")
      .def("create_replaceable_composite_type",
           &LLVMDIBuilderWrapper::create_replaceable_composite_type, "tag"_a,
           "name"_a, "scope"_a, "file"_a, "line"_a, "runtime_lang"_a,
           "size_in_bits"_a, "align_in_bits"_a, "flags"_a, "unique_id"_a,
           R"(Create a temporary replaceable composite type.

<sub>C API: LLVMDIBuilderCreateReplaceableCompositeType</sub>)")
      .def("create_subrange_type", &LLVMDIBuilderWrapper::create_subrange_type,
           "scope"_a, "name"_a, "line"_a, "file"_a, "size_in_bits"_a,
           "align_in_bits"_a, "flags"_a, "element_type"_a,
           "lower_bound"_a.none(), "upper_bound"_a.none(), "stride"_a.none(),
           "bias"_a.none(),
           R"(Create subrange type with metadata bounds.

<sub>C API: LLVMDIBuilderCreateSubrangeType</sub>)")
      .def("create_set_type", &LLVMDIBuilderWrapper::create_set_type, "scope"_a,
           "name"_a, "file"_a, "line"_a, "size_in_bits"_a, "align_in_bits"_a,
           "base_type"_a, R"(Create set type debug info.

<sub>C API: LLVMDIBuilderCreateSetType</sub>)")
      .def("create_dynamic_array_type",
           &LLVMDIBuilderWrapper::create_dynamic_array_type, "scope"_a,
           "name"_a, "line"_a, "file"_a, "size_in_bits"_a, "align_in_bits"_a,
           "element_type"_a, "subscripts"_a, "data_location"_a,
           "associated"_a.none(), "allocated"_a.none(), "rank"_a.none(),
           "bit_stride"_a.none(),
           R"(Create dynamic array type debug info.

<sub>C API: LLVMDIBuilderCreateDynamicArrayType</sub>)")
      // Variable & Expression
      .def("create_parameter_variable",
           &LLVMDIBuilderWrapper::create_parameter_variable, "scope"_a,
           "name"_a, "arg_no"_a, "file"_a, "line_no"_a, "type"_a,
           "always_preserve"_a, "flags"_a,
           R"(Create parameter variable debug info.

<sub>C API: LLVMDIBuilderCreateParameterVariable</sub>)")
      .def("create_auto_variable", &LLVMDIBuilderWrapper::create_auto_variable,
           "scope"_a, "name"_a, "file"_a, "line_no"_a, "type"_a,
           "always_preserve"_a, "flags"_a, "align_in_bits"_a,
           R"(Create local (auto) variable debug info.

<sub>C API: LLVMDIBuilderCreateAutoVariable</sub>)")
      .def("create_global_variable_expression",
           &LLVMDIBuilderWrapper::create_global_variable_expression, "scope"_a,
           "name"_a, "linkage"_a, "file"_a, "line_no"_a, "type"_a,
           "is_local_to_unit"_a, "expr"_a, "decl"_a.none(), "align_in_bits"_a,
           R"(Create global variable expression debug info.

<sub>C API: LLVMDIBuilderCreateGlobalVariableExpression</sub>)")
      .def("create_expression", &LLVMDIBuilderWrapper::create_expression,
           "addr"_a, R"(Create debug info expression from address operations.

<sub>C API: LLVMDIBuilderCreateExpression</sub>)")
      .def("create_constant_value_expression",
           &LLVMDIBuilderWrapper::create_constant_value_expression, "value"_a,
           R"(Create debug info expression for a constant value.

<sub>C API: LLVMDIBuilderCreateConstantValueExpression</sub>)")
      // Label Methods
      .def("create_label", &LLVMDIBuilderWrapper::create_label, "scope"_a,
           "name"_a, "file"_a, "line_no"_a, "always_preserve"_a,
           R"(Create label debug info.

<sub>C API: LLVMDIBuilderCreateLabel</sub>)")
      .def("insert_label_at_end", &LLVMDIBuilderWrapper::insert_label_at_end,
           "label_info"_a, "debug_loc"_a, "block"_a,
           R"(Insert label debug record at end of block.

<sub>C API: LLVMDIBuilderInsertLabelAtEnd</sub>)")
      .def("insert_label_before", &LLVMDIBuilderWrapper::insert_label_before,
           "label_info"_a, "debug_loc"_a, "insert_before"_a,
           R"(Insert label debug record before instruction.

<sub>C API: LLVMDIBuilderInsertLabelBefore</sub>)")
      // Debug Record Insertion
      .def("insert_declare_record_at_end",
           &LLVMDIBuilderWrapper::insert_declare_record_at_end, "storage"_a,
           "var_info"_a, "expr"_a, "debug_loc"_a, "block"_a,
           R"(Insert variable declaration at end of block.

<sub>C API: LLVMDIBuilderInsertDeclareRecordAtEnd</sub>)")
      .def("insert_dbg_value_record_at_end",
           &LLVMDIBuilderWrapper::insert_dbg_value_record_at_end, "val"_a,
           "var_info"_a, "expr"_a, "debug_loc"_a, "block"_a,
           R"(Insert debug value record at end of block.

<sub>C API: LLVMDIBuilderInsertDbgValueRecordAtEnd</sub>)")
      // Array & Subrange
      .def("get_or_create_subrange",
           &LLVMDIBuilderWrapper::get_or_create_subrange, "lo"_a, "count"_a,
           R"(Get or create array subrange descriptor.

<sub>C API: LLVMDIBuilderGetOrCreateSubrange</sub>)")
      .def("get_or_create_array", &LLVMDIBuilderWrapper::get_or_create_array,
           "elements"_a,
           R"(Get or create array.

<sub>C API: LLVMDIBuilderGetOrCreateArray</sub>)")
      // Enumerator Methods
      .def("create_enumerator", &LLVMDIBuilderWrapper::create_enumerator,
           "name"_a, "value"_a, "is_unsigned"_a,
           R"(Create enumerator.

<sub>C API: LLVMDIBuilderCreateEnumerator</sub>)")
      .def("create_enumerator_of_arbitrary_precision",
           &LLVMDIBuilderWrapper::create_enumerator_of_arbitrary_precision,
           "name"_a, "value"_a, "is_unsigned"_a,
           R"(Create enumerator arbitrary precision.

<sub>C API: LLVMDIBuilderCreateEnumeratorOfArbitraryPrecision</sub>)")
      // ObjC & Inheritance Methods
      .def("create_objc_property", &LLVMDIBuilderWrapper::create_objc_property,
           "name"_a, "file"_a, "line_no"_a, "getter_name"_a, "setter_name"_a,
           "property_attributes"_a, "type"_a,
           R"(Create ObjC property.

<sub>C API: LLVMDIBuilderCreateObjCProperty</sub>)")
      .def("create_objc_ivar", &LLVMDIBuilderWrapper::create_objc_ivar,
           "name"_a, "file"_a, "line_no"_a, "size_in_bits"_a, "align_in_bits"_a,
           "offset_in_bits"_a, "flags"_a, "type"_a, "property"_a,
           R"(Create ObjC ivar.

<sub>C API: LLVMDIBuilderCreateObjCIVar</sub>)")
      .def("create_inheritance", &LLVMDIBuilderWrapper::create_inheritance,
           "derived_type"_a, "base_type"_a, "offset_in_bits"_a,
           "v_bptr_offset"_a, "flags"_a,
           R"(Create inheritance.

<sub>C API: LLVMDIBuilderCreateInheritance</sub>)")
      // Import & Macro Methods
      .def("create_imported_module_from_module",
           &LLVMDIBuilderWrapper::create_imported_module_from_module, "scope"_a,
           "import_module"_a, "file"_a, "line"_a, "elements"_a,
           R"(Import from module.

<sub>C API: LLVMDIBuilderCreateImportedModuleFromModule</sub>)")
      .def("create_imported_module_from_alias",
           &LLVMDIBuilderWrapper::create_imported_module_from_alias, "scope"_a,
           "imported_entity"_a, "file"_a, "line"_a, "elements"_a,
           R"(Import from alias.

<sub>C API: LLVMDIBuilderCreateImportedModuleFromAlias</sub>)")
      .def("create_temp_macro_file",
           &LLVMDIBuilderWrapper::create_temp_macro_file,
           "parent_macro_file"_a.none(), "line"_a, "file"_a,
           R"(Create temp macro file.

<sub>C API: LLVMDIBuilderCreateTempMacroFile</sub>)")
      .def("create_macro", &LLVMDIBuilderWrapper::create_macro,
           "parent_macro_file"_a, "line"_a, "macro_type"_a, "name"_a, "value"_a,
           R"(Create macro.

<sub>C API: LLVMDIBuilderCreateMacro</sub>)")
      // Additional DIBuilder Methods
      .def("finalize_subprogram", &LLVMDIBuilderWrapper::finalize_subprogram,
           "subprogram"_a,
           R"(Finalize subprogram.

<sub>C API: LLVMDIBuilderFinalizeSubprogram</sub>)")
      .def("create_member_type", &LLVMDIBuilderWrapper::create_member_type,
           "scope"_a, "name"_a, "file"_a, "line_no"_a, "size_in_bits"_a,
           "align_in_bits"_a, "offset_in_bits"_a, "flags"_a, "type"_a,
           R"(Create member type.

<sub>C API: LLVMDIBuilderCreateMemberType</sub>)")
      .def("create_union_type", &LLVMDIBuilderWrapper::create_union_type,
           "scope"_a, "name"_a, "file"_a, "line_number"_a, "size_in_bits"_a,
           "align_in_bits"_a, "flags"_a, "elements"_a, "runtime_lang"_a,
           "unique_id"_a,
           R"(Create union type.

<sub>C API: LLVMDIBuilderCreateUnionType</sub>)")
      .def("create_array_type", &LLVMDIBuilderWrapper::create_array_type,
           "size_in_bits"_a, "align_in_bits"_a, "element_type"_a,
           "subscripts"_a,
           R"(Create array type.

<sub>C API: LLVMDIBuilderCreateArrayType</sub>)")
      .def("create_qualified_type",
           &LLVMDIBuilderWrapper::create_qualified_type, "tag"_a, "type"_a,
           R"(Create qualified type.

<sub>C API: LLVMDIBuilderCreateQualifiedType</sub>)")
      .def("create_reference_type",
           &LLVMDIBuilderWrapper::create_reference_type, "tag"_a, "type"_a,
           R"(Create reference type.

<sub>C API: LLVMDIBuilderCreateReferenceType</sub>)")
      .def("create_null_ptr_type", &LLVMDIBuilderWrapper::create_null_ptr_type,
           R"(Create nullptr type.

<sub>C API: LLVMDIBuilderCreateNullPtrType</sub>)")
      .def("create_bit_field_member_type",
           &LLVMDIBuilderWrapper::create_bit_field_member_type, "scope"_a,
           "name"_a, "file"_a, "line_no"_a, "size_in_bits"_a,
           "offset_in_bits"_a, "storage_offset_in_bits"_a, "flags"_a, "type"_a,
           R"(Create bitfield member.

<sub>C API: LLVMDIBuilderCreateBitFieldMemberType</sub>)")
      .def("create_artificial_type",
           &LLVMDIBuilderWrapper::create_artificial_type, "type"_a,
           R"(Create artificial type.

<sub>C API: LLVMDIBuilderCreateArtificialType</sub>)")
      .def("get_or_create_type_array",
           &LLVMDIBuilderWrapper::get_or_create_type_array, "types"_a,
           R"(Get type array.

<sub>C API: LLVMDIBuilderGetOrCreateTypeArray</sub>)")
      .def("create_lexical_block_file",
           &LLVMDIBuilderWrapper::create_lexical_block_file, "scope"_a,
           "file"_a, "discriminator"_a,
           R"(Create lexical block file.

<sub>C API: LLVMDIBuilderCreateLexicalBlockFile</sub>)")
      .def("create_imported_declaration",
           &LLVMDIBuilderWrapper::create_imported_declaration, "scope"_a,
           "decl"_a, "file"_a, "line"_a, "name"_a, "elements"_a,
           R"(Create imported decl.

<sub>C API: LLVMDIBuilderCreateImportedDeclaration</sub>)")
      .def("create_imported_module_from_namespace",
           &LLVMDIBuilderWrapper::create_imported_module_from_namespace,
           "scope"_a, "ns"_a, "file"_a, "line"_a,
           R"(Import from namespace.

<sub>C API: LLVMDIBuilderCreateImportedModuleFromNamespace</sub>)")
      // C++ class/member types
      .def("create_class_type", &LLVMDIBuilderWrapper::create_class_type,
           "scope"_a, "name"_a, "file"_a, "line_number"_a, "size_in_bits"_a,
           "align_in_bits"_a, "offset_in_bits"_a, "flags"_a,
           "derived_from"_a.none(), "elements"_a, "vtable_holder"_a.none(),
           "template_params"_a.none(), "unique_id"_a,
           R"(Create a C++ class type.
           
           Args:
               scope: The scope containing this class.
               name: Class name.
               file: The source file.
               line_number: Line number.
               size_in_bits: Size in bits.
               align_in_bits: Alignment in bits.
               offset_in_bits: Offset in bits (for nested classes).
               flags: DI flags.
               derived_from: Base class (optional).
               elements: List of member types.
               vtable_holder: VTable holder (optional).
               template_params: Template parameters (optional).
               unique_id: Unique identifier for the type.

<sub>C API: LLVMDIBuilderCreateClassType</sub>)")
      .def("create_static_member_type",
           &LLVMDIBuilderWrapper::create_static_member_type, "scope"_a,
           "name"_a, "file"_a, "line_no"_a, "type"_a, "flags"_a,
           "const_val"_a.none(), "align_in_bits"_a,
           R"(Create a static member type.
           
           Used for C++ static class members.
           
           Args:
               scope: The scope containing this member.
               name: Member name.
               file: The source file.
               line_no: Line number.
               type: Type of the static member.
               flags: DI flags.
               const_val: Constant initializer value (optional).
               align_in_bits: Alignment in bits.

<sub>C API: LLVMDIBuilderCreateStaticMemberType</sub>)")
      .def("create_member_pointer_type",
           &LLVMDIBuilderWrapper::create_member_pointer_type, "pointee_type"_a,
           "class_type"_a, "size_in_bits"_a, "align_in_bits"_a, "flags"_a,
           R"(Create a C++ member pointer type.
           
           Used for pointer-to-member types (e.g., int MyClass::*).
           
           Args:
               pointee_type: The type being pointed to.
               class_type: The class type.
               size_in_bits: Size in bits.
               align_in_bits: Alignment in bits.
               flags: DI flags.

<sub>C API: LLVMDIBuilderCreateMemberPointerType</sub>)")
      // Insert debug records before an instruction
      .def("insert_declare_record_before",
           &LLVMDIBuilderWrapper::insert_declare_record_before, "storage"_a,
           "var_info"_a, "expr"_a, "debug_loc"_a, "insert_before"_a,
           R"(Insert a declare record before an instruction.
           
           Only use in "new debug format" mode (LLVMIsNewDbgInfoFormat is true).
           
           Args:
               storage: The storage location (alloca).
               var_info: Variable debug info.
               expr: Debug expression.
               debug_loc: Debug location.
               insert_before: Instruction to insert before.

<sub>C API: LLVMDIBuilderInsertDeclareRecordBefore</sub>)")
      .def("insert_dbg_value_record_before",
           &LLVMDIBuilderWrapper::insert_dbg_value_record_before, "val"_a,
           "var_info"_a, "expr"_a, "debug_loc"_a, "insert_before"_a,
           R"(Insert a dbg value record before an instruction.
           
           Only use in "new debug format" mode (LLVMIsNewDbgInfoFormat is true).
           
           Args:
               val: The value to track.
               var_info: Variable debug info.
               expr: Debug expression.
               debug_loc: Debug location.
               insert_before: Instruction to insert before.

<sub>C API: LLVMDIBuilderInsertDbgValueRecordBefore</sub>)")
      // Utility Methods
      .def("replace_arrays", &LLVMDIBuilderWrapper::replace_arrays,
           "composite_types"_a, "arrays"_a,
           R"(Replace arrays in composite type.

<sub>C API: LLVMDIBuilderReplaceArrays</sub>)");

  nb::class_<LLVMDIBuilderManager>(m, "DIBuilderManager")
      .def("__enter__", &LLVMDIBuilderManager::enter,
           nb::rv_policy::reference_internal,
           R"(Enter the manager and obtain a DIBuilder.

Valid when:
  - manager is not disposed
  - manager has not already been entered
  - backing module is still valid
)")
      .def("__exit__", &LLVMDIBuilderManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none(),
           R"(Exit the manager and dispose the owned DIBuilder.

Valid when:
  - manager is entered
  - manager is not already disposed
)")
      .def("dispose", &LLVMDIBuilderManager::dispose,
           R"(Dispose the DIBuilder manager without using context manager.

Valid when:
  - manager is not already disposed
  - manager has not been entered yet
)");

  // NOTE: create_dibuilder has been moved to Module.create_dibuilder() method

  nb::class_<LLVMMetadataWrapper>(m, "Metadata")
      .def("__eq__", [](const LLVMMetadataWrapper &a,
                        const LLVMMetadataWrapper &b) { return a.m_ref == b.m_ref; })
      .def("__ne__", [](const LLVMMetadataWrapper &a,
                        const LLVMMetadataWrapper &b) { return a.m_ref != b.m_ref; })
      .def("__hash__",
           [](const LLVMMetadataWrapper &v) {
             return std::hash<LLVMMetadataRef>{}(v.m_ref);
           })
      .def("as_value", &LLVMMetadataWrapper::as_value, "ctx"_a,
           R"(Convert metadata to value.

<sub>C API: LLVMMetadataAsValue</sub>)")
      .def("replace_all_uses_with", &LLVMMetadataWrapper::replace_all_uses_with,
           "md"_a,
           R"(Replace all uses of this temporary metadata.

<sub>C API: LLVMMetadataReplaceAllUsesWith</sub>)")
      // DI accessors as methods (also available as module-level functions)
      .def_prop_ro("di_node_tag", [](const LLVMMetadataWrapper &self) -> unsigned {
        self.check_valid();
        return LLVMGetDINodeTag(self.m_ref);
      }, R"(Get DWARF tag from this debug info node.

<sub>C API: LLVMGetDINodeTag</sub>)")
      .def_prop_ro("di_type_name", [](const LLVMMetadataWrapper &self) -> std::string {
        self.check_valid();
        size_t len;
        const char *name = LLVMDITypeGetName(self.m_ref, &len);
        return std::string(name, len);
      }, R"(Get name from this debug info type.

<sub>C API: LLVMDITypeGetName</sub>)")
      .def_prop_ro("di_location_line", [](const LLVMMetadataWrapper &self) -> unsigned {
        self.check_valid();
        return LLVMDILocationGetLine(self.m_ref);
      }, R"(Get line number from this debug location.

<sub>C API: LLVMDILocationGetLine</sub>)")
      .def_prop_ro("di_location_column", [](const LLVMMetadataWrapper &self) -> unsigned {
        self.check_valid();
        return LLVMDILocationGetColumn(self.m_ref);
      }, R"(Get column number from this debug location.

<sub>C API: LLVMDILocationGetColumn</sub>)")
      .def_prop_ro("di_location_scope", [](const LLVMMetadataWrapper &self) -> LLVMMetadataWrapper {
        self.check_valid();
        return LLVMMetadataWrapper(LLVMDILocationGetScope(self.m_ref),
                                   self.m_context_token);
      }, R"(Get scope from this debug location.

<sub>C API: LLVMDILocationGetScope</sub>)")
      .def_prop_ro("di_location_inlined_at", [](const LLVMMetadataWrapper &self)
          -> std::optional<LLVMMetadataWrapper> {
        self.check_valid();
        LLVMMetadataRef ref = LLVMDILocationGetInlinedAt(self.m_ref);
        if (ref)
          return LLVMMetadataWrapper(ref, self.m_context_token);
        return std::nullopt;
      }, R"(Get inlined-at location from this debug location, or None.

<sub>C API: LLVMDILocationGetInlinedAt</sub>)")
      .def_prop_ro("di_scope_file", [](const LLVMMetadataWrapper &self)
          -> std::optional<LLVMMetadataWrapper> {
        self.check_valid();
        LLVMMetadataRef ref = LLVMDIScopeGetFile(self.m_ref);
        if (ref)
          return LLVMMetadataWrapper(ref, self.m_context_token);
        return std::nullopt;
      }, R"(Get file from this debug scope, or None.

<sub>C API: LLVMDIScopeGetFile</sub>)")
      .def_prop_ro("di_file_directory", [](const LLVMMetadataWrapper &self) -> std::string {
        self.check_valid();
        unsigned len;
        const char *dir = LLVMDIFileGetDirectory(self.m_ref, &len);
        return std::string(dir, len);
      }, R"(Get directory from this debug file.

<sub>C API: LLVMDIFileGetDirectory</sub>)")
      .def_prop_ro("di_file_filename", [](const LLVMMetadataWrapper &self) -> std::string {
        self.check_valid();
        unsigned len;
        const char *name = LLVMDIFileGetFilename(self.m_ref, &len);
        return std::string(name, len);
      }, R"(Get filename from this debug file.

<sub>C API: LLVMDIFileGetFilename</sub>)")
      .def_prop_ro("di_file_source", [](const LLVMMetadataWrapper &self) -> std::string {
        self.check_valid();
        unsigned len;
        const char *src = LLVMDIFileGetSource(self.m_ref, &len);
        return std::string(src, len);
      }, R"(Get embedded source from this debug file.

<sub>C API: LLVMDIFileGetSource</sub>)")
      .def_prop_ro("di_subprogram_line", [](const LLVMMetadataWrapper &self) -> unsigned {
        self.check_valid();
        return LLVMDISubprogramGetLine(self.m_ref);
      }, R"(Get line number from this debug subprogram.

<sub>C API: LLVMDISubprogramGetLine</sub>)")
      .def_prop_ro("di_variable_file", [](const LLVMMetadataWrapper &self)
          -> std::optional<LLVMMetadataWrapper> {
        self.check_valid();
        LLVMMetadataRef ref = LLVMDIVariableGetFile(self.m_ref);
        if (ref)
          return LLVMMetadataWrapper(ref, self.m_context_token);
        return std::nullopt;
      }, R"(Get file from this debug variable, or None.

<sub>C API: LLVMDIVariableGetFile</sub>)")
      .def_prop_ro("di_variable_scope", [](const LLVMMetadataWrapper &self)
          -> std::optional<LLVMMetadataWrapper> {
        self.check_valid();
        LLVMMetadataRef ref = LLVMDIVariableGetScope(self.m_ref);
        if (ref)
          return LLVMMetadataWrapper(ref, self.m_context_token);
        return std::nullopt;
      }, R"(Get scope from this debug variable, or None.

<sub>C API: LLVMDIVariableGetScope</sub>)")
      .def_prop_ro("di_variable_line", [](const LLVMMetadataWrapper &self) -> unsigned {
        self.check_valid();
        return LLVMDIVariableGetLine(self.m_ref);
      }, R"(Get line number from this debug variable.

<sub>C API: LLVMDIVariableGetLine</sub>)")
      .def_prop_ro("di_gve_variable", [](const LLVMMetadataWrapper &self) -> LLVMMetadataWrapper {
        self.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIGlobalVariableExpressionGetVariable(self.m_ref),
            self.m_context_token);
      }, R"(Get the DIGlobalVariable from this DIGlobalVariableExpression.

<sub>C API: LLVMDIGlobalVariableExpressionGetVariable</sub>)")
      .def_prop_ro("di_gve_expression", [](const LLVMMetadataWrapper &self) -> LLVMMetadataWrapper {
        self.check_valid();
        return LLVMMetadataWrapper(
            LLVMDIGlobalVariableExpressionGetExpression(self.m_ref),
            self.m_context_token);
      }, R"(Get the DIExpression from this DIGlobalVariableExpression.

<sub>C API: LLVMDIGlobalVariableExpressionGetExpression</sub>)");

  m.def(
      "get_di_node_tag",
      [](const LLVMMetadataWrapper &md) -> unsigned {
        md.check_valid();
        return LLVMGetDINodeTag(md.m_ref);
      },
      "md"_a, R"(Get DWARF tag from debug info node.

<sub>C API: LLVMGetDINodeTag</sub>)");

  m.def(
      "di_type_get_name",
      [](const LLVMMetadataWrapper &di_type) -> std::string {
        di_type.check_valid();
        size_t len;
        const char *name = LLVMDITypeGetName(di_type.m_ref, &len);
        return std::string(name, len);
      },
      "di_type"_a, R"(Get name from debug info type.

<sub>C API: LLVMDITypeGetName</sub>)");

  // DILocation accessors
  m.def(
      "di_location_get_line",
      [](const LLVMMetadataWrapper &location) -> unsigned {
        location.check_valid();
        return LLVMDILocationGetLine(location.m_ref);
      },
      "location"_a, R"(Get line number from debug location.

<sub>C API: LLVMDILocationGetLine</sub>)");

  m.def(
      "di_location_get_column",
      [](const LLVMMetadataWrapper &location) -> unsigned {
        location.check_valid();
        return LLVMDILocationGetColumn(location.m_ref);
      },
      "location"_a, R"(Get column number from debug location.

<sub>C API: LLVMDILocationGetColumn</sub>)");

  m.def(
      "di_location_get_scope",
      [](const LLVMMetadataWrapper &location) -> LLVMMetadataWrapper {
        location.check_valid();
        return LLVMMetadataWrapper(LLVMDILocationGetScope(location.m_ref),
                                   location.m_context_token);
      },
      "location"_a, R"(Get scope from debug location.

<sub>C API: LLVMDILocationGetScope</sub>)");

  m.def(
      "di_location_get_inlined_at",
      [](const LLVMMetadataWrapper &location)
          -> std::optional<LLVMMetadataWrapper> {
        location.check_valid();
        LLVMMetadataRef ref = LLVMDILocationGetInlinedAt(location.m_ref);
        if (ref)
          return LLVMMetadataWrapper(ref, location.m_context_token);
        return std::nullopt;
      },
      "location"_a, R"(Get inlined-at location from debug location, or None.

<sub>C API: LLVMDILocationGetInlinedAt</sub>)");

  // Debug metadata version functions
  m.def(
      "debug_metadata_version",
      []() -> unsigned { return LLVMDebugMetadataVersion(); },
      R"(Get the version of debug metadata supported by this LLVM version.

<sub>C API: LLVMDebugMetadataVersion</sub>)");

  m.def(
      "get_module_debug_metadata_version",
      [](const LLVMModuleWrapper &mod) -> unsigned {
        mod.check_valid();
        return LLVMGetModuleDebugMetadataVersion(mod.m_ref);
      },
      "mod"_a, R"(Get the debug metadata version from a module.

<sub>C API: LLVMGetModuleDebugMetadataVersion</sub>)");

  m.def(
      "strip_module_debug_info",
      [](LLVMModuleWrapper &mod) -> bool {
        mod.check_valid();
        return LLVMStripModuleDebugInfo(mod.m_ref);
      },
      "mod"_a, R"(Strip debug info from a module. Returns true if changed.

<sub>C API: LLVMStripModuleDebugInfo</sub>)");

  // DI file/scope query functions
  m.def(
      "di_scope_get_file",
      [](const LLVMMetadataWrapper &scope)
          -> std::optional<LLVMMetadataWrapper> {
        scope.check_valid();
        LLVMMetadataRef ref = LLVMDIScopeGetFile(scope.m_ref);
        if (ref)
          return LLVMMetadataWrapper(ref, scope.m_context_token);
        return std::nullopt;
      },
      "scope"_a, R"(Get file from debug scope, or None.

<sub>C API: LLVMDIScopeGetFile</sub>)");

  m.def(
      "di_file_get_directory",
      [](const LLVMMetadataWrapper &file) -> std::string {
        file.check_valid();
        unsigned len;
        const char *dir = LLVMDIFileGetDirectory(file.m_ref, &len);
        return std::string(dir, len);
      },
      "file"_a, R"(Get directory from debug file.

<sub>C API: LLVMDIFileGetDirectory</sub>)");

  m.def(
      "di_file_get_filename",
      [](const LLVMMetadataWrapper &file) -> std::string {
        file.check_valid();
        unsigned len;
        const char *name = LLVMDIFileGetFilename(file.m_ref, &len);
        return std::string(name, len);
      },
      "file"_a, R"(Get filename from debug file.

<sub>C API: LLVMDIFileGetFilename</sub>)");

  m.def(
      "di_file_get_source",
      [](const LLVMMetadataWrapper &file) -> std::string {
        file.check_valid();
        unsigned len;
        const char *src = LLVMDIFileGetSource(file.m_ref, &len);
        return std::string(src, len);
      },
      "file"_a, R"(Get embedded source from debug file.

<sub>C API: LLVMDIFileGetSource</sub>)");

  // DI subprogram/variable query functions
  m.def(
      "di_subprogram_get_line",
      [](const LLVMMetadataWrapper &subprogram) -> unsigned {
        subprogram.check_valid();
        return LLVMDISubprogramGetLine(subprogram.m_ref);
      },
      "subprogram"_a, R"(Get line number from debug subprogram.

<sub>C API: LLVMDISubprogramGetLine</sub>)");

  m.def(
      "di_variable_get_file",
      [](const LLVMMetadataWrapper &variable)
          -> std::optional<LLVMMetadataWrapper> {
        variable.check_valid();
        LLVMMetadataRef ref = LLVMDIVariableGetFile(variable.m_ref);
        if (ref)
          return LLVMMetadataWrapper(ref, variable.m_context_token);
        return std::nullopt;
      },
      "variable"_a, R"(Get file from debug variable, or None.

<sub>C API: LLVMDIVariableGetFile</sub>)");

  m.def(
      "di_variable_get_scope",
      [](const LLVMMetadataWrapper &variable)
          -> std::optional<LLVMMetadataWrapper> {
        variable.check_valid();
        LLVMMetadataRef ref = LLVMDIVariableGetScope(variable.m_ref);
        if (ref)
          return LLVMMetadataWrapper(ref, variable.m_context_token);
        return std::nullopt;
      },
      "variable"_a, R"(Get scope from debug variable, or None.

<sub>C API: LLVMDIVariableGetScope</sub>)");

  m.def(
      "di_variable_get_line",
      [](const LLVMMetadataWrapper &variable) -> unsigned {
        variable.check_valid();
        return LLVMDIVariableGetLine(variable.m_ref);
      },
      "variable"_a, R"(Get line number from debug variable.

<sub>C API: LLVMDIVariableGetLine</sub>)");

  // DIGlobalVariableExpression accessors
  m.def(
      "di_global_variable_expression_get_variable",
      [](const LLVMMetadataWrapper &gve) -> LLVMMetadataWrapper {
        gve.check_valid();
        LLVMMetadataRef ref =
            LLVMDIGlobalVariableExpressionGetVariable(gve.m_ref);
        return LLVMMetadataWrapper(ref, gve.m_context_token);
      },
      "gve"_a,
      R"(Get the DIGlobalVariable from a DIGlobalVariableExpression.

<sub>C API: LLVMDIGlobalVariableExpressionGetVariable</sub>)");

  m.def(
      "di_global_variable_expression_get_expression",
      [](const LLVMMetadataWrapper &gve) -> LLVMMetadataWrapper {
        gve.check_valid();
        LLVMMetadataRef ref =
            LLVMDIGlobalVariableExpressionGetExpression(gve.m_ref);
        return LLVMMetadataWrapper(ref, gve.m_context_token);
      },
      "gve"_a,
      R"(Get the DIExpression from a DIGlobalVariableExpression.

<sub>C API: LLVMDIGlobalVariableExpressionGetExpression</sub>)");

  // ==========================================================================
  // Intrinsic lookup
  // ==========================================================================

  m.def(
      "lookup_intrinsic_id",
      [](const std::string &name) -> unsigned {
        return LLVMLookupIntrinsicID(name.c_str(), name.size());
      },
      "name"_a,
      R"(Look up an intrinsic ID by name.
      
      Args:
          name: Intrinsic name (e.g., "llvm.memcpy")
          
      Returns:
          Intrinsic ID (0 if not found).

<sub>C API: LLVMLookupIntrinsicID</sub>)");

  m.def(
      "intrinsic_is_overloaded",
      [](unsigned id) -> bool { return LLVMIntrinsicIsOverloaded(id); }, "id"_a,
      R"(Check if an intrinsic is overloaded.
      
      Overloaded intrinsics require type parameters to get a declaration.

<sub>C API: LLVMIntrinsicIsOverloaded</sub>)");

  m.def(
      "intrinsic_get_name",
      [](unsigned id) -> std::string {
        size_t len;
        const char *name = LLVMIntrinsicGetName(id, &len);
        return std::string(name, len);
      },
      "id"_a,
      R"(Get the name of an intrinsic by ID.

<sub>C API: LLVMIntrinsicGetName</sub>)");

  m.def("intrinsic_get_type", &intrinsic_get_type, "ctx"_a, "id"_a,
        "param_types"_a,
        R"(Get the type of an intrinsic function.
        
        Args:
            ctx: The LLVM context.
            id: The intrinsic ID.
            param_types: List of parameter types for overloaded intrinsics.
            
        Returns:
            The function type of the intrinsic.

<sub>C API: LLVMIntrinsicGetType</sub>)");

  m.def("get_cast_opcode", &get_cast_opcode, "src"_a, "src_is_signed"_a,
        "dest_ty"_a, "dest_is_signed"_a,
        R"(Get the appropriate cast opcode for converting between types.
        
        Args:
            src: The source value.
            src_is_signed: Whether the source is signed.
            dest_ty: The destination type.
            dest_is_signed: Whether the destination is signed.
            
        Returns:
            The LLVMOpcode for the appropriate cast instruction.

<sub>C API: LLVMGetCastOpcode</sub>)");

  m.def("replace_md_node_operand_with", &replace_md_node_operand_with, "val"_a,
        "index"_a, "replacement"_a,
        R"(Replace a metadata operand in a value's metadata node.
        
        Args:
            val: The value containing the metadata node.
            index: The operand index to replace.
            replacement: The new metadata to use.

<sub>C API: LLVMReplaceMDNodeOperandWith</sub>)");

  m.def(
      "dibuilder_create_debug_location",
      [](LLVMContextWrapper &ctx, unsigned line, unsigned column,
         const LLVMMetadataWrapper &scope,
         const LLVMMetadataWrapper *inlined_at) -> LLVMMetadataWrapper {
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

  // NOTE: set_subprogram has been moved to Function.set_subprogram() method

  m.def(
      "di_subprogram_replace_type",
      [](const LLVMMetadataWrapper &subprogram,
         const LLVMMetadataWrapper &type) {
        subprogram.check_valid();
        type.check_valid();
        LLVMDISubprogramReplaceType(subprogram.m_ref, type.m_ref);
      },
      "subprogram"_a, "type"_a, R"(Replace subprogram type.)");

  // ==========================================================================
  // Builder Positioning and Debug Records
  // ==========================================================================

  // NOTE: set_is_new_dbg_info_format and is_new_dbg_info_format have been
  // moved to Module.is_new_dbg_info_format property

  // Debug record iteration (opaque DbgRecord type - kept as global functions)
  m.def(
      "get_first_dbg_record",
      [](const LLVMValueWrapper &instr) -> void * {
        instr.check_valid();
        if (!instr.is_a_instruction())
          throw LLVMAssertionError(
              "get_first_dbg_record requires an instruction value");
        return LLVMGetFirstDbgRecord(instr.m_ref);
      },
      "instr"_a, R"(Get first debug record attached to instruction.)");

  m.def(
      "get_last_dbg_record",
      [](const LLVMValueWrapper &instr) -> void * {
        instr.check_valid();
        if (!instr.is_a_instruction())
          throw LLVMAssertionError(
              "get_last_dbg_record requires an instruction value");
        return LLVMGetLastDbgRecord(instr.m_ref);
      },
      "instr"_a, R"(Get last debug record attached to instruction.)");

  m.def(
      "get_next_dbg_record",
      [](void *dbg_record) -> void * {
        return LLVMGetNextDbgRecord((LLVMDbgRecordRef)dbg_record);
      },
      "dbg_record"_a, R"(Get next debug record.)");

  m.def(
      "get_previous_dbg_record",
      [](void *dbg_record) -> void * {
        return LLVMGetPreviousDbgRecord((LLVMDbgRecordRef)dbg_record);
      },
      "dbg_record"_a, R"(Get previous debug record.)");

  // Constants for DIFlags
  m.attr("DIFlagZero") = nb::int_((unsigned)LLVMDIFlagZero);
  m.attr("DIFlagPrivate") = nb::int_((unsigned)LLVMDIFlagPrivate);
  m.attr("DIFlagProtected") = nb::int_((unsigned)LLVMDIFlagProtected);
  m.attr("DIFlagPublic") = nb::int_((unsigned)LLVMDIFlagPublic);
  m.attr("DIFlagFwdDecl") = nb::int_((unsigned)LLVMDIFlagFwdDecl);
  m.attr("DIFlagObjcClassComplete") =
      nb::int_((unsigned)LLVMDIFlagObjcClassComplete);

  // Constants for DWARF
  m.attr("DWARFSourceLanguageC") = nb::int_((unsigned)LLVMDWARFSourceLanguageC);
  m.attr("DWARFEmissionFull") = nb::int_((unsigned)LLVMDWARFEmissionFull);
  m.attr("DWARFMacinfoRecordTypeDefine") =
      nb::int_((unsigned)LLVMDWARFMacinfoRecordTypeDefine);
}
