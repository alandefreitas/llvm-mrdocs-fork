//===- MachineBlockFrequencyInfo.h - MBB Frequency Analysis -----*- C++ -*-===//
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

#ifndef LLVM_CODEGEN_MACHINEBLOCKFREQUENCYINFO_H
#define LLVM_CODEGEN_MACHINEBLOCKFREQUENCYINFO_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace llvm {

template <class BlockT> class BlockFrequencyInfoImpl;
class MachineBasicBlock;
class MachineBranchProbabilityInfo;
class MachineFunction;
class MachineCycleInfo;
class raw_ostream;

/// MachineBlockFrequencyInfo pass uses BlockFrequencyInfoImpl implementation
/// to estimate machine basic block frequencies.
class MachineBlockFrequencyInfo {
  using ImplType = BlockFrequencyInfoImpl<MachineBasicBlock>;
  std::unique_ptr<ImplType> MBFI;

public:
  /// Construct an empty analysis; call calculate() before querying.
  LLVM_ABI MachineBlockFrequencyInfo(); // Legacy pass manager only.
  /// Construct and compute frequencies for \p F.
  /// @param F Machine function whose CFG is analyzed.
  /// @param MBPI Branch probabilities for edges in \p F.
  /// @param MCI Cycle information identifying loops and irreducible SCCs.
  LLVM_ABI explicit MachineBlockFrequencyInfo(
      const MachineFunction &F, const MachineBranchProbabilityInfo &MBPI,
      const MachineCycleInfo &MCI);
  /// Move-construct from \p Other.
  /// @param Other MachineBlockFrequencyInfo to move from.
  LLVM_ABI MachineBlockFrequencyInfo(MachineBlockFrequencyInfo &&Other);
  /// Destroy this MachineBlockFrequencyInfo and release owned implementation
  /// state.
  LLVM_ABI ~MachineBlockFrequencyInfo();

  /// Handle invalidation explicitly.
  /// @param F Machine function being invalidated.
  /// @param PA Set of preserved analyses.
  /// @param Inv Invalidator for dependent analyses.
  /// @return True if this analysis result should be discarded.
  LLVM_ABI bool invalidate(MachineFunction &F, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Compute block frequency info for the given function.
  /// @param F Machine function whose CFG is analyzed.
  /// @param MBPI Branch probabilities for edges in \p F.
  /// @param MCI Cycle information identifying loops and irreducible SCCs.
  LLVM_ABI void calculate(const MachineFunction &F,
                          const MachineBranchProbabilityInfo &MBPI,
                          const MachineCycleInfo &MCI);

  /// Print block frequencies for the current function to \p OS.
  /// @param OS Output stream to write frequencies to.
  LLVM_ABI void print(raw_ostream &OS);

  /// Release owned frequency data and reset to an empty state.
  LLVM_ABI void releaseMemory();

  /// Return the frequency of machine basic block \p MBB.
  ///
  /// Returns 0 if we don't have the information. Please note that initial
  /// frequency is equal to 1024. It means that we should not rely on the value
  /// itself, but only on the comparison to the other block frequencies. We do
  /// this to avoid using of floating points. For example, to get the frequency
  /// of a block relative to the entry block, divide the integral value returned
  /// by this function (the BlockFrequency::getFrequency() value) by
  /// getEntryFreq().
  /// @param MBB Block whose frequency is requested.
  /// @return Absolute frequency of \p MBB, or 0 if unknown.
  LLVM_ABI BlockFrequency getBlockFreq(const MachineBasicBlock *MBB) const;

  /// Compute the frequency of the block, relative to the entry block.
  /// This API assumes getEntryFreq() is non-zero.
  /// @param MBB Block whose relative frequency is requested.
  /// @return Frequency of \p MBB divided by the entry-block frequency.
  double getBlockFreqRelativeToEntryBlock(const MachineBasicBlock *MBB) const {
    assert(getEntryFreq() != BlockFrequency(0) &&
           "getEntryFreq() should not return 0 here!");
    return static_cast<double>(getBlockFreq(MBB).getFrequency()) /
           static_cast<double>(getEntryFreq().getFrequency());
  }

  /// Return the estimated profile count of \p MBB.
  /// @param MBB Block whose profile count is requested.
  /// @return Estimated profile count for \p MBB, or nullopt if unavailable.
  LLVM_ABI std::optional<uint64_t>
  getBlockProfileCount(const MachineBasicBlock *MBB) const;
  /// Return the estimated profile count of frequency \p Freq.
  /// @param Freq Relative block frequency to convert.
  /// @return Estimated profile count for \p Freq, or nullopt if unavailable.
  LLVM_ABI std::optional<uint64_t>
  getProfileCountFromFreq(BlockFrequency Freq) const;

  /// Returns true if \p MBB is an irreducible loop header block.
  /// @param MBB Block to test.
  /// @return True if \p MBB is an irreducible loop header.
  LLVM_ABI bool isIrrLoopHeader(const MachineBasicBlock *MBB) const;

  /// Incrementally update frequencies after splitting an edge.
  ///
  /// Avoids a full CFG traversal when an edge is split into
  /// \p NewPredecessor -> \p NewSuccessor.
  /// @param NewPredecessor New predecessor block created by the split.
  /// @param NewSuccessor New successor block created by the split.
  /// @param MBPI Branch probabilities used to scale the new edge.
  LLVM_ABI void onEdgeSplit(const MachineBasicBlock &NewPredecessor,
                            const MachineBasicBlock &NewSuccessor,
                            const MachineBranchProbabilityInfo &MBPI);

  /// Return the function this analysis was computed for, or null if empty.
  /// @return Pointer to the analyzed MachineFunction, or null if empty.
  LLVM_ABI const MachineFunction *getFunction() const;
  /// Return the branch probability info used to compute these frequencies.
  /// @return Pointer to the MachineBranchProbabilityInfo used for computation.
  LLVM_ABI const MachineBranchProbabilityInfo *getMBPI() const;

  /// Pop up a ghostview window with the current block frequency propagation
  /// rendered using dot.
  /// @param Name DOT graph title for the visualization window.
  /// @param isSimple Whether to use a simplified graph rendering.
  LLVM_ABI void view(const Twine &Name, bool isSimple = true) const;

  /// Divide a block's BlockFrequency::getFrequency() value by this value to
  /// obtain the entry block - relative frequency of said block.
  /// @return Frequency assigned to the function entry block.
  LLVM_ABI BlockFrequency getEntryFreq() const;
};

/// Print the block frequency @p Freq relative to the current functions entry
/// frequency. Returns a Printable object that can be piped via `<<` to a
/// `raw_ostream`.
/// @param MBFI Analysis providing the entry frequency scale.
/// @param Freq Absolute block frequency to print relatively.
/// @return Printable object that formats \p Freq relative to entry frequency.
LLVM_ABI Printable printBlockFreq(const MachineBlockFrequencyInfo &MBFI,
                                  BlockFrequency Freq);

/// Convenience function equivalent to calling
/// `printBlockFreq(MBFI, MBFI.getBlockFreq(&MBB))`.
/// @param MBFI Analysis providing frequencies for \p MBB.
/// @param MBB Machine basic block whose frequency is printed.
/// @return Printable object that formats \p MBB's relative frequency.
LLVM_ABI Printable printBlockFreq(const MachineBlockFrequencyInfo &MBFI,
                                  const MachineBasicBlock &MBB);

/// Analysis pass which computes \c MachineBlockFrequencyInfo.
class MachineBlockFrequencyAnalysis
    : public AnalysisInfoMixin<MachineBlockFrequencyAnalysis> {
  friend AnalysisInfoMixin<MachineBlockFrequencyAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = MachineBlockFrequencyInfo;

  /// Run the analysis pass over a machine function and produce MBFI.
  /// @param MF Machine function to analyze.
  /// @param MFAM Machine function analysis manager providing dependencies.
  /// @return Computed MachineBlockFrequencyInfo for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c MachineBlockFrequencyInfo results.
class MachineBlockFrequencyPrinterPass
    : public RequiredPassInfoMixin<MachineBlockFrequencyPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes block frequencies to \p OS.
  /// @param OS Output stream for the printed frequencies.
  explicit MachineBlockFrequencyPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print block frequency results for \p MF.
  /// @param MF Machine function whose frequencies are printed.
  /// @param MFAM Machine function analysis manager providing
  ///        MachineBlockFrequencyAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Legacy analysis pass which computes \c MachineBlockFrequencyInfo.
class LLVM_ABI MachineBlockFrequencyInfoWrapperPass
    : public MachineFunctionPass {
  MachineBlockFrequencyInfo MBFI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy machine block frequency analysis wrapper pass.
  MachineBlockFrequencyInfoWrapperPass();

  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Compute block frequency information for machine function \p F.
  /// @param F Machine function to analyze.
  /// @return False; this analysis does not modify the function.
  bool runOnMachineFunction(MachineFunction &F) override;

  /// Release the cached MachineBlockFrequencyInfo between runs.
  void releaseMemory() override { MBFI.releaseMemory(); }

  /// Return the cached MachineBlockFrequencyInfo for the last analyzed
  /// function.
  /// @return Mutable reference to the cached frequency analysis.
  MachineBlockFrequencyInfo &getMBFI() { return MBFI; }

  /// Return the cached MachineBlockFrequencyInfo for the last analyzed
  /// function.
  /// @return Const reference to the cached frequency analysis.
  const MachineBlockFrequencyInfo &getMBFI() const { return MBFI; }
};
} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEBLOCKFREQUENCYINFO_H
