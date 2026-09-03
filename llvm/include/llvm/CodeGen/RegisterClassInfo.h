//===- RegisterClassInfo.h - Dynamic Register Class Info --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the RegisterClassInfo class which provides dynamic
// information about target register classes. Callee saved and reserved
// registers depends on calling conventions and other dynamic information, so
// some things cannot be determined statically.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERCLASSINFO_H
#define LLVM_CODEGEN_REGISTERCLASSINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <memory>

namespace llvm {

class MachineRegisterClassAnalysis;

/// Provides dynamic information about target register classes for a function.
class RegisterClassInfo {
  struct RCInfo {
    unsigned Tag = 0;
    unsigned NumRegs = 0;
    bool ProperSubClass = false;
    uint8_t MinCost = 0;
    uint16_t LastCostChange = 0;
    std::unique_ptr<MCPhysReg[]> Order;

    RCInfo() = default;

    operator ArrayRef<MCPhysReg>() const {
      return ArrayRef(Order.get(), NumRegs);
    }
  };

  // Brief cached information for each register class.
  std::unique_ptr<RCInfo[]> RegClass;

  // Tag changes whenever cached information needs to be recomputed. An RCInfo
  // entry is valid when its tag matches.
  unsigned Tag = 0;

  bool Reverse = false;

  const MachineFunction *MF = nullptr;
  const TargetRegisterInfo *TRI = nullptr;

  // Callee saved registers of last MF.
  // Used only to determine if an update for CalleeSavedAliases is necessary.
  SmallVector<MCPhysReg, 16> LastCalleeSavedRegs;

  // Map regunit to the callee saved Register.
  SmallVector<MCPhysReg> CalleeSavedAliases;

  // Indicate if a specified callee saved register be in the allocation order
  // exactly as written in the tablegen descriptions or listed later.
  BitVector IgnoreCSRForAllocOrder;

  // Reserved registers in the current MF.
  BitVector Reserved;

  std::unique_ptr<unsigned[]> PSetLimits;

  // The register cost values.
  ArrayRef<uint8_t> RegCosts;

  // Compute all information about RC.
  LLVM_ABI void compute(const TargetRegisterClass *RC) const;

  // Return an up-to-date RCInfo for RC.
  const RCInfo &get(const TargetRegisterClass *RC) const {
    const RCInfo &RCI = RegClass[RC->getID()];
    if (Tag != RCI.Tag)
      compute(RC);
    return RCI;
  }

public:
  /// Construct an uninitialized RegisterClassInfo.
  LLVM_ABI RegisterClassInfo();

  /// Prepare to answer questions about \p MF.
  ///
  /// \p Rev indicates to use reversed raw order when computing register order.
  /// This must be called before any other methods are used.
  ///
  /// @param MF Machine function whose register classes will be queried.
  /// @param Rev If true, reverse the raw register order when computing order.
  LLVM_ABI void runOnMachineFunction(const MachineFunction &MF,
                                     bool Rev = false);

  /// Invalidate this result unless the analysis is preserved.
  ///
  /// @param F Machine function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(MachineFunction &F, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv) {
    auto PAC = PA.getChecker<MachineRegisterClassAnalysis>();
    return !PAC.preservedWhenStateless();
  }

  /// getNumAllocatableRegs - Returns the number of actually allocatable
  /// registers in RC in the current function.
  ///
  /// @param RC Register class whose allocatable register count is requested.
  /// @return Number of allocatable registers in \p RC for the current function.
  unsigned getNumAllocatableRegs(const TargetRegisterClass *RC) const {
    return get(RC).NumRegs;
  }

  /// getOrder - Returns the preferred allocation order for RC. The order
  /// contains no reserved registers, and registers that alias callee saved
  /// registers come last.
  ///
  /// @param RC Register class whose preferred allocation order is requested.
  /// @return Preferred allocation order for \p RC in the current function.
  ArrayRef<MCPhysReg> getOrder(const TargetRegisterClass *RC) const {
    return get(RC);
  }

  /// isProperSubClass - Returns true if RC has a legal super-class with more
  /// allocatable registers.
  ///
  /// Register classes like GR32_NOSP are not proper sub-classes because %esp
  /// is not allocatable.  Similarly, tGPR is not a proper sub-class in Thumb
  /// mode because the GPR super-class is not legal.
  ///
  /// @param RC Register class to test for a proper super-class.
  /// @return True if \p RC has a legal super-class with more allocatable
  /// registers.
  bool isProperSubClass(const TargetRegisterClass *RC) const {
    return get(RC).ProperSubClass;
  }

  /// getLastCalleeSavedAlias - Returns the last callee saved register that
  /// overlaps PhysReg, or NoRegister if PhysReg doesn't overlap a
  /// CalleeSavedAliases.
  ///
  /// @param PhysReg Physical register whose overlapping CSR alias is sought.
  /// @return Last overlapping callee-saved register, or NoRegister if none.
  MCRegister getLastCalleeSavedAlias(MCRegister PhysReg) const {
    MCRegister CSR;
    for (MCRegUnit Unit : TRI->regunits(PhysReg)) {
      CSR = CalleeSavedAliases[static_cast<unsigned>(Unit)];
      if (CSR)
        break;
    }
    return CSR;
  }

  /// Get the minimum register cost in RC's allocation order.
  /// This is the smallest value in RegCosts[Reg] for all
  /// the registers in getOrder(RC).
  ///
  /// @param RC Register class whose minimum allocation-order cost is requested.
  /// @return Smallest RegCosts value among registers in getOrder(\p RC).
  uint8_t getMinCost(const TargetRegisterClass *RC) const {
    return get(RC).MinCost;
  }

  /// Get the position of the last cost change in getOrder(RC).
  ///
  /// All registers in getOrder(RC).slice(getLastCostChange(RC)) will have the
  /// same cost according to RegCosts[Reg].
  ///
  /// @param RC Register class whose last cost-change index is requested.
  /// @return Index of the last cost change in getOrder(\p RC).
  unsigned getLastCostChange(const TargetRegisterClass *RC) const {
    return get(RC).LastCostChange;
  }

  /// Get the register unit limit for the given pressure set index.
  ///
  /// RegisterClassInfo adjusts this limit for reserved registers.
  ///
  /// @param Idx Pressure set index whose adjusted unit limit is requested.
  /// @return Register unit limit for pressure set \p Idx, adjusted for reserved
  /// registers.
  unsigned getRegPressureSetLimit(unsigned Idx) const {
    if (!PSetLimits[Idx])
      PSetLimits[Idx] = computePSetLimit(Idx);
    return PSetLimits[Idx];
  }

protected:
  /// Compute the register pressure set limit for \p Idx, adjusted for reserved
  /// registers.
  ///
  /// @param Idx Pressure set index whose limit is computed.
  /// @return Pressure set limit for \p Idx after adjusting for reserved
  /// registers.
  LLVM_ABI unsigned computePSetLimit(unsigned Idx) const;
};

/// Analysis pass that computes RegisterClassInfo for a machine function.
class MachineRegisterClassAnalysis
    : public AnalysisInfoMixin<MachineRegisterClassAnalysis> {
  friend AnalysisInfoMixin<MachineRegisterClassAnalysis>;

  static AnalysisKey Key;

public:
  /// Provide the result type for this analysis pass.
  using Result = RegisterClassInfo;

  /// Run the analysis over a machine function and produce RegisterClassInfo.
  ///
  /// @param MF Machine function to analyze.
  /// @param MFAM Machine function analysis manager for this pass.
  /// @return The computed RegisterClassInfo for \p MF.
  Result run(MachineFunction &MF, MachineFunctionAnalysisManager &MFAM);
};

/// Legacy machine function pass wrapping RegisterClassInfo.
class MachineRegisterClassInfoWrapperPass : public MachineFunctionPass {
  virtual void anchor();

  RegisterClassInfo RCI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy RegisterClassInfo wrapper pass.
  MachineRegisterClassInfoWrapperPass();

  /// Report analysis usage for this pass.
  ///
  /// @param AU Analysis usage to populate with required and preserved analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Compute RegisterClassInfo for \p MF.
  ///
  /// @param MF Machine function to analyze.
  /// @return False; this analysis pass does not modify the function.
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Return the RegisterClassInfo computed by this pass.
  /// @return The RegisterClassInfo computed by this pass.
  RegisterClassInfo &getRCI() { return RCI; }
  /// Return the RegisterClassInfo computed by this pass.
  /// @return The RegisterClassInfo computed by this pass.
  const RegisterClassInfo &getRCI() const { return RCI; }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_REGISTERCLASSINFO_H
