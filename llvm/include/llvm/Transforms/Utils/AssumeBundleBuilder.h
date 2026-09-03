//===- AssumeBundleBuilder.h - utils to build assume bundles ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contain tools to preserve informations. They should be used before
// performing a transformation that may move and delete instructions as those
// transformation may destroy or worsen information that can be derived from the
// IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_ASSUMEBUNDLEBUILDER_H
#define LLVM_TRANSFORMS_UTILS_ASSUMEBUNDLEBUILDER_H

#include "llvm/Analysis/AssumeBundleQueries.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
class AssumeInst;
class Function;
class Instruction;
class AssumptionCache;
class DominatorTree;

/// Enable preservation of attributes throughout code transformation.
LLVM_ABI extern cl::opt<bool> EnableKnowledgeRetention;

/// Build an llvm.assume call preserving information derived from an instruction.
///
/// If no information derived from \p I, this call returns null.
/// The returned instruction is not inserted anywhere.
/// @param I Instruction to derive assume knowledge from.
/// @return An uninserted llvm.assume, or null if no information was derived.
LLVM_ABI AssumeInst *buildAssumeFromInst(Instruction *I);

/// Insert an llvm.assume before an instruction to salvage its derived knowledge.
///
/// Calls BuildAssumeFromInst and if the resulting llvm.assume is valid insert
/// if before I. This is usually what need to be done to salvage the knowledge
/// contained in the instruction I.
/// The AssumptionCache must be provided if it is available or the cache may
/// become silently be invalid.
/// The DominatorTree can optionally be provided to enable cross-block
/// reasoning.
/// This returns if a change was made.
/// @param I Instruction whose knowledge should be salvaged.
/// @param AC Optional assumption cache to update; provide it when available.
/// @param DT Optional dominator tree enabling cross-block reasoning.
/// @return True if an assume was inserted.
LLVM_ABI bool salvageKnowledge(Instruction *I, AssumptionCache *AC = nullptr,
                               DominatorTree *DT = nullptr);

/// This pass attempts to minimize the number of assume without loosing any
/// information.
struct AssumeSimplifyPass : public OptionalPassInfoMixin<AssumeSimplifyPass> {
  /// Run the assume-simplify pass over the function.
  /// @param F Function whose assume intrinsics should be simplified.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// This pass will try to build an llvm.assume for every instruction in the
/// function. Its main purpose is testing.
struct AssumeBuilderPass : public OptionalPassInfoMixin<AssumeBuilderPass> {
  /// Run the assume-builder pass over the function.
  /// @param F Function to build llvm.assume intrinsics for.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm

#endif
