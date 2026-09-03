//===-- llvm/CodeGen/LiveVariables.h - Live Variable Analysis ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the LiveVariables analysis pass.  For each machine
// instruction in the function, this pass calculates the set of registers that
// are immediately dead after the instruction (i.e., the instruction calculates
// the value, but it is never used) and the set of registers that are used by
// the instruction, but are never used after the instruction (i.e., they are
// killed).
//
// This class computes live variables using a sparse implementation based on
// the machine code SSA form.  This class computes live variable information for
// each virtual and _register allocatable_ physical register in a function.  It
// uses the dominance properties of SSA form to efficiently compute live
// variables for virtual registers, and assumes that physical registers are only
// live within a single basic block (allowing it to do a single local analysis
// to resolve physical register lifetimes in each basic block).  If a physical
// register is not register allocatable, it is not tracked.  This is useful for
// things like the stack pointer and condition codes.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_LIVEVARIABLES_H
#define LLVM_CODEGEN_LIVEVARIABLES_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/PassRegistry.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MachineBasicBlock;
class MachineRegisterInfo;

/// Sparse live-variable analysis for virtual and allocatable physical registers.
class LiveVariables {
  friend class LiveVariablesWrapperPass;

public:
  /// Live regions of a virtual register across the machine function.
  ///
  /// We represent this with three different pieces of information: the set of
  /// blocks in which the instruction is live throughout, the set of blocks in
  /// which the instruction is actually used, and the set of non-phi
  /// instructions that are the last users of the value.
  ///
  /// In the common case where a value is defined and killed in the same block,
  /// There is one killing instruction, and AliveBlocks is empty.
  ///
  /// Otherwise, the value is live out of the block.  If the value is live
  /// throughout any blocks, these blocks are listed in AliveBlocks.  Blocks
  /// where the liveness range ends are not included in AliveBlocks, instead
  /// being captured by the Kills set.  In these blocks, the value is live into
  /// the block (unless the value is defined and killed in the same block) and
  /// lives until the specified instruction.  Note that there cannot ever be a
  /// value whose Kills set contains two instructions from the same basic block.
  ///
  /// PHI nodes complicate things a bit.  If a PHI node is the last user of a
  /// value in one of its predecessor blocks, it is not listed in the kills set,
  /// but does include the predecessor block in the AliveBlocks set (unless that
  /// block also defines the value).  This leads to the (perfectly sensical)
  /// situation where a value is defined in a block, and the last use is a phi
  /// node in the successor.  In this case, AliveBlocks is empty (the value is
  /// not live across any  blocks) and Kills is empty (phi nodes are not
  /// included). This is sensical because the value must be live to the end of
  /// the block, but is not live in any successor blocks.
  struct VarInfo {
    /// AliveBlocks - Set of blocks in which this value is alive completely
    /// through.  This is a bit set which uses the basic block number as an
    /// index.
    ///
    SparseBitVector<> AliveBlocks;

    /// Kills - List of MachineInstruction's which are the last use of this
    /// virtual register (kill it) in their basic block.
    ///
    std::vector<MachineInstr*> Kills;

    /// Delete the kill entry for the specified machine instruction.
    ///
    /// \param MI Instruction whose kill entry should be removed.
    /// \return True if a kill corresponding to this instruction existed.
    bool removeKill(MachineInstr &MI) {
      std::vector<MachineInstr *>::iterator I = find(Kills, &MI);
      if (I == Kills.end())
        return false;
      Kills.erase(I);
      return true;
    }

    /// Find a kill instruction in \p MBB, or null if none exists.
    ///
    /// \param MBB Basic block to search for a kill of this register.
    /// \return The kill instruction in \p MBB, or null if none exists.
    LLVM_ABI MachineInstr *findKill(const MachineBasicBlock *MBB) const;

    /// Return true if \p Reg is live into \p MBB, ignoring PHI-only uses.
    ///
    /// This means that Reg is live through MBB, or it is killed in MBB. If Reg
    /// is only used by PHI instructions in MBB, it is not considered live in.
    ///
    /// \param MBB Basic block to query for liveness.
    /// \param Reg Virtual register whose live-in status is tested.
    /// \param MRI Machine register info used for the query.
    /// \return True if \p Reg is live into \p MBB, ignoring PHI-only uses.
    LLVM_ABI bool isLiveIn(const MachineBasicBlock &MBB, Register Reg,
                           MachineRegisterInfo &MRI);

    /// Print this variable's live info to \p OS.
    ///
    /// \param OS Output stream for the dump.
    LLVM_ABI void print(raw_ostream &OS) const;

    /// Dump this variable's live info to the debug stream.
    LLVM_ABI void dump() const;
  };

private:
  /// VirtRegInfo - This list is a mapping from virtual register number to
  /// variable information.
  ///
  IndexedMap<VarInfo, VirtReg2IndexFunctor> VirtRegInfo;

private:   // Intermediate data structures
  MachineFunction *MF = nullptr;

  MachineRegisterInfo *MRI = nullptr;

  const TargetRegisterInfo *TRI = nullptr;

  // PhysRegInfo - Keep track of which instruction was the last def of a
  // physical register. This is a purely local property, because all physical
  // register references are presumed dead across basic blocks.
  std::vector<MachineInstr *> PhysRegDef;

  // PhysRegInfo - Keep track of which instruction was the last use of a
  // physical register. This is a purely local property, because all physical
  // register references are presumed dead across basic blocks.
  std::vector<MachineInstr *> PhysRegUse;

  std::vector<SmallVector<Register, 4>> PHIVarInfo;

  // DistanceMap - Keep track the distance of a MI from the start of the
  // current basic block.
  DenseMap<MachineInstr*, unsigned> DistanceMap;

  // For legacy pass.
  LiveVariables() = default;

  LLVM_ABI void analyze(MachineFunction &MF);

  /// HandlePhysRegKill - Add kills of Reg and its sub-registers to the
  /// uses. Pay special attention to the sub-register uses which may come below
  /// the last use of the whole register.
  bool HandlePhysRegKill(Register Reg, MachineInstr *MI);

  /// HandleRegMask - Call HandlePhysRegKill for all registers clobbered by Mask.
  void HandleRegMask(const MachineOperand &, unsigned);

  void HandlePhysRegUse(Register Reg, MachineInstr &MI);
  void HandlePhysRegDef(Register Reg, MachineInstr *MI,
                        SmallVectorImpl<Register> &Defs);
  void UpdatePhysRegDefs(MachineInstr &MI, SmallVectorImpl<Register> &Defs);

  /// FindLastRefOrPartRef - Return the last reference or partial reference of
  /// the specified register.
  MachineInstr *FindLastRefOrPartRef(Register Reg);

  /// FindLastPartialDef - Return the last partial def of the specified
  /// register.
  MachineInstr *FindLastPartialDef(Register Reg);

  /// analyzePHINodes - Gather information about the PHI nodes in here. In
  /// particular, we want to map the variable information of a virtual
  /// register which is used in a PHI node. We map that to the BB the vreg
  /// is coming from.
  void analyzePHINodes(const MachineFunction& Fn);

  void runOnInstr(MachineInstr &MI, SmallVectorImpl<Register> &Defs,
                  unsigned NumRegs);

  void runOnBlock(MachineBasicBlock *MBB, unsigned NumRegs);

public:
  /// Analyze live variables for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  LLVM_ABI LiveVariables(MachineFunction &MF);

  /// Print live variable information to \p OS.
  ///
  /// \param OS Output stream for the dump.
  LLVM_ABI void print(raw_ostream &OS) const;

  //===--------------------------------------------------------------------===//
  //  API to update live variable information

  /// Recompute liveness from scratch for a single-def virtual register.
  ///
  /// Recompute liveness from scratch for a virtual register \p Reg that is
  /// known to have a single def that dominates all uses. This can be useful
  /// after removing some uses of \p Reg. It is not necessary for the whole
  /// machine function to be in SSA form.
  ///
  /// \param Reg Virtual register whose liveness should be recomputed.
  LLVM_ABI void recomputeForSingleDefVirtReg(Register Reg);

  /// Update register kill info by replacing a kill instruction with a new one.
  ///
  /// \param Reg Virtual register whose kill is being updated.
  /// \param OldMI Existing kill instruction to replace.
  /// \param NewMI Instruction that becomes the new kill.
  LLVM_ABI void replaceKillInstruction(Register Reg, MachineInstr &OldMI,
                                       MachineInstr &NewMI);

  /// Record that \p IncomingReg is killed by \p MI.
  ///
  /// If AddIfNotFound is true, add an implicit operand if it's not found.
  ///
  /// \param IncomingReg Virtual register killed by \p MI.
  /// \param MI Instruction that kills \p IncomingReg.
  /// \param AddIfNotFound If true, add an implicit kill operand when missing.
  void addVirtualRegisterKilled(Register IncomingReg, MachineInstr &MI,
                                bool AddIfNotFound = false) {
    if (MI.addRegisterKilled(IncomingReg, TRI, AddIfNotFound))
      getVarInfo(IncomingReg).Kills.push_back(&MI);
  }

  /// Remove the specified kill of a virtual register from live info.
  ///
  /// Returns true if the variable was marked as killed by the specified
  /// instruction, false otherwise.
  ///
  /// \param Reg Virtual register whose kill should be cleared.
  /// \param MI Instruction that previously killed \p Reg.
  /// \return True if \p Reg was marked killed by \p MI.
  bool removeVirtualRegisterKilled(Register Reg, MachineInstr &MI) {
    if (!getVarInfo(Reg).removeKill(MI))
      return false;

    bool Removed = false;
    for (MachineOperand &MO : MI.operands()) {
      if (MO.isReg() && MO.isKill() && MO.getReg() == Reg) {
        MO.setIsKill(false);
        Removed = true;
        break;
      }
    }

    assert(Removed && "Register is not used by this instruction!");
    (void)Removed;
    return true;
  }

  /// Remove all killed info for the specified instruction.
  ///
  /// \param MI Instruction whose kill flags and VarInfo entries are cleared.
  LLVM_ABI void removeVirtualRegistersKilled(MachineInstr &MI);

  /// Record that \p IncomingReg is dead after being defined by \p MI.
  ///
  /// If AddIfNotFound is true, add an implicit operand if it's not found.
  ///
  /// \param IncomingReg Virtual register that becomes dead at \p MI.
  /// \param MI Instruction that defines and kills \p IncomingReg.
  /// \param AddIfNotFound If true, add an implicit dead operand when missing.
  void addVirtualRegisterDead(Register IncomingReg, MachineInstr &MI,
                              bool AddIfNotFound = false) {
    if (MI.addRegisterDead(IncomingReg, TRI, AddIfNotFound))
      getVarInfo(IncomingReg).Kills.push_back(&MI);
  }

  /// Remove the specified dead mark of a virtual register from live info.
  ///
  /// Returns true if the variable was marked dead at the specified instruction,
  /// false otherwise.
  ///
  /// \param Reg Virtual register whose dead mark should be cleared.
  /// \param MI Instruction that previously marked \p Reg dead.
  /// \return True if \p Reg was marked dead at \p MI.
  bool removeVirtualRegisterDead(Register Reg, MachineInstr &MI) {
    if (!getVarInfo(Reg).removeKill(MI))
      return false;

    bool Removed = false;
    for (MachineOperand &MO : MI.all_defs()) {
      if (MO.getReg() == Reg) {
        MO.setIsDead(false);
        Removed = true;
        break;
      }
    }
    assert(Removed && "Register is not defined by this instruction!");
    (void)Removed;
    return true;
  }

  /// Return the VarInfo structure for the specified VIRTUAL register.
  ///
  /// \param Reg Virtual register whose live info is requested.
  /// \return Live variable info for \p Reg.
  LLVM_ABI VarInfo &getVarInfo(Register Reg);

  /// Mark \p VRInfo as alive through \p BB up to \p DefBlock.
  ///
  /// \param VRInfo Variable info being updated.
  /// \param DefBlock Block that defines the virtual register.
  /// \param BB Block in which the register is marked alive.
  LLVM_ABI void MarkVirtRegAliveInBlock(VarInfo &VRInfo,
                                        MachineBasicBlock *DefBlock,
                                        MachineBasicBlock *BB);
  /// Mark \p VRInfo as alive through \p BB, appending work to \p WorkList.
  ///
  /// \param VRInfo Variable info being updated.
  /// \param DefBlock Block that defines the virtual register.
  /// \param BB Block in which the register is marked alive.
  /// \param WorkList Blocks still to process while propagating liveness.
  LLVM_ABI void
  MarkVirtRegAliveInBlock(VarInfo &VRInfo, MachineBasicBlock *DefBlock,
                          MachineBasicBlock *BB,
                          SmallVectorImpl<MachineBasicBlock *> &WorkList);

  /// Update live info for a virtual register defined by \p MI.
  ///
  /// \param reg Virtual register being defined.
  /// \param MI Defining instruction.
  LLVM_ABI void HandleVirtRegDef(Register reg, MachineInstr &MI);
  /// Update live info for a virtual register used by \p MI in \p MBB.
  ///
  /// \param reg Virtual register being used.
  /// \param MBB Basic block containing \p MI.
  /// \param MI Using instruction.
  LLVM_ABI void HandleVirtRegUse(Register reg, MachineBasicBlock *MBB,
                                 MachineInstr &MI);

  /// Return true if \p Reg is live into \p MBB.
  ///
  /// \param Reg Virtual register to query.
  /// \param MBB Basic block to test for a live-in value.
  /// \return True if \p Reg is live into \p MBB.
  bool isLiveIn(Register Reg, const MachineBasicBlock &MBB) {
    return getVarInfo(Reg).isLiveIn(MBB, Reg, *MRI);
  }

  /// Return true if \p Reg is live out of \p MBB, ignoring PHI nodes.
  ///
  /// This means that Reg is either killed by a successor block or passed
  /// through one.
  ///
  /// \param Reg Virtual register to query.
  /// \param MBB Basic block whose live-out set is tested.
  /// \return True if \p Reg is live out of \p MBB, ignoring PHI nodes.
  LLVM_ABI bool isLiveOut(Register Reg, const MachineBasicBlock &MBB);

  /// Insert \p BB between \p DomBB and \p SuccBB and update live-through info.
  ///
  /// All variables that are live out of DomBB and live into SuccBB will be
  /// marked as passing live through BB. This method assumes that the machine
  /// code is still in SSA form.
  ///
  /// \param BB Newly inserted basic block.
  /// \param DomBB Dominating predecessor of \p BB.
  /// \param SuccBB Successor of \p BB that was previously reached from \p DomBB.
  LLVM_ABI void addNewBlock(MachineBasicBlock *BB, MachineBasicBlock *DomBB,
                            MachineBasicBlock *SuccBB);

  /// Insert \p BB between \p DomBB and \p SuccBB using precomputed live-ins.
  ///
  /// \param BB Newly inserted basic block.
  /// \param DomBB Dominating predecessor of \p BB.
  /// \param SuccBB Successor of \p BB that was previously reached from \p DomBB.
  /// \param LiveInSets Per-block live-in sets used to update VarInfo.
  LLVM_ABI void addNewBlock(MachineBasicBlock *BB, MachineBasicBlock *DomBB,
                            MachineBasicBlock *SuccBB,
                            std::vector<SparseBitVector<>> &LiveInSets);
};

/// Analysis pass that computes \c LiveVariables for a machine function.
class LiveVariablesAnalysis : public AnalysisInfoMixin<LiveVariablesAnalysis> {
  friend AnalysisInfoMixin<LiveVariablesAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = LiveVariables;
  /// Compute LiveVariables for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Live variable info for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c LiveVariablesAnalysis results.
class LiveVariablesPrinterPass
    : public RequiredPassInfoMixin<LiveVariablesPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the live variables dump.
  explicit LiveVariablesPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print LiveVariablesAnalysis results for \p MF.
  ///
  /// \param MF Machine function whose live variables are printed.
  /// \param MFAM Analysis manager providing LiveVariablesAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Legacy pass wrapper for LiveVariables.
class LLVM_ABI LiveVariablesWrapperPass : public MachineFunctionPass {
  LiveVariables LV;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy LiveVariables wrapper pass.
  LiveVariablesWrapperPass() : MachineFunctionPass(ID) {}

  /// Run LiveVariables on machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &MF) override {
    LV.analyze(MF);
    return false;
  }

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Release memory used by the wrapped analysis.
  void releaseMemory() override { LV.VirtRegInfo.clear(); }

  /// Return the computed LiveVariables analysis.
  ///
  /// \return The wrapped LiveVariables result.
  LiveVariables &getLV() { return LV; }
};

} // End llvm namespace

#endif
