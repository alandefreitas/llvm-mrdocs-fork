//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a model runner wrapping an EmitC compiled ML model.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_EMITCMODELRUNNER_H
#define LLVM_ANALYSIS_EMITCMODELRUNNER_H

#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"

#include <type_traits>

namespace llvm {

/// MLModelRunner that evaluates an EmitC-compiled ML model.
///
/// Wraps a compile-time EmitC model (\p TGen) for inference.
template <class TGen> class EmitCModelRunner final : public MLModelRunner {
public:
  /// Construct a runner over an EmitC-compiled model.
  ///
  /// InputSpec's type should be an indexed collection of TensorSpec values,
  /// like std::array or std::vector, that has a size() method.
  /// @param Ctx LLVM context used for diagnostics.
  /// @param InputSpec Indexed collection of TensorSpec describing model inputs.
  template <class FType>
  EmitCModelRunner(LLVMContext &Ctx, const FType &InputSpec)
      : MLModelRunner(Ctx, MLModelRunner::Kind::Release, InputSpec.size()) {
    for (auto [I, Spec] : llvm::enumerate(InputSpec))
      populateTensor(I, Spec);
  }

  /// Destroy this EmitCModelRunner.
  ~EmitCModelRunner() override = default;

  /// Return true if \p R is an EmitCModelRunner.
  /// @param R Model runner to test.
  /// @return True if \p R is an EmitCModelRunner.
  static bool classof(const MLModelRunner *R) {
    return R->getKind() == MLModelRunner::Kind::Release;
  }

protected:
  /// Run the EmitC-compiled model and return a pointer to the result.
  /// @return Pointer to the stored model evaluation result.
  void *evaluateUntyped() override { return evaluateImpl(); }

private:
  void populateTensor(size_t Pos, const TensorSpec &Spec) {
    void *Buffer = nullptr;
    auto It = CompiledModel.reflectionMap.find(Spec.name());
    if (It != CompiledModel.reflectionMap.end())
      Buffer = static_cast<void *>(It->second);
    setUpBufferForTensor(Pos, Spec, Buffer);
  }

  using ResultType = decltype(std::declval<TGen>()());
  static_assert(!std::is_void_v<ResultType>,
                "EmitCModelRunner models must return a non-void result.");

  void *evaluateImpl() {
    Result = CompiledModel();
    return &Result;
  }

  ResultType Result = {};
  TGen CompiledModel = {};
};

} // namespace llvm

#endif // LLVM_ANALYSIS_EMITCMODELRUNNER_H
