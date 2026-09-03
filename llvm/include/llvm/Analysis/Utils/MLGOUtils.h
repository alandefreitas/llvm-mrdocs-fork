//===- MLGOUtils.h - Utilities for MLGO Release Mode ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file provides helper functions for creating MLModelRunners and checking
/// model validity in release mode.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_UTILS_MLGOUTILS_H
#define LLVM_ANALYSIS_UTILS_MLGOUTILS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Analysis/InteractiveModelRunner.h"
#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/Analysis/ReleaseModeModelRunner.h"
#include "llvm/Analysis/TensorSpec.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/CommandLine.h"
#include <memory>
#include <string>
#include <vector>

namespace llvm {

/// Check whether a release-mode ML advisor has a valid model to execute.
///
/// Overload for \c cl::opt<EnumType>.
/// \param InteractiveChannelBaseName Base name of the interactive channel; if
/// non-empty, the model is considered valid.
/// \param SelectedModel Command-line option holding the currently selected
/// model.
/// \param DefaultModelVal Default model value; when neither an embedded nor an
/// interactive model is available, validity requires \p SelectedModel to differ
/// from this.
/// \return True if an embedded, interactive, or non-default selected model is
/// available.
template <class CompiledModelType, class EnumType, bool ExternalStorage,
          class ParserClass>
bool isReleaseModelValid(
    StringRef InteractiveChannelBaseName,
    const cl::opt<EnumType, ExternalStorage, ParserClass> &SelectedModel,
    EnumType DefaultModelVal = EnumType::Default) {
  return isEmbeddedModelEvaluatorValid<CompiledModelType>() ||
         !InteractiveChannelBaseName.empty() ||
         SelectedModel != DefaultModelVal;
}

/// Check whether a release-mode ML advisor has a valid model to execute.
///
/// Overload for plain \c EnumType.
/// \param InteractiveChannelBaseName Base name of the interactive channel; if
/// non-empty, the model is considered valid.
/// \param SelectedModel Currently selected model value.
/// \param DefaultModelVal Default model value; when neither an embedded nor an
/// interactive model is available, validity requires \p SelectedModel to differ
/// from this.
/// \return True if an embedded, interactive, or non-default selected model is
/// available.
template <class CompiledModelType, class EnumType>
bool isReleaseModelValid(StringRef InteractiveChannelBaseName,
                         EnumType SelectedModel,
                         EnumType DefaultModelVal = EnumType::Default) {
  return isEmbeddedModelEvaluatorValid<CompiledModelType>() ||
         !InteractiveChannelBaseName.empty() ||
         SelectedModel != DefaultModelVal;
}

/// Construct the appropriate MLModelRunner for release mode.
///
/// Chooses among:
/// 1. InteractiveModelRunner if an interactive channel is specified.
/// 2. EmitCModelRunner if MLIR lowering is enabled.
/// 3. ReleaseModeModelRunner<CompiledModelType> otherwise.
/// \param Ctx LLVM context used to construct the runner.
/// \param InputFeatures Specs describing the model input tensors.
/// \param DecisionName Name of the decision/output tensor.
/// \param InteractiveChannelBaseName Base name for interactive pipes; if
/// non-empty, an InteractiveModelRunner is created.
/// \param InteractiveDecisionSpec TensorSpec for the interactive decision.
/// \param CreateEmitCModelRunner Callable that builds an EmitC model runner
/// when MLIR lowering is enabled.
/// \param Options Options for the embedded release-mode model runner.
/// \return A unique pointer to the constructed interactive, EmitC, or
/// release-mode model runner.
template <class CompiledModelType, bool HaveMLIRLowering, class CreateEmitCFunc>
std::unique_ptr<MLModelRunner> createReleaseModeModelRunner(
    LLVMContext &Ctx, const std::vector<TensorSpec> &InputFeatures,
    StringRef DecisionName, const std::string &InteractiveChannelBaseName,
    const TensorSpec &InteractiveDecisionSpec,
    CreateEmitCFunc &&CreateEmitCModelRunner,
    const EmbeddedModelRunnerOptions &Options = {}) {
  if (!InteractiveChannelBaseName.empty()) {
    return std::make_unique<InteractiveModelRunner>(
        Ctx, InputFeatures, InteractiveDecisionSpec,
        InteractiveChannelBaseName + ".out",
        InteractiveChannelBaseName + ".in");
  }
  if constexpr (HaveMLIRLowering) {
    return CreateEmitCModelRunner(Ctx, InputFeatures);
  } else {
    return std::make_unique<ReleaseModeModelRunner<CompiledModelType>>(
        Ctx, InputFeatures, DecisionName, Options);
  }
}

} // namespace llvm

#endif // LLVM_ANALYSIS_UTILS_MLGOUTILS_H
