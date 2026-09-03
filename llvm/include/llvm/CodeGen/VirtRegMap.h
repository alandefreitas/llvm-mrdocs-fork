//===- llvm/CodeGen/VirtRegMap.h - Virtual Register Map ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements a virtual register map. This maps virtual registers to
// physical registers and virtual registers to stack slots. It is created and
// updated by a register allocator and then used by a machine code rewriter that
// adds spill code and rewrites virtual into physical register references.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_VIRTREGMAP_H
#define LLVM_CODEGEN_VIRTREGMAP_H

#include "llvm/ADT/IndexedMap.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TileShapeInfo.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <cassert>

namespace llvm {

class MachineFunction;
class MachineRegisterInfo;
class raw_ostream;
class TargetInstrInfo;

/// Maps virtual registers to physical registers and stack slots.
class VirtRegMap {
  MachineRegisterInfo *MRI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  MachineFunction *MF = nullptr;

  /// Virt2PhysMap - This is a virtual to physical register
  /// mapping. Each virtual register is required to have an entry in
  /// it; even spilled virtual registers (the register mapped to a
  /// spilled register is the temporary used to load it from the
  /// stack).
  IndexedMap<MCRegister, VirtReg2IndexFunctor> Virt2PhysMap;

  /// Virt2StackSlotMap - This is virtual register to stack slot
  /// mapping. Each spilled virtual register has an entry in it
  /// which corresponds to the stack slot this register is spilled
  /// at.
  IndexedMap<int, VirtReg2IndexFunctor> Virt2StackSlotMap;

  /// Virt2SplitMap - This is virtual register to splitted virtual register
  /// mapping.
  IndexedMap<Register, VirtReg2IndexFunctor> Virt2SplitMap;

  /// Virt2ShapeMap - For X86 AMX register whose register is bound shape
  /// information.
  DenseMap<Register, ShapeT> Virt2ShapeMap;

  /// createSpillSlot - Allocate a spill slot for RC from MFI.
  unsigned createSpillSlot(const TargetRegisterClass *RC);

public:
  /// Sentinel value meaning no stack slot is assigned.
  static constexpr int NO_STACK_SLOT = INT_MAX;

  /// Construct an empty virtual register map.
  VirtRegMap() : Virt2StackSlotMap(NO_STACK_SLOT) {}
  /// Copy construction is deleted; VirtRegMap is not copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  VirtRegMap(const VirtRegMap &Other) = delete;
  /// Copy assignment is deleted; VirtRegMap is not copyable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  VirtRegMap &operator=(const VirtRegMap &Other) = delete;
  /// Move-construct a VirtRegMap.
  ///
  /// \param Other Source map to move from.
  VirtRegMap(VirtRegMap &&Other) = default;

  /// Initialize the map for machine function \p MF.
  ///
  /// \param MF Machine function whose registers are tracked.
  LLVM_ABI void init(MachineFunction &MF);

  /// Return the machine function associated with this map.
  ///
  /// \return Machine function whose registers are tracked by this map.
  MachineFunction &getMachineFunction() const {
    assert(MF && "getMachineFunction called before runOnMachineFunction");
    return *MF;
  }

  /// Return the machine register info for the mapped function.
  ///
  /// \return Machine register info for the associated machine function.
  MachineRegisterInfo &getRegInfo() const { return *MRI; }
  /// Return the target register info for the mapped function.
  ///
  /// \return Target register info for the associated machine function.
  const TargetRegisterInfo &getTargetRegInfo() const { return *TRI; }

  /// Grow internal maps to cover newly created virtual registers.
  LLVM_ABI void grow();

  /// Return true if \p virtReg is mapped to a physical register.
  ///
  /// \param virtReg Virtual register to query.
  /// \return True if \p virtReg is mapped to a physical register.
  bool hasPhys(Register virtReg) const { return getPhys(virtReg).isValid(); }

  /// Return the physical register mapped to \p virtReg.
  ///
  /// \param virtReg Virtual register to look up.
  /// \return Physical register mapped to \p virtReg, or an invalid register.
  MCRegister getPhys(Register virtReg) const {
    assert(virtReg.isVirtual());
    return Virt2PhysMap[virtReg];
  }

  /// Map \p virtReg to physical register \p physReg.
  ///
  /// \param virtReg Virtual register to assign.
  /// \param physReg Physical register to assign to.
  LLVM_ABI void assignVirt2Phys(Register virtReg, MCRegister physReg);

  /// Return true if no virtual registers have shape information.
  ///
  /// \return True if the shape map is empty.
  bool isShapeMapEmpty() const { return Virt2ShapeMap.empty(); }

  /// Return true if \p virtReg has associated shape information.
  ///
  /// \param virtReg Virtual register to query.
  /// \return True if \p virtReg has associated shape information.
  bool hasShape(Register virtReg) const {
    return Virt2ShapeMap.contains(virtReg);
  }

  /// Return the shape information for \p virtReg.
  ///
  /// \param virtReg Virtual register to look up.
  /// \return Shape bound to \p virtReg.
  ShapeT getShape(Register virtReg) const {
    assert(virtReg.isVirtual());
    return Virt2ShapeMap.lookup(virtReg);
  }

  /// Assign shape information \p shape to \p virtReg.
  ///
  /// \param virtReg Virtual register to update.
  /// \param shape Shape bound to \p virtReg.
  void assignVirt2Shape(Register virtReg, ShapeT shape) {
    Virt2ShapeMap[virtReg] = shape;
  }

  /// Clear the physical register mapping for \p virtReg.
  ///
  /// \param virtReg Virtual register whose physreg mapping is cleared.
  void clearVirt(Register virtReg) {
    assert(virtReg.isVirtual());
    assert(Virt2PhysMap[virtReg] &&
           "attempt to clear a not assigned virtual register");
    Virt2PhysMap[virtReg] = MCRegister();
  }

  /// clears all virtual to physical register mappings
  void clearAllVirt() {
    Virt2PhysMap.clear();
    grow();
  }

  /// Return true if \p VirtReg is assigned to its preferred physreg.
  ///
  /// \param VirtReg Virtual register to query.
  /// \return True if \p VirtReg is assigned to its preferred physreg.
  LLVM_ABI bool hasPreferredPhys(Register VirtReg) const;

  /// Return true if \p VirtReg has a known preferred register.
  ///
  /// This returns false if VirtReg has a preference that is a virtual
  /// register that hasn't been assigned yet.
  ///
  /// \param VirtReg Virtual register to query.
  /// \return True if \p VirtReg has a known preferred physical register.
  LLVM_ABI bool hasKnownPreference(Register VirtReg) const;

  /// Record that \p virtReg is a split live interval from \p SReg.
  ///
  /// \param virtReg Split virtual register.
  /// \param SReg Original virtual register that \p virtReg was split from.
  void setIsSplitFromReg(Register virtReg, Register SReg) {
    Virt2SplitMap[virtReg] = SReg;
    if (hasShape(SReg)) {
      Virt2ShapeMap[virtReg] = getShape(SReg);
    }
  }

  /// Return the live interval that \p virtReg was split from.
  ///
  /// \param virtReg Split virtual register to look up.
  /// \return Pre-split virtual register, or an invalid register if none.
  Register getPreSplitReg(Register virtReg) const {
    return Virt2SplitMap[virtReg];
  }

  /// Return the original virtual register that \p VirtReg descends from.
  ///
  /// Return the original virtual register that VirtReg descends from through
  /// splitting. A register that was not created by splitting is its own
  /// original. This operation is idempotent.
  ///
  /// \param VirtReg Virtual register whose split ancestor is requested.
  /// \return Original virtual register that \p VirtReg descends from.
  Register getOriginal(Register VirtReg) const {
    Register Orig = getPreSplitReg(VirtReg);
    return Orig ? Orig : VirtReg;
  }

  /// Return true if \p virtReg is assigned a register, not only a spill.
  ///
  /// Returns true if the specified virtual register is not mapped to a stack
  /// slot or rematerialized.
  ///
  /// \param virtReg Virtual register to query.
  /// \return True if \p virtReg is assigned a register, not only a spill.
  bool isAssignedReg(Register virtReg) const {
    if (getStackSlot(virtReg) == NO_STACK_SLOT)
      return true;
    // Split register can be assigned a physical register as well as a
    // stack slot or remat id.
    return (Virt2SplitMap[virtReg] && Virt2PhysMap[virtReg]);
  }

  /// Return the stack slot mapped to \p virtReg.
  ///
  /// \param virtReg Virtual register to look up.
  /// \return Stack slot index for \p virtReg, or \c NO_STACK_SLOT if none.
  int getStackSlot(Register virtReg) const {
    assert(virtReg.isVirtual());
    return Virt2StackSlotMap[virtReg];
  }

  /// Map \p virtReg to the next available stack slot.
  ///
  /// \param virtReg Virtual register to spill.
  /// \return Newly allocated stack slot index.
  LLVM_ABI int assignVirt2StackSlot(Register virtReg);

  /// Map \p virtReg to the specified stack slot \p SS.
  ///
  /// \param virtReg Virtual register to spill.
  /// \param SS Stack slot index to assign.
  LLVM_ABI void assignVirt2StackSlot(Register virtReg, int SS);

  /// Print the virtual register map to \p OS.
  ///
  /// \param OS Output stream for the dump.
  /// \param M Optional module providing additional context.
  LLVM_ABI void print(raw_ostream &OS, const Module *M = nullptr) const;
  /// Dump the virtual register map to the debug stream.
  LLVM_ABI void dump() const;
};

/// Write virtual register map \p VRM to stream \p OS.
///
/// \param OS Output stream.
/// \param VRM Virtual register map to print.
/// \return Reference to \p OS after printing.
inline raw_ostream &operator<<(raw_ostream &OS, const VirtRegMap &VRM) {
  VRM.print(OS);
  return OS;
}

/// Legacy MachineFunctionPass wrapper that owns a \c VirtRegMap.
class VirtRegMapWrapperLegacy : public MachineFunctionPass {
  VirtRegMap VRM;

public:
  /// Pass identification, replacement for typeid.
  LLVM_ABI static char ID;

  /// Construct the legacy VirtRegMap wrapper pass.
  VirtRegMapWrapperLegacy() : MachineFunctionPass(ID) {}

  /// Print the virtual register map owned by this pass.
  ///
  /// \param OS Output stream for the dump.
  /// \param M Optional module providing additional context.
  void print(raw_ostream &OS, const Module *M = nullptr) const override {
    VRM.print(OS, M);
  }

  /// Return the VirtRegMap owned by this pass.
  ///
  /// \return Mutable VirtRegMap owned by this pass.
  VirtRegMap &getVRM() { return VRM; }
  /// Return the VirtRegMap owned by this pass.
  ///
  /// \return Const VirtRegMap owned by this pass.
  const VirtRegMap &getVRM() const { return VRM; }

  /// Initialize the VirtRegMap for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &MF) override {
    VRM.init(MF);
    return false;
  }

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
};

/// Analysis pass that computes \c VirtRegMap for a machine function.
class VirtRegMapAnalysis : public AnalysisInfoMixin<VirtRegMapAnalysis> {
  friend AnalysisInfoMixin<VirtRegMapAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = VirtRegMap;

  /// Compute VirtRegMap for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MAM Analysis manager for the machine function.
  /// \return Virtual register map for \p MF.
  LLVM_ABI VirtRegMap run(MachineFunction &MF,
                          MachineFunctionAnalysisManager &MAM);
};

/// Printer pass for the \c VirtRegMapAnalysis results.
class VirtRegMapPrinterPass
    : public RequiredPassInfoMixin<VirtRegMapPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the VirtRegMap dump.
  explicit VirtRegMapPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print VirtRegMapAnalysis results for \p MF.
  ///
  /// \param MF Machine function whose VirtRegMap is printed.
  /// \param MFAM Analysis manager providing VirtRegMapAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Pass that rewrites virtual registers to physical registers via VirtRegMap.
class VirtRegRewriterPass : public RequiredPassInfoMixin<VirtRegRewriterPass> {
  bool ClearVirtRegs = true;

public:
  /// Construct a rewriter, optionally clearing virtual registers afterward.
  ///
  /// \param ClearVirtRegs If true, clear virtual registers after rewriting.
  VirtRegRewriterPass(bool ClearVirtRegs = true)
      : ClearVirtRegs(ClearVirtRegs) {}
  /// Rewrite virtual registers in \p MF according to VirtRegMap.
  ///
  /// \param MF Machine function to rewrite.
  /// \param MFAM Analysis manager providing VirtRegMap and related analyses.
  /// \return Analyses preserved by this transformation.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);

  /// Print this pass's pipeline representation to \p OS.
  ///
  /// \param OS Output stream for the pipeline description.
  /// \param MapClassName2PassName Maps pass class names to pipeline names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName) const;

  /// Return machine function properties set by this pass.
  ///
  /// \return Properties set by this pass, including \c NoVRegs when clearing.
  MachineFunctionProperties getSetProperties() const {
    if (ClearVirtRegs)
      return MachineFunctionProperties().setNoVRegs();
    return {};
  }
};

} // end llvm namespace

#endif // LLVM_CODEGEN_VIRTREGMAP_H
