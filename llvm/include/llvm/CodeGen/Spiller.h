//===- llvm/CodeGen/Spiller.h - Spiller -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_SPILLER_H
#define LLVM_CODEGEN_SPILLER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/CodeGen/Register.h"

namespace llvm {

class LiveRangeEdit;
class MachineFunction;
class MachineFunctionPass;
class VirtRegMap;
class VirtRegAuxInfo;
class LiveIntervals;
class LiveRegMatrix;
class LiveStacks;
class MachineDominatorTree;
class MachineBlockFrequencyInfo;
class AllocationOrder;

/// Spiller interface.
///
/// Implementations are utility classes which insert spill or remat code on
/// demand.
class LLVM_ABI Spiller {
  virtual void anchor();

public:
  /// Destroy this spiller.
  virtual ~Spiller() = 0;

  /// spill - Spill the LRE.getParent() live interval.
  ///
  /// \param LRE Live range edit describing the interval being spilled.
  /// \param Order Optional preferred physical register allocation order.
  virtual void spill(LiveRangeEdit &LRE, AllocationOrder *Order = nullptr) = 0;

  /// Return the registers that were spilled.
  ///
  /// \return Array of virtual registers that were spilled.
  virtual ArrayRef<Register> getSpilledRegs() = 0;

  /// Return registers that were not spilled, but otherwise replaced
  /// (e.g. rematerialized).
  ///
  /// \return Array of virtual registers that were replaced without spilling.
  virtual ArrayRef<Register> getReplacedRegs() = 0;

  /// Perform post-allocation optimizations such as spill hoisting.
  virtual void postOptimization() {}

  /// Analysis results required to construct a spiller.
  struct RequiredAnalyses {
    /// Live interval analysis for the current function.
    LiveIntervals &LIS;
    /// Live stack slot analysis for spilled values.
    LiveStacks &LSS;
    /// Machine dominator tree for the current function.
    MachineDominatorTree &MDT;
    /// Block frequency info used for spill placement heuristics.
    const MachineBlockFrequencyInfo &MBFI;
  };
};

/// Create and return a spiller that will insert spill code directly instead
/// of deferring though VirtRegMap.
///
/// \param Analyses Analysis results required by the inline spiller.
/// \param MF Machine function being allocated.
/// \param VRM Mapping from virtual to physical registers.
/// \param VRAI Auxiliary virtual register info for spill weights and hints.
/// \param Matrix Optional live register matrix for interference queries.
/// \return A newly created inline Spiller owned by the caller.
LLVM_ABI Spiller *createInlineSpiller(const Spiller::RequiredAnalyses &Analyses,
                                      MachineFunction &MF, VirtRegMap &VRM,
                                      VirtRegAuxInfo &VRAI,
                                      LiveRegMatrix *Matrix = nullptr);

} // end namespace llvm

#endif // LLVM_CODEGEN_SPILLER_H
