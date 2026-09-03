//===- StripSymbols.h - Strip symbols and debug info from a module --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The StripSymbols transformation implements code stripping. Specifically, it
// can delete:
//
//   * names for virtual registers
//   * symbols for internal globals and functions
//   * debug information
//
// Note that this transformation makes code much less readable, so it should
// only be used in situations where the 'strip' utility would be used, such as
// reducing code size or making it harder to reverse engineer code.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_STRIPSYMBOLS_H
#define LLVM_TRANSFORMS_IPO_STRIPSYMBOLS_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Pass that strips symbol names and debug info from a module.
struct StripSymbolsPass : OptionalPassInfoMixin<StripSymbolsPass> {
  /// Strip symbol names and debug info from module \p M.
  ///
  /// \param M Module whose symbols and debug info are stripped.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Pass that strips non-debug symbol names while preserving debug info.
struct StripNonDebugSymbolsPass
    : OptionalPassInfoMixin<StripNonDebugSymbolsPass> {
  /// Strip non-debug symbol names from module \p M.
  ///
  /// \param M Module whose non-debug symbols are stripped.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Pass that removes llvm.dbg.declare intrinsics from a module.
struct StripDebugDeclarePass : OptionalPassInfoMixin<StripDebugDeclarePass> {
  /// Strip llvm.dbg.declare intrinsics from module \p M.
  ///
  /// \param M Module whose dbg.declare intrinsics are removed.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Pass that removes debug info for deleted functions and globals.
struct StripDeadDebugInfoPass : OptionalPassInfoMixin<StripDeadDebugInfoPass> {
  /// Strip dead debug info from module \p M.
  ///
  /// \param M Module whose dead debug info is removed.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Pass that removes dead edges from the CG Profile module flag.
struct StripDeadCGProfilePass : OptionalPassInfoMixin<StripDeadCGProfilePass> {
  /// Strip dead CG Profile edges from module \p M.
  ///
  /// \param M Module whose CG Profile module flag is cleaned up.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_STRIPSYMBOLS_H
