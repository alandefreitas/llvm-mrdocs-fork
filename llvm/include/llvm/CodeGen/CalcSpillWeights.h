//===- lib/CodeGen/CalcSpillWeights.h ---------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_CALCSPILLWEIGHTS_H
#define LLVM_CODEGEN_CALCSPILLWEIGHTS_H

#include "llvm/CodeGen/SlotIndexes.h"
#include <optional>

namespace llvm {

class LiveInterval;
class LiveIntervals;
class MachineBlockFrequencyInfo;
class MachineFunction;
class MachineLoopInfo;
class ProfileSummaryInfo;
class VirtRegMap;

  /// Normalize the spill weight of a live interval
  ///
  /// The spill weight of a live interval is computed as:
  ///
  ///   (sum(use freq) + sum(def freq)) / (K + size)
  ///
  /// @param UseDefFreq Expected number of executed use and def instructions
  ///                   per function call. Derived from block frequencies.
  /// @param Size       Size of live interval as returnexd by getSize()
  /// @param NumInstr   Number of instructions using this live interval
  static inline float normalizeSpillWeight(float UseDefFreq, unsigned Size,
                                           unsigned NumInstr) {
    // The constant 25 instructions is added to avoid depending too much on
    // accidental SlotIndex gaps for small intervals. The effect is that small
    // intervals have a spill weight that is mostly proportional to the number
    // of uses, while large intervals get a spill weight that is closer to a use
    // density.
    return UseDefFreq / (Size + 25*SlotIndex::InstrDist);
  }

  /// Calculate auxiliary information for a virtual register such as its
  /// spill weight and allocation hint.
  class VirtRegAuxInfo {
    MachineFunction &MF;
    LiveIntervals &LIS;
    const VirtRegMap &VRM;
    const MachineLoopInfo &Loops;
    ProfileSummaryInfo *PSI;
    const MachineBlockFrequencyInfo &MBFI;

    /// Memoized llvm::shouldOptimizeForSize(&MF, PSI, &MBFI).
    std::optional<bool> CachedOptForSize;

    /// Lazily computes and caches the above.
    bool getCachedOptimizeForSize();

    /// Returns true if Reg of live interval LI is used in instruction with many
    /// operands like STATEPOINT.
    bool isLiveAtStatepointVarArg(LiveInterval &LI);

  public:
    /// Construct auxiliary info for virtual-register spill weight calculation.
    ///
    /// \param MF Machine function whose virtual registers are analyzed.
    /// \param LIS Live interval information for \p MF.
    /// \param VRM Mapping from virtual to physical registers.
    /// \param Loops Loop information used to bias spill weights.
    /// \param MBFI Block frequency info used to weight uses and defs.
    /// \param PSI Optional profile summary used for size-optimization decisions.
    VirtRegAuxInfo(MachineFunction &MF, LiveIntervals &LIS,
                   const VirtRegMap &VRM, const MachineLoopInfo &Loops,
                   const MachineBlockFrequencyInfo &MBFI,
                   ProfileSummaryInfo *PSI = nullptr)
        : MF(MF), LIS(LIS), VRM(VRM), Loops(Loops), PSI(PSI), MBFI(MBFI) {}

    /// Virtual destructor.
    virtual ~VirtRegAuxInfo() = default;

    /// (re)compute li's spill weight and allocation hint.
    ///
    /// \param LI Live interval whose spill weight and hint are recomputed.
    LLVM_ABI void calculateSpillWeightAndHint(LiveInterval &LI);

    /// Compute spill weights and allocation hints for all virtual register
    /// live intervals.
    LLVM_ABI void calculateSpillWeightsAndHints();

    /// Return the preferred allocation register for reg, given a COPY
    /// instruction.
    ///
    /// \param MI COPY instruction that may imply a preferred register.
    /// \param Reg Virtual register for which a copy hint is requested.
    /// \param TRI Target register info used to interpret the COPY operands.
    /// \param MRI Machine register info for the function.
    /// \return The preferred physical register hinted by the COPY, or an
    ///         invalid register if no useful hint is found.
    LLVM_ABI static Register copyHint(const MachineInstr *MI, Register Reg,
                                      const TargetRegisterInfo &TRI,
                                      const MachineRegisterInfo &MRI);

    /// Determine if all values in LI are rematerializable.
    ///
    /// \param LI Live interval whose values are checked for rematerialization.
    /// \param LIS Live interval information for the function.
    /// \param VRM Mapping from virtual to physical registers.
    /// \param MRI Machine register info for the function.
    /// \param TII Target instruction info used to query rematerialization.
    /// \return true if every value in \p LI can be rematerialized.
    LLVM_ABI static bool isRematerializable(const LiveInterval &LI,
                                            const LiveIntervals &LIS,
                                            const VirtRegMap &VRM,
                                            const MachineRegisterInfo &MRI,
                                            const TargetInstrInfo &TII);

    /// Check whether every register used by MI is available at UseIdx.
    ///
    /// \param MI Instruction whose register uses are checked.
    /// \param UseIdx Slot index at which availability is queried.
    /// \param LIS Live interval information for the function.
    /// \param MRI Machine register info for the function.
    /// \param TII Target instruction info used for instruction queries.
    /// \returns true if all registers used by \p MI are also available with the
    /// same value at \p UseIdx.
    LLVM_ABI static bool allUsesAvailableAt(const MachineInstr *MI,
                                            SlotIndex UseIdx,
                                            const LiveIntervals &LIS,
                                            const MachineRegisterInfo &MRI,
                                            const TargetInstrInfo &TII);

  protected:
    /// Compute the spill weight and allocation hint for a live interval.
    ///
    /// (Re)compute LI's spill weight and allocation hint, or, for non null
    /// start and end - compute future expected spill weight of a split
    /// artifact of LI that will span between start and end slot indexes.
    /// \param LI The live interval for which to compute the weight.
    /// \return The spill weight. Returns negative weight for unspillable LI.
    LLVM_ABI float weightCalcHelper(LiveInterval &LI);

    /// Normalize a raw spill weight for a live interval.
    ///
    /// \param UseDefFreq Expected number of executed use and def instructions
    ///        per function call. Derived from block frequencies.
    /// \param Size Size of the live interval as returned by getSize().
    /// \param NumInstr Number of instructions using this live interval.
    /// \return The normalized spill weight.
    virtual float normalize(float UseDefFreq, unsigned Size,
                            unsigned NumInstr) {
      return normalizeSpillWeight(UseDefFreq, Size, NumInstr);
    }
  };
} // end namespace llvm

#endif // LLVM_CODEGEN_CALCSPILLWEIGHTS_H
