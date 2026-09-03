//===- SROA.h - Scalar Replacement Of Aggregates ----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file This file provides the interface for LLVM's Scalar Replacement of Aggregates pass.
///
/// This pass provides both aggregate splitting and the primary SSA formation used in the compiler.
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_SROA_H
#define LLVM_TRANSFORMS_SCALAR_SROA_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Configuration options for the SROA pass.
struct SROAOptions {
  /// Whether the pass may modify the CFG.
  enum CFGOption {
    /// Allow the pass to modify the CFG.
    ModifyCFG,
    /// Disallow any CFG modifications.
    PreserveCFG
  };

  /// Selected CFG modification policy.
  CFGOption CFG;
  /// If true, try converting homogeneous struct allocas into vector allocas.
  bool AggregateToVector;

  /// Construct SROA options with the given CFG and vectorization settings.
  /// @param CFG Whether the pass may modify the CFG.
  /// @param AggregateToVector Whether to convert homogeneous struct allocas
  /// into vector allocas.
  SROAOptions(CFGOption CFG = PreserveCFG, bool AggregateToVector = false)
      : CFG(CFG), AggregateToVector(AggregateToVector) {}
};

/// Scalar Replacement of Aggregates pass.
///
/// Provides both aggregate splitting and the primary SSA formation used in the
/// compiler.
class SROAPass : public OptionalPassInfoMixin<SROAPass> {
  const SROAOptions Options;

public:
  /// Construct an SROA pass with the given options.
  ///
  /// If \p PreserveCFG is set, then the pass is not allowed to modify CFG
  /// in any way, even if it would update CFG analyses.
  /// If \p AggregateToVector is set, then the pass will try to convert
  /// allocas of homogeneous structs into vector allocas.
  /// @param Options Configuration controlling CFG modification and aggregate
  /// to vector conversion.
  LLVM_ABI SROAPass(SROAOptions Options);

  /// Run the pass over the function.
  /// @param F Function to run SROA on.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_SROA_H
