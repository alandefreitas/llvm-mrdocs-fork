//===- ExpandVariadics.h - expand variadic functions ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H
#define LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Module;
class ModulePass;

/// Operating mode for the expand-variadics pass.
enum class ExpandVariadicsMode {
  Unspecified, ///< Use the implementation defaults.
  Disable,     ///< Disable the pass entirely.
  Optimize,    ///< Optimise without changing ABI.
  Lowering,    ///< Change the variadic calling convention.
};

/// Pass that expands variadic functions into fixed-arity equivalents.
class ExpandVariadicsPass : public RequiredPassInfoMixin<ExpandVariadicsPass> {
  const ExpandVariadicsMode Mode;

public:
  /// Construct an expand-variadics pass for the given mode.
  ///
  /// Operates under the passed mode unless overridden on the command line.
  ///
  /// \param Mode Operating mode that controls whether the pass disables,
  /// optimises, or lowers variadic functions.
  LLVM_ABI ExpandVariadicsPass(ExpandVariadicsMode Mode);

  /// Run expand-variadics over the given module.
  ///
  /// \param M Module whose variadic functions are expanded.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Create a legacy pass manager instance of the expand-variadics pass.
///
/// \param Mode Operating mode that controls whether the pass disables,
/// optimises, or lowers variadic functions.
/// \return A new expand-variadics pass for the legacy pass manager.
LLVM_ABI ModulePass *createExpandVariadicsPass(ExpandVariadicsMode Mode);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_EXPANDVARIADICS_H
