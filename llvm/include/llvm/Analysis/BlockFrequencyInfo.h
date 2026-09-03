//===- BlockFrequencyInfo.h - Block Frequency Analysis ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Loops should be simplified before this analysis.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BLOCKFREQUENCYINFO_H
#define LLVM_ANALYSIS_BLOCKFREQUENCYINFO_H

#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Printable.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace llvm {

class BasicBlock;
class BranchProbabilityInfo;
class CycleInfo;
class Module;
class raw_ostream;
template <class BlockT> class BlockFrequencyInfoImpl;

/// How to display block profile counts after PGO annotation.
enum PGOViewCountsType {
  /// Do not show profile counts.
  PGOVCT_None,
  /// Show profile counts as a Graphviz CFG.
  PGOVCT_Graph,
  /// Show profile counts as text.
  PGOVCT_Text
};

/// BlockFrequencyInfo pass uses BlockFrequencyInfoImpl implementation to
/// estimate IR basic block frequencies.
class BlockFrequencyInfo {
  using ImplType = BlockFrequencyInfoImpl<BasicBlock>;

  std::unique_ptr<ImplType> BFI;

public:
  /// Construct an empty analysis; call calculate() before querying.
  LLVM_ABI BlockFrequencyInfo();
  /// Construct and compute frequencies for \p F.
  /// @param F Function whose CFG is analyzed.
  /// @param BPI Branch probabilities for edges in \p F.
  /// @param CI Cycle information identifying loops and irreducible SCCs.
  LLVM_ABI BlockFrequencyInfo(const Function &F,
                              const BranchProbabilityInfo &BPI,
                              const CycleInfo &CI);
  /// Deleted copy constructor; BlockFrequencyInfo is not copyable.
  /// @param Other Unused source object; copying is not supported.
  BlockFrequencyInfo(const BlockFrequencyInfo &Other) = delete;
  /// Deleted copy assignment; BlockFrequencyInfo is not copyable.
  /// @param RHS Unused right-hand side; copying is not supported.
  BlockFrequencyInfo &operator=(const BlockFrequencyInfo &RHS) = delete;
  /// Move-construct from \p Arg.
  /// @param Arg BlockFrequencyInfo to move from.
  LLVM_ABI BlockFrequencyInfo(BlockFrequencyInfo &&Arg);
  /// Move-assign from \p RHS.
  /// @param RHS BlockFrequencyInfo to move from.
  /// @return Reference to this BlockFrequencyInfo.
  LLVM_ABI BlockFrequencyInfo &operator=(BlockFrequencyInfo &&RHS);
  /// Destroy this BlockFrequencyInfo and release owned implementation state.
  LLVM_ABI ~BlockFrequencyInfo();

  /// Handle invalidation explicitly.
  /// @param F Function being invalidated.
  /// @param PA Set of preserved analyses.
  /// @param Inv Invalidator for dependent analyses.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  /// Return the function this analysis was computed for, or null if empty.
  /// @return The function this analysis was computed for, or null if empty.
  LLVM_ABI const Function *getFunction() const;
  /// Return the branch probability info used to compute these frequencies.
  /// @return The branch probability info used to compute these frequencies.
  LLVM_ABI const BranchProbabilityInfo *getBPI() const;
  /// Pop up a Graphviz window showing block frequency propagation.
  ///
  /// Renders the current CFG with frequency annotations. The optional argument
  /// is the DOT graph title (default "BlockFrequencyDAGs").
  /// @param Title DOT graph title for the visualization window.
  LLVM_ABI void view(StringRef Title = "BlockFrequencyDAGs") const;

  /// Return the frequency of basic block \p BB.
  ///
  /// Returns 0 if we don't have the information. Please note that initial
  /// frequency is equal to ENTRY_FREQ. It means that we should not rely on the
  /// value itself, but only on the comparison to the other block frequencies.
  /// We do this to avoid using of floating points.
  /// @param BB Block whose frequency is requested.
  /// @return The frequency of basic block \p BB.
  LLVM_ABI BlockFrequency getBlockFreq(const BasicBlock *BB) const;

  /// Return the estimated profile count of \p BB.
  ///
  /// This computes the relative block frequency of \p BB and multiplies it by
  /// the enclosing function's count (if available) and returns the value.
  /// @param BB Block whose profile count is requested.
  /// @param AllowSynthetic Whether synthetic profile counts may be used.
  /// @return The estimated profile count, or nullopt if unavailable.
  LLVM_ABI std::optional<uint64_t>
  getBlockProfileCount(const BasicBlock *BB, bool AllowSynthetic = false) const;

  /// Return the estimated profile count of frequency \p Freq.
  ///
  /// This uses the frequency \p Freq and multiplies it by the enclosing
  /// function's count (if available) and returns the value.
  /// @param Freq Relative block frequency to convert.
  /// @return The estimated profile count, or nullopt if unavailable.
  LLVM_ABI std::optional<uint64_t>
  getProfileCountFromFreq(BlockFrequency Freq) const;

  /// Returns true if \p BB is an irreducible loop header
  /// block. Otherwise false.
  /// @param BB Block to test.
  /// @return True if \p BB is an irreducible loop header.
  LLVM_ABI bool isIrrLoopHeader(const BasicBlock *BB);

  /// Set the frequency of \p BB to \p Freq.
  /// @param BB Block whose frequency is updated.
  /// @param Freq New frequency to store.
  LLVM_ABI void setBlockFreq(const BasicBlock *BB, BlockFrequency Freq);

  /// Set \p ReferenceBB's frequency and scale related blocks proportionally.
  ///
  /// Sets the frequency of \p ReferenceBB to \p Freq and scales the frequencies
  /// of the blocks in \p BlocksToScale such that their frequencies relative to
  /// \p ReferenceBB remain unchanged.
  /// @param ReferenceBB Block whose frequency is set to \p Freq.
  /// @param Freq New frequency for \p ReferenceBB.
  /// @param BlocksToScale Blocks whose frequencies are scaled relative to
  ///        \p ReferenceBB.
  LLVM_ABI void
  setBlockFreqAndScale(const BasicBlock *ReferenceBB, BlockFrequency Freq,
                       SmallPtrSetImpl<BasicBlock *> &BlocksToScale);

  /// Compute block frequency info for the given function.
  /// @param F Function whose CFG is analyzed.
  /// @param BPI Branch probabilities for edges in \p F.
  /// @param CI Cycle information identifying loops and irreducible SCCs.
  LLVM_ABI void calculate(const Function &F, const BranchProbabilityInfo &BPI,
                          const CycleInfo &CI);

  /// Return the frequency of the function entry block.
  /// @return The frequency of the function entry block.
  LLVM_ABI BlockFrequency getEntryFreq() const;
  /// Release owned frequency data and reset to an empty state.
  LLVM_ABI void releaseMemory();
  /// Print block frequencies for the current function to \p OS.
  /// @param OS Output stream to write frequencies to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Assert that this analysis matches \p Other block-for-block.
  /// @param Other Other BlockFrequencyInfo to compare against.
  LLVM_ABI void verifyMatch(BlockFrequencyInfo &Other) const;
};

/// Print the block frequency @p Freq relative to the current functions entry
/// frequency. Returns a Printable object that can be piped via `<<` to a
/// `raw_ostream`.
/// @param BFI Analysis providing the entry frequency scale.
/// @param Freq Absolute block frequency to print relatively.
/// @return A Printable object that can be piped via `<<` to a `raw_ostream`.
LLVM_ABI Printable printBlockFreq(const BlockFrequencyInfo &BFI,
                                  BlockFrequency Freq);

/// Convenience function equivalent to calling
/// `printBlockFreq(BFI, BFI.getBlocakFreq(&BB))`.
/// @param BFI Analysis providing frequencies for \p BB.
/// @param BB Basic block whose frequency is printed.
/// @return A Printable object that can be piped via `<<` to a `raw_ostream`.
LLVM_ABI Printable printBlockFreq(const BlockFrequencyInfo &BFI,
                                  const BasicBlock &BB);

/// Analysis pass which computes \c BlockFrequencyInfo.
class BlockFrequencyAnalysis
    : public AnalysisInfoMixin<BlockFrequencyAnalysis> {
  friend AnalysisInfoMixin<BlockFrequencyAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = BlockFrequencyInfo;

  /// Run the analysis pass over a function and produce BFI.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return The computed BlockFrequencyInfo for \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for the \c BlockFrequencyInfo results.
class BlockFrequencyPrinterPass
    : public RequiredPassInfoMixin<BlockFrequencyPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes block frequencies to \p OS.
  /// @param OS Output stream for the printed frequencies.
  explicit BlockFrequencyPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print block frequency results for \p F.
  /// @param F Function whose frequencies are printed.
  /// @param AM Function analysis manager providing BlockFrequencyAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Legacy analysis pass which computes \c BlockFrequencyInfo.
class LLVM_ABI BlockFrequencyInfoWrapperPass : public FunctionPass {
  BlockFrequencyInfo BFI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy block frequency analysis wrapper pass.
  BlockFrequencyInfoWrapperPass();
  /// Destroy this wrapper and release owned frequency data.
  ~BlockFrequencyInfoWrapperPass() override;

  /// Return the cached BlockFrequencyInfo for the last analyzed function.
  /// @return The cached BlockFrequencyInfo for the last analyzed function.
  BlockFrequencyInfo &getBFI() { return BFI; }
  /// Return the cached BlockFrequencyInfo for the last analyzed function.
  /// @return The cached BlockFrequencyInfo for the last analyzed function.
  const BlockFrequencyInfo &getBFI() const { return BFI; }

  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Compute block frequency information for function \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;
  /// Release the cached BlockFrequencyInfo between runs.
  void releaseMemory() override;
  /// Print the cached block frequency information.
  /// @param OS Stream to write the printed results to.
  /// @param M Optional module context; unused by this pass.
  void print(raw_ostream &OS, const Module *M) const override;
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_BLOCKFREQUENCYINFO_H
