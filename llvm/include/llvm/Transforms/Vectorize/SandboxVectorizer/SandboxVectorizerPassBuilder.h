//===- SandboxVectorizerPassBuilder.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Utility functions so passes with sub-pipelines can create SandboxVectorizer
// passes without replicating the same logic in each pass.
//
#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SANDBOXVECTORIZERPASSBUILDER_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SANDBOXVECTORIZERPASSBUILDER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/SandboxIR/Pass.h"

#include <memory>

namespace llvm::sandboxir {

/// Factory for creating SandboxVectorizer function and region passes by name.
///
/// Used by passes with sub-pipelines so they can instantiate registered
/// SandboxVectorizer passes without duplicating creation logic.
class SandboxVectorizerPassBuilder {
public:
  /// Create a function pass named \p Name with the given arguments.
  /// \param Name Registered function-pass name.
  /// \param Args Standard pass arguments from the pipeline (`<...>`).
  /// \param AuxArg Auxiliary pass argument from the pipeline (`(...)`).
  /// \return The created pass, or nullptr if \p Name is unknown.
  LLVM_ABI static std::unique_ptr<FunctionPass>
  createFunctionPass(StringRef Name, StringRef Args, StringRef AuxArg);
  /// Create a region pass named \p Name with the given arguments.
  /// \param Name Registered region-pass name.
  /// \param Args Standard pass arguments from the pipeline (`<...>`); must be
  /// empty for currently registered region passes.
  /// \param AuxArg Auxiliary pass argument from the pipeline (`(...)`).
  /// \return The created pass, or nullptr if \p Name is unknown.
  LLVM_ABI static std::unique_ptr<RegionPass>
  createRegionPass(StringRef Name, StringRef Args, StringRef AuxArg);
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_SANDBOXVECTORIZERPASSBUILDER_H
