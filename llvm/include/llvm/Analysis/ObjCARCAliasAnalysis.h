//===- ObjCARCAliasAnalysis.h - ObjC ARC Alias Analysis ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file declares a simple ARC-aware AliasAnalysis using special knowledge
/// of Objective C to enhance other optimization passes which rely on the Alias
/// Analysis infrastructure.
///
/// WARNING: This file knows about certain library functions. It recognizes them
/// by name, and hardwires knowledge of their semantics.
///
/// WARNING: This file knows about how certain Objective-C library functions are
/// used. Naive LLVM IR transformations which would otherwise be
/// behavior-preserving may break these assumptions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OBJCARCALIASANALYSIS_H
#define LLVM_ANALYSIS_OBJCARCALIASANALYSIS_H

#include "llvm/Analysis/AliasAnalysis.h"

namespace llvm {
/// Objective-C Automatic Reference Counting analyses and utilities.
namespace objcarc {

/// This is a simple alias analysis implementation that uses knowledge
/// of ARC constructs to answer queries.
///
/// TODO: This class could be generalized to know about other ObjC-specific
/// tricks. Such as knowing that ivars in the non-fragile ABI are non-aliasing
/// even though their offsets are dynamic.
class ObjCARCAAResult : public AAResultBase {
  const DataLayout &DL;

public:
  /// Construct an ObjCARCAAResult for data layout \p DL.
  /// @param DL Data layout for the module.
  explicit ObjCARCAAResult(const DataLayout &DL) : DL(DL) {}
  /// Move-construct an ObjCARCAAResult from \p Arg.
  /// @param Arg ObjCARCAAResult to move from.
  ObjCARCAAResult(ObjCARCAAResult &&Arg)
      : AAResultBase(std::move(Arg)), DL(Arg.DL) {}

  /// Handle invalidation events from the new pass manager.
  ///
  /// By definition, this result is stateless and so remains valid.
  /// @param Fn Function whose analyses may have been invalidated.
  /// @param PA Set of analyses preserved by the invalidating transform.
  /// @param Inv Invalidator used to check dependent analyses.
  /// @return False; this result is never invalidated.
  bool invalidate(Function &Fn, const PreservedAnalyses &PA,
                  FunctionAnalysisManager::Invalidator &Inv) {
    return false;
  }

  /// Query whether two memory locations may alias using ARC knowledge.
  /// @param LocA First memory location.
  /// @param LocB Second memory location.
  /// @param AAQI Query state and caches for this alias query.
  /// @param CtxI Optional context instruction for the query.
  /// @return An AliasResult indicating whether the locations alias.
  LLVM_ABI AliasResult alias(const MemoryLocation &LocA,
                             const MemoryLocation &LocB, AAQueryInfo &AAQI,
                             const Instruction *CtxI);
  /// Return a ModRef bitmask for a memory location using ARC knowledge.
  /// @param Loc Memory location whose ModRef mask is requested.
  /// @param AAQI Query state and caches for this query.
  /// @param IgnoreLocals When true, treat local allocas as NoModRef.
  /// @return A ModRef bitmask that can be applied to ModRef info for \p Loc.
  LLVM_ABI ModRefInfo getModRefInfoMask(const MemoryLocation &Loc,
                                        AAQueryInfo &AAQI, bool IgnoreLocals);

  /// Inherit getMemoryEffects overloads from AAResultBase.
  using AAResultBase::getMemoryEffects;
  /// Return memory effects for function \p F using ARC knowledge.
  /// @param F Function whose memory effects are queried.
  /// @return Memory effects of the function.
  LLVM_ABI MemoryEffects getMemoryEffects(const Function *F);

  /// Inherit getModRefInfo overloads from AAResultBase.
  using AAResultBase::getModRefInfo;
  /// Return ModRef info for call \p Call against location \p Loc.
  /// @param Call Call site whose ModRef behavior is queried.
  /// @param Loc Memory location to check against the call.
  /// @param AAQI Query state and caches for this query.
  /// @return ModRef info describing how the call may access \p Loc.
  LLVM_ABI ModRefInfo getModRefInfo(const CallBase *Call,
                                    const MemoryLocation &Loc,
                                    AAQueryInfo &AAQI);
};

/// Analysis pass providing a never-invalidated alias analysis result.
class ObjCARCAA : public AnalysisInfoMixin<ObjCARCAA> {
  friend AnalysisInfoMixin<ObjCARCAA>;
  static AnalysisKey Key;

public:
  /// Analysis result type produced by this pass.
  typedef ObjCARCAAResult Result;

  /// Run ObjC ARC alias analysis on function \p F.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return An ObjCARCAAResult for \p F.
  LLVM_ABI ObjCARCAAResult run(Function &F, FunctionAnalysisManager &AM);
};

} // namespace objcarc
} // namespace llvm

#endif
