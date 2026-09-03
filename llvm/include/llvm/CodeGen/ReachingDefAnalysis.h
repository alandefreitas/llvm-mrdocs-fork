//==--- llvm/CodeGen/ReachingDefAnalysis.h - Reaching Def Analysis -*- C++ -*---==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file Reaching Defs Analysis pass.
///
/// This pass tracks for each instruction what is the "closest" reaching def of
/// a given register. It is used by BreakFalseDeps (for clearance calculation)
/// and ExecutionDomainFix (for arbitrating conflicting domains).
///
/// Note that this is different from the usual definition notion of liveness.
/// The CPU doesn't care whether or not we consider a register killed.
///
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REACHINGDEFANALYSIS_H
#define LLVM_CODEGEN_REACHINGDEFANALYSIS_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/TinyPtrVector.h"
#include "llvm/CodeGen/LoopTraversal.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/InitializePasses.h"

namespace llvm {

class MachineBasicBlock;
class MachineInstr;

/// Thin wrapper around an int encoding a reaching definition.
///
/// Uses an encoding that makes it compatible with TinyPtrVector. The 0th LSB
/// is forced zero (and will be used for pointer union tagging). The 1st LSB
/// is forced one (to make sure the value is non-zero).
class ReachingDef {
  uintptr_t Encoded;
  friend struct PointerLikeTypeTraits<ReachingDef>;
  explicit ReachingDef(uintptr_t Encoded) : Encoded(Encoded) {}

public:
  /// Construct a null reaching definition.
  ///
  /// \param Unused Null pointer tag selecting the null constructor.
  ReachingDef(std::nullptr_t Unused) : Encoded(0) { (void)Unused; }
  /// Construct a reaching definition from instruction id \p Instr.
  ///
  /// \param Instr Instruction id to encode.
  ReachingDef(int Instr) : Encoded(((uintptr_t) Instr << 2) | 2) {}
  /// Convert this reaching definition to its instruction id.
  ///
  /// \return Instruction id encoded by this reaching definition.
  operator int() const { return ((int) Encoded) >> 2; }
};

/// PointerLikeTypeTraits specialization treating ReachingDef as a pointer.
template<>
struct PointerLikeTypeTraits<ReachingDef> {
  /// Number of spare low bits available for pointer-union tagging.
  static constexpr int NumLowBitsAvailable = 1;

  /// Return the encoded bit pattern of \p RD as a void pointer.
  ///
  /// \param RD Reaching definition to convert.
  /// \return Opaque void pointer encoding of \p RD.
  static inline void *getAsVoidPointer(const ReachingDef &RD) {
    return reinterpret_cast<void *>(RD.Encoded);
  }

  /// Reconstruct a ReachingDef from opaque void pointer \p P.
  ///
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return ReachingDef reconstructed from \p P.
  static inline ReachingDef getFromVoidPointer(void *P) {
    return ReachingDef(reinterpret_cast<uintptr_t>(P));
  }

  /// Reconstruct a ReachingDef from opaque const void pointer \p P.
  ///
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return ReachingDef reconstructed from \p P.
  static inline ReachingDef getFromVoidPointer(const void *P) {
    return ReachingDef(reinterpret_cast<uintptr_t>(P));
  }
};

/// Storage for all reaching definitions across basic blocks and reg units.
class MBBReachingDefsInfo {
public:
  /// Resize storage to hold definitions for \p NumBlockIDs basic blocks.
  ///
  /// \param NumBlockIDs Number of basic-block IDs to allocate for.
  void init(unsigned NumBlockIDs) { AllReachingDefs.resize(NumBlockIDs); }

  /// Return the number of basic-block slots currently allocated.
  ///
  /// \return Number of basic-block IDs currently allocated in storage.
  unsigned numBlockIDs() const { return AllReachingDefs.size(); }

  /// Prepare storage for \p NumRegUnits register units in block \p MBBNumber.
  ///
  /// \param MBBNumber Basic-block number whose definition lists are prepared.
  /// \param NumRegUnits Number of register units tracked in the block.
  void startBasicBlock(unsigned MBBNumber, unsigned NumRegUnits) {
    AllReachingDefs[MBBNumber].resize(NumRegUnits);
  }

  /// Append definition id \p Def for register unit \p Unit in block \p MBBNumber.
  ///
  /// \param MBBNumber Basic-block number that owns the definition list.
  /// \param Unit Register unit whose reaching definitions are updated.
  /// \param Def Instruction id of the definition to append.
  void append(unsigned MBBNumber, MCRegUnit Unit, int Def) {
    AllReachingDefs[MBBNumber][static_cast<unsigned>(Unit)].push_back(Def);
  }

  /// Prepend definition id \p Def for register unit \p Unit in block \p MBBNumber.
  ///
  /// \param MBBNumber Basic-block number that owns the definition list.
  /// \param Unit Register unit whose reaching definitions are updated.
  /// \param Def Instruction id of the definition to prepend.
  void prepend(unsigned MBBNumber, MCRegUnit Unit, int Def) {
    auto &Defs = AllReachingDefs[MBBNumber][static_cast<unsigned>(Unit)];
    Defs.insert(Defs.begin(), Def);
  }

  /// Replace the front definition for register unit \p Unit in block \p MBBNumber.
  ///
  /// \param MBBNumber Basic-block number that owns the definition list.
  /// \param Unit Register unit whose front definition is replaced.
  /// \param Def Instruction id that becomes the new front definition.
  void replaceFront(unsigned MBBNumber, MCRegUnit Unit, int Def) {
    assert(!AllReachingDefs[MBBNumber][static_cast<unsigned>(Unit)].empty());
    *AllReachingDefs[MBBNumber][static_cast<unsigned>(Unit)].begin() = Def;
  }

  /// Clear all stored reaching definitions.
  void clear() { AllReachingDefs.clear(); }

  /// Return the reaching definitions of \p Unit in block \p MBBNumber.
  ///
  /// \param MBBNumber Basic-block number to query.
  /// \param Unit Register unit whose reaching definitions are requested.
  /// \return Reaching definition ids for \p Unit in \p MBBNumber, or empty if
  ///         the block has no stored definitions.
  ArrayRef<ReachingDef> defs(unsigned MBBNumber, MCRegUnit Unit) const {
    if (AllReachingDefs[MBBNumber].empty())
      // Block IDs are not necessarily dense.
      return ArrayRef<ReachingDef>();
    return AllReachingDefs[MBBNumber][static_cast<unsigned>(Unit)];
  }

private:
  /// All reaching defs of a given RegUnit for a given MBB.
  using MBBRegUnitDefs = TinyPtrVector<ReachingDef>;
  /// All reaching defs of all reg units for a given MBB
  using MBBDefsInfo = std::vector<MBBRegUnitDefs>;

  /// All reaching defs of all reg units for all MBBs
  SmallVector<MBBDefsInfo, 4> AllReachingDefs;
};

/// This class provides the reaching def analysis.
class ReachingDefInfo {
private:
  MachineFunction *MF = nullptr;
  const TargetRegisterInfo *TRI = nullptr;
  const TargetInstrInfo *TII = nullptr;
  LoopTraversal::TraversalOrder TraversedMBBOrder;
  unsigned NumRegUnits = 0;
  unsigned NumStackObjects = 0;
  int ObjectIndexBegin = 0;
  /// Instruction that defined each register, relative to the beginning of the
  /// current basic block.  When a LiveRegsDefInfo is used to represent a
  /// live-out register, this value is relative to the end of the basic block,
  /// so it will be a negative number.
  using LiveRegsDefInfo = std::vector<int>;
  LiveRegsDefInfo LiveRegs;

  /// Keeps clearance information for all registers. Note that this
  /// is different from the usual definition notion of liveness. The CPU
  /// doesn't care whether or not we consider a register killed.
  using OutRegsInfoMap = SmallVector<LiveRegsDefInfo, 4>;
  OutRegsInfoMap MBBOutRegsInfos;

  /// Current instruction number.
  /// The first instruction in each basic block is 0.
  int CurInstr = -1;

  /// Maps instructions to their instruction Ids, relative to the beginning of
  /// their basic blocks.
  DenseMap<MachineInstr *, int> InstIds;

  MBBReachingDefsInfo MBBReachingDefs;

  /// MBBFrameObjsReachingDefs[{i, j}] is a list of instruction indices
  /// (relative to begining of MBB i) that define frame index j in MBB i. This
  /// is used in answering reaching definition queries.
  using MBBFrameObjsReachingDefsInfo =
      DenseMap<std::pair<unsigned, int>, SmallVector<int>>;
  MBBFrameObjsReachingDefsInfo MBBFrameObjsReachingDefs;

  /// Default values are 'nothing happened a long time ago'.
  static constexpr int ReachingDefDefaultVal = -(1 << 21);
  /// Special values for function live-ins.
  static constexpr int FunctionLiveInMarker = -1;

  using InstSet = SmallPtrSetImpl<MachineInstr*>;
  using BlockSet = SmallPtrSetImpl<MachineBasicBlock*>;

public:
  /// Construct an empty reaching-def analysis result.
  LLVM_ABI ReachingDefInfo();
  /// Move-construct a reaching-def analysis result.
  ///
  /// \param Other Result to move from.
  LLVM_ABI ReachingDefInfo(ReachingDefInfo &&Other);
  /// Destroy the reaching-def analysis result.
  LLVM_ABI ~ReachingDefInfo();
  /// Handle invalidation explicitly.
  ///
  /// \param F Machine function whose analysis result may be invalidated.
  /// \param PA Set of analyses preserved by the transform.
  /// \param Inv Invalidator for resolving analysis dependencies.
  /// \return True if this result should be discarded.
  LLVM_ABI bool invalidate(MachineFunction &F, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Compute reaching definitions for machine function \p mf.
  ///
  /// \param mf Machine function to analyze.
  LLVM_ABI void run(MachineFunction &mf);
  /// Print reaching-definition information to \p OS.
  ///
  /// \param OS Output stream for the dump.
  LLVM_ABI void print(raw_ostream &OS);
  /// Release memory used by the analysis result.
  LLVM_ABI void releaseMemory();

  /// Re-run the analysis.
  LLVM_ABI void reset();

  /// Initialize data structures.
  LLVM_ABI void init();

  /// Traverse the machine function, mapping definitions.
  LLVM_ABI void traverse();

  /// Return the instruction id of the closest reaching def of \p Reg for \p MI.
  ///
  /// The id is relative to the beginning of MI's basic block. Note that Reg may
  /// represent a stack slot.
  ///
  /// \param MI Instruction whose reaching definition is queried.
  /// \param Reg Register or stack slot whose reaching definition is sought.
  /// \return Instruction id of the closest reaching def, relative to the start
  ///         of \p MI's basic block.
  LLVM_ABI int getReachingDef(MachineInstr *MI, Register Reg) const;

  /// Return whether A and B use the same def of Reg.
  ///
  /// \param A First instruction whose reaching definition is compared.
  /// \param B Second instruction whose reaching definition is compared.
  /// \param Reg Register whose reaching definitions are compared.
  /// \return True if \p A and \p B share the same reaching def of \p Reg.
  LLVM_ABI bool hasSameReachingDef(MachineInstr *A, MachineInstr *B,
                                   Register Reg) const;

  /// Return whether the reaching def for MI also is live out of its parent
  /// block.
  ///
  /// \param MI Instruction whose reaching definition is tested.
  /// \param Reg Register whose live-out status is queried.
  /// \return True if the reaching def of \p Reg for \p MI is live out.
  LLVM_ABI bool isReachingDefLiveOut(MachineInstr *MI, Register Reg) const;

  /// Return the local MI that produces the live out value for Reg, or
  /// nullptr for a non-live out or non-local def.
  ///
  /// \param MBB Basic block whose live-out definition is sought.
  /// \param Reg Register whose live-out defining instruction is requested.
  /// \return Local defining instruction for the live-out value, or nullptr.
  LLVM_ABI MachineInstr *getLocalLiveOutMIDef(MachineBasicBlock *MBB,
                                              Register Reg) const;

  /// If a single MachineInstr creates the reaching definition, then return it.
  /// Otherwise return null.
  ///
  /// \param MI Instruction that uses the reaching definition.
  /// \param Reg Register whose unique reaching definition is sought.
  /// \return Unique reaching defining instruction, or null if none or ambiguous.
  LLVM_ABI MachineInstr *getUniqueReachingMIDef(MachineInstr *MI,
                                                Register Reg) const;

  /// If a single MachineInstr creates the reaching definition, for MIs operand
  /// at Idx, then return it. Otherwise return null.
  ///
  /// \param MI Instruction whose operand reaching definition is queried.
  /// \param Idx Operand index within \p MI.
  /// \return Unique reaching defining instruction, or null if none or ambiguous.
  LLVM_ABI MachineInstr *getMIOperand(MachineInstr *MI, unsigned Idx) const;

  /// If a single MachineInstr creates the reaching definition, for MIs MO,
  /// then return it. Otherwise return null.
  ///
  /// \param MI Instruction that owns \p MO.
  /// \param MO Operand whose reaching definition is queried.
  /// \return Unique reaching defining instruction, or null if none or ambiguous.
  LLVM_ABI MachineInstr *getMIOperand(MachineInstr *MI,
                                      MachineOperand &MO) const;

  /// Provide whether the register has been defined in the same basic block as,
  /// and before, MI.
  ///
  /// \param MI Instruction used as the search limit within its basic block.
  /// \param Reg Register whose prior local definition is tested.
  /// \return True if \p Reg has a local definition before \p MI.
  LLVM_ABI bool hasLocalDefBefore(MachineInstr *MI, Register Reg) const;

  /// Return whether the given register is used after MI, whether it's a local
  /// use or a live out.
  ///
  /// \param MI Instruction after which uses are searched.
  /// \param Reg Register whose later uses are tested.
  /// \return True if \p Reg is used after \p MI or live out of its block.
  LLVM_ABI bool isRegUsedAfter(MachineInstr *MI, Register Reg) const;

  /// Return whether the given register is defined after MI.
  ///
  /// \param MI Instruction after which definitions are searched.
  /// \param Reg Register whose later definitions are tested.
  /// \return True if \p Reg is defined after \p MI.
  LLVM_ABI bool isRegDefinedAfter(MachineInstr *MI, Register Reg) const;

  /// Provides the clearance - the number of instructions since the closest
  /// reaching def instuction of Reg that reaches MI.
  ///
  /// \param MI Instruction for which clearance is computed.
  /// \param Reg Register whose clearance from its reaching def is requested.
  /// \return Number of instructions between the closest reaching def and \p MI.
  LLVM_ABI int getClearance(MachineInstr *MI, Register Reg) const;

  /// Provides the uses, in the same block as MI, of register that MI defines.
  /// This does not consider live-outs.
  ///
  /// \param MI Defining instruction whose local uses are collected.
  /// \param Reg Register defined by \p MI.
  /// \param Uses Set filled with local users of \p Reg.
  LLVM_ABI void getReachingLocalUses(MachineInstr *MI, Register Reg,
                                     InstSet &Uses) const;

  /// Search MBB for a definition of Reg and insert it into Defs. If no
  /// definition is found, recursively search the predecessor blocks for them.
  ///
  /// \param MBB Basic block whose live-out definitions are collected.
  /// \param Reg Register whose live-out definitions are sought.
  /// \param Defs Set filled with instructions that define the live-out value.
  /// \param VisitedBBs Set of blocks already visited during the search.
  LLVM_ABI void getLiveOuts(MachineBasicBlock *MBB, Register Reg, InstSet &Defs,
                            BlockSet &VisitedBBs) const;
  /// Collect live-out definitions of \p Reg that reach the end of \p MBB.
  ///
  /// \param MBB Basic block whose live-out definitions are collected.
  /// \param Reg Register whose live-out definitions are sought.
  /// \param Defs Set filled with instructions that define the live-out value.
  LLVM_ABI void getLiveOuts(MachineBasicBlock *MBB, Register Reg,
                            InstSet &Defs) const;

  /// For the given block, collect the instructions that use the live-in
  /// value of the provided register. Return whether the value is still
  /// live on exit.
  ///
  /// \param MBB Basic block whose live-in uses are collected.
  /// \param Reg Register whose live-in value is tracked.
  /// \param Uses Set filled with instructions that use the live-in value.
  /// \return True if the live-in value of \p Reg is still live on exit.
  LLVM_ABI bool getLiveInUses(MachineBasicBlock *MBB, Register Reg,
                              InstSet &Uses) const;

  /// Collect the users of the value stored in Reg, which is defined
  /// by MI.
  ///
  /// \param MI Instruction that defines \p Reg.
  /// \param Reg Register whose users are collected.
  /// \param Uses Set filled with instructions that use the defined value.
  LLVM_ABI void getGlobalUses(MachineInstr *MI, Register Reg,
                              InstSet &Uses) const;

  /// Collect all possible definitions of the value stored in Reg, which is
  /// used by MI.
  ///
  /// \param MI Instruction that uses \p Reg.
  /// \param Reg Register whose reaching definitions are collected.
  /// \param Defs Set filled with possible defining instructions.
  LLVM_ABI void getGlobalReachingDefs(MachineInstr *MI, Register Reg,
                                      InstSet &Defs) const;

  /// Return whether From can be moved forwards to just before To.
  ///
  /// \param From Instruction that would be moved forward.
  /// \param To Destination instruction; \p From would be placed just before it.
  /// \return True if \p From can safely be moved just before \p To.
  LLVM_ABI bool isSafeToMoveForwards(MachineInstr *From,
                                     MachineInstr *To) const;

  /// Return whether From can be moved backwards to just after To.
  ///
  /// \param From Instruction that would be moved backward.
  /// \param To Destination instruction; \p From would be placed just after it.
  /// \return True if \p From can safely be moved just after \p To.
  LLVM_ABI bool isSafeToMoveBackwards(MachineInstr *From,
                                      MachineInstr *To) const;

  /// Assuming MI is dead, recursively search the incoming operands which are
  /// killed by MI and collect those that would become dead.
  ///
  /// \param MI Dead instruction whose killed operands are examined.
  /// \param Dead Set filled with operands that would become dead.
  LLVM_ABI void collectKilledOperands(MachineInstr *MI, InstSet &Dead) const;

  /// Return whether removing this instruction will have no effect on the
  /// program, returning the redundant use-def chain.
  ///
  /// \param MI Instruction to consider for removal.
  /// \param ToRemove Set filled with instructions that would become dead.
  /// \return True if removing \p MI does not affect the program.
  LLVM_ABI bool isSafeToRemove(MachineInstr *MI, InstSet &ToRemove) const;

  /// Return whether removing \p MI is a no-op, ignoring effects on \p Ignore.
  ///
  /// Returns the redundant use-def chain in \p ToRemove when removal is safe.
  ///
  /// \param MI Instruction to consider for removal.
  /// \param ToRemove Set filled with instructions that would become dead.
  /// \param Ignore Instructions whose effects should be ignored.
  /// \return True if removing \p MI is safe ignoring \p Ignore.
  LLVM_ABI bool isSafeToRemove(MachineInstr *MI, InstSet &ToRemove,
                               InstSet &Ignore) const;

  /// Return whether a MachineInstr could be inserted at MI and safely define
  /// the given register without affecting the program.
  ///
  /// \param MI Insertion point for the hypothetical defining instruction.
  /// \param Reg Register that would be defined.
  /// \return True if defining \p Reg at \p MI would not affect the program.
  LLVM_ABI bool isSafeToDefRegAt(MachineInstr *MI, Register Reg) const;

  /// Return whether a def of \p Reg could be inserted at \p MI safely.
  ///
  /// Ignores any effects on the instructions in \p Ignore.
  ///
  /// \param MI Insertion point for the hypothetical defining instruction.
  /// \param Reg Register that would be defined.
  /// \param Ignore Instructions whose effects should be ignored.
  /// \return True if defining \p Reg at \p MI is safe ignoring \p Ignore.
  LLVM_ABI bool isSafeToDefRegAt(MachineInstr *MI, Register Reg,
                                 InstSet &Ignore) const;

private:
  /// Set up LiveRegs by merging predecessor live-out values.
  void enterBasicBlock(MachineBasicBlock *MBB);

  /// Update live-out values.
  void leaveBasicBlock(MachineBasicBlock *MBB);

  /// Process he given basic block.
  void processBasicBlock(const LoopTraversal::TraversedMBBInfo &TraversedMBB);

  /// Process block that is part of a loop again.
  void reprocessBasicBlock(MachineBasicBlock *MBB);

  /// Update def-ages for registers defined by MI.
  /// Also break dependencies on partial defs and undef uses.
  void processDefs(MachineInstr *);

  /// Utility function for isSafeToMoveForwards/Backwards.
  template<typename Iterator>
  bool isSafeToMove(MachineInstr *From, MachineInstr *To) const;

  /// Return whether removing this instruction will have no effect on the
  /// program, ignoring the possible effects on some instructions, returning
  /// the redundant use-def chain.
  bool isSafeToRemove(MachineInstr *MI, InstSet &Visited,
                      InstSet &ToRemove, InstSet &Ignore) const;

  /// Provides the MI, from the given block, corresponding to the Id or a
  /// nullptr if the id does not refer to the block.
  MachineInstr *getInstFromId(MachineBasicBlock *MBB, int InstId) const;

  /// Provides the instruction of the closest reaching def instruction of
  /// Reg that reaches MI, relative to the begining of MI's basic block.
  /// Note that Reg may represent a stack slot.
  MachineInstr *getReachingLocalMIDef(MachineInstr *MI, Register Reg) const;
};

/// Analysis pass that computes \c ReachingDefInfo for a machine function.
class ReachingDefAnalysis : public AnalysisInfoMixin<ReachingDefAnalysis> {
  friend AnalysisInfoMixin<ReachingDefAnalysis>;
  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = ReachingDefInfo;

  /// Compute reaching definitions for machine function \p MF.
  ///
  /// \param MF Machine function to analyze.
  /// \param MFAM Analysis manager for the machine function.
  /// \return Reaching-def info for \p MF.
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// Printer pass for the \c ReachingDefInfo results.
class ReachingDefPrinterPass
    : public RequiredPassInfoMixin<ReachingDefPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  ///
  /// \param OS Output stream for the reaching-def dump.
  explicit ReachingDefPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print ReachingDefAnalysis results for \p MF.
  ///
  /// \param MF Machine function whose reaching defs are printed.
  /// \param MFAM Analysis manager providing ReachingDefAnalysis.
  /// \return All analyses preserved; this pass does not transform \p MF.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// Legacy pass wrapper for ReachingDefInfo.
class LLVM_ABI ReachingDefInfoWrapperPass : public MachineFunctionPass {
  ReachingDefInfo RDI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy ReachingDefInfo wrapper pass.
  ReachingDefInfoWrapperPass();

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Return the properties this pass requires of the machine function.
  ///
  /// \return Machine function properties required by this pass.
  MachineFunctionProperties getRequiredProperties() const override;
  /// Compute reaching definitions for machine function \p F.
  ///
  /// \param F Machine function to analyze.
  /// \return False; this analysis does not modify the machine function.
  bool runOnMachineFunction(MachineFunction &F) override;
  /// Release memory used by the wrapped analysis.
  void releaseMemory() override { RDI.releaseMemory(); }

  /// Return the computed ReachingDefInfo analysis.
  ///
  /// \return Mutable reference to the wrapped ReachingDefInfo.
  ReachingDefInfo &getRDI() { return RDI; }
  /// Return the computed ReachingDefInfo analysis.
  ///
  /// \return Const reference to the wrapped ReachingDefInfo.
  const ReachingDefInfo &getRDI() const { return RDI; }
};

} // namespace llvm

#endif // LLVM_CODEGEN_REACHINGDEFANALYSIS_H
