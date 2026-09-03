//===- ArgumentPromotion.h - Promote by-reference arguments -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_ARGUMENTPROMOTION_H
#define LLVM_TRANSFORMS_IPO_ARGUMENTPROMOTION_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

/// Argument promotion pass.
///
/// This pass walks the functions in each SCC and for each one tries to
/// transform it and all of its callers to replace indirect arguments with
/// direct (by-value) arguments.
class ArgumentPromotionPass
    : public OptionalPassInfoMixin<ArgumentPromotionPass> {
  unsigned MaxElements;

public:
  /// Construct an argument-promotion pass.
  ///
  /// \param MaxElements Maximum number of aggregate elements to promote into
  /// by-value arguments; zero means no limit.
  ArgumentPromotionPass(unsigned MaxElements = 2u) : MaxElements(MaxElements) {}

  /// Run argument promotion over the functions in SCC \p C.
  ///
  /// \param C The SCC whose functions are candidates for promotion.
  /// \param AM The CGSCC analysis manager.
  /// \param CG The lazy call graph.
  /// \param UR The CGSCC update result.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(LazyCallGraph::SCC &C,
                                 CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                 CGSCCUpdateResult &UR);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_ARGUMENTPROMOTION_H
