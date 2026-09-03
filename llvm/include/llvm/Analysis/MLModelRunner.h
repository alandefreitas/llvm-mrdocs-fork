//===- MLModelRunner.h ---- ML model runner interface -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//

#ifndef LLVM_ANALYSIS_MLMODELRUNNER_H
#define LLVM_ANALYSIS_MLMODELRUNNER_H

#include "llvm/Analysis/TensorSpec.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class LLVMContext;

/// Interface for evaluating an ML model given tensors as arguments.
///
/// More abstractly, this evaluates a function that takes tensors as arguments,
/// described via TensorSpecs, and returns a tensor. Currently, the latter is
/// assumed to be a scalar, in absence of more elaborate scenarios.
/// NOTE: feature indices are expected to be consistent all accross
/// MLModelRunners (pertaining to the same model), and also Loggers (see
/// TFUtils.h)
class MLModelRunner {
public:
  /// Deleted copy constructor; MLModelRunner is not copyable.
  /// @param Other Unused; copy construction is deleted.
  MLModelRunner(const MLModelRunner &Other) = delete;
  /// Deleted copy assignment; MLModelRunner is not assignable.
  /// @param Other Unused; copy assignment is deleted.
  MLModelRunner &operator=(const MLModelRunner &Other) = delete;
  /// Destroy this MLModelRunner.
  virtual ~MLModelRunner() = default;

  /// Evaluate the model and return the result cast to type \p T.
  /// @tparam T Element type of the scalar result.
  /// @return Model output reinterpreted as \p T.
  template <typename T> T evaluate() {
    return *reinterpret_cast<T *>(evaluateUntyped());
  }

  /// Return a mutable pointer to the buffer for feature \p FeatureID.
  /// @tparam T Element type of the tensor buffer.
  /// @tparam I Feature identifier type convertible to size_t.
  /// @param FeatureID Index or enum identifying the input feature.
  /// @return Mutable pointer to the tensor buffer for \p FeatureID.
  template <typename T, typename I> T *getTensor(I FeatureID) {
    return reinterpret_cast<T *>(
        getTensorUntyped(static_cast<size_t>(FeatureID)));
  }

  /// Return a const pointer to the buffer for feature \p FeatureID.
  /// @tparam T Element type of the tensor buffer.
  /// @tparam I Feature identifier type convertible to size_t.
  /// @param FeatureID Index or enum identifying the input feature.
  /// @return Const pointer to the tensor buffer for \p FeatureID.
  template <typename T, typename I> const T *getTensor(I FeatureID) const {
    return reinterpret_cast<const T *>(
        getTensorUntyped(static_cast<size_t>(FeatureID)));
  }

  /// Return an untyped mutable pointer to the input buffer at \p Index.
  /// @param Index Zero-based index of the input tensor buffer.
  /// @return Mutable void pointer to the buffer, or null if unset.
  void *getTensorUntyped(size_t Index) { return InputBuffers[Index]; }
  /// Return an untyped const pointer to the input buffer at \p Index.
  /// @param Index Zero-based index of the input tensor buffer.
  /// @return Const void pointer to the buffer, or null if unset.
  const void *getTensorUntyped(size_t Index) const {
    return (const_cast<MLModelRunner *>(this))->getTensorUntyped(Index);
  }

  /// Kind of concrete MLModelRunner implementation.
  enum class Kind : int {
    Unknown,     ///< Placeholder; not a valid runner type.
    Release,     ///< AOT-compiled / release-mode model runner.
    Development, ///< Training or development-mode model runner.
    NoOp,        ///< No-op runner that does not evaluate a real model.
    Interactive  ///< Runner that exchanges tensors with an external agent.
  };
  /// Return the concrete runner kind of this instance.
  /// @return The Kind value identifying this runner's implementation.
  Kind getKind() const { return Type; }
  /// Switch the evaluation context to the named context \p Name.
  /// @param Name New context name to switch to.
  virtual void switchContext(StringRef Name) {}

protected:
  /// Construct an MLModelRunner of the given kind with \p NumInputs buffers.
  /// @param Ctx LLVM context used for diagnostics.
  /// @param Type Concrete runner kind; must not be Kind::Unknown.
  /// @param NumInputs Number of input tensor buffers to allocate slots for.
  MLModelRunner(LLVMContext &Ctx, Kind Type, size_t NumInputs)
      : Ctx(Ctx), Type(Type), InputBuffers(NumInputs) {
    assert(Type != Kind::Unknown);
  }
  /// Run the model and return an untyped pointer to the result buffer.
  /// @return Void pointer to the evaluation result.
  virtual void *evaluateUntyped() = 0;

  /// Bind input buffer \p Index to \p Buffer, or allocate owned storage.
  /// @param Index Zero-based index of the input tensor.
  /// @param Spec Tensor specification used when allocating owned storage.
  /// @param Buffer Existing buffer to use, or null to allocate one.
  void setUpBufferForTensor(size_t Index, const TensorSpec &Spec,
                            void *Buffer) {
    if (!Buffer) {
      OwnedBuffers.emplace_back(Spec.getTotalTensorBufferSize());
      Buffer = OwnedBuffers.back().data();
    }
    InputBuffers[Index] = Buffer;
  }

  /// LLVM context used for diagnostics by this runner.
  LLVMContext &Ctx;
  /// Concrete kind of this MLModelRunner instance.
  const Kind Type;

private:
  std::vector<void *> InputBuffers;
  std::vector<std::vector<char *>> OwnedBuffers;
};
} // namespace llvm

#endif // LLVM_ANALYSIS_MLMODELRUNNER_H
