//===- LazyBranchProbabilityInfo.h - Lazy Branch Probability ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is an alternative analysis pass to BranchProbabilityInfoWrapperPass.
// The difference is that with this pass the branch probabilities are not
// computed when the analysis pass is executed but rather when the BPI results
// is explicitly requested by the analysis client.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LAZYBRANCHPROBABILITYINFO_H
#define LLVM_ANALYSIS_LAZYBRANCHPROBABILITYINFO_H

#include "llvm/Analysis/BranchProbabilityInfo.h"
#include "llvm/Pass.h"

namespace llvm {
class CycleInfo;
class Function;
class TargetLibraryInfo;

/// Alternative analysis pass that computes branch probabilities on demand.
///
/// This is an alternative analysis pass to
/// BranchProbabilityInfoWrapperPass.  The difference is that with this pass the
/// branch probabilities are not computed when the analysis pass is executed but
/// rather when the BPI results is explicitly requested by the analysis client.
///
/// There are some additional requirements for any client pass that wants to use
/// the analysis:
///
/// 1. The pass needs to initialize dependent passes with:
///
///   INITIALIZE_PASS_DEPENDENCY(LazyBPIPass)
///
/// 2. Similarly, getAnalysisUsage should call:
///
///   LazyBranchProbabilityInfoPass::getLazyBPIAnalysisUsage(AU)
///
/// 3. The computed BPI should be requested with
///    getAnalysis<LazyBranchProbabilityInfoPass>().getBPI() before CycleInfo
///    could be invalidated for example by changing the CFG.
///
/// Note that it is expected that we wouldn't need this functionality for the
/// new PM since with the new PM, analyses are executed on demand.
class LLVM_ABI LazyBranchProbabilityInfoPass : public FunctionPass {

  /// Wraps a BPI to allow lazy computation of the branch probabilities.
  ///
  /// A pass that only conditionally uses BPI can uncondtionally require the
  /// analysis without paying for the overhead if BPI doesn't end up being used.
  class LazyBranchProbabilityInfo {
  public:
    LazyBranchProbabilityInfo(const Function *F, const CycleInfo *CI,
                              const TargetLibraryInfo *TLI)
        : F(F), CI(CI), TLI(TLI) {}

    /// Retrieve the BPI with the branch probabilities computed.
    BranchProbabilityInfo &getCalculated() {
      if (!Calculated) {
        assert(F && CI && "call setAnalysis");
        BPI.calculate(*F, *CI, TLI, nullptr, nullptr);
        Calculated = true;
      }
      return BPI;
    }

    const BranchProbabilityInfo &getCalculated() const {
      return const_cast<LazyBranchProbabilityInfo *>(this)->getCalculated();
    }

  private:
    BranchProbabilityInfo BPI;
    bool Calculated = false;
    const Function *F;
    const CycleInfo *CI;
    const TargetLibraryInfo *TLI;
  };

  std::unique_ptr<LazyBranchProbabilityInfo> LBPI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a LazyBranchProbabilityInfoPass.
  LazyBranchProbabilityInfoPass();

  /// Compute and return the branch probabilities.
  /// @return Branch probability info, computed on demand if needed.
  BranchProbabilityInfo &getBPI() { return LBPI->getCalculated(); }

  /// Compute and return the branch probabilities.
  /// @return Const branch probability info, computed on demand if needed.
  const BranchProbabilityInfo &getBPI() const { return LBPI->getCalculated(); }

  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Helper for client passes to set up the analysis usage on behalf of this
  /// pass.
  /// @param AU Analysis usage to update.
  static void getLazyBPIAnalysisUsage(AnalysisUsage &AU);

  /// Set up lazy BPI analysis for function \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;
  /// Release the cached lazy BPI between runs.
  void releaseMemory() override;
  /// Print the computed branch probabilities.
  /// @param OS Stream to write the printed results to.
  /// @param M Optional module context; unused by this pass.
  void print(raw_ostream &OS, const Module *M) const override;
};

/// Helper for client passes to initialize dependent passes for LBPI.
/// @param Registry Pass registry used to register dependent passes.
LLVM_ABI void initializeLazyBPIPassPass(PassRegistry &Registry);

/// Simple trait class that provides a mapping between BPI passes and the
/// corresponding BPInfo.
template <typename PassT> struct BPIPassTrait {
  /// Return the BPI-related result associated with pass \p P.
  /// @param P Pass instance that directly holds the BPI result.
  /// @return Reference to the BPI-related result held by \p P.
  static PassT &getBPI(PassT *P) { return *P; }
};

/// Trait specialization that maps LazyBranchProbabilityInfoPass to BPI.
template <> struct BPIPassTrait<LazyBranchProbabilityInfoPass> {
  /// Return the BranchProbabilityInfo computed by pass \p P.
  /// @param P Lazy BPI pass whose computed probabilities are requested.
  /// @return Computed branch probability info for the function.
  static BranchProbabilityInfo &getBPI(LazyBranchProbabilityInfoPass *P) {
    return P->getBPI();
  }
};
} // namespace llvm
#endif
