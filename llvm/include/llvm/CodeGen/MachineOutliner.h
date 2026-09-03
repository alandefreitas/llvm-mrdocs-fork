//===---- MachineOutliner.h - Outliner data structures ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Contains all data structures shared between the outliner implemented in
/// MachineOutliner.cpp and target implementations of the outliner.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEOUTLINER_H
#define LLVM_CODEGEN_MACHINEOUTLINER_H

#include "llvm/CodeGen/LiveRegUnits.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/MachineStableHash.h"
#include <initializer_list>

namespace llvm {
/// Shared data structures for the MachineOutliner and target outlining hooks.
namespace outliner {

/// How an instruction should be mapped by the outliner.
enum InstrType {
  /// Safe to outline.
  Legal,
  /// Safe to outline, but only as the last instruction in a sequence.
  LegalTerminator,
  /// Cannot be outlined.
  Illegal,
  /// Can be outlined, but should not affect the outlining result.
  Invisible
};

/// An individual sequence of instructions to be replaced with a call to
/// an outlined function.
struct Candidate {
private:
  /// The start index of this \p Candidate in the instruction list.
  unsigned StartIdx = 0;

  /// The number of instructions in this \p Candidate.
  unsigned Len = 0;

  // The first instruction in this \p Candidate.
  MachineBasicBlock::iterator FirstInst;

  // The last instruction in this \p Candidate.
  MachineBasicBlock::iterator LastInst;

  // The basic block that contains this Candidate.
  MachineBasicBlock *MBB = nullptr;

  /// Cost of calling an outlined function from this point as defined by the
  /// target.
  unsigned CallOverhead = 0;

  /// Liveness information for this Candidate. Tracks from the end of the
  /// block containing this Candidate to the beginning of its sequence.
  ///
  /// Optional. Can be used to fine-tune the cost model, or fine-tune legality
  /// decisions.
  LiveRegUnits FromEndOfBlockToStartOfSeq;

  /// Liveness information restricted to this Candidate's instruction sequence.
  ///
  /// Optional. Can be used to fine-tune the cost model, or fine-tune legality
  /// decisions.
  LiveRegUnits InSeq;

  /// True if FromEndOfBlockToStartOfSeq has been initialized.
  bool FromEndOfBlockToStartOfSeqWasSet = false;

  /// True if InSeq has been initialized.
  bool InSeqWasSet = false;

  /// Populate FromEndOfBlockToStartOfSeq with liveness information.
  void initFromEndOfBlockToStartOfSeq(const TargetRegisterInfo &TRI) {
    assert(MBB->getParent()->getRegInfo().tracksLiveness() &&
           "Candidate's Machine Function must track liveness");
    // Only initialize once.
    if (FromEndOfBlockToStartOfSeqWasSet)
      return;
    FromEndOfBlockToStartOfSeqWasSet = true;
    FromEndOfBlockToStartOfSeq.init(TRI);
    FromEndOfBlockToStartOfSeq.addLiveOuts(*MBB);
    // Compute liveness from the end of the block up to the beginning of the
    // outlining candidate.
    for (auto &MI : make_range(MBB->rbegin(),
                               (MachineBasicBlock::reverse_iterator)begin()))
      if (!MI.isDebugInstr())
        FromEndOfBlockToStartOfSeq.stepBackward(MI);
  }

  /// Populate InSeq with liveness information.
  void initInSeq(const TargetRegisterInfo &TRI) {
    assert(MBB->getParent()->getRegInfo().tracksLiveness() &&
           "Candidate's Machine Function must track liveness");
    // Only initialize once.
    if (InSeqWasSet)
      return;
    InSeqWasSet = true;
    InSeq.init(TRI);
    for (auto &MI : *this)
      InSeq.accumulate(MI);
  }

public:
  /// The index of this \p Candidate's \p OutlinedFunction in the list of
  /// \p OutlinedFunctions.
  unsigned FunctionIdx = 0;

  /// Identifier denoting the instructions to emit to call an outlined function
  /// from this point. Defined by the target.
  unsigned CallConstructionID = 0;

  /// Target-specific flags for this Candidate's MBB.
  unsigned Flags = 0x0;

  /// Return the number of instructions in this Candidate.
  ///
  /// \returns Number of instructions in this Candidate.
  unsigned getLength() const { return Len; }

  /// Return the start index of this candidate.
  ///
  /// \returns Start index of this Candidate in the instruction list.
  unsigned getStartIdx() const { return StartIdx; }

  /// Return the end index of this candidate.
  ///
  /// \returns End index of this Candidate in the instruction list.
  unsigned getEndIdx() const { return StartIdx + Len - 1; }

  /// Set the CallConstructionID and CallOverhead of this candidate to CID and
  /// CO respectively.
  ///
  /// \param CID Target-defined identifier for emitting the outlined call.
  /// \param CO Target-defined cost of calling the outlined function here.
  void setCallInfo(unsigned CID, unsigned CO) {
    CallConstructionID = CID;
    CallOverhead = CO;
  }

  /// Returns the call overhead of this candidate if it is in the list.
  ///
  /// \returns Target-defined cost of calling the outlined function from here.
  unsigned getCallOverhead() const { return CallOverhead; }

  /// Return an iterator to the first instruction in this Candidate.
  ///
  /// \returns Iterator to the first instruction in this Candidate.
  MachineBasicBlock::iterator begin() { return FirstInst; }
  /// Return an iterator past the last instruction in this Candidate.
  ///
  /// \returns Iterator past the last instruction in this Candidate.
  MachineBasicBlock::iterator end() { return std::next(LastInst); }

  /// Return the first instruction in this Candidate.
  ///
  /// \returns Reference to the first instruction in this Candidate.
  MachineInstr &front() { return *FirstInst; }
  /// Return the last instruction in this Candidate.
  ///
  /// \returns Reference to the last instruction in this Candidate.
  MachineInstr &back() { return *LastInst; }
  /// Return the MachineFunction that contains this Candidate.
  ///
  /// \returns MachineFunction containing this Candidate.
  MachineFunction *getMF() const { return MBB->getParent(); }
  /// Return the MachineBasicBlock that contains this Candidate.
  ///
  /// \returns MachineBasicBlock containing this Candidate.
  MachineBasicBlock *getMBB() const { return MBB; }

  /// \returns True if \p Reg is available from the end of the block to the
  /// beginning of the sequence.
  ///
  /// This query considers the following range:
  ///
  /// in_seq_1
  /// in_seq_2
  /// ...
  /// in_seq_n
  /// not_in_seq_1
  /// ...
  /// <end of block>
  ///
  /// \param Reg Register to query for availability.
  /// \param TRI Target register info used to initialize liveness.
  bool isAvailableAcrossAndOutOfSeq(Register Reg,
                                    const TargetRegisterInfo &TRI) {
    if (!FromEndOfBlockToStartOfSeqWasSet)
      initFromEndOfBlockToStartOfSeq(TRI);
    return FromEndOfBlockToStartOfSeq.available(Reg);
  }

  /// Return true if any register in \p Regs is unavailable across or out of
  /// the sequence.
  ///
  /// \param Regs Registers to test for unavailability.
  /// \param TRI Target register info used to initialize liveness.
  /// \returns True if `isAvailableAcrossAndOutOfSeq` fails for any register
  /// in \p Regs.
  bool isAnyUnavailableAcrossOrOutOfSeq(std::initializer_list<Register> Regs,
                                        const TargetRegisterInfo &TRI) {
    if (!FromEndOfBlockToStartOfSeqWasSet)
      initFromEndOfBlockToStartOfSeq(TRI);
    return any_of(Regs, [&](Register Reg) {
      return !FromEndOfBlockToStartOfSeq.available(Reg);
    });
  }

  /// \returns True if \p Reg is available within the sequence itself.
  ///
  /// This query considers the following range:
  ///
  /// in_seq_1
  /// in_seq_2
  /// ...
  /// in_seq_n
  ///
  /// \param Reg Register to query for availability.
  /// \param TRI Target register info used to initialize liveness.
  bool isAvailableInsideSeq(Register Reg, const TargetRegisterInfo &TRI) {
    if (!InSeqWasSet)
      initInSeq(TRI);
    return InSeq.available(Reg);
  }

  /// The number of instructions that would be saved by outlining every
  /// candidate of this type.
  ///
  /// This is a fixed value which is not updated during the candidate pruning
  /// process. It is only used for deciding which candidate to keep if two
  /// candidates overlap. The true benefit is stored in the OutlinedFunction
  /// for some given candidate.
  unsigned Benefit = 0;

  /// Construct a Candidate spanning \p FirstInst through \p LastInst.
  ///
  /// \param StartIdx Start index of this Candidate in the instruction list.
  /// \param Len Number of instructions in this Candidate.
  /// \param FirstInst Iterator to the first instruction in the sequence.
  /// \param LastInst Iterator to the last instruction in the sequence.
  /// \param MBB Basic block containing this Candidate.
  /// \param FunctionIdx Index of this Candidate's OutlinedFunction.
  /// \param Flags Target-specific flags for this Candidate's MBB.
  Candidate(unsigned StartIdx, unsigned Len,
            MachineBasicBlock::iterator &FirstInst,
            MachineBasicBlock::iterator &LastInst, MachineBasicBlock *MBB,
            unsigned FunctionIdx, unsigned Flags)
      : StartIdx(StartIdx), Len(Len), FirstInst(FirstInst), LastInst(LastInst),
        MBB(MBB), FunctionIdx(FunctionIdx), Flags(Flags) {}
  /// Deleted; a Candidate requires a concrete instruction sequence.
  Candidate() = delete;

  /// Used to ensure that \p Candidates are outlined in an order that
  /// preserves the start and end indices of other \p Candidates.
  ///
  /// \param RHS Other Candidate to compare start indices against.
  /// \returns True if this Candidate starts after \p RHS.
  bool operator<(const Candidate &RHS) const {
    return getStartIdx() > RHS.getStartIdx();
  }

};

/// The information necessary to create an outlined function for some
/// class of candidate.
struct OutlinedFunction {

public:
  /// Candidates that map to this outlined function.
  std::vector<Candidate> Candidates;

  /// The actual outlined function created.
  /// This is initialized after we go through and create the actual function.
  MachineFunction *MF = nullptr;

  /// Represents the size of a sequence in bytes. (Some instructions vary
  /// widely in size, so just counting the instructions isn't very useful.)
  unsigned SequenceSize = 0;

  /// Target-defined overhead of constructing a frame for this function.
  unsigned FrameOverhead = 0;

  /// Target-defined identifier for constructing a frame for this function.
  unsigned FrameConstructionID = 0;

  /// Return the number of candidates for this \p OutlinedFunction.
  ///
  /// \returns Number of candidates that would call this outlined function.
  virtual unsigned getOccurrenceCount() const { return Candidates.size(); }

  /// Return the number of bytes it would take to outline this
  /// function.
  ///
  /// \returns Total cost in bytes to outline this function.
  virtual unsigned getOutliningCost() const {
    unsigned CallOverhead = 0;
    for (const Candidate &C : Candidates)
      CallOverhead += C.getCallOverhead();
    return CallOverhead + SequenceSize + FrameOverhead;
  }

  /// Return the size in bytes of the unoutlined sequences.
  ///
  /// \returns Total size in bytes of all unoutlined sequence occurrences.
  unsigned getNotOutlinedCost() const {
    return getOccurrenceCount() * SequenceSize;
  }

  /// Return the number of instructions that would be saved by outlining
  /// this function.
  ///
  /// \returns Bytes saved by outlining, or 0 if outlining is not beneficial.
  unsigned getBenefit() const {
    unsigned NotOutlinedCost = getNotOutlinedCost();
    unsigned OutlinedCost = getOutliningCost();
    return (NotOutlinedCost < OutlinedCost) ? 0
                                            : NotOutlinedCost - OutlinedCost;
  }

  /// Return the number of instructions in this sequence.
  ///
  /// \returns Number of instructions in the shared candidate sequence.
  unsigned getNumInstrs() const { return Candidates[0].getLength(); }

  /// Construct an OutlinedFunction for a class of Candidates.
  ///
  /// \param Candidates Sequences that would call this outlined function.
  /// \param SequenceSize Size in bytes of the repeated instruction sequence.
  /// \param FrameOverhead Target-defined cost of building the outlined frame.
  /// \param FrameConstructionID Target-defined frame construction identifier.
  OutlinedFunction(std::vector<Candidate> &Candidates, unsigned SequenceSize,
                   unsigned FrameOverhead, unsigned FrameConstructionID)
      : Candidates(Candidates), SequenceSize(SequenceSize),
        FrameOverhead(FrameOverhead), FrameConstructionID(FrameConstructionID) {
    const unsigned B = getBenefit();
    for (Candidate &C : Candidates)
      C.Benefit = B;
  }

  /// Deleted; an OutlinedFunction requires Candidates and size/overhead data.
  OutlinedFunction() = delete;
  /// Destroy this OutlinedFunction.
  virtual ~OutlinedFunction() = default;
};

/// The information necessary to create an outlined function that is matched
/// globally.
struct GlobalOutlinedFunction : public OutlinedFunction {
  /// Construct a globally matched OutlinedFunction from \p OF.
  ///
  /// \param OF OutlinedFunction whose fields are copied into this instance.
  /// \param GlobalOccurrenceCount Global match count for this candidate.
  explicit GlobalOutlinedFunction(std::unique_ptr<OutlinedFunction> OF,
                                  unsigned GlobalOccurrenceCount)
      : OutlinedFunction(*OF), GlobalOccurrenceCount(GlobalOccurrenceCount) {}

  /// Number of times this sequence appears globally across the module.
  unsigned GlobalOccurrenceCount;

  /// Return the global occurrence count for this outlining candidate.
  ///
  /// A global outlining candidate is uniquely created per match, but it might
  /// be erased when overlapped with a previous outlining instance.
  ///
  /// \returns Global occurrence count, or 0 if this candidate was erased.
  unsigned getOccurrenceCount() const override {
    assert(Candidates.size() <= 1);
    return Candidates.empty() ? 0 : GlobalOccurrenceCount;
  }

  /// Return the outlining cost using the global occurrence count
  /// with the same cost as the first (unique) candidate.
  ///
  /// \returns Total cost in bytes to outline this function globally.
  unsigned getOutliningCost() const override {
    assert(Candidates.size() <= 1);
    unsigned CallOverhead =
        Candidates.empty()
            ? 0
            : Candidates[0].getCallOverhead() * getOccurrenceCount();
    return CallOverhead + SequenceSize + FrameOverhead;
  }

  /// Deleted; a GlobalOutlinedFunction requires an OutlinedFunction and count.
  GlobalOutlinedFunction() = delete;
  /// Destroy this GlobalOutlinedFunction.
  ~GlobalOutlinedFunction() override = default;
};

} // namespace outliner
} // namespace llvm

#endif
