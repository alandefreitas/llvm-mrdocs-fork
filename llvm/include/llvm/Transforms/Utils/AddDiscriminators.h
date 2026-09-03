//===- AddDiscriminators.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass adds DWARF discriminators to the IR. Path discriminators are used
// to decide what CFG path was taken inside sub-graphs whose instructions share
// the same line and column number information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H
#define LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;

/// Pass that adds DWARF path discriminators to the IR.
///
/// Path discriminators distinguish which CFG path was taken inside sub-graphs
/// whose instructions share the same line and column number information.
class AddDiscriminatorsPass
    : public RequiredPassInfoMixin<AddDiscriminatorsPass> {
public:
  /// Run the add-discriminators pass over the function.
  /// @param F Function to add DWARF discriminators to.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_ADDDISCRIMINATORS_H
