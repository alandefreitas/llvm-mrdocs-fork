//===- llvm/CodeGen/DFAPacketizer.h - DFA Packetizer for VLIW ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This class implements a deterministic finite automaton (DFA) based
// packetizing mechanism for VLIW architectures. It provides APIs to
// determine whether there exists a legal mapping of instructions to
// functional unit assignments in a packet. The DFA is auto-generated from
// the target's Schedule.td file.
//
// A DFA consists of 3 major elements: states, inputs, and transitions. For
// the packetizing mechanism, the input is the set of instruction classes for
// a target. The state models all possible combinations of functional unit
// consumption for a given set of instructions in a packet. A transition
// models the addition of an instruction to a packet. In the DFA constructed
// by this class, if an instruction can be added to a packet, then a valid
// transition exists from the corresponding state. Invalid transitions
// indicate that the instruction cannot be added to the current packet.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_DFAPACKETIZER_H
#define LLVM_CODEGEN_DFAPACKETIZER_H

#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/ScheduleDAGInstrs.h"
#include "llvm/CodeGen/ScheduleDAGMutation.h"
#include "llvm/Support/Automaton.h"
#include <cstdint>
#include <map>
#include <memory>
#include <utility>
#include <vector>

namespace llvm {

class ScheduleDAGMutation;
class InstrItineraryData;
class MachineFunction;
class MachineInstr;
class MachineLoopInfo;
class MCInstrDesc;
class SUnit;
class TargetInstrInfo;

/// VLIW scheduler that builds the dependence graph used for packetizing.
///
/// Extends ScheduleDAGInstrs and overrides the schedule method to build the
/// dependence graph.
class LLVM_ABI DefaultVLIWScheduler : public ScheduleDAGInstrs {
private:
  AAResults *AA;
  /// Ordered list of DAG postprocessing steps.
  std::vector<std::unique_ptr<ScheduleDAGMutation>> Mutations;

public:
  /// Construct a VLIW scheduler for \p MF.
  /// @param MF Machine function whose instructions are scheduled.
  /// @param MLI Loop information for \p MF.
  /// @param AA Optional alias-analysis results; may be nullptr.
  DefaultVLIWScheduler(MachineFunction &MF, MachineLoopInfo &MLI,
                       AAResults *AA);

  /// Build the dependence graph for the current scheduling region.
  void schedule() override;

  /// DefaultVLIWScheduler takes ownership of the Mutation object.
  /// @param Mutation DAG mutation applied after normal DAG building.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation) {
    Mutations.push_back(std::move(Mutation));
  }

protected:
  /// Apply registered DAG mutations to the current dependence graph.
  void postProcessDAG();
};

/// Deterministic finite automaton (DFA) based packetizer for VLIW resources.
class DFAPacketizer {
private:
  const InstrItineraryData *InstrItins;
  Automaton<uint64_t> A;
  /// For every itinerary, an "action" to apply to the automaton. This removes
  /// the redundancy in actions between itinerary classes.
  ArrayRef<unsigned> ItinActions;

public:
  /// Construct a DFA packetizer from itinerary data and an automaton.
  /// @param InstrItins Instruction itinerary data for the target.
  /// @param a Automaton that models functional-unit resource transitions.
  /// @param ItinActions Per-itinerary actions applied to the automaton.
  DFAPacketizer(const InstrItineraryData *InstrItins, Automaton<uint64_t> a,
                ArrayRef<unsigned> ItinActions)
      : InstrItins(InstrItins), A(std::move(a)), ItinActions(ItinActions) {
    // Start off with resource tracking disabled.
    A.enableTranscription(false);
  }

  /// Reset the current state to make all resources available.
  void clearResources() {
    A.reset();
  }

  /// Enable or disable tracking of which functional units each instruction uses.
  ///
  /// When enabled, the packetizer tracks not just whether instructions can be
  /// packetized, but also which functional units each instruction ends up using
  /// after packetization.
  /// @param Track True to enable functional-unit transcription.
  void setTrackResources(bool Track) {
    A.enableTranscription(Track);
  }

  /// Return true if resources occupied by \p MID are available in the current
  /// state.
  /// @param MID Instruction description whose resources are checked.
  /// @return True if resources for \p MID are available in the current state.
  LLVM_ABI bool canReserveResources(const MCInstrDesc *MID);

  /// Reserve the resources occupied by \p MID and update the current state.
  /// @param MID Instruction description whose resources are reserved.
  LLVM_ABI void reserveResources(const MCInstrDesc *MID);

  /// Return true if resources occupied by \p MI are available in the current
  /// state.
  /// @param MI Machine instruction whose resources are checked.
  /// @return True if resources for \p MI are available in the current state.
  LLVM_ABI bool canReserveResources(MachineInstr &MI);

  /// Reserve the resources occupied by \p MI and update the current state.
  /// @param MI Machine instruction whose resources are reserved.
  LLVM_ABI void reserveResources(MachineInstr &MI);

  /// Return the resources used by the InstIdx'th instruction added to this
  /// packet.
  ///
  /// The resources are returned as a bitvector of functional units.
  ///
  /// Note that a bundle may be packed in multiple valid ways. This function
  /// returns one arbitrary valid packing.
  ///
  /// Requires setTrackResources(true) to have been called.
  /// @param InstIdx Index of the instruction within the current packet.
  /// @return A bitvector of functional units used by the indexed instruction.
  LLVM_ABI unsigned getUsedResources(unsigned InstIdx);

  /// Return the instruction itinerary data used by this packetizer.
  /// @return The instruction itinerary data used by this packetizer.
  const InstrItineraryData *getInstrItins() const { return InstrItins; }
};

/// Simple VLIW packetizer that groups instructions using a DFA resource model.
///
/// The packetizer works on machine basic blocks. For each instruction I in BB,
/// the packetizer consults the DFA to see if machine resources are available
/// to execute I. If so, the packetizer checks if I depends on any instruction
/// in the current packet. If no dependency is found, I is added to current
/// packet and the machine resource is marked as taken. If any dependency is
/// found, a target API call is made to prune the dependence.
class LLVM_ABI VLIWPacketizerList {
protected:
  /// Machine function being packetized.
  MachineFunction &MF;
  /// Target instruction info for the current subtarget.
  const TargetInstrInfo *TII;
  /// Optional alias-analysis results; may be nullptr.
  AAResults *AA;

  /// VLIW scheduler that builds the dependence graph for packetizing.
  DefaultVLIWScheduler *VLIWScheduler;
  /// Instructions currently assigned to the open packet.
  std::vector<MachineInstr*> CurrentPacketMIs;
  /// DFA resource tracker for the current packet.
  DFAPacketizer *ResourceTracker;
  /// Map from machine instructions to their schedule units.
  std::map<MachineInstr*, SUnit*> MIToSUnit;

public:
  /// Construct a VLIW packetizer for \p MF.
  ///
  /// The AAResults parameter can be nullptr.
  /// @param MF Machine function whose instructions are packetized.
  /// @param MLI Loop information for \p MF.
  /// @param AA Optional alias-analysis results; may be nullptr.
  VLIWPacketizerList(MachineFunction &MF, MachineLoopInfo &MLI,
                     AAResults *AA);
  /// Assignment is deleted; VLIWPacketizerList is not copyable.
  /// @param other Unused; copy assignment is deleted.
  VLIWPacketizerList &operator=(const VLIWPacketizerList &other) = delete;
  /// Copy construction is deleted; VLIWPacketizerList is not copyable.
  /// @param other Unused; copy construction is deleted.
  VLIWPacketizerList(const VLIWPacketizerList &other) = delete;
  /// Destroy the packetizer and its owned scheduler and resource tracker.
  virtual ~VLIWPacketizerList();

  /// Bundle instructions in \p MBB between \p BeginItr and \p EndItr.
  ///
  /// Implement this API in the backend to bundle instructions.
  /// @param MBB Basic block whose instructions are packetized.
  /// @param BeginItr First instruction in the packetizing range.
  /// @param EndItr One-past-the-last instruction in the packetizing range.
  void PacketizeMIs(MachineBasicBlock *MBB,
                    MachineBasicBlock::iterator BeginItr,
                    MachineBasicBlock::iterator EndItr);

  /// Return the DFA resource tracker used by this packetizer.
  /// @return The DFA resource tracker for the current packet.
  DFAPacketizer *getResourceTracker() {return ResourceTracker;}

  /// Add \p MI to the current packet and reserve its resources.
  /// @param MI Instruction to add to the open packet.
  /// @return An iterator to \p MI in its basic block.
  virtual MachineBasicBlock::iterator addToPacket(MachineInstr &MI) {
    CurrentPacketMIs.push_back(&MI);
    ResourceTracker->reserveResources(MI);
    return MI;
  }

  /// End the current packet and reset the state of the packetizer.
  ///
  /// Overriding this function allows the target-specific packetizer
  /// to perform custom finalization.
  /// @param MBB Basic block containing the packet being finalized.
  /// @param MI Iterator pointing at the instruction after the packet.
  virtual void endPacket(MachineBasicBlock *MBB,
                         MachineBasicBlock::iterator MI);

  /// Perform initialization before packetizing an instruction.
  ///
  /// This function is supposed to be overridden by the target-dependent
  /// packetizer.
  virtual void initPacketizerState() {}

  /// Return true if \p I should be ignored by the packetizer.
  /// @param I Instruction that may be a pseudo to skip.
  /// @param MBB Basic block containing \p I.
  /// @return True if \p I should be ignored by the packetizer.
  virtual bool ignorePseudoInstruction(const MachineInstr &I,
                                       const MachineBasicBlock *MBB) {
    return false;
  }

  /// Return true if \p MI cannot be packetized with any other instruction.
  ///
  /// A solo instruction forms a packet by itself.
  /// @param MI Instruction tested for solo packetization.
  /// @return True if \p MI must form a packet by itself.
  virtual bool isSoloInstruction(const MachineInstr &MI) { return true; }

  /// Return true if the packetizer should try to add \p MI to the current
  /// packet.
  ///
  /// One reason it may not be desirable to include an instruction in the
  /// current packet is that it would cause a stall. If this function returns
  /// false, the current packet will be ended, and the instruction will be
  /// added to the next packet.
  /// @param MI Instruction considered for the current packet.
  /// @return True if \p MI should be considered for the current packet.
  virtual bool shouldAddToPacket(const MachineInstr &MI) { return true; }

  /// Return true if it is legal to packetize \p SUI and \p SUJ together.
  /// @param SUI First schedule unit considered for the same packet.
  /// @param SUJ Second schedule unit considered for the same packet.
  /// @return True if \p SUI and \p SUJ may share a packet.
  virtual bool isLegalToPacketizeTogether(SUnit *SUI, SUnit *SUJ) {
    return false;
  }

  /// Return true if it is legal to prune the dependence between \p SUI and
  /// \p SUJ.
  /// @param SUI Schedule unit whose dependence may be pruned.
  /// @param SUJ Other schedule unit in the dependence being pruned.
  /// @return True if the dependence between \p SUI and \p SUJ may be pruned.
  virtual bool isLegalToPruneDependencies(SUnit *SUI, SUnit *SUJ) {
    return false;
  }

  /// Add a DAG mutation to be done before the packetization begins.
  /// @param Mutation DAG mutation applied before packetizing.
  void addMutation(std::unique_ptr<ScheduleDAGMutation> Mutation);

  /// Return true if \p MI1 and \p MI2 may alias.
  /// @param MI1 First instruction whose memory effects are compared.
  /// @param MI2 Second instruction whose memory effects are compared.
  /// @param UseTBAA Whether type-based alias analysis may be used.
  /// @return True if \p MI1 and \p MI2 may alias.
  bool alias(const MachineInstr &MI1, const MachineInstr &MI2,
             bool UseTBAA = true) const;

private:
  bool alias(const MachineMemOperand &Op1, const MachineMemOperand &Op2,
             bool UseTBAA = true) const;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_DFAPACKETIZER_H
