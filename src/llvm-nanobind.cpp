#include <nanobind/nanobind.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/vector.h>

#include <atomic>
#include <memory>
#include <stdexcept>

#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>

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

// =============================================================================
// Type Wrapper
// =============================================================================

struct LLVMTypeWrapper {
  LLVMTypeRef m_ref = nullptr;
  std::shared_ptr<ValidityToken> m_context_token;

  LLVMTypeWrapper() = default;
  LLVMTypeWrapper(LLVMTypeRef ref, std::shared_ptr<ValidityToken> token)
      : m_ref(ref), m_context_token(std::move(token)) {}

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

  void erase() {
    check_valid();
    LLVMDeleteFunction(m_ref);
    m_ref = nullptr;
  }
};

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

  ~LLVMModuleWrapper() {
    if (m_ref) {
      LLVMDisposeModule(m_ref);
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

  std::optional<LLVMValueWrapper> get_global(const std::string &name) {
    check_valid();
    LLVMValueRef global = LLVMGetNamedGlobal(m_ref, name.c_str());
    if (!global)
      return std::nullopt;
    return LLVMValueWrapper(global, m_context_token);
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

  // Clone
  LLVMModuleWrapper *clone() const {
    check_valid();
    LLVMModuleRef cloned = LLVMCloneModule(m_ref);
    auto *wrapper = new LLVMModuleWrapper();
    wrapper->m_ref = cloned;
    wrapper->m_context_token = m_context_token;
    wrapper->m_token = std::make_shared<ValidityToken>();
    wrapper->m_ctx_ref = m_ctx_ref;
    return wrapper;
  }

private:
  // Private constructor for clone
  LLVMModuleWrapper() = default;
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

  LLVMModuleManager(std::string name, LLVMContextWrapper *context)
      : m_name(std::move(name)), m_context(context) {}

  LLVMModuleWrapper &enter() {
    if (m_module)
      throw LLVMException("Module manager already entered");
    if (!m_context)
      throw LLVMException("No context provided");
    m_context->check_valid();
    m_module = std::make_unique<LLVMModuleWrapper>(m_name, m_context->m_ref,
                                                   m_context->m_token);
    return *m_module;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (!m_module)
      throw LLVMException("Module manager not entered");
    m_module.reset();
  }
};

// =============================================================================
// Builder Manager for Python with statement
// =============================================================================

struct LLVMBuilderManager : NoMoveCopy {
  LLVMContextWrapper *m_context = nullptr;
  std::unique_ptr<LLVMBuilderWrapper> m_builder;

  explicit LLVMBuilderManager(LLVMContextWrapper *context)
      : m_context(context) {}

  LLVMBuilderWrapper &enter() {
    if (m_builder)
      throw LLVMException("Builder manager already entered");
    if (!m_context)
      throw LLVMException("No context provided");
    m_context->check_valid();
    m_builder =
        std::make_unique<LLVMBuilderWrapper>(m_context->m_ref, m_context->m_token);
    return *m_builder;
  }

  void exit(const nb::object &, const nb::object &, const nb::object &) {
    if (!m_builder)
      throw LLVMException("Builder manager not entered");
    m_builder.reset();
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

  nb::enum_<LLVMTypeKind>(m, "TypeKind")
      .value("Void", LLVMVoidTypeKind)
      .value("Half", LLVMHalfTypeKind)
      .value("Float", LLVMFloatTypeKind)
      .value("Double", LLVMDoubleTypeKind)
      .value("FP128", LLVMFP128TypeKind)
      .value("Label", LLVMLabelTypeKind)
      .value("Integer", LLVMIntegerTypeKind)
      .value("Function", LLVMFunctionTypeKind)
      .value("Struct", LLVMStructTypeKind)
      .value("Array", LLVMArrayTypeKind)
      .value("Pointer", LLVMPointerTypeKind)
      .value("Vector", LLVMVectorTypeKind)
      .value("Metadata", LLVMMetadataTypeKind)
      .value("Token", LLVMTokenTypeKind)
      .value("ScalableVector", LLVMScalableVectorTypeKind)
      .value("BFloat", LLVMBFloatTypeKind)
      .export_values();

  // Type wrapper
  nb::class_<LLVMTypeWrapper>(m, "Type")
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
      .def("set_body", &struct_set_body, "elem_types"_a, "packed"_a = false);

  // Value wrapper
  nb::class_<LLVMValueWrapper>(m, "Value")
      .def_prop_ro("type", &LLVMValueWrapper::type)
      .def_prop_rw("name", &LLVMValueWrapper::get_name,
                   &LLVMValueWrapper::set_name)
      .def("__str__", &LLVMValueWrapper::to_string)
      .def("__repr__", &LLVMValueWrapper::to_string)
      .def_prop_ro("is_constant", &LLVMValueWrapper::is_constant)
      .def_prop_ro("is_undef", &LLVMValueWrapper::is_undef)
      .def_prop_ro("is_poison", &LLVMValueWrapper::is_poison)
      .def("add_incoming", &phi_add_incoming, "val"_a, "bb"_a)
      .def("add_case", &switch_add_case, "val"_a, "bb"_a)
      .def("set_initializer", &global_set_initializer, "init"_a)
      .def("set_constant", &global_set_constant, "is_const"_a)
      .def("set_linkage", &global_set_linkage, "linkage"_a)
      .def("set_alignment", &global_set_alignment, "align"_a);

  // BasicBlock wrapper
  nb::class_<LLVMBasicBlockWrapper>(m, "BasicBlock")
      .def_prop_ro("name", &LLVMBasicBlockWrapper::get_name)
      .def("as_value", &LLVMBasicBlockWrapper::as_value)
      .def_prop_ro("next_block", &LLVMBasicBlockWrapper::next_block)
      .def_prop_ro("prev_block", &LLVMBasicBlockWrapper::prev_block)
      .def_prop_ro("terminator", &LLVMBasicBlockWrapper::terminator)
      .def_prop_ro("first_instruction",
                   &LLVMBasicBlockWrapper::first_instruction)
      .def_prop_ro("last_instruction", &LLVMBasicBlockWrapper::last_instruction);

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
      .def("erase", &LLVMFunctionWrapper::erase);

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
      .def("mul", &LLVMBuilderWrapper::mul, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("nsw_mul", &LLVMBuilderWrapper::nsw_mul, "lhs"_a, "rhs"_a,
           "name"_a = "")
      .def("sdiv", &LLVMBuilderWrapper::sdiv, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("udiv", &LLVMBuilderWrapper::udiv, "lhs"_a, "rhs"_a, "name"_a = "")
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
      .def("fneg", &LLVMBuilderWrapper::fneg, "val"_a, "name"_a = "")
      .def("not_", &LLVMBuilderWrapper::not_, "val"_a, "name"_a = "")
      // Bitwise
      .def("shl", &LLVMBuilderWrapper::shl, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("lshr", &LLVMBuilderWrapper::lshr, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("ashr", &LLVMBuilderWrapper::ashr, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("and_", &LLVMBuilderWrapper::and_, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("or_", &LLVMBuilderWrapper::or_, "lhs"_a, "rhs"_a, "name"_a = "")
      .def("xor_", &LLVMBuilderWrapper::xor_, "lhs"_a, "rhs"_a, "name"_a = "")
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
      .def("phi", &LLVMBuilderWrapper::phi, "ty"_a, "name"_a = "");

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
      .def("get_global", &LLVMModuleWrapper::get_global, "name"_a)
      .def("__str__", &LLVMModuleWrapper::to_string)
      .def("to_string", &LLVMModuleWrapper::to_string)
      .def("verify", &LLVMModuleWrapper::verify)
      .def("get_verification_error", &LLVMModuleWrapper::get_verification_error)
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
      .def("pointer_type", &LLVMContextWrapper::pointer_type,
           "address_space"_a = 0)
      .def("array_type", &LLVMContextWrapper::array_type, "elem_ty"_a,
           "count"_a)
      .def("vector_type", &LLVMContextWrapper::vector_type, "elem_ty"_a,
           "elem_count"_a)
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
           nb::rv_policy::take_ownership);

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
           "exc_value"_a.none(), "traceback"_a.none());

  // Builder manager
  nb::class_<LLVMBuilderManager>(m, "BuilderManager")
      .def("__enter__", &LLVMBuilderManager::enter,
           nb::rv_policy::reference_internal)
      .def("__exit__", &LLVMBuilderManager::exit, "exc_type"_a.none(),
           "exc_value"_a.none(), "traceback"_a.none());

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
}
