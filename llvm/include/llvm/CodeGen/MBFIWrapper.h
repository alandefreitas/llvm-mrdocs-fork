//===- llvm/CodeGen/MBFIWrapper.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This class keeps track of branch frequencies of newly created blocks and
// tail-merged blocks. Used by the TailDuplication and MachineBlockPlacement.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MBFIWRAPPER_H
#define LLVM_CODEGEN_MBFIWRAPPER_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/Support/BlockFrequency.h"
#include <optional>

namespace llvm {

class MachineBasicBlock;
class MachineBlockFrequencyInfo;

/// Tracks block frequencies for newly created and tail-merged blocks.
///
/// Wraps a MachineBlockFrequencyInfo and overlays frequencies for blocks that
/// do not yet appear in the underlying analysis. Used by TailDuplication and
/// MachineBlockPlacement.
class MBFIWrapper {
public:
  /// Construct a wrapper around the given MachineBlockFrequencyInfo.
  /// @param I Underlying block frequency analysis to query and visualize.
  MBFIWrapper(const MachineBlockFrequencyInfo &I) : MBFI(I) {}

  /// Return the frequency of machine basic block \p MBB.
  ///
  /// Prefers an overridden frequency stored in this wrapper; otherwise
  /// forwards to the underlying MachineBlockFrequencyInfo.
  /// @param MBB Block whose frequency is requested.
  /// @return Absolute frequency of \p MBB, or 0 if unknown.
  LLVM_ABI BlockFrequency getBlockFreq(const MachineBasicBlock *MBB) const;
  /// Set an overridden frequency for machine basic block \p MBB.
  /// @param MBB Block whose frequency is updated.
  /// @param F New frequency to store for \p MBB.
  LLVM_ABI void setBlockFreq(const MachineBasicBlock *MBB, BlockFrequency F);
  /// Return the estimated profile count of \p MBB.
  ///
  /// When this wrapper has an overridden frequency for \p MBB, the count is
  /// derived from that frequency via the underlying analysis.
  /// @param MBB Block whose profile count is requested.
  /// @return Estimated profile count for \p MBB, or nullopt if unavailable.
  LLVM_ABI std::optional<uint64_t>
  getBlockProfileCount(const MachineBasicBlock *MBB) const;

  /// Pop up a ghostview window with the current block frequency propagation
  /// rendered using dot.
  /// @param Name DOT graph title for the visualization window.
  /// @param isSimple Whether to use a simplified graph rendering.
  LLVM_ABI void view(const Twine &Name, bool isSimple = true);
  /// Return the frequency of the function entry block.
  /// @return Frequency assigned to the function entry block.
  LLVM_ABI BlockFrequency getEntryFreq() const;
  /// Return the underlying MachineBlockFrequencyInfo.
  /// @return Const reference to the wrapped MachineBlockFrequencyInfo.
  const MachineBlockFrequencyInfo &getMBFI() const { return MBFI; }

private:
  const MachineBlockFrequencyInfo &MBFI;
  DenseMap<const MachineBasicBlock *, BlockFrequency> MergedBBFreq;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MBFIWRAPPER_H
