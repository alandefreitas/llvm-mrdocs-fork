//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file provides the interface for LLVM's Logical Scalar Replacement of
/// Aggregates pass. This pass provides both aggregate splitting and the
/// primary SSA formation used in the compiler when used with structured GEP
/// and allocas.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOGICALSROA_H
#define LLVM_TRANSFORMS_SCALAR_LOGICALSROA_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Logical Scalar Replacement of Aggregates pass.
///
/// Provides aggregate splitting and the primary SSA formation used in the
/// compiler when used with structured GEP and allocas.
class LogicalSROAPass : public OptionalPassInfoMixin<LogicalSROAPass> {
public:
  /// Construct a LogicalSROA pass.
  LogicalSROAPass();

  /// Run the pass over the function.
  /// @param F Function to run LogicalSROA on.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOGICALSROA_H
