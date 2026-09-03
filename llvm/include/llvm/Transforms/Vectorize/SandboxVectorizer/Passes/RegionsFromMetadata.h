//===- RegionsFromMetadata.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A SandboxIR function pass that builds regions from IR metadata and then runs
// a pipeline of region passes on them. This is useful to test region passes in
// isolation without relying on the output of the bundle vectorizer.
//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_REGIONSFROMMETADATA_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_REGIONSFROMMETADATA_H

#include "llvm/ADT/StringRef.h"
#include "llvm/SandboxIR/Pass.h"
#include "llvm/SandboxIR/PassManager.h"

namespace llvm::sandboxir {

/// A SandboxIR function pass that builds regions from IR metadata and then runs
/// a pipeline of region passes on them.
///
/// This is useful to test region passes in isolation without relying on the
/// output of the bundle vectorizer.
class LLVM_ABI RegionsFromMetadata final : public FunctionPass {
  // The PM containing the pipeline of region passes.
  RegionPassManager RPM;

public:
  /// Construct a RegionsFromMetadata pass with the given region-pass pipeline.
  /// \param Pipeline Comma-separated region-pass pipeline string.
  /// \param AuxArg Unused.
  RegionsFromMetadata(StringRef Pipeline, StringRef AuxArg);
  /// Build regions from metadata and run the region-pass pipeline on each.
  /// \param F Function to transform.
  /// \param A Analyses available to the pass.
  /// \returns True if the IR was modified.
  bool runOnFunction(Function &F, const Analyses &A) final;
  /// Print this pass and its nested region-pass pipeline to \p OS.
  /// \param OS Output stream.
  void printPipeline(raw_ostream &OS) const final {
    OS << getName() << "\n";
    RPM.printPipeline(OS);
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_REGIONSFROMMETADATA_H
