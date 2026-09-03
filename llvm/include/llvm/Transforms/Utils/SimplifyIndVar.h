//===-- llvm/Transforms/Utils/SimplifyIndVar.h - Indvar Utils ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines in interface for induction variable simplification. It does
// not define any actual pass or policy, but provides a single function to
// simplify a loop's induction variables based on ScalarEvolution.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H
#define LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H

#include "llvm/Support/Compiler.h"
#include <utility>

namespace llvm {

class Type;
class WeakTrackingVH;
template <typename T> class SmallVectorImpl;
class CastInst;
class DominatorTree;
class Loop;
class LoopInfo;
class PHINode;
class ScalarEvolution;
class SCEVExpander;
class TargetTransformInfo;

/// Interface for visiting interesting IV users that are recognized but not
/// simplified by this utility.
class LLVM_ABI IVVisitor {
protected:
  /// Optional dominator tree available to subclasses.
  const DominatorTree *DT = nullptr;

  /// Anchor the vtable in the implementation translation unit.
  virtual void anchor();

public:
  /// Construct an empty IV visitor.
  IVVisitor() = default;
  /// Destroy the IV visitor.
  virtual ~IVVisitor() = default;

  /// Return the dominator tree associated with this visitor, if any.
  /// \return The dominator tree, or nullptr if none is available.
  const DominatorTree *getDomTree() const { return DT; }
  /// Visit a cast of an induction variable that was recognized but not
  /// simplified.
  /// \param Cast Cast instruction that uses the induction variable.
  virtual void visitCast(CastInst *Cast) = 0;
};

/// Simplify instructions that use this induction variable.
///
/// Uses ScalarEvolution to analyze the IV's recurrence.
///
/// \param CurrIV Induction-variable PHI whose users may be simplified.
/// \param SE ScalarEvolution used to analyze the IV recurrence.
/// \param DT Dominator tree used by simplification.
/// \param LI Loop info for the function containing \p CurrIV.
/// \param TTI Target transform info used for profitability checks.
/// \param Dead Receives instructions that become dead and may be deleted.
/// \param Rewriter SCEV expander used when rewriting users.
/// \param V Optional visitor notified about interesting IV users.
/// \return A pair whose first entry is true if changes were made and whose
/// second entry is true if new loop-unswitching opportunities were introduced.
LLVM_ABI std::pair<bool, bool>
simplifyUsersOfIV(PHINode *CurrIV, ScalarEvolution *SE, DominatorTree *DT,
                  LoopInfo *LI, const TargetTransformInfo *TTI,
                  SmallVectorImpl<WeakTrackingVH> &Dead, SCEVExpander &Rewriter,
                  IVVisitor *V = nullptr);

/// Simplify users of induction variables within this loop.
///
/// This does not actually change or add IVs.
///
/// \param L Loop whose induction-variable users may be simplified.
/// \param SE ScalarEvolution used to analyze IV recurrences.
/// \param DT Dominator tree used by simplification.
/// \param LI Loop info for the function containing \p L.
/// \param TTI Target transform info used for profitability checks.
/// \param Dead Receives instructions that become dead and may be deleted.
/// \return True if any induction-variable users were simplified.
LLVM_ABI bool simplifyLoopIVs(Loop *L, ScalarEvolution *SE, DominatorTree *DT,
                              LoopInfo *LI, const TargetTransformInfo *TTI,
                              SmallVectorImpl<WeakTrackingVH> &Dead);

/// Information about an induction variable used by sign or zero extends.
///
/// This information is recorded by CollectExtend and provides the input to
/// WidenIV.
struct WideIVInfo {
  /// Narrow induction variable considered for widening.
  PHINode *NarrowIV = nullptr;

  /// Widest integer type created by a sign or zero extend of the IV.
  Type *WidestNativeType = nullptr;

  /// True if a signed extend user was seen before a zero extend.
  bool IsSigned = false;
};

/// Extend the width of an IV to cover its widest uses.
///
/// \param WI Information about the narrow IV and its widest extend uses.
/// \param LI Loop info for the function containing the IV.
/// \param SE ScalarEvolution used to analyze and rewrite the IV.
/// \param Rewriter SCEV expander used to materialize the wide IV.
/// \param DT Dominator tree used while rewriting.
/// \param DeadInsts Receives instructions that become dead and may be deleted.
/// \param NumElimExt Incremented by the number of eliminated extend operations.
/// \param NumWidened Incremented by the number of widened induction variables.
/// \param HasGuards True if the module may contain llvm.experimental.guard.
/// \param UsePostIncrementRanges Whether to use post-increment IV ranges.
/// \return The wide induction-variable PHI, or nullptr if widening failed.
LLVM_ABI PHINode *createWideIV(const WideIVInfo &WI, LoopInfo *LI,
                               ScalarEvolution *SE, SCEVExpander &Rewriter,
                               DominatorTree *DT,
                               SmallVectorImpl<WeakTrackingVH> &DeadInsts,
                               unsigned &NumElimExt, unsigned &NumWidened,
                               bool HasGuards, bool UsePostIncrementRanges);

} // end namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SIMPLIFYINDVAR_H
