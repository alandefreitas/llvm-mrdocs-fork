//===- PartialInlining.h - Inline parts of functions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs partial inlining, typically by inlining an if statement
// that surrounds the body of the function.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_PARTIALINLINING_H
#define LLVM_TRANSFORMS_IPO_PARTIALINLINING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;

/// Pass to remove unused function declarations.
class PartialInlinerPass : public OptionalPassInfoMixin<PartialInlinerPass> {
public:
  /// Run partial inlining over the given module.
  ///
  /// \param M Module whose functions may be partially inlined.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_PARTIALINLINING_H
