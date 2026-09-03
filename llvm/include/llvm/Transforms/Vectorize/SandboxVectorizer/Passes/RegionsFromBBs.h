//===- RegionsFromBBs.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A SandboxIR function pass that builds one region per BB and then runs a
// pipeline of region passes on them. This is useful to test region passes in
// isolation without relying on the output of other vectorizer components.
//

#ifndef LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_REGIONSFROMBBS_H
#define LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_REGIONSFROMBBS_H

#include "llvm/ADT/StringRef.h"
#include "llvm/SandboxIR/Pass.h"
#include "llvm/SandboxIR/PassManager.h"

namespace llvm::sandboxir {

/// A SandboxIR function pass that builds one region per BB and runs a region
/// pass pipeline on them.
///
/// This is useful to test region passes in isolation without relying on the
/// output of other vectorizer components.
class LLVM_ABI RegionsFromBBs final : public FunctionPass {
  // The PM containing the pipeline of region passes.
  RegionPassManager RPM;

public:
  /// Construct a RegionsFromBBs pass with the given region-pass pipeline.
  /// \param Pipeline Comma-separated region-pass pipeline to run on each BB
  /// region.
  /// \param AuxArg Unused.
  RegionsFromBBs(StringRef Pipeline, StringRef AuxArg);
  /// Build one region per basic block in \p F and run the region-pass pipeline
  /// on each.
  /// \param F Function whose basic blocks become regions.
  /// \param A Analyses available to the pass.
  /// \returns False; this pass never reports IR modifications.
  bool runOnFunction(Function &F, const Analyses &A) final;
  /// Print this pass and its region-pass pipeline to \p OS.
  /// \param OS Output stream.
  void printPipeline(raw_ostream &OS) const final {
    OS << getName() << "\n";
    RPM.printPipeline(OS);
  }
};

} // namespace llvm::sandboxir

#endif // LLVM_TRANSFORMS_VECTORIZE_SANDBOXVECTORIZER_PASSES_REGIONSFROMBBS_H
