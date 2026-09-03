//===- ElimAvailExtern.h - Optimize Global Variables ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This transform is designed to eliminate available external global
// definitions from the program, turning them into declarations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_ELIMAVAILEXTERN_H
#define LLVM_TRANSFORMS_IPO_ELIMAVAILEXTERN_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// A pass that transforms external global definitions into declarations.
class EliminateAvailableExternallyPass
    : public OptionalPassInfoMixin<EliminateAvailableExternallyPass> {
public:
  /// Run available-externally elimination over the given module.
  ///
  /// \param M Module whose available external definitions are turned into
  /// declarations.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_ELIMAVAILEXTERN_H
