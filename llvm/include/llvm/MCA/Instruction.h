//===--------------------- Instruction.h ------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines abstractions used by the Pipeline to model register reads,
/// register writes and instructions.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_INSTRUCTION_H
#define LLVM_MCA_INSTRUCTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCRegister.h" // definition of MCPhysReg.
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"

#ifndef NDEBUG
#include "llvm/Support/raw_ostream.h"
#endif

namespace llvm {

namespace mca {

/// Sentinel latency value meaning the cycle count is not yet known.
constexpr int UNKNOWN_CYCLES = -512;

/// A representation of an mca::Instruction operand
/// for use in mca::CustomBehaviour.
class MCAOperand {
  // This class is mostly copied from MCOperand within
  // MCInst.h except that we don't keep track of
  // expressions or sub-instructions.
  enum MCAOperandType : unsigned char {
    kInvalid,   ///< Uninitialized, Relocatable immediate, or Sub-instruction.
    kRegister,  ///< Register operand.
    kImmediate, ///< Immediate operand.
    kSFPImmediate, ///< Single-floating-point immediate operand.
    kDFPImmediate, ///< Double-Floating-point immediate operand.
  };
  MCAOperandType Kind;

  union {
    /// Register value when Kind is kRegister.
    MCRegister RegVal;
    /// Integer immediate when Kind is kImmediate.
    int64_t ImmVal;
    /// Single-precision FP immediate bits when Kind is kSFPImmediate.
    uint32_t SFPImmVal;
    /// Double-precision FP immediate bits when Kind is kDFPImmediate.
    uint64_t FPImmVal;
  };

  // We only store specific operands for specific instructions
  // so an instruction's operand 3 may be stored within the list
  // of MCAOperand as element 0. This Index attribute keeps track
  // of the original index (3 for this example).
  unsigned Index;

public:
  /// Construct an invalid (uninitialized) operand.
  MCAOperand() : Kind(kInvalid), FPImmVal(), Index() {}

  /// Return true if this operand is initialized.
  /// @return True if this operand is initialized.
  bool isValid() const { return Kind != kInvalid; }
  /// Return true if this is a register operand.
  /// @return True if this is a register operand.
  bool isReg() const { return Kind == kRegister; }
  /// Return true if this is an integer immediate operand.
  /// @return True if this is an integer immediate operand.
  bool isImm() const { return Kind == kImmediate; }
  /// Return true if this is a single-precision FP immediate operand.
  /// @return True if this is a single-precision FP immediate operand.
  bool isSFPImm() const { return Kind == kSFPImmediate; }
  /// Return true if this is a double-precision FP immediate operand.
  /// @return True if this is a double-precision FP immediate operand.
  bool isDFPImm() const { return Kind == kDFPImmediate; }

  /// Returns the register number.
  /// @return The register value encoded by this operand.
  MCRegister getReg() const {
    assert(isReg() && "This is not a register operand!");
    return RegVal;
  }

  /// Return the integer immediate value.
  /// @return The integer immediate value.
  int64_t getImm() const {
    assert(isImm() && "This is not an immediate");
    return ImmVal;
  }

  /// Return the single-precision FP immediate bit pattern.
  /// @return The single-precision FP immediate bit pattern.
  uint32_t getSFPImm() const {
    assert(isSFPImm() && "This is not an SFP immediate");
    return SFPImmVal;
  }

  /// Return the double-precision FP immediate bit pattern.
  /// @return The double-precision FP immediate bit pattern.
  uint64_t getDFPImm() const {
    assert(isDFPImm() && "This is not an FP immediate");
    return FPImmVal;
  }

  /// Set the original MCInst operand index for this value.
  /// @param Idx Operand index in the original MCInst.
  void setIndex(const unsigned Idx) { Index = Idx; }

  /// Return the original MCInst operand index for this value.
  /// @return The original MCInst operand index for this value.
  unsigned getIndex() const { return Index; }

  /// Create a register operand for \p Reg.
  /// @param Reg Physical register encoded by this operand.
  /// @return A register MCAOperand for \p Reg.
  static MCAOperand createReg(MCRegister Reg) {
    MCAOperand Op;
    Op.Kind = kRegister;
    Op.RegVal = Reg;
    return Op;
  }

  /// Create an integer immediate operand with value \p Val.
  /// @param Val Immediate integer value.
  /// @return An integer immediate MCAOperand with value \p Val.
  static MCAOperand createImm(int64_t Val) {
    MCAOperand Op;
    Op.Kind = kImmediate;
    Op.ImmVal = Val;
    return Op;
  }

  /// Create a single-precision FP immediate operand from bits \p Val.
  /// @param Val IEEE-754 single-precision bit pattern.
  /// @return A single-precision FP immediate MCAOperand from bits \p Val.
  static MCAOperand createSFPImm(uint32_t Val) {
    MCAOperand Op;
    Op.Kind = kSFPImmediate;
    Op.SFPImmVal = Val;
    return Op;
  }

  /// Create a double-precision FP immediate operand from bits \p Val.
  /// @param Val IEEE-754 double-precision bit pattern.
  /// @return A double-precision FP immediate MCAOperand from bits \p Val.
  static MCAOperand createDFPImm(uint64_t Val) {
    MCAOperand Op;
    Op.Kind = kDFPImmediate;
    Op.FPImmVal = Val;
    return Op;
  }

  /// Create an invalid (uninitialized) operand.
  /// @return An invalid (uninitialized) MCAOperand.
  static MCAOperand createInvalid() {
    MCAOperand Op;
    Op.Kind = kInvalid;
    Op.FPImmVal = 0;
    return Op;
  }
};

/// A register write descriptor.
struct WriteDescriptor {
  /// Operand index; negative for implicit writes.
  ///
  /// For implicit writes, the actual operand index is computed performing a
  /// bitwise not of the OpIndex.
  int OpIndex;
  /// Write latency in cycles before the write-back stage.
  unsigned Latency;
  /// Implicit definition register; non-zero only for implicit writes.
  MCPhysReg RegisterID;
  /// Scheduling class or write-resource ID used for read-latency matching.
  ///
  /// Instruction itineraries would set this field to the SchedClass ID.
  /// Otherwise, it defaults to the WriteResourceID from the MCWriteLatencyEntry
  /// element associated to this write. When computing read latencies, this
  /// value is matched against the "ReadAdvance" information. The hardware
  /// backend may implement dedicated forwarding paths to quickly propagate
  /// write results to dependent instructions waiting in the reservation station
  /// (effectively bypassing the write-back stage).
  unsigned SClassOrWriteResourceID;
  /// True if this write comes from an optional definition.
  ///
  /// Optional definitions are allowed to reference regID zero (i.e. "no
  /// register").
  bool IsOptionalDef;

  /// Return true if this descriptor models an implicit write.
  /// @return True if this descriptor models an implicit write.
  bool isImplicitWrite() const { return OpIndex < 0; };
};

/// A register read descriptor.
struct ReadDescriptor {
  /// MCOperand index used to identify the register read.
  ///
  /// Implicit reads have negative indices. The actual operand index of an
  /// implicit read is the bitwise not of field OpIndex.
  int OpIndex;
  /// Use index for querying the ReadAdvance table.
  ///
  /// Explicit uses always come first in the sequence of uses.
  unsigned UseIndex;
  /// Implicit-use register; set only for implicit reads.
  MCPhysReg RegisterID;
  /// Scheduling class index for querying the scheduling model.
  ///
  /// It is used to query the scheduling model for the MCSchedClassDesc object.
  unsigned SchedClassID;

  /// Return true if this descriptor models an implicit read.
  /// @return True if this descriptor models an implicit read.
  bool isImplicitRead() const { return OpIndex < 0; };
};

class ReadState;

/// A critical data dependency descriptor.
///
/// Field RegID is set to the invalid register for memory dependencies.
struct CriticalDependency {
  /// Instruction identifier of the producer.
  unsigned IID;
  /// Register that carries the dependency, or invalid for memory deps.
  MCPhysReg RegID;
  /// Cost of the dependency in cycles.
  unsigned Cycles;
};

/// Tracks uses of a register definition (e.g. register write).
///
/// Each implicit/explicit register write is associated with an instance of
/// this class. A WriteState object tracks the dependent users of a
/// register write. It also tracks how many cycles are left before the write
/// back stage.
class WriteState {
  const WriteDescriptor *WD;
  // On instruction issue, this field is set equal to the write latency.
  // Before instruction issue, this field defaults to -512, a special
  // value that represents an "unknown" number of cycles.
  int CyclesLeft;

  // Actual register defined by this write. This field is only used
  // to speedup queries on the register file.
  // For implicit writes, this field always matches the value of
  // field RegisterID from WD.
  MCPhysReg RegisterID;

  // Physical register file that serves register RegisterID.
  unsigned PRFID;

  // True if this write implicitly clears the upper portion of RegisterID's
  // super-registers.
  bool ClearsSuperRegs;

  // True if this write is from a dependency breaking zero-idiom instruction.
  bool WritesZero;

  // True if this write has been eliminated at register renaming stage.
  // Example: a register move doesn't consume scheduler/pipleline resources if
  // it is eliminated at register renaming stage. It still consumes
  // decode bandwidth, and ROB entries.
  bool IsEliminated;

  // This field is set if this is a partial register write, and it has a false
  // dependency on any previous write of the same register (or a portion of it).
  // DependentWrite must be able to complete before this write completes, so
  // that we don't break the WAW, and the two writes can be merged together.
  const WriteState *DependentWrite;

  // A partial write that is in a false dependency with this write.
  WriteState *PartialWrite;
  unsigned DependentWriteCyclesLeft;

  // Critical register dependency for this write.
  CriticalDependency CRD;

  // A list of dependent reads. Users is a set of dependent
  // reads. A dependent read is added to the set only if CyclesLeft
  // is "unknown". As soon as CyclesLeft is 'known', each user in the set
  // gets notified with the actual CyclesLeft.

  // The 'second' element of a pair is a "ReadAdvance" number of cycles.
  SmallVector<std::pair<ReadState *, int>, 4> Users;

public:
  /// Construct write state for descriptor \p Desc defining \p RegID.
  /// @param Desc Write descriptor for this definition.
  /// @param RegID Physical register defined by this write.
  /// @param clearsSuperRegs Whether upper bits of super-registers are cleared.
  /// @param writesZero Whether this write produces a known zero value.
  WriteState(const WriteDescriptor &Desc, MCPhysReg RegID,
             bool clearsSuperRegs = false, bool writesZero = false)
      : WD(&Desc), CyclesLeft(UNKNOWN_CYCLES), RegisterID(RegID), PRFID(0),
        ClearsSuperRegs(clearsSuperRegs), WritesZero(writesZero),
        IsEliminated(false), DependentWrite(nullptr), PartialWrite(nullptr),
        DependentWriteCyclesLeft(0), CRD() {}

  /// Copy-construct write state from \p Other.
  /// @param Other Write state to copy.
  WriteState(const WriteState &Other) = default;
  /// Copy-assign write state from \p Other.
  /// @param Other Write state to assign from.
  /// @return Reference to this write state.
  WriteState &operator=(const WriteState &Other) = default;

  /// Return cycles remaining until write-back, or UNKNOWN_CYCLES.
  /// @return Cycles remaining until write-back, or UNKNOWN_CYCLES.
  int getCyclesLeft() const { return CyclesLeft; }
  /// Return the write-resource or scheduling-class ID for this write.
  /// @return The write-resource or scheduling-class ID for this write.
  unsigned getWriteResourceID() const { return WD->SClassOrWriteResourceID; }
  /// Return the physical register defined by this write.
  /// @return The physical register defined by this write.
  MCPhysReg getRegisterID() const { return RegisterID; }
  /// Set the physical register defined by this write to \p RegID.
  /// @param RegID Physical register defined by this write.
  void setRegisterID(const MCPhysReg RegID) { RegisterID = RegID; }
  /// Return the physical register file that serves this write.
  /// @return The physical register file that serves this write.
  unsigned getRegisterFileID() const { return PRFID; }
  /// Return the write latency from the descriptor.
  /// @return The write latency from the descriptor.
  unsigned getLatency() const { return WD->Latency; }
  /// Return cycles left on the dependent false-dependency write.
  /// @return Cycles left on the dependent false-dependency write.
  unsigned getDependentWriteCyclesLeft() const {
    return DependentWriteCyclesLeft;
  }
  /// Return the write that this partial write falsely depends on, if any.
  /// @return The write that this partial write falsely depends on, or nullptr.
  const WriteState *getDependentWrite() const { return DependentWrite; }
  /// Return the critical register dependency recorded for this write.
  /// @return The critical register dependency recorded for this write.
  const CriticalDependency &getCriticalRegDep() const { return CRD; }

  /// Add data-dependent read \p Use with optional \p ReadAdvance.
  ///
  /// IID is the instruction identifier associated with this write. ReadAdvance
  /// is the number of cycles to subtract from the latency of this data
  /// dependency. Use is in a RAW dependency with this write.
  /// @param IID Instruction identifier of this write.
  /// @param Use Dependent read to notify when latency becomes known.
  /// @param ReadAdvance Cycles to subtract from the dependency latency.
  LLVM_ABI void addUser(unsigned IID, ReadState *Use, int ReadAdvance);

  /// Add younger write \p Use that has a false dependency on this write.
  ///
  /// IID is the instruction identifier associated with this write.
  /// @param IID Instruction identifier of this write.
  /// @param Use Younger write in a false dependency with this write.
  LLVM_ABI void addUser(unsigned IID, WriteState *Use);

  /// Return the number of dependent reads plus any partial-write user.
  /// @return The number of dependent reads plus any partial-write user.
  unsigned getNumUsers() const {
    unsigned NumUsers = Users.size();
    if (PartialWrite)
      ++NumUsers;
    return NumUsers;
  }

  /// Return true if this write clears upper bits of super-registers.
  /// @return True if this write clears upper bits of super-registers.
  bool clearsSuperRegisters() const { return ClearsSuperRegs; }
  /// Return true if this write produces a known zero value.
  /// @return True if this write produces a known zero value.
  bool isWriteZero() const { return WritesZero; }
  /// Return true if this write was eliminated at register renaming.
  /// @return True if this write was eliminated at register renaming.
  bool isEliminated() const { return IsEliminated; }

  /// Return true if no outstanding false dependency blocks this write.
  /// @return True if no outstanding false dependency blocks this write.
  bool isReady() const {
    if (DependentWrite)
      return false;
    unsigned CyclesLeft = getDependentWriteCyclesLeft();
    return !CyclesLeft || CyclesLeft < getLatency();
  }

  /// Return true if write-back has completed (CyclesLeft known and <= 0).
  /// @return True if write-back has completed (CyclesLeft known and <= 0).
  bool isExecuted() const {
    return CyclesLeft != UNKNOWN_CYCLES && CyclesLeft <= 0;
  }

  /// Record that this write has a false dependency on \p Other.
  /// @param Other Prior write that must complete before this one.
  void setDependentWrite(const WriteState *Other) { DependentWrite = Other; }
  /// Notify this write that producer \p IID wrote \p RegID in \p Cycles.
  /// @param IID Instruction identifier of the producer.
  /// @param RegID Register written by the producer.
  /// @param Cycles Cycles until the producer write-back.
  LLVM_ABI void writeStartEvent(unsigned IID, MCPhysReg RegID, unsigned Cycles);
  /// Mark this write as producing a known zero value.
  void setWriteZero() { WritesZero = true; }
  /// Mark this write as eliminated and force CyclesLeft to zero.
  void setEliminated() {
    assert(Users.empty() && "Write is in an inconsistent state.");
    CyclesLeft = 0;
    IsEliminated = true;
  }

  /// Set the physical register file ID serving this write.
  /// @param PRF Physical register file identifier.
  void setPRF(unsigned PRF) { PRFID = PRF; }

  /// Advance one cycle: update CyclesLeft and notify dependent users.
  LLVM_ABI void cycleEvent();
  /// Record that instruction \p IID issued and initialize write latency.
  /// @param IID Instruction identifier that issued.
  LLVM_ABI void onInstructionIssued(unsigned IID);

#ifndef NDEBUG
  /// Dump this write state to stderr for debugging.
  void dump() const;
#endif
};

/// Tracks register operand latency in cycles.
///
/// A read may be dependent on more than one write. This occurs when some
/// writes only partially update the register associated to this read.
class ReadState {
  const ReadDescriptor *RD;
  // Physical register identified associated to this read.
  MCPhysReg RegisterID;
  // Physical register file that serves register RegisterID.
  unsigned PRFID;
  // Number of writes that contribute to the definition of RegisterID.
  // In the absence of partial register updates, the number of DependentWrites
  // cannot be more than one.
  unsigned DependentWrites;
  // Number of cycles left before RegisterID can be read. This value depends on
  // the latency of all the dependent writes. It defaults to UNKNOWN_CYCLES.
  // It gets set to the value of field TotalCycles only when the 'CyclesLeft' of
  // every dependent write is known.
  int CyclesLeft;
  // This field is updated on every writeStartEvent(). When the number of
  // dependent writes (i.e. field DependentWrite) is zero, this value is
  // propagated to field CyclesLeft.
  unsigned TotalCycles;
  // Longest register dependency.
  CriticalDependency CRD;
  // This field is set to true only if there are no dependent writes, and
  // there are no `CyclesLeft' to wait.
  bool IsReady;
  // True if this is a read from a known zero register.
  bool IsZero;
  // True if this register read is from a dependency-breaking instruction.
  bool IndependentFromDef;

public:
  /// Construct read state for descriptor \p Desc reading \p RegID.
  /// @param Desc Read descriptor for this use.
  /// @param RegID Physical register read by this use.
  ReadState(const ReadDescriptor &Desc, MCPhysReg RegID)
      : RD(&Desc), RegisterID(RegID), PRFID(0), DependentWrites(0),
        CyclesLeft(UNKNOWN_CYCLES), TotalCycles(0), CRD(), IsReady(true),
        IsZero(false), IndependentFromDef(false) {}

  /// Return the read descriptor associated with this state.
  /// @return The read descriptor associated with this state.
  const ReadDescriptor &getDescriptor() const { return *RD; }
  /// Return the scheduling class ID from the descriptor.
  /// @return The scheduling class ID from the descriptor.
  unsigned getSchedClass() const { return RD->SchedClassID; }
  /// Return the physical register read by this use.
  /// @return The physical register read by this use.
  MCPhysReg getRegisterID() const { return RegisterID; }
  /// Return the physical register file that serves this read.
  /// @return The physical register file that serves this read.
  unsigned getRegisterFileID() const { return PRFID; }
  /// Return the critical register dependency for this read.
  /// @return The critical register dependency for this read.
  const CriticalDependency &getCriticalRegDep() const { return CRD; }

  /// Return true if latency is known but the read is not yet ready.
  /// @return True if latency is known but the read is not yet ready.
  bool isPending() const { return !IndependentFromDef && CyclesLeft > 0; }
  /// Return true if this read can proceed (no outstanding wait).
  /// @return True if this read can proceed (no outstanding wait).
  bool isReady() const { return IsReady; }
  /// Return true if this is an implicit register read.
  /// @return True if this is an implicit register read.
  bool isImplicitRead() const { return RD->isImplicitRead(); }

  /// Return true if this read ignores its reaching definition.
  /// @return True if this read ignores its reaching definition.
  bool isIndependentFromDef() const { return IndependentFromDef; }
  /// Mark this read as independent from its reaching definition.
  void setIndependentFromDef() { IndependentFromDef = true; }

  /// Advance one cycle of pending read latency.
  LLVM_ABI void cycleEvent();
  /// Record that producer \p IID wrote \p RegID with \p Cycles remaining.
  /// @param IID Instruction identifier of the producer write.
  /// @param RegID Register written by the producer.
  /// @param Cycles Cycles until the producer write-back.
  LLVM_ABI void writeStartEvent(unsigned IID, MCPhysReg RegID, unsigned Cycles);
  /// Set the number of dependent writes contributing to this read.
  /// @param Writes Number of writes that define the read register.
  void setDependentWrites(unsigned Writes) {
    DependentWrites = Writes;
    IsReady = !Writes;
  }

  /// Return true if this reads a known-zero register.
  /// @return True if this reads a known-zero register.
  bool isReadZero() const { return IsZero; }
  /// Mark this read as reading a known-zero register.
  void setReadZero() { IsZero = true; }
  /// Set the physical register file ID serving this read.
  /// @param ID Physical register file identifier.
  void setPRF(unsigned ID) { PRFID = ID; }

#ifndef NDEBUG
  /// Dump this read state to stderr for debugging.
  void dump() const;
#endif
};

/// A sequence of cycles.
///
/// This class can be used as a building block to construct ranges of cycles.
class CycleSegment {
  unsigned Begin; // Inclusive.
  unsigned End;   // Exclusive.
  bool Reserved;  // Resources associated to this segment must be reserved.

public:
  /// Construct a half-open cycle range [\p StartCycle, \p EndCycle).
  /// @param StartCycle Inclusive start cycle.
  /// @param EndCycle Exclusive end cycle.
  /// @param IsReserved Whether resources in this segment must be reserved.
  CycleSegment(unsigned StartCycle, unsigned EndCycle, bool IsReserved = false)
      : Begin(StartCycle), End(EndCycle), Reserved(IsReserved) {}

  /// Return true if \p Cycle lies in [Begin, End).
  /// @param Cycle Cycle index to test.
  /// @return True if \p Cycle lies in [Begin, End).
  bool contains(unsigned Cycle) const { return Cycle >= Begin && Cycle < End; }
  /// Return true if this segment ends at or before \p CS begins.
  /// @param CS Other cycle segment to compare against.
  /// @return True if this segment ends at or before \p CS begins.
  bool startsAfter(const CycleSegment &CS) const { return End <= CS.Begin; }
  /// Return true if this segment begins at or after \p CS ends.
  /// @param CS Other cycle segment to compare against.
  /// @return True if this segment begins at or after \p CS ends.
  bool endsBefore(const CycleSegment &CS) const { return Begin >= CS.End; }
  /// Return true if this segment overlaps \p CS.
  /// @param CS Other cycle segment to compare against.
  /// @return True if this segment overlaps \p CS.
  bool overlaps(const CycleSegment &CS) const {
    return !startsAfter(CS) && !endsBefore(CS);
  }
  /// Return true if execution of this segment has started but not finished.
  /// @return True if execution of this segment has started but not finished.
  bool isExecuting() const { return Begin == 0 && End != 0; }
  /// Return true if this segment has fully executed (End == 0).
  /// @return True if this segment has fully executed (End == 0).
  bool isExecuted() const { return End == 0; }
  /// Order segments by ascending Begin cycle.
  /// @param Other Segment to compare Begin against.
  /// @return True if this segment begins before \p Other.
  bool operator<(const CycleSegment &Other) const {
    return Begin < Other.Begin;
  }
  /// Decrement Begin and End by one cycle when non-zero.
  /// @return Reference to this cycle segment.
  CycleSegment &operator--() {
    if (Begin)
      Begin--;
    if (End)
      End--;
    return *this;
  }

  /// Return true if Begin is not past End.
  /// @return True if Begin is not past End.
  bool isValid() const { return Begin <= End; }
  /// Return the number of cycles covered by this segment.
  /// @return The number of cycles covered by this segment.
  unsigned size() const { return End - Begin; };
  /// Shrink the segment by subtracting \p Cycles from End.
  /// @param Cycles Number of cycles to remove from the end.
  void subtract(unsigned Cycles) {
    assert(End >= Cycles);
    End -= Cycles;
  }

  /// Return the inclusive start cycle.
  /// @return The inclusive start cycle.
  unsigned begin() const { return Begin; }
  /// Return the exclusive end cycle.
  /// @return The exclusive end cycle.
  unsigned end() const { return End; }
  /// Set the exclusive end cycle to \p NewEnd.
  /// @param NewEnd New exclusive end cycle.
  void setEnd(unsigned NewEnd) { End = NewEnd; }
  /// Return true if resources for this segment must be reserved.
  /// @return True if resources for this segment must be reserved.
  bool isReserved() const { return Reserved; }
  /// Mark resources associated with this segment as reserved.
  void setReserved() { Reserved = true; }
};

/// Helper used by class InstrDesc to describe how hardware resources
/// are used.
///
/// This class describes how many resource units of a specific resource kind
/// (and how many cycles) are "used" by an instruction.
struct ResourceUsage {
  /// Cycle range during which the resource units are consumed.
  CycleSegment CS;
  /// Number of resource units consumed.
  unsigned NumUnits;
  /// Construct usage of \p Units over cycle segment \p Cycles.
  /// @param Cycles Cycle range of the resource consumption.
  /// @param Units Number of resource units consumed (defaults to 1).
  ResourceUsage(CycleSegment Cycles, unsigned Units = 1)
      : CS(Cycles), NumUnits(Units) {}
  /// Return the number of cycles in the usage segment.
  /// @return The number of cycles in the usage segment.
  unsigned size() const { return CS.size(); }
  /// Return true if the usage segment is reserved.
  /// @return True if the usage segment is reserved.
  bool isReserved() const { return CS.isReserved(); }
  /// Mark the usage segment as reserved.
  void setReserved() { CS.setReserved(); }
};

/// An instruction descriptor
struct InstrDesc {
  /// Register write descriptors; implicit writes are at the end.
  SmallVector<WriteDescriptor, 2> Writes;
  /// Register read descriptors; implicit reads are at the end.
  SmallVector<ReadDescriptor, 4> Reads;

  /// Per-resource consumption: mask paired with cycle/unit usage.
  ///
  /// For every resource used by an instruction of this kind, this vector
  /// reports the number of "consumed cycles".
  SmallVector<std::pair<uint64_t, ResourceUsage>, 4> Resources;

  /// Bitmask of hardware buffers used by this instruction kind.
  uint64_t UsedBuffers;

  /// Bitmask of processor resource units used by this instruction kind.
  uint64_t UsedProcResUnits;

  /// Bitmask of processor resource groups used by this instruction kind.
  uint64_t UsedProcResGroups;

  /// Maximum write latency for instructions of this kind.
  unsigned MaxLatency;
  /// Number of micro-ops for this instruction kind.
  unsigned NumMicroOps;
  /// Scheduling class ID used to construct this descriptor.
  ///
  /// This information is currently used by views to do fast queries on the
  /// subtarget when computing the reciprocal throughput.
  unsigned SchedClassID;

  /// True if buffered resources force immediate issue (dispatch hazard).
  ///
  /// True if all buffered resources are in-order, and there is at least one
  /// buffer which is a dispatch hazard (BufferSize = 0).
  unsigned MustIssueImmediately : 1;

  /// True if corresponding Instruction objects may be recycled.
  ///
  /// Currently only instructions that are neither variadic nor have any
  /// variant can be recycled.
  unsigned IsRecyclable : 1;

  /// True if some consumed group resources partially overlap.
  unsigned HasPartiallyOverlappingGroups : 1;

  /// Return true if this instruction has zero latency and uses no resources.
  /// @return True if this instruction has zero latency and uses no resources.
  bool isZeroLatency() const { return !MaxLatency && Resources.empty(); }

  /// Default-construct an empty instruction descriptor.
  InstrDesc() = default;
  /// Deleted copy constructor.
  /// @param Other Unused; copy construction is deleted.
  InstrDesc(const InstrDesc &Other) = delete;
  /// Deleted copy assignment.
  /// @param Other Unused; copy assignment is deleted.
  InstrDesc &operator=(const InstrDesc &Other) = delete;
};

/// Base class for instructions consumed by the simulation pipeline.
///
/// This class tracks data dependencies as well as generic properties
/// of the instruction.
class InstructionBase {
  const InstrDesc &Desc;

  // This field is set for instructions that are candidates for move
  // elimination. For more information about move elimination, see the
  // definition of RegisterMappingTracker in RegisterFile.h
  bool IsOptimizableMove;

  // Output dependencies.
  // One entry per each implicit and explicit register definition.
  SmallVector<WriteState, 2> Defs;

  // Input dependencies.
  // One entry per each implicit and explicit register use.
  SmallVector<ReadState, 4> Uses;

  // List of operands which can be used by mca::CustomBehaviour
  std::vector<MCAOperand> Operands;

  // Instruction opcode which can be used by mca::CustomBehaviour
  unsigned Opcode;

  // Flags used by the LSUnit.
  bool IsALoadBarrier : 1;
  bool IsAStoreBarrier : 1;
  // Flags copied from the InstrDesc and potentially modified by
  // CustomBehaviour or (more likely) InstrPostProcess.
  bool MayLoad : 1;
  bool MayStore : 1;
  bool HasSideEffects : 1;
  bool BeginGroup : 1;
  bool EndGroup : 1;
  bool RetireOOO : 1;

public:
  /// Construct a base instruction from descriptor \p D and opcode \p Opcode.
  /// @param D Instruction descriptor.
  /// @param Opcode Opcode value for CustomBehaviour queries.
  InstructionBase(const InstrDesc &D, const unsigned Opcode)
      : Desc(D), IsOptimizableMove(false), Operands(0), Opcode(Opcode),
        IsALoadBarrier(false), IsAStoreBarrier(false) {}

  /// Return a mutable view of the register definitions.
  /// @return A mutable view of the register definitions.
  SmallVectorImpl<WriteState> &getDefs() { return Defs; }
  /// Return a const view of the register definitions.
  /// @return A const view of the register definitions.
  ArrayRef<WriteState> getDefs() const { return Defs; }
  /// Return a mutable view of the register uses.
  /// @return A mutable view of the register uses.
  SmallVectorImpl<ReadState> &getUses() { return Uses; }
  /// Return a const view of the register uses.
  /// @return A const view of the register uses.
  ArrayRef<ReadState> getUses() const { return Uses; }
  /// Return the instruction descriptor.
  /// @return The instruction descriptor.
  const InstrDesc &getDesc() const { return Desc; }

  /// Return the maximum latency from the descriptor.
  /// @return The maximum latency from the descriptor.
  unsigned getLatency() const { return Desc.MaxLatency; }
  /// Return the number of micro-ops from the descriptor.
  /// @return The number of micro-ops from the descriptor.
  unsigned getNumMicroOps() const { return Desc.NumMicroOps; }
  /// Return the instruction opcode.
  /// @return The instruction opcode.
  unsigned getOpcode() const { return Opcode; }
  /// Return true if this instruction is a load barrier.
  /// @return True if this instruction is a load barrier.
  bool isALoadBarrier() const { return IsALoadBarrier; }
  /// Return true if this instruction is a store barrier.
  /// @return True if this instruction is a store barrier.
  bool isAStoreBarrier() const { return IsAStoreBarrier; }
  /// Set whether this instruction is a load barrier.
  /// @param IsBarrier True to mark this instruction as a load barrier.
  void setLoadBarrier(bool IsBarrier) { IsALoadBarrier = IsBarrier; }
  /// Set whether this instruction is a store barrier.
  /// @param IsBarrier True to mark this instruction as a store barrier.
  void setStoreBarrier(bool IsBarrier) { IsAStoreBarrier = IsBarrier; }

  /// Return the MCAOperand which corresponds to index Idx within the original
  /// MCInst.
  /// @param Idx Operand index in the original MCInst.
  /// @return Pointer to the matching MCAOperand, or nullptr if not found.
  const MCAOperand *getOperand(const unsigned Idx) const {
    auto It = llvm::find_if(Operands, [&Idx](const MCAOperand &Op) {
      return Op.getIndex() == Idx;
    });
    if (It == Operands.end())
      return nullptr;
    return &(*It);
  }
  /// Return the number of MCA operands attached to this instruction.
  /// @return The number of MCA operands attached to this instruction.
  unsigned getNumOperands() const { return Operands.size(); }
  /// Append MCA operand \p Op to this instruction.
  /// @param Op Operand to append.
  void addOperand(const MCAOperand Op) { Operands.push_back(Op); }

  /// Return true if any register definition still has dependent users.
  /// @return True if any register definition still has dependent users.
  bool hasDependentUsers() const {
    return any_of(Defs,
                  [](const WriteState &Def) { return Def.getNumUsers() > 0; });
  }

  /// Return the total number of dependent users across all definitions.
  /// @return The total number of dependent users across all definitions.
  unsigned getNumUsers() const {
    unsigned NumUsers = 0;
    for (const WriteState &Def : Defs)
      NumUsers += Def.getNumUsers();
    return NumUsers;
  }

  /// Return true if this instruction is a candidate for move elimination.
  /// @return True if this instruction is a candidate for move elimination.
  bool isOptimizableMove() const { return IsOptimizableMove; }
  /// Mark this instruction as a move-elimination candidate.
  void setOptimizableMove() { IsOptimizableMove = true; }
  /// Clear the move-elimination candidate flag.
  void clearOptimizableMove() { IsOptimizableMove = false; }
  /// Return true if this instruction may load or store memory.
  /// @return True if this instruction may load or store memory.
  bool isMemOp() const { return MayLoad || MayStore; }

  /// Set whether this instruction may load from memory.
  /// @param newVal New MayLoad flag value.
  void setMayLoad(bool newVal) { MayLoad = newVal; }
  /// Set whether this instruction may store to memory.
  /// @param newVal New MayStore flag value.
  void setMayStore(bool newVal) { MayStore = newVal; }
  /// Set whether this instruction has side effects.
  /// @param newVal New HasSideEffects flag value.
  void setHasSideEffects(bool newVal) { HasSideEffects = newVal; }
  /// Set whether this instruction begins an instruction group.
  /// @param newVal New BeginGroup flag value.
  void setBeginGroup(bool newVal) { BeginGroup = newVal; }
  /// Set whether this instruction ends an instruction group.
  /// @param newVal New EndGroup flag value.
  void setEndGroup(bool newVal) { EndGroup = newVal; }
  /// Set whether this instruction may retire out of order.
  /// @param newVal New RetireOOO flag value.
  void setRetireOOO(bool newVal) { RetireOOO = newVal; }

  /// Return true if this instruction may load from memory.
  /// @return True if this instruction may load from memory.
  bool getMayLoad() const { return MayLoad; }
  /// Return true if this instruction may store to memory.
  /// @return True if this instruction may store to memory.
  bool getMayStore() const { return MayStore; }
  /// Return true if this instruction has side effects.
  /// @return True if this instruction has side effects.
  bool getHasSideEffects() const { return HasSideEffects; }
  /// Return true if this instruction begins an instruction group.
  /// @return True if this instruction begins an instruction group.
  bool getBeginGroup() const { return BeginGroup; }
  /// Return true if this instruction ends an instruction group.
  /// @return True if this instruction ends an instruction group.
  bool getEndGroup() const { return EndGroup; }
  /// Return true if this instruction may retire out of order.
  /// @return True if this instruction may retire out of order.
  bool getRetireOOO() const { return RetireOOO; }
};

/// An instruction propagated through the simulated instruction pipeline.
///
/// This class is used to monitor changes to the internal state of instructions
/// that are sent to the various components of the simulated hardware pipeline.
class Instruction : public InstructionBase {
  enum InstrStage {
    IS_INVALID,    // Instruction in an invalid state.
    IS_DISPATCHED, // Instruction dispatched but operands are not ready.
    IS_PENDING,    // Instruction is not ready, but operand latency is known.
    IS_READY,      // Instruction dispatched and operands ready.
    IS_EXECUTING,  // Instruction issued.
    IS_EXECUTED,   // Instruction executed. Values are written back.
    IS_RETIRED     // Instruction retired.
  };

  // The current instruction stage.
  enum InstrStage Stage;

  // This value defaults to the instruction latency. This instruction is
  // considered executed when field CyclesLeft goes to zero.
  int CyclesLeft;

  // Retire Unit token ID for this instruction.
  unsigned RCUTokenID;

  // LS token ID for this instruction.
  // This field is set to the invalid null token if this is not a memory
  // operation.
  unsigned LSUTokenID;

  // A resource mask which identifies buffered resources consumed by this
  // instruction at dispatch stage. In the absence of macro-fusion, this value
  // should always match the value of field `UsedBuffers` from the instruction
  // descriptor (see field InstrBase::Desc).
  uint64_t UsedBuffers;

  // Critical register dependency.
  CriticalDependency CriticalRegDep;

  // Critical memory dependency.
  CriticalDependency CriticalMemDep;

  // A bitmask of busy processor resource units.
  // This field is set to zero only if execution is not delayed during this
  // cycle because of unavailable pipeline resources.
  uint64_t CriticalResourceMask;

  // True if this instruction has been optimized at register renaming stage.
  bool IsEliminated;

public:
  /// Construct a pipeline instruction from descriptor \p D and opcode \p Opcode.
  /// @param D Instruction descriptor.
  /// @param Opcode Instruction opcode.
  Instruction(const InstrDesc &D, const unsigned Opcode)
      : InstructionBase(D, Opcode), Stage(IS_INVALID),
        CyclesLeft(UNKNOWN_CYCLES), RCUTokenID(0), LSUTokenID(0),
        UsedBuffers(D.UsedBuffers), CriticalRegDep(), CriticalMemDep(),
        CriticalResourceMask(0), IsEliminated(false) {}

  /// Reset pipeline state so this instruction can be reused.
  LLVM_ABI void reset();

  /// Return the Retire Control Unit token ID.
  /// @return The Retire Control Unit token ID.
  unsigned getRCUTokenID() const { return RCUTokenID; }
  /// Return the Load/Store Unit token ID.
  /// @return The Load/Store Unit token ID.
  unsigned getLSUTokenID() const { return LSUTokenID; }
  /// Set the Load/Store Unit token ID to \p LSUTok.
  /// @param LSUTok Load/Store Unit token identifier.
  void setLSUTokenID(unsigned LSUTok) { LSUTokenID = LSUTok; }

  /// Return the bitmask of buffered resources used at dispatch.
  /// @return The bitmask of buffered resources used at dispatch.
  uint64_t getUsedBuffers() const { return UsedBuffers; }
  /// Set the bitmask of buffered resources used at dispatch.
  /// @param Mask Bitmask of used hardware buffers.
  void setUsedBuffers(uint64_t Mask) { UsedBuffers = Mask; }
  /// Clear the bitmask of buffered resources.
  void clearUsedBuffers() { UsedBuffers = 0ULL; }

  /// Return cycles remaining until execution completes.
  /// @return Cycles remaining until execution completes.
  int getCyclesLeft() const { return CyclesLeft; }

  /// Transition to the dispatch stage and assign Retire Unit token \p RCUTokenID.
  ///
  /// The RCUToken is used to track the completion of every register write
  /// performed by this instruction.
  /// @param RCUTokenID Retire Control Unit token for this instruction.
  LLVM_ABI void dispatch(unsigned RCUTokenID);

  /// Issue this instruction: transition to executing and update definitions.
  /// @param IID Instruction identifier used when notifying writes.
  LLVM_ABI void execute(unsigned IID);

  /// Recompute stage after dispatch when operand readiness may have changed.
  ///
  /// Force a transition from the IS_DISPATCHED state to the IS_READY or
  /// IS_PENDING state. State transitions normally occur either at the beginning
  /// of a new cycle (see method cycleEvent()), or as a result of another issue
  /// event. This method is called every time the instruction might have changed
  /// in state. It internally delegates to method updateDispatched() and
  /// updateWaiting().
  LLVM_ABI void update();
  /// Update state while still in the dispatched stage; return true if changed.
  /// @return True if the instruction stage changed.
  LLVM_ABI bool updateDispatched();
  /// Update state while pending on known latency; return true if changed.
  /// @return True if the instruction stage changed.
  LLVM_ABI bool updatePending();

  /// Return true if the instruction is in the invalid stage.
  /// @return True if the instruction is in the invalid stage.
  bool isInvalid() const { return Stage == IS_INVALID; }
  /// Return true if the instruction has been dispatched.
  /// @return True if the instruction has been dispatched.
  bool isDispatched() const { return Stage == IS_DISPATCHED; }
  /// Return true if the instruction is pending on known operand latency.
  /// @return True if the instruction is pending on known operand latency.
  bool isPending() const { return Stage == IS_PENDING; }
  /// Return true if the instruction is ready to issue.
  /// @return True if the instruction is ready to issue.
  bool isReady() const { return Stage == IS_READY; }
  /// Return true if the instruction is currently executing.
  /// @return True if the instruction is currently executing.
  bool isExecuting() const { return Stage == IS_EXECUTING; }
  /// Return true if the instruction has finished executing.
  /// @return True if the instruction has finished executing.
  bool isExecuted() const { return Stage == IS_EXECUTED; }
  /// Return true if the instruction has retired.
  /// @return True if the instruction has retired.
  bool isRetired() const { return Stage == IS_RETIRED; }
  /// Return true if the instruction was eliminated at register renaming.
  /// @return True if the instruction was eliminated at register renaming.
  bool isEliminated() const { return IsEliminated; }

  /// Force a transition from dispatched directly to executed.
  LLVM_ABI void forceExecuted();
  /// Mark this instruction as eliminated at register renaming.
  void setEliminated() { IsEliminated = true; }

  /// Transition from executed to retired.
  void retire() {
    assert(isExecuted() && "Instruction is in an invalid state!");
    Stage = IS_RETIRED;
  }

  /// Return the critical register dependency for this instruction.
  /// @return The critical register dependency for this instruction.
  const CriticalDependency &getCriticalRegDep() const { return CriticalRegDep; }
  /// Return the critical memory dependency for this instruction.
  /// @return The critical memory dependency for this instruction.
  const CriticalDependency &getCriticalMemDep() const { return CriticalMemDep; }
  /// Compute and cache the critical register dependency from operand state.
  /// @return The computed critical register dependency.
  LLVM_ABI const CriticalDependency &computeCriticalRegDep();
  /// Set the critical memory dependency to \p MemDep.
  /// @param MemDep Critical memory dependency to record.
  void setCriticalMemDep(const CriticalDependency &MemDep) {
    CriticalMemDep = MemDep;
  }

  /// Return the bitmask of busy processor resources delaying execution.
  /// @return The bitmask of busy processor resources delaying execution.
  uint64_t getCriticalResourceMask() const { return CriticalResourceMask; }
  /// Set the bitmask of busy processor resources delaying execution.
  /// @param ResourceMask Bitmask of unavailable pipeline resources.
  void setCriticalResourceMask(uint64_t ResourceMask) {
    CriticalResourceMask = ResourceMask;
  }

  /// Advance instruction state by one simulated cycle.
  LLVM_ABI void cycleEvent();
};

/// A reference to an instruction paired with its SourceMgr index.
///
/// An InstRef contains both a SourceMgr index and Instruction pair. The index
/// is used as a unique identifier for the instruction. MCA will make use of
/// this index as a key throughout MCA.
class InstRef {
  std::pair<unsigned, Instruction *> Data;

public:
  /// Construct a null instruction reference.
  InstRef() : Data(std::make_pair(0, nullptr)) {}
  /// Construct a reference to instruction \p I at source index \p Index.
  /// @param Index SourceMgr index used as the instruction identifier.
  /// @param I Instruction being referenced.
  InstRef(unsigned Index, Instruction *I) : Data(std::make_pair(Index, I)) {}

  /// Return true if both index and instruction pointer match \p Other.
  /// @param Other Reference to compare against.
  /// @return True if both index and instruction pointer match \p Other.
  bool operator==(const InstRef &Other) const { return Data == Other.Data; }
  /// Return true if this reference differs from \p Other.
  /// @param Other Reference to compare against.
  /// @return True if this reference differs from \p Other.
  bool operator!=(const InstRef &Other) const { return Data != Other.Data; }
  /// Order references by ascending SourceMgr index.
  /// @param Other Reference whose index is compared.
  /// @return True if this source index is less than \p Other's.
  bool operator<(const InstRef &Other) const {
    return Data.first < Other.Data.first;
  }

  /// Return the SourceMgr index for this instruction.
  /// @return The SourceMgr index for this instruction.
  unsigned getSourceIndex() const { return Data.first; }
  /// Return the referenced instruction.
  /// @return Pointer to the referenced instruction.
  Instruction *getInstruction() { return Data.second; }
  /// Return the referenced instruction.
  /// @return Const pointer to the referenced instruction.
  const Instruction *getInstruction() const { return Data.second; }

  /// Returns true if this references a valid instruction.
  /// @return True if this references a valid instruction.
  explicit operator bool() const { return Data.second != nullptr; }

  /// Invalidate this reference.
  void invalidate() { Data.second = nullptr; }

#ifndef NDEBUG
  /// Print the source index of this reference to \p OS.
  /// @param OS Output stream.
  void print(raw_ostream &OS) const { OS << getSourceIndex(); }
#endif
};

#ifndef NDEBUG
/// Write InstRef \p IR to stream \p OS.
/// @param OS Output stream.
/// @param IR Instruction reference to print.
/// @return Reference to the output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const InstRef &IR) {
  IR.print(OS);
  return OS;
}
#endif

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_INSTRUCTION_H
