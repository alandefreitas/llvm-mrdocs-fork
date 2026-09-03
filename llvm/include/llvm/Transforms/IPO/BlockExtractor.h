//===- BlockExtractor.h - Extracts blocks into their own functions --------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass extracts the specified basic blocks from the module into their
// own functions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_BLOCKEXTRACTOR_H
#define LLVM_TRANSFORMS_IPO_BLOCKEXTRACTOR_H

#include "llvm/Support/Compiler.h"
#include <vector>

#include "llvm/IR/PassManager.h"

namespace llvm {
class BasicBlock;

/// Pass that extracts specified basic blocks into their own functions.
struct BlockExtractorPass : OptionalPassInfoMixin<BlockExtractorPass> {
  /// Construct a block-extractor pass.
  ///
  /// \param GroupsOfBlocks Groups of basic blocks to extract; each group is
  /// extracted into one new function.
  /// \param EraseFunctions Whether to erase the original functions after
  /// extraction.
  LLVM_ABI
  BlockExtractorPass(std::vector<std::vector<BasicBlock *>> &&GroupsOfBlocks,
                     bool EraseFunctions);

  /// Run block extraction over the given module.
  ///
  /// \param M Module whose basic blocks are extracted.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

private:
  std::vector<std::vector<BasicBlock *>> GroupsOfBlocks;
  bool EraseFunctions;
};
} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_BLOCKEXTRACTOR_H
