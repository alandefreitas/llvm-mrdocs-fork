//===- LoopVectorize.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is the LLVM loop vectorizer. This pass modifies 'vectorizable' loops
// and generates target-independent LLVM-IR.
// The vectorizer uses the TargetTransformInfo analysis to estimate the costs
// of instructions in order to estimate the profitability of vectorization.
//
// The loop vectorizer combines consecutive loop iterations into a single
// 'wide' iteration. After this transformation the index is incremented
// by the SIMD vector width, and not by one.
//
// This pass has four parts:
// 1. The main loop pass that drives the different parts.
// 2. LoopVectorizationLegality - A unit that checks for the legality
//    of the vectorization.
// 3. InnerLoopVectorizer - A unit that performs the actual
//    widening of instructions.
// 4. LoopVectorizationCostModel - A unit that checks for the profitability
//    of vectorization. It decides on the optimal vector width, which
//    can be one, if vectorization is not profitable.
//
// There is a development effort going on to migrate loop vectorizer to the
// VPlan infrastructure and to introduce outer loop vectorization support (see
// docs/VectorizationPlan.rst and
// http://lists.llvm.org/pipermail/llvm-dev/2017-December/119523.html). For this
// purpose, we temporarily introduced the VPlan-native vectorization path: an
// alternative vectorization path that is natively implemented on top of the
// VPlan infrastructure. See EnableVPlanNativePath for enabling.
//
//===----------------------------------------------------------------------===//
//
// The reduction-variable vectorization is based on the paper:
//  D. Nuzman and R. Henderson. Multi-platform Auto-vectorization.
//
// Variable uniformity checks are inspired by:
//  Karrenberg, R. and Hack, S. Whole Function Vectorization.
//
// The interleaved access vectorization is based on the paper:
//  Dorit Nuzman, Ira Rosen and Ayal Zaks.  Auto-Vectorization of Interleaved
//  Data for SIMD
//
// Other ideas/concepts are from:
//  A. Zaks and D. Nuzman. Autovectorization in GCC-two years later.
//
//  S. Maleki, Y. Gao, M. Garzaran, T. Wong and D. Padua.  An Evaluation of
//  Vectorizing Compilers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H
#define LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/ExtraPassManager.h"
#include <functional>

namespace llvm {

class AssumptionCache;
class BlockFrequencyInfo;
class DemandedBits;
class DominatorTree;
class Function;
class Instruction;
class Loop;
class LoopAccessInfoManager;
class LoopInfo;
class OptimizationRemarkEmitter;
class ProfileSummaryInfo;
class ScalarEvolution;
class TargetLibraryInfo;
class TargetTransformInfo;

/// Command-line option that enables automatic loop interleaving.
LLVM_ABI extern cl::opt<bool> EnableLoopInterleaving;
/// Command-line option that enables automatic loop vectorization.
LLVM_ABI extern cl::opt<bool> EnableLoopVectorization;

/// Options that control when loop interleaving and vectorization may run.
struct LoopVectorizeOptions {
  /// If false, consider all loops for interleaving.
  /// If true, only loops that explicitly request interleaving are considered.
  bool InterleaveOnlyWhenForced;

  /// If false, consider all loops for vectorization.
  /// If true, only loops that explicitly request vectorization are considered.
  bool VectorizeOnlyWhenForced;

  /// Construct options that allow both interleaving and vectorization by
  /// default.
  ///
  /// The current defaults when creating the pass with no arguments are:
  /// EnableLoopInterleaving = true and EnableLoopVectorization = true. This
  /// means that interleaving default is consistent with the cl::opt flag, while
  /// vectorization is not.
  /// FIXME: The default for EnableLoopVectorization in the cl::opt should be
  /// set to true, and the corresponding change to account for this be made in
  /// opt.cpp. The initializations below will become:
  /// InterleaveOnlyWhenForced(!EnableLoopInterleaving)
  /// VectorizeOnlyWhenForced(!EnableLoopVectorization).
  LoopVectorizeOptions()
      : InterleaveOnlyWhenForced(false), VectorizeOnlyWhenForced(false) {}
  /// Construct options with explicit interleave and vectorize force flags.
  ///
  /// @param InterleaveOnlyWhenForced If true, only interleaved loops that
  /// explicitly request it are considered.
  /// @param VectorizeOnlyWhenForced If true, only vectorized loops that
  /// explicitly request it are considered.
  LoopVectorizeOptions(bool InterleaveOnlyWhenForced,
                       bool VectorizeOnlyWhenForced)
      : InterleaveOnlyWhenForced(InterleaveOnlyWhenForced),
        VectorizeOnlyWhenForced(VectorizeOnlyWhenForced) {}

  /// Set whether interleaving runs only when forced by a pragma or option.
  /// @param Value If true, only forced interleaving is considered.
  /// @return A reference to this options object for chaining.
  LoopVectorizeOptions &setInterleaveOnlyWhenForced(bool Value) {
    InterleaveOnlyWhenForced = Value;
    return *this;
  }

  /// Set whether vectorization runs only when forced by a pragma or option.
  /// @param Value If true, only forced vectorization is considered.
  /// @return A reference to this options object for chaining.
  LoopVectorizeOptions &setVectorizeOnlyWhenForced(bool Value) {
    VectorizeOnlyWhenForced = Value;
    return *this;
  }
};

/// Storage for information about made changes.
struct LoopVectorizeResult {
  /// Whether the pass made any IR change.
  bool MadeAnyChange;
  /// Whether the pass made a change that affects the CFG.
  bool MadeCFGChange;

  /// Construct a result recording whether any change and CFG change occurred.
  /// @param MadeAnyChange True if any IR change was made.
  /// @param MadeCFGChange True if a CFG-affecting change was made.
  LoopVectorizeResult(bool MadeAnyChange, bool MadeCFGChange)
      : MadeAnyChange(MadeAnyChange), MadeCFGChange(MadeCFGChange) {}
};

/// The LoopVectorize Pass.
struct LoopVectorizePass : public OptionalPassInfoMixin<LoopVectorizePass> {
private:
  /// If false, consider all loops for interleaving.
  /// If true, only loops that explicitly request interleaving are considered.
  bool InterleaveOnlyWhenForced;

  /// If false, consider all loops for vectorization.
  /// If true, only loops that explicitly request vectorization are considered.
  bool VectorizeOnlyWhenForced;

public:
  /// Construct a loop vectorize pass with the given options.
  /// @param Opts Options controlling forced interleaving and vectorization.
  LLVM_ABI LoopVectorizePass(LoopVectorizeOptions Opts = {});

  /// Scalar evolution analysis used while vectorizing loops.
  ScalarEvolution *SE;
  /// Loop info for the function being vectorized.
  LoopInfo *LI;
  /// Target transform info used for cost modeling and legality.
  TargetTransformInfo *TTI;
  /// Dominator tree for the function being vectorized.
  DominatorTree *DT;
  /// Lazy accessor for block frequency information.
  std::function<BlockFrequencyInfo &()> GetBFI;
  /// Target library info used when recognizing library calls.
  TargetLibraryInfo *TLI;
  /// Demanded-bits analysis used during vectorization.
  DemandedBits *DB;
  /// Assumption cache for the function being vectorized.
  AssumptionCache *AC;
  /// Manager providing loop access info for memory dependence checks.
  LoopAccessInfoManager *LAIs;
  /// Optimization remark emitter for vectorization diagnostics.
  OptimizationRemarkEmitter *ORE;
  /// Profile summary info used for hotness-based decisions.
  ProfileSummaryInfo *PSI;
  /// Alias analysis results used during vectorization.
  AAResults *AA;
  /// Function analysis manager, when available under the new pass manager.
  FunctionAnalysisManager *FAM = nullptr;

  /// Run loop vectorization over the function.
  /// @param F Function whose loops may be vectorized.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

  /// Run loop vectorization as a shim for the legacy pass manager.
  /// @param F Function whose loops may be vectorized.
  /// @return Whether any change and any CFG change were made.
  LLVM_ABI LoopVectorizeResult runImpl(Function &F);

  /// Attempt to vectorize a single loop.
  /// @param L Loop to consider for vectorization.
  /// @return True if the loop was modified.
  LLVM_ABI bool processLoop(Loop *L);
};

/// A marker analysis to determine if extra passes should be run after loop
/// vectorization.
struct ShouldRunExtraVectorPasses
    : public ShouldRunExtraPasses<ShouldRunExtraVectorPasses>,
      public AnalysisInfoMixin<ShouldRunExtraVectorPasses> {
  /// Analysis key used to identify ShouldRunExtraVectorPasses.
  LLVM_ABI static AnalysisKey Key;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_VECTORIZE_LOOPVECTORIZE_H
