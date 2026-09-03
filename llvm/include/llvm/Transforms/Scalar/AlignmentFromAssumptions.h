//===---- AlignmentFromAssumptions.h ----------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a ScalarEvolution-based transformation to set
// the alignments of load, stores and memory intrinsics based on the truth
// expressions of assume intrinsics. The primary motivation is to handle
// complex alignment assumptions that apply to vector loads and stores that
// appear after vectorization and unrolling.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_ALIGNMENTFROMASSUMPTIONS_H
#define LLVM_TRANSFORMS_SCALAR_ALIGNMENTFROMASSUMPTIONS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class AssumptionCache;
class CallInst;
class DominatorTree;
class ScalarEvolution;
class SCEV;
class Value;

/// A pass that sets load/store/mem-intrinsic alignments from assume bundles.
///
/// Uses ScalarEvolution to interpret align operand bundles on \@llvm.assume
/// calls and strengthen the alignments of dominated loads, stores, and memory
/// intrinsics. The main motivation is complex alignment assumptions on vector
/// memory operations that appear after vectorization and unrolling.
struct AlignmentFromAssumptionsPass
    : public OptionalPassInfoMixin<AlignmentFromAssumptionsPass> {
  /// Run the pass over \p F using analyses from \p AM.
  /// @param F Function whose memory operations may be realigned.
  /// @param AM Function analysis manager providing AssumptionAnalysis,
  /// ScalarEvolution, and DominatorTree.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Run the transformation using already-fetched analyses (legacy PM glue).
  /// @param F Function whose memory operations may be realigned.
  /// @param AC Cache of \@llvm.assume calls in \p F.
  /// @param SE_ ScalarEvolution analysis used to interpret alignment SCEVs.
  /// @param DT_ Dominator tree used to validate assume contexts.
  /// @return True if any alignment was changed.
  LLVM_ABI bool runImpl(Function &F, AssumptionCache &AC, ScalarEvolution *SE_,
                        DominatorTree *DT_);

  /// ScalarEvolution used while extracting and applying alignment info.
  ScalarEvolution *SE = nullptr;
  /// Dominator tree used to check whether an assume applies at a use.
  DominatorTree *DT = nullptr;

  /// Extract pointer, alignment, and offset SCEVs from an assume align bundle.
  /// @param I The \@llvm.assume call containing the operand bundle.
  /// @param Idx Index of the operand bundle to inspect.
  /// @param AAPtr On success, set to the aligned pointer value.
  /// @param AlignSCEV On success, set to the power-of-two alignment SCEV.
  /// @param OffSCEV On success, set to the optional offset SCEV (or zero).
  /// @return True if the bundle was a valid constant power-of-two align.
  LLVM_ABI bool extractAlignmentInfo(CallInst *I, unsigned Idx, Value *&AAPtr,
                                     const SCEV *&AlignSCEV,
                                     const SCEV *&OffSCEV);
  /// Apply one assume align bundle to dominated users of the aligned pointer.
  /// @param I The \@llvm.assume call providing the alignment knowledge.
  /// @param Idx Index of the align operand bundle to process.
  /// @return True if any load, store, or mem-intrinsic alignment was updated.
  LLVM_ABI bool processAssumption(CallInst *I, unsigned Idx);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_ALIGNMENTFROMASSUMPTIONS_H
