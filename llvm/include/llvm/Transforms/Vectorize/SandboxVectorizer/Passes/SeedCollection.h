//===- SeedCollection.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The seed-collection pass of the bundle vectorizer.
//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_SEEDCOLLECTION_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_SEEDCOLLECTION_H

#include "llvm/SandboxIR/Pass.h"
#include "llvm/SandboxIR/PassManager.h"

namespace llvm::sandboxir {

/// Collects vectorization seed instructions and runs a region-pass pipeline.
///
/// This pass collects the instructions that can become vectorization "seeds",
/// like stores to consecutive memory addresses. It then goes over the collected
/// seeds, slicing them into appropriately sized chunks, creating a Region with
/// the seed slice as the Auxiliary vector and runs the region pass pipeline.
class LLVM_ABI SeedCollection final : public FunctionPass {

  /// The PM containing the pipeline of region passes.
  RegionPassManager RPM;
  /// The auxiliary argument passed to the pass that tells us that we should
  /// collect seeds of different types.
  static constexpr StringRef DiffTypesArgStr = "enable-diff-types";
  /// Collect seeds of different types.
  bool AllowDiffTypes = false;

public:
  /// Construct a SeedCollection pass with a region-pass pipeline.
  /// \param Pipeline Pipeline of region passes to run on each seed slice.
  /// \param AuxArg Optional; if set to "enable-diff-types", collect seeds of
  /// different types.
  SeedCollection(StringRef Pipeline, StringRef AuxArg);
  /// Collect seeds in \p F and run the region-pass pipeline on each slice.
  /// \param F Function to transform.
  /// \param A Analyses available to the pass.
  /// \returns True if the IR was modified.
  bool runOnFunction(Function &F, const Analyses &A) final;
  /// Print this pass and its nested region-pass pipeline.
  /// \param OS Output stream.
  void printPipeline(raw_ostream &OS) const final {
    OS << getName() << "\n";
    RPM.printPipeline(OS);
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_SEEDCOLLECTION_H
