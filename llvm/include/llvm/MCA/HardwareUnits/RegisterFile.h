//===--------------------- RegisterFile.h -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a register mapping file class.  This class is responsible
/// for managing hardware register files and the tracking of data dependencies
/// between registers.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MCA_HARDWAREUNITS_REGISTERFILE_H
#define LLVM_MCA_HARDWAREUNITS_REGISTERFILE_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/MC/MCSchedule.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MCA/HardwareUnits/HardwareUnit.h"

namespace llvm {
namespace mca {

class ReadState;
class WriteState;
class Instruction;

/// A reference to a register write.
///
/// This class is mainly used by the register file to describe register
/// mappings. It correlates a register write to the source index of the
/// defining instruction.
class WriteRef {
  unsigned IID;
  unsigned WriteBackCycle;
  unsigned WriteResID;
  MCPhysReg RegisterID;
  WriteState *Write;

  LLVM_ABI static const unsigned INVALID_IID;

public:
  /// Construct an invalid write reference.
  WriteRef()
      : IID(INVALID_IID), WriteBackCycle(), WriteResID(), RegisterID(),
        Write() {}
  /// Construct a write reference for instruction \p SourceIndex and state \p WS.
  ///
  /// \param SourceIndex Source index of the defining instruction.
  /// \param WS          Write state describing this register definition.
  LLVM_ABI WriteRef(unsigned SourceIndex, WriteState *WS);

  /// Return the source index of the defining instruction.
  ///
  /// \return Source index of the defining instruction.
  unsigned getSourceIndex() const { return IID; }
  /// Return the cycle in which this write was written back.
  ///
  /// \return Cycle in which this write was written back.
  LLVM_ABI unsigned getWriteBackCycle() const;

  /// Return a const pointer to the underlying write state.
  ///
  /// \return Const pointer to the underlying write state, or null if none.
  const WriteState *getWriteState() const { return Write; }
  /// Return a mutable pointer to the underlying write state.
  ///
  /// \return Mutable pointer to the underlying write state, or null if none.
  WriteState *getWriteState() { return Write; }
  /// Return the write resource identifier associated with this write.
  ///
  /// \return Write resource identifier for this write.
  LLVM_ABI unsigned getWriteResourceID() const;
  /// Return the physical register written by this reference.
  ///
  /// \return Physical register written by this reference.
  LLVM_ABI MCPhysReg getRegisterID() const;

  /// Mark this write as committed and clear the write-state pointer.
  LLVM_ABI void commit();
  /// Record that the defining instruction executed in cycle \p Cycle.
  ///
  /// \param Cycle Cycle in which the instruction finished execution.
  LLVM_ABI void notifyExecuted(unsigned Cycle);

  /// Return true if the write-back cycle for this write is known.
  ///
  /// \return True if the write-back cycle for this write is known.
  LLVM_ABI bool hasKnownWriteBackCycle() const;
  /// Return true if this write produces a known-zero register value.
  ///
  /// \return True if this write produces a known-zero register value.
  LLVM_ABI bool isWriteZero() const;
  /// Return true if this write reference refers to a valid definition.
  ///
  /// \return True if this write reference refers to a valid definition.
  bool isValid() const { return getSourceIndex() != INVALID_IID; }

  /// Returns true if this register write has been executed, and the new
  /// register value is therefore available to users.
  ///
  /// \return True if the new register value is available to users.
  bool isAvailable() const { return hasKnownWriteBackCycle(); }

  /// Return true if both references point to the same write state.
  ///
  /// \param Other Write reference to compare against.
  /// \return True if both references point to the same write state.
  bool operator==(const WriteRef &Other) const {
    return Write && Other.Write && Write == Other.Write;
  }

#ifndef NDEBUG
  /// Dump this write reference for debugging.
  void dump() const;
#endif
};

/// Manages hardware register files, and tracks register definitions for
/// register renaming purposes.
class RegisterFile : public HardwareUnit {
  const MCRegisterInfo &MRI;

  // class RegisterMappingTracker is a  physical register file (PRF) descriptor.
  // There is one RegisterMappingTracker for every PRF definition in the
  // scheduling model.
  //
  // An instance of RegisterMappingTracker tracks the number of physical
  // registers available for renaming. It also tracks  the number of register
  // moves eliminated per cycle.
  struct RegisterMappingTracker {
    // The total number of physical registers that are available in this
    // register file for register renaming purpouses.  A value of zero for this
    // field means: this register file has an unbounded number of physical
    // registers.
    const unsigned NumPhysRegs;
    // Number of physical registers that are currently in use.
    unsigned NumUsedPhysRegs;

    // Maximum number of register moves that can be eliminated by this PRF every
    // cycle. A value of zero means that there is no limit in the number of
    // moves which can be eliminated every cycle.
    const unsigned MaxMoveEliminatedPerCycle;

    // Number of register moves eliminated during this cycle.
    //
    // This value is increased by one every time a register move is eliminated.
    // Every new cycle, this value is reset to zero.
    // A move can be eliminated only if MaxMoveEliminatedPerCycle is zero, or if
    // NumMoveEliminated is less than MaxMoveEliminatedPerCycle.
    unsigned NumMoveEliminated;

    // If set, move elimination is restricted to zero-register moves only.
    bool AllowZeroMoveEliminationOnly;

    RegisterMappingTracker(unsigned NumPhysRegisters,
                           unsigned MaxMoveEliminated = 0U,
                           bool AllowZeroMoveElimOnly = false)
        : NumPhysRegs(NumPhysRegisters), NumUsedPhysRegs(0),
          MaxMoveEliminatedPerCycle(MaxMoveEliminated), NumMoveEliminated(0U),
          AllowZeroMoveEliminationOnly(AllowZeroMoveElimOnly) {}
  };

  // A vector of register file descriptors.  This set always contains at least
  // one entry. Entry at index #0 is reserved.  That entry describes a register
  // file with an unbounded number of physical registers that "sees" all the
  // hardware registers declared by the target (i.e. all the register
  // definitions in the target specific `XYZRegisterInfo.td` - where `XYZ` is
  // the target name).
  //
  // Users can limit the number of physical registers that are available in
  // register file #0 specifying command line flag `-register-file-size=<uint>`.
  SmallVector<RegisterMappingTracker, 4> RegisterFiles;

  // This type is used to propagate information about the owner of a register,
  // and the cost of allocating it in the PRF. Register cost is defined as the
  // number of physical registers consumed by the PRF to allocate a user
  // register.
  //
  // For example: on X86 BtVer2, a YMM register consumes 2 128-bit physical
  // registers. So, the cost of allocating a YMM register in BtVer2 is 2.
  using IndexPlusCostPairTy = std::pair<unsigned, unsigned>;

  // Struct RegisterRenamingInfo is used to map logical registers to register
  // files.
  //
  // There is a RegisterRenamingInfo object for every logical register defined
  // by the target. RegisteRenamingInfo objects are stored into vector
  // `RegisterMappings`, and MCPhysReg IDs can be used to reference
  // elements in that vector.
  //
  // Each RegisterRenamingInfo is owned by a PRF, and field `IndexPlusCost`
  // specifies both the owning PRF, as well as the number of physical registers
  // consumed at register renaming stage.
  //
  // Field `AllowMoveElimination` is set for registers that are used as
  // destination by optimizable register moves.
  //
  // Field `AliasRegID` is set by writes from register moves that have been
  // eliminated at register renaming stage. A move eliminated at register
  // renaming stage is effectively bypassed, and its write aliases the source
  // register definition.
  struct RegisterRenamingInfo {
    IndexPlusCostPairTy IndexPlusCost;
    MCPhysReg RenameAs;
    MCPhysReg AliasRegID;
    bool AllowMoveElimination;
    RegisterRenamingInfo()
        : IndexPlusCost(std::make_pair(0U, 1U)), RenameAs(0U), AliasRegID(0U),
          AllowMoveElimination(false) {}
  };

  // RegisterMapping objects are mainly used to track physical register
  // definitions and resolve data dependencies.
  //
  // Every register declared by the Target is associated with an instance of
  // RegisterMapping. RegisterMapping objects keep track of writes to a logical
  // register.  That information is used by class RegisterFile to resolve data
  // dependencies, and correctly set latencies for register uses.
  //
  // This implementation does not allow overlapping register files. The only
  // register file that is allowed to overlap with other register files is
  // register file #0. If we exclude register #0, every register is "owned" by
  // at most one register file.
  using RegisterMapping = std::pair<WriteRef, RegisterRenamingInfo>;

  // There is one entry per each register defined by the target.
  std::vector<RegisterMapping> RegisterMappings;

  // Used to track zero registers. There is one bit for each register defined by
  // the target. Bits are set for registers that are known to be zero.
  APInt ZeroRegisters;

  unsigned CurrentCycle;

  // This method creates a new register file descriptor.
  // The new register file owns all of the registers declared by register
  // classes in the 'RegisterClasses' set.
  //
  // Processor models allow the definition of RegisterFile(s) via tablegen. For
  // example, this is a tablegen definition for a x86 register file for
  // XMM[0-15] and YMM[0-15], that allows up to 60 renames (each rename costs 1
  // physical register).
  //
  //    def FPRegisterFile : RegisterFile<60, [VR128RegClass, VR256RegClass]>
  //
  // Here FPRegisterFile contains all the registers defined by register class
  // VR128RegClass and VR256RegClass. FPRegisterFile implements 60
  // registers which can be used for register renaming purpose.
  void addRegisterFile(const MCRegisterFileDesc &RF,
                       ArrayRef<MCRegisterCostEntry> Entries);

  // Consumes physical registers in each register file specified by the
  // `IndexPlusCostPairTy`. This method is called from `addRegisterMapping()`.
  void allocatePhysRegs(const RegisterRenamingInfo &Entry,
                        MutableArrayRef<unsigned> UsedPhysRegs);

  // Releases previously allocated physical registers from the register file(s).
  // This method is called from `invalidateRegisterMapping()`.
  void freePhysRegs(const RegisterRenamingInfo &Entry,
                    MutableArrayRef<unsigned> FreedPhysRegs);

  // Create an instance of RegisterMappingTracker for every register file
  // specified by the processor model.
  // If no register file is specified, then this method creates a default
  // register file with an unbounded number of physical registers.
  void initialize(const MCSchedModel &SM, unsigned NumRegs);

public:
  /// Construct a register file for scheduling model \p SM.
  ///
  /// \param SM      Scheduling model that declares register files.
  /// \param mri     Target register information.
  /// \param NumRegs Optional size limit for the unbounded register file #0;
  ///                zero means unbounded.
  LLVM_ABI RegisterFile(const MCSchedModel &SM, const MCRegisterInfo &mri,
                        unsigned NumRegs = 0);

  /// Collect writes that are in a RAW dependency with \p RS.
  ///
  /// \param STI             Subtarget information used for register queries.
  /// \param RS              Register read whose RAW producers to collect.
  /// \param Writes          Output vector filled with in-flight producer writes.
  /// \param CommittedWrites Output vector filled with already-committed
  ///                        producer writes.
  LLVM_ABI void collectWrites(const MCSubtargetInfo &STI, const ReadState &RS,
                              SmallVectorImpl<WriteRef> &Writes,
                              SmallVectorImpl<WriteRef> &CommittedWrites) const;
  /// Describes a read-after-write hazard on a physical register.
  struct RAWHazard {
    /// Physical register that carries the RAW dependency.
    MCPhysReg RegisterID = 0;
    /// Cycles remaining until the producer write becomes available.
    ///
    /// A negative value means the remaining latency is still unknown.
    int CyclesLeft = 0;

    /// Construct an invalid RAW hazard.
    RAWHazard() = default;
    /// Return true if this hazard identifies a valid register.
    ///
    /// \return True if this hazard identifies a valid register.
    bool isValid() const { return RegisterID; }
    /// Return true if the remaining stall cycles are not yet known.
    ///
    /// \return True if the remaining stall cycles are not yet known.
    bool hasUnknownCycles() const { return CyclesLeft < 0; }
  };

  /// Check for RAW hazards affecting register read \p RS.
  ///
  /// \param STI Subtarget information used for register queries.
  /// \param RS  Register read to check for outstanding RAW producers.
  /// \return A RAWHazard describing the critical producer, if any.
  LLVM_ABI RAWHazard checkRAWHazards(const MCSubtargetInfo &STI,
                                     const ReadState &RS) const;

  /// Insert a new register definition into the register mappings.
  ///
  /// This method is also responsible for updating the number of allocated
  /// physical registers in each register file modified by the write. No
  /// physical register is allocated if this write is from a zero-idiom.
  ///
  /// \param Write         Write reference describing the new definition.
  /// \param UsedPhysRegs  Per-register-file counts of newly allocated physical
  ///                      registers.
  LLVM_ABI void addRegisterWrite(WriteRef Write,
                                 MutableArrayRef<unsigned> UsedPhysRegs);

  /// Collect writes that are in a data dependency with \p RS, and update \p RS.
  ///
  /// \param RS  Register read whose producers and readiness should be updated.
  /// \param STI Subtarget information used for register queries.
  LLVM_ABI void addRegisterRead(ReadState &RS,
                                const MCSubtargetInfo &STI) const;

  /// Remove write \p WS from the register mappings.
  ///
  /// Physical registers may be released to reflect this update. No registers
  /// are released if this write is from a zero-idiom.
  ///
  /// \param WS            Write state to remove from the mappings.
  /// \param FreedPhysRegs Per-register-file counts of released physical
  ///                      registers.
  LLVM_ABI void removeRegisterWrite(const WriteState &WS,
                                    MutableArrayRef<unsigned> FreedPhysRegs);

  /// Return true if the PRF at index \p PRFIndex can eliminate a move from \p RS
  /// to \p WS.
  ///
  /// \param WS       Destination write of the candidate move.
  /// \param RS       Source read of the candidate move.
  /// \param PRFIndex Index of the physical register file to query.
  /// \return True if the PRF can eliminate the move from \p RS to \p WS.
  LLVM_ABI bool canEliminateMove(const WriteState &WS, const ReadState &RS,
                                 unsigned PRFIndex) const;

  /// Try to eliminate a register move or swap at register renaming.
  ///
  /// Returns true if this instruction can be fully eliminated at register
  /// renaming stage. On success, this method updates the internal state of each
  /// WriteState by setting flag `WS.isEliminated`, and by propagating the zero
  /// flag for known zero registers. It internally uses `canEliminateMove` to
  /// determine if a read/write pair can be eliminated. By default, it assumes a
  /// register swap if there is more than one register definition.
  ///
  /// \param Writes Write operands of the candidate move or swap.
  /// \param Reads  Read operands of the candidate move or swap.
  /// \return True if the instruction can be fully eliminated at renaming.
  LLVM_ABI bool tryEliminateMoveOrSwap(MutableArrayRef<WriteState> Writes,
                                       MutableArrayRef<ReadState> Reads);

  /// Check whether there are enough physical registers for \p Regs.
  ///
  /// Returns a "response mask" where each bit represents the response from a
  /// different register file. A mask of all zeroes means that all register
  /// files are available. Otherwise, the mask can be used to identify which
  /// register file was busy. This semantic allows us to classify dispatch
  /// stalls caused by the lack of register file resources.
  ///
  /// Current implementation can simulate up to 32 register files (including the
  /// special register file at index #0).
  ///
  /// \param Regs Logical registers that would be allocated on dispatch.
  /// \return Zero if all register files can allocate \p Regs; otherwise a busy
  ///         mask.
  LLVM_ABI unsigned isAvailable(ArrayRef<MCPhysReg> Regs) const;

  /// Return the number of PRFs implemented by this processor.
  ///
  /// \return Number of physical register files implemented by this processor.
  unsigned getNumRegisterFiles() const { return RegisterFiles.size(); }

  /// Return how many cycles have elapsed since write \p WR was written back.
  ///
  /// \param WR Write reference whose write-back cycle is known.
  /// \return Cycles elapsed since \p WR was written back.
  LLVM_ABI unsigned getElapsedCyclesFromWriteBack(const WriteRef &WR) const;

  /// Update register mappings when instruction \p IS finishes execution.
  ///
  /// \param IS Instruction that has just been executed.
  LLVM_ABI void onInstructionExecuted(Instruction *IS);

  /// Notify each PRF that a new cycle just started.
  LLVM_ABI void cycleStart();

  /// Advance the register file to the next cycle.
  void cycleEnd() { ++CurrentCycle; }

#ifndef NDEBUG
  /// Dump register file state for debugging.
  void dump() const;
#endif
};

} // namespace mca
} // namespace llvm

#endif // LLVM_MCA_HARDWAREUNITS_REGISTERFILE_H
