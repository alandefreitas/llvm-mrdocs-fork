//===- ScalarEvolutionAliasAnalysis.h - SCEV-based AA -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This is the interface for a SCEV-based alias analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_SCALAREVOLUTIONALIASANALYSIS_H
#define LLVM_ANALYSIS_SCALAREVOLUTIONALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class Function;
class ScalarEvolution;
class SCEV;

/// A simple alias analysis implementation that uses ScalarEvolution to answer
/// queries.
class SCEVAAResult : public AAResultBase {
  ScalarEvolution &SE;

public:
  /// Construct a SCEVAAResult for ScalarEvolution analysis \p SE.
  /// @param SE ScalarEvolution analysis used to answer queries.
  explicit SCEVAAResult(ScalarEvolution &SE) : SE(SE) {}
  /// Move-construct a SCEVAAResult from \p Arg.
  /// @param Arg SCEVAAResult to move from.
  SCEVAAResult(SCEVAAResult &&Arg) : AAResultBase(std::move(Arg)), SE(Arg.SE) {}

  /// Query whether two memory locations may alias using ScalarEvolution.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @param AAQI Query state and caches for this alias query.
  /// @param CtxI Optional context instruction for the query.
  /// @return An AliasResult indicating whether the locations alias.
  LLVM_ABI AliasResult alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB, AAQueryInfo &AAQI,
                             const Instruction *CtxI);

  /// Handle invalidation events in the new pass manager.
  /// @param F Function whose analyses may have been invalidated.
  /// @param PA Set of analyses preserved by the invalidating transform.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return True if this result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

private:
  Value *GetBaseValue(const SCEV *S);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class SCEVAA : public AnalysisInfoMixin<SCEVAA> {
  friend AnalysisInfoMixin<SCEVAA>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Analysis result type produced by this pass.
  typedef SCEVAAResult Result;

  /// Run SCEV-based alias analysis on function \p F.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return A SCEVAAResult for \p F.
  LLVM_ABI SCEVAAResult run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy wrapper pass to provide the SCEVAAResult object.
class LLVM_ABI SCEVAAWrapperPass : public FunctionPass {
  std::unique_ptr<SCEVAAResult> Result;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a SCEVAAWrapperPass.
  SCEVAAWrapperPass();

  /// Return the SCEVAAResult computed for the last function.
  /// @return The SCEVAAResult computed for the last function.
  SCEVAAResult &getResult() { return *Result; }
  /// Return the SCEVAAResult computed for the last function.
  /// @return The SCEVAAResult computed for the last function.
  const SCEVAAResult &getResult() const { return *Result; }

  /// Compute SCEVAAResult for function \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;
  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
};

/// Creates an instance of \c SCEVAAWrapperPass.
/// @return A FunctionPass that provides SCEVAAResult.
LLVM_ABI FunctionPass *createSCEVAAWrapperPass();
}

#endif
