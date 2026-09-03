//===- LiveStacks.h - Live Stack Slot Analysis ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the live stack slot analysis pass. It is analogous to
// live interval analysis except it's analyzing liveness of stack slots rather
// than registers.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVESTACKS_H
#define LLVM_CODEGEN_LIVESTACKS_H

#include "llvm/CodeGen/LiveInterval.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/IR/PassManager.h"
#include "llvm/InitializePasses.h"
#include "llvm/PassRegistry.h"
#include <cassert>
#include <map>
#include <unordered_map>

namespace llvm {

class AnalysisUsage;
class MachineFunction;
class Module;
class raw_ostream;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;

/// Analysis that tracks live intervals for stack slots.
class LiveStacks {
  const TargetRegisterInfo *TRI = nullptr;

  /// Special pool allocator for VNInfo's (LiveInterval val#).
  ///
  VNInfo::Allocator VNInfoAllocator;

  /// S2IMap - Stack slot indices to live interval mapping.
  using SS2IntervalMap = std::unordered_map<int, LiveInterval>;
  SS2IntervalMap S2IMap;

  /// S2RCMap - Stack slot indices to register class mapping.
  std::map<int, const TargetRegisterClass *> S2RCMap;

public:
  /// Iterator over stack-slot to live-interval mappings.
  using iterator = SS2IntervalMap::iterator;
  /// Const iterator over stack-slot to live-interval mappings.
  using const_iterator = SS2IntervalMap::const_iterator;

  /// Return an iterator to the first stack-slot interval.
  ///
  /// \return Const iterator to the first mapping in the interval map.
  const_iterator begin() const { return S2IMap.begin(); }
  /// Return an iterator past the last stack-slot interval.
  ///
  /// \return Const iterator past the last mapping in the interval map.
  const_iterator end() const { return S2IMap.end(); }
  /// Return an iterator to the first stack-slot interval.
  ///
  /// \return Iterator to the first mapping in the interval map.
  iterator begin() { return S2IMap.begin(); }
  /// Return an iterator past the last stack-slot interval.
  ///
  /// \return Iterator past the last mapping in the interval map.
  iterator end() { return S2IMap.end(); }

  /// Return the number of tracked stack-slot live intervals.
  ///
  /// \return Number of stack-slot live intervals currently tracked.
  unsigned getNumIntervals() const { return (unsigned)S2IMap.size(); }

  /// Return the live interval for \p Slot, creating it if needed.
  ///
  /// \param Slot Stack slot index to look up or create.
  /// \param RC Register class associated with \p Slot.
  /// \return Live interval for \p Slot.
  LLVM_ABI LiveInterval &getOrCreateInterval(int Slot,
                                             const TargetRegisterClass *RC);

  /// Return the live interval for stack slot \p Slot.
  ///
  /// \param Slot Stack slot index to look up.
  /// \return Mutable live interval for \p Slot.
  LiveInterval &getInterval(int Slot) {
    assert(Slot >= 0 && "Spill slot indice must be >= 0");
    SS2IntervalMap::iterator I = S2IMap.find(Slot);
    assert(I != S2IMap.end() && "Interval does not exist for stack slot");
    return I->second;
  }

  /// Return the live interval for stack slot \p Slot.
  ///
  /// \param Slot Stack slot index to look up.
  /// \return Const live interval for \p Slot.
  const LiveInterval &getInterval(int Slot) const {
    assert(Slot >= 0 && "Spill slot indice must be >= 0");
    SS2IntervalMap::const_iterator I = S2IMap.find(Slot);
    assert(I != S2IMap.end() && "Interval does not exist for stack slot");
    return I->second;
  }

  /// Return true if a live interval exists for stack slot \p Slot.
  ///
  /// \param Slot Stack slot index to query.
  /// \return True if an interval is present for \p Slot.
  bool hasInterval(int Slot) const { return S2IMap.count(Slot); }

  /// Return the register class associated with stack slot \p Slot.
  ///
  /// \param Slot Stack slot index to look up.
  /// \return Register class recorded for \p Slot.
  const TargetRegisterClass *getIntervalRegClass(int Slot) const {
    assert(Slot >= 0 && "Spill slot indice must be >= 0");
    std::map<int, const TargetRegisterClass *>::const_iterator I =
        S2RCMap.find(Slot);
    assert(I != S2RCMap.end() &&
           "Register class info does not exist for stack slot");
    return I->second;
  }

  /// Return the allocator used for VNInfo values.
  ///
  /// \return Allocator used to create VNInfo values for live intervals.
  VNInfo::Allocator &getVNInfoAllocator() { return VNInfoAllocator; }

  /// Release memory used by the analysis.
  LLVM_ABI void releaseMemory();
  /// Initialize live stack analysis for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  LLVM_ABI void init(MachineFunction &MF);
  /// Print live stack intervals to \p O.
  ///
  /// \param O Output stream for the dump.
  /// \param M Optional module providing additional context.
  LLVM_ABI void print(raw_ostream &O, const Module *M = nullptr) const;
};

/// Legacy pass wrapper for LiveStacks.
class LLVM_ABI LiveStacksWrapperLegacy : public MachineFunctionPass {
  LiveStacks Impl;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy LiveStacks wrapper pass.
  LiveStacksWrapperLegacy() : MachineFunctionPass(ID) {}

  /// Return the computed LiveStacks analysis.
  ///
  /// \return Mutable reference to the wrapped LiveStacks analysis.
  LiveStacks &getLS() { return Impl; }
  /// Return the computed LiveStacks analysis.
  ///
  /// \return Const reference to the wrapped LiveStacks analysis.
  const LiveStacks &getLS() const { return Impl; }

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Release memory used by the wrapped analysis.
  void releaseMemory() override;

  /// Run live stack analysis on machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Print the live stack intervals computed by this pass.
  ///
  /// \param O Output stream for the dump.
  /// \param M Optional module providing additional context.
  void print(raw_ostream &O, const Module *M = nullptr) const override;
};

/// Analysis pass that computes \c LiveStacks for a machine function.
class LiveStacksAnalysis : public AnalysisInfoMixin<LiveStacksAnalysis> {
  static AnalysisKey Key;
  friend AnalysisInfoMixin<LiveStacksAnalysis>;

public:
  /// Result type produced by this analysis.
  using Result = LiveStacks;

  /// Compute LiveStacks for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Live stack slot intervals for \p MF.
  LLVM_ABI LiveStacks run(MachineFunction &MF,
                          MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c LiveStacksAnalysis results.
class LiveStacksPrinterPass
    : public RequiredPassInfoMixin<LiveStacksPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the live stacks dump.
  LiveStacksPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print LiveStacksAnalysis results for \p MF.
  ///
  /// \param MF Machine function whose live stacks are printed.
  /// \param AM Analysis manager providing LiveStacksAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &AM);
};
} // end namespace llvm

#endif
