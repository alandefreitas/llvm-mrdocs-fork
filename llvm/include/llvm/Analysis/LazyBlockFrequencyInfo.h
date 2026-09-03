//===- LazyBlockFrequencyInfo.h - Lazy Block Frequency Analysis -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This is an alternative analysis pass to BlockFrequencyInfoWrapperPass.  The
// difference is that with this pass the block frequencies are not computed when
// the analysis pass is executed but rather when the BFI result is explicitly
// requested by the analysis client.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LAZYBLOCKFREQUENCYINFO_H
#define LLVM_ANALYSIS_LAZYBLOCKFREQUENCYINFO_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/Analysis/LazyBranchProbabilityInfo.h"
#include "llvm/Pass.h"

namespace llvm {
class Function;
class CycleInfo;

/// Wraps a BFI to allow lazy computation of the block frequencies.
///
/// A pass that only conditionally uses BFI can uncondtionally require the
/// analysis without paying for the overhead if BFI doesn't end up being used.
template <typename FunctionT, typename BranchProbabilityInfoPassT,
          typename CycleInfoT, typename BlockFrequencyInfoT>
class LazyBlockFrequencyInfo {
public:
  /// Construct an empty lazy block frequency info wrapper.
  LazyBlockFrequencyInfo() = default;

  /// Set up the per-function input.
  /// @param F Function whose block frequencies will be computed on demand.
  /// @param BPIPass Pass that provides branch probability info for \p F.
  /// @param CI Cycle information identifying loops and irreducible SCCs.
  void setAnalysis(const FunctionT *F, BranchProbabilityInfoPassT *BPIPass,
                   const CycleInfoT *CI) {
    this->F = F;
    this->BPIPass = BPIPass;
    this->CI = CI;
  }

  /// Retrieve the BFI with the block frequencies computed.
  /// @return Block frequency info, computed on demand if needed.
  BlockFrequencyInfoT &getCalculated() {
    if (!Calculated) {
      assert(F && BPIPass && CI && "call setAnalysis");
      BFI.calculate(
          *F, BPIPassTrait<BranchProbabilityInfoPassT>::getBPI(BPIPass), *CI);
      Calculated = true;
    }
    return BFI;
  }

  /// Retrieve the BFI with the block frequencies computed.
  /// @return Const block frequency info, computed on demand if needed.
  const BlockFrequencyInfoT &getCalculated() const {
    return const_cast<LazyBlockFrequencyInfo *>(this)->getCalculated();
  }

  /// Release computed frequencies and clear the analysis inputs.
  void releaseMemory() {
    BFI.releaseMemory();
    Calculated = false;
    setAnalysis(nullptr, nullptr, nullptr);
  }

private:
  BlockFrequencyInfoT BFI;
  bool Calculated = false;
  const FunctionT *F = nullptr;
  BranchProbabilityInfoPassT *BPIPass = nullptr;
  const CycleInfoT *CI = nullptr;
};

/// Alternative analysis pass that computes block frequencies on demand.
///
/// This is an alternative analysis pass to
/// BlockFrequencyInfoWrapperPass.  The difference is that with this pass the
/// block frequencies are not computed when the analysis pass is executed but
/// rather when the BFI result is explicitly requested by the analysis client.
///
/// There are some additional requirements for any client pass that wants to use
/// the analysis:
///
/// 1. The pass needs to initialize dependent passes with:
///
///   INITIALIZE_PASS_DEPENDENCY(LazyBFIPass)
///
/// 2. Similarly, getAnalysisUsage should call:
///
///   LazyBlockFrequencyInfoPass::getLazyBFIAnalysisUsage(AU)
///
/// 3. The computed BFI should be requested with
///    getAnalysis<LazyBlockFrequencyInfoPass>().getBFI() before either
///    CycleInfo or BPI could be invalidated for example by changing the CFG.
///
/// Note that it is expected that we wouldn't need this functionality for the
/// new PM since with the new PM, analyses are executed on demand.

class LLVM_ABI LazyBlockFrequencyInfoPass : public FunctionPass {
private:
  LazyBlockFrequencyInfo<Function, LazyBranchProbabilityInfoPass, CycleInfo,
                         BlockFrequencyInfo>
      LBFI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct a LazyBlockFrequencyInfoPass.
  LazyBlockFrequencyInfoPass();

  /// Compute and return the block frequencies.
  /// @return Block frequency info, computed on demand if needed.
  BlockFrequencyInfo &getBFI() { return LBFI.getCalculated(); }

  /// Compute and return the block frequencies.
  /// @return Const block frequency info, computed on demand if needed.
  const BlockFrequencyInfo &getBFI() const { return LBFI.getCalculated(); }

  /// Declare the analyses required and preserved by this pass.
  /// @param AU Analysis usage to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Helper for client passes to set up the analysis usage on behalf of this
  /// pass.
  /// @param AU Analysis usage to update.
  static void getLazyBFIAnalysisUsage(AnalysisUsage &AU);

  /// Set up lazy BFI analysis for function \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnFunction(Function &F) override;
  /// Release the cached lazy BFI between runs.
  void releaseMemory() override;
  /// Print the computed block frequencies.
  /// @param OS Stream to write the printed results to.
  /// @param M Optional module context; unused by this pass.
  void print(raw_ostream &OS, const Module *M) const override;
};

} // namespace llvm
#endif
