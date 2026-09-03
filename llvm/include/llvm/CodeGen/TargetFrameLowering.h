//===-- llvm/CodeGen/TargetFrameLowering.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Interface to describe the layout of a stack frame on the target machine.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETFRAMELOWERING_H
#define LLVM_CODEGEN_TARGETFRAMELOWERING_H

#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineOptimizationRemarkEmitter.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/TypeSize.h"
#include <vector>

namespace llvm {
  class BitVector;
  class CalleeSavedInfo;
  class MachineFunction;
  class RegScavenger;

/// Identifiers for distinct target stack allocation spaces.
namespace TargetStackID {
/// Kind of stack ID used when allocating frame objects.
enum Value {
  /// Default process stack.
  Default = 0,
  /// AMDGPU SGPR spill stack.
  SGPRSpill = 1,
  /// Scalable-vector stack area.
  ScalableVector = 2,
  /// WebAssembly local allocation space.
  WasmLocal = 3,
  /// Scalable-predicate-vector stack area.
  ScalablePredicateVector = 4,
  /// AVR special alignment stack ID.
  AvrAlign = 5,
  /// Object that should not be allocated a stack slot.
  NoAlloc = 255
};
}

/// Information about stack frame layout on the target.
///
/// It holds the direction of stack growth, the known stack alignment on entry
/// to each function, and the offset to the locals area.
///
/// The offset to the local area is the offset from the stack pointer on
/// function entry to the first location where function data (local variables,
/// spill locations) can be stored.
class LLVM_ABI TargetFrameLowering {
public:
  /// Direction in which the stack grows.
  enum StackDirection {
    /// Adding to the stack increases the stack address.
    StackGrowsUp,
    /// Adding to the stack decreases the stack address.
    StackGrowsDown
  };

  /// Maps a callee-saved register to a stack slot with a fixed offset.
  struct SpillSlot {
    /// Callee-saved register that must spill at a fixed offset.
    unsigned Reg;
    /// Offset relative to the stack pointer on function entry.
    int64_t Offset;
  };

  /// Frame-base location used when emitting DWARF unwind / debug info.
  struct DwarfFrameBase {
    /// Selected frame-base encoding kind.
    ///
    /// The frame base may be either a register (the default), the CFA with an
    /// offset, or a WebAssembly-specific location description.
    enum FrameBaseKind {
      /// Frame base is a register.
      Register,
      /// Frame base is the CFA plus an offset.
      CFA,
      /// Frame base is a WebAssembly-specific location.
      WasmFrameBase
    } Kind;
    /// WebAssembly-specific frame-base location description.
    struct WasmFrameBase {
      /// Wasm local, global, or value-stack kind tag.
      unsigned Kind;
      /// Index of the Wasm local, global, or value-stack slot.
      unsigned Index;
    };
    /// Discriminated payload for the selected \c Kind.
    union {
      /// Register number used with \c FrameBaseKind::Register.
      unsigned Reg;
      /// CFA offset used with \c FrameBaseKind::CFA.
      int64_t Offset;
      /// Wasm location used with \c FrameBaseKind::WasmFrameBase.
      struct WasmFrameBase WasmLoc;
    } Location;
  };

private:
  StackDirection StackDir;
  Align StackAlignment;
  Align TransientStackAlignment;
  int LocalAreaOffset;
  bool StackRealignable;
public:
  /// Construct target frame-lowering information.
  ///
  /// \param D Direction the stack grows.
  /// \param StackAl Required stack alignment on function entry.
  /// \param LAO Offset from the entry stack pointer to the local area.
  /// \param TransAl Alignment that must be maintained even between calls.
  /// \param StackReal Whether the stack may be realigned.
  TargetFrameLowering(StackDirection D, Align StackAl, int LAO,
                      Align TransAl = Align(1), bool StackReal = true)
      : StackDir(D), StackAlignment(StackAl), TransientStackAlignment(TransAl),
        LocalAreaOffset(LAO), StackRealignable(StackReal) {}

  /// Destroy the target frame-lowering object.
  virtual ~TargetFrameLowering();

  // These methods return information that describes the abstract stack layout
  // of the target machine.

  /// getStackGrowthDirection - Return the direction the stack grows
  ///
  /// \return Whether the stack grows up or down.
  StackDirection getStackGrowthDirection() const { return StackDir; }

  /// Return how many bytes the stack pointer must be aligned to on entry.
  ///
  /// Typically, this is the largest alignment for any data object in the
  /// target.
  ///
  /// \return The required stack alignment in bytes on function entry.
  unsigned getStackAlignment() const { return StackAlignment.value(); }
  /// Return the stack alignment required on function entry.
  ///
  /// Typically, this is the largest alignment for any data object in the
  /// target.
  ///
  /// \return The required stack alignment on function entry.
  Align getStackAlign() const { return StackAlignment; }

  /// getStackThreshold - Return the maximum stack size
  ///
  /// \return The maximum stack size in bytes.
  virtual uint64_t getStackThreshold() const { return UINT_MAX; }

  /// alignSPAdjust - This method aligns the stack adjustment to the correct
  /// alignment.
  ///
  /// \param SPAdj Stack-pointer adjustment to align.
  /// \return \p SPAdj rounded to the required stack alignment.
  int alignSPAdjust(int SPAdj) const {
    if (SPAdj < 0) {
      SPAdj = -alignTo(-SPAdj, StackAlignment);
    } else {
      SPAdj = alignTo(SPAdj, StackAlignment);
    }
    return SPAdj;
  }

  /// getTransientStackAlignment - This method returns the number of bytes to
  /// which the stack pointer must be aligned at all times, even between
  /// calls.
  ///
  /// \return The alignment that must be maintained even between calls.
  Align getTransientStackAlign() const { return TransientStackAlignment; }

  /// isStackRealignable - This method returns whether the stack can be
  /// realigned.
  ///
  /// \return True if the stack may be realigned.
  bool isStackRealignable() const {
    return StackRealignable;
  }

  /// This method returns whether or not it is safe for an object with the
  /// given stack id to be bundled into the local area.
  ///
  /// \param StackId Target stack ID to check.
  /// \return True if objects with \p StackId may be placed in the local area.
  virtual bool isStackIdSafeForLocalArea(unsigned StackId) const {
    return true;
  }

  /// getOffsetOfLocalArea - This method returns the offset of the local area
  /// from the stack pointer on entrance to a function.
  ///
  /// \return The offset from the entry stack pointer to the local area.
  int getOffsetOfLocalArea() const { return LocalAreaOffset; }

  /// Control the placement of special register scavenging spill slots when
  /// allocating a stack frame.
  ///
  /// If this returns true, the frame indexes used by the RegScavenger will be
  /// allocated closest to the incoming stack pointer.
  ///
  /// \param MF Function whose scavenging slots are being placed.
  /// \return True if scavenging frame indexes should be near the incoming SP.
  virtual bool allocateScavengingFrameIndexesNearIncomingSP(
    const MachineFunction &MF) const;

  /// Allow the target to override callee-saved spill slot assignment.
  ///
  /// If implemented, assignCalleeSavedSpillSlots() should assign frame slots
  /// to all CSI entries and return true.  If this method returns false, spill
  /// slots will be assigned using generic implementation.
  /// assignCalleeSavedSpillSlots() may add, delete or rearrange elements of
  /// CSI.
  ///
  /// \param MF Function whose callee-saved spill slots are being assigned.
  /// \param TRI Target register info for \p MF.
  /// \param CSI Callee-saved register descriptors to assign slots for.
  /// \return True if this target assigned all spill slots in \p CSI.
  virtual bool
  assignCalleeSavedSpillSlots(MachineFunction &MF,
                              const TargetRegisterInfo *TRI,
                              std::vector<CalleeSavedInfo> &CSI) const {
    return false;
  }

  /// Return fixed spill slots for callee-saved registers that need them.
  ///
  /// Each entry in this array contains a <register,offset> pair, indicating the
  /// fixed offset from the incoming stack pointer that each register should be
  /// spilled at. If a register is not listed here, the code generator is
  /// allowed to spill it anywhere it chooses.
  ///
  /// \param NumEntries Set to the number of entries in the returned array.
  /// \return An array of fixed callee-saved spill slots, or null if none.
  virtual const SpillSlot *
  getCalleeSavedSpillSlots(unsigned &NumEntries) const {
    NumEntries = 0;
    return nullptr;
  }

  /// targetHandlesStackFrameRounding - Returns true if the target is
  /// responsible for rounding up the stack frame (probably at emitPrologue
  /// time).
  ///
  /// \return True if the target rounds the stack frame itself.
  virtual bool targetHandlesStackFrameRounding() const {
    return false;
  }

  /// Returns true if the target will correctly handle shrink wrapping.
  ///
  /// \param MF Function being considered for shrink wrapping.
  /// \return True if shrink wrapping is supported for \p MF.
  virtual bool enableShrinkWrapping(const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the stack slot holes in the fixed and callee-save stack
  /// area should be used when allocating other stack locations to reduce stack
  /// size.
  ///
  /// \param MF Function whose stack slots may be scavenged.
  /// \return True if stack slot holes in \p MF may be scavenged.
  virtual bool enableStackSlotScavenging(const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the target can safely skip saving callee-saved registers
  /// for noreturn nounwind functions.
  ///
  /// \param MF Function being checked for callee-save skipping.
  /// \return True if callee-saved registers may be skipped for \p MF.
  virtual bool enableCalleeSaveSkip(const MachineFunction &MF) const;

  /// Insert prologue code into the function.
  ///
  /// \param MF Function receiving the prologue.
  /// \param MBB Basic block into which the prologue is inserted.
  virtual void emitPrologue(MachineFunction &MF,
                            MachineBasicBlock &MBB) const = 0;
  /// Insert epilogue code into the function.
  ///
  /// \param MF Function receiving the epilogue.
  /// \param MBB Basic block into which the epilogue is inserted.
  virtual void emitEpilogue(MachineFunction &MF,
                            MachineBasicBlock &MBB) const = 0;

  /// emitZeroCallUsedRegs - Zeros out call used registers.
  ///
  /// \param RegsToZero Bitvector of registers that must be zeroed.
  /// \param MBB Basic block that receives the zeroing instructions.
  /// \param RS Optional register scavenger for temporary registers.
  virtual void emitZeroCallUsedRegs(BitVector RegsToZero,
                                    MachineBasicBlock &MBB,
                                    RegScavenger *RS) const {}

  /// With basic block sections, emit callee saved frame moves for basic blocks
  /// that are in a different section.
  ///
  /// \param MBB Basic block that receives the CFI moves.
  /// \param MBBI Insertion point within \p MBB.
  virtual void
  emitCalleeSavedFrameMovesFullCFA(MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator MBBI) const {}

  /// Returns true if we may need to fix the unwind information for the
  /// function.
  ///
  /// \param MF Function whose CFI may need fixing.
  /// \return True if unwind information for \p MF may need fixing.
  virtual bool enableCFIFixup(const MachineFunction &MF) const;

  /// Return true if unwind info may need to be accurate for every instruction.
  ///
  /// For example, this is needed if the function has an async unwind table.
  ///
  /// \param MF Function whose full CFI fixup requirement is queried.
  /// \return True if unwind info must stay accurate for every instruction.
  virtual bool enableFullCFIFixup(const MachineFunction &MF) const {
    return enableCFIFixup(MF);
  };

  /// Emit CFI instructions that recreate the state of the unwind information
  /// upon function entry.
  ///
  /// \param MBB Basic block that receives the CFI reset.
  virtual void resetCFIToInitialState(MachineBasicBlock &MBB) const {}

  /// Replace a StackProbe stub (if any) with the actual probe code inline
  ///
  /// \param MF Function whose stack probe may be inlined.
  /// \param PrologueMBB Prologue block that should contain the probe.
  virtual void inlineStackProbe(MachineFunction &MF,
                                MachineBasicBlock &PrologueMBB) const {}

  /// Does the stack probe function call return with a modified stack pointer?
  ///
  /// \return True if the stack probe call returns with a modified SP.
  virtual bool stackProbeFunctionModifiesSP() const { return false; }

  /// Adjust the prologue to have the function use segmented stacks. This works
  /// by adding a check even before the "normal" function prologue.
  ///
  /// \param MF Function being adjusted for segmented stacks.
  /// \param PrologueMBB Prologue block that receives the check.
  virtual void adjustForSegmentedStacks(MachineFunction &MF,
                                        MachineBasicBlock &PrologueMBB) const {}

  /// Adjust the prologue to add Erlang Run-Time System (ERTS) specific code in
  /// the assembly prologue to explicitly handle the stack.
  ///
  /// \param MF Function being adjusted for HiPE.
  /// \param PrologueMBB Prologue block that receives the HiPE code.
  virtual void adjustForHiPEPrologue(MachineFunction &MF,
                                     MachineBasicBlock &PrologueMBB) const {}

  /// Spill all callee-saved registers, or report that per-register stores
  /// should be used instead.
  ///
  /// Issues instruction(s) to spill all callee saved registers and returns true
  /// if it isn't possible / profitable to do so by issuing a series of store
  /// instructions via storeRegToStackSlot(). Returns false otherwise.
  ///
  /// \param MBB Block that receives the spill instructions.
  /// \param MI Insertion point within \p MBB.
  /// \param CSI Callee-saved registers to spill.
  /// \param TRI Target register info for the function.
  /// \return True if registers were spilled by this method; false to use stores.
  virtual bool spillCalleeSavedRegisters(MachineBasicBlock &MBB,
                                         MachineBasicBlock::iterator MI,
                                         ArrayRef<CalleeSavedInfo> CSI,
                                         const TargetRegisterInfo *TRI) const {
    return false;
  }

  /// spillCalleeSavedRegister - Default implementation for spilling a single
  /// callee saved register.
  ///
  /// \param SaveBlock Block that receives the spill.
  /// \param MI Insertion point within \p SaveBlock.
  /// \param CS Callee-saved register being spilled.
  /// \param TII Target instruction info used to build the store.
  /// \param TRI Target register info for the function.
  void spillCalleeSavedRegister(MachineBasicBlock &SaveBlock,
                                MachineBasicBlock::iterator MI,
                                const CalleeSavedInfo &CS,
                                const TargetInstrInfo *TII,
                                const TargetRegisterInfo *TRI) const;

  /// Restore all callee-saved registers, or report that per-register loads
  /// should be used instead.
  ///
  /// Issues instruction(s) to restore all callee saved registers and returns
  /// true if it isn't possible / profitable to do so by issuing a series of
  /// load instructions via loadRegToStackSlot().
  /// If it returns true, and any of the registers in CSI is not restored,
  /// it sets the corresponding Restored flag in CSI to false.
  /// Returns false otherwise.
  ///
  /// \param MBB Block that receives the restore instructions.
  /// \param MI Insertion point within \p MBB.
  /// \param CSI Callee-saved registers to restore.
  /// \param TRI Target register info for the function.
  /// \return True if registers were restored by this method; false to use loads.
  virtual bool
  restoreCalleeSavedRegisters(MachineBasicBlock &MBB,
                              MachineBasicBlock::iterator MI,
                              MutableArrayRef<CalleeSavedInfo> CSI,
                              const TargetRegisterInfo *TRI) const {
    return false;
  }

  /// Restore a single callee-saved register.
  ///
  /// Default implementation for restoring a single callee saved register.
  /// Should be called in reverse order. Can insert multiple instructions.
  ///
  /// \param MBB Block that receives the restore.
  /// \param MI Insertion point within \p MBB.
  /// \param CS Callee-saved register being restored.
  /// \param TII Target instruction info used to build the load.
  /// \param TRI Target register info for the function.
  void restoreCalleeSavedRegister(MachineBasicBlock &MBB,
                                  MachineBasicBlock::iterator MI,
                                  const CalleeSavedInfo &CS,
                                  const TargetInstrInfo *TII,
                                  const TargetRegisterInfo *TRI) const;

  /// Return true if the function should have a dedicated frame pointer.
  ///
  /// For most targets this is true only if the function has variable sized
  /// allocas or if frame pointer elimination is disabled. For all targets,
  /// this is false if the function has the naked attribute since there is no
  /// prologue to set up the frame pointer.
  ///
  /// \param MF Function being queried.
  /// \return True if \p MF should use a dedicated frame pointer.
  bool hasFP(const MachineFunction &MF) const {
    return !MF.getFunction().hasFnAttribute(Attribute::Naked) && hasFPImpl(MF);
  }

  /// Return true if the call frame is included as part of the stack frame.
  ///
  /// Under normal circumstances, when a frame pointer is not required, we
  /// reserve argument space for call sites in the function immediately on entry
  /// to the current function. This eliminates the need for add/sub sp brackets
  /// around call sites.
  ///
  /// \param MF Function being queried.
  /// \return True if \p MF reserves call-frame space in its stack frame.
  virtual bool hasReservedCallFrame(const MachineFunction &MF) const {
    return !hasFP(MF);
  }

  /// Return true if call-frame pseudos can be simplified before FI elimination.
  ///
  /// When possible, it's best to simplify the call frame pseudo ops before
  /// doing frame index elimination. This is possible only when frame index
  /// references between the pseudos won't need adjusting for the call frame
  /// adjustments. Normally, that's true if the function has a reserved call
  /// frame or a frame pointer. Some targets (Thumb2, for example) may have more
  /// complicated criteria, however, and can override this behavior.
  ///
  /// \param MF Function being queried.
  /// \return True if call-frame pseudos in \p MF can be simplified early.
  virtual bool canSimplifyCallFramePseudos(const MachineFunction &MF) const {
    return hasReservedCallFrame(MF) || hasFP(MF);
  }

  /// Return whether frame-index operands need resolution for this function.
  ///
  /// Normally, this is required only when the function has any stack objects.
  /// However, targets may want to override this.
  ///
  /// \param MF Function being queried.
  /// \return True if frame-index operands in \p MF need resolution.
  virtual bool needsFrameIndexResolution(const MachineFunction &MF) const;

  /// Return the base register and offset used to reference a frame index.
  ///
  /// The offset is returned directly, and the base register is returned via
  /// FrameReg.
  ///
  /// \param MF Function containing the frame index.
  /// \param FI Frame index being referenced.
  /// \param FrameReg Set to the base register used for the reference.
  /// \return The offset from \p FrameReg to frame index \p FI.
  virtual StackOffset getFrameIndexReference(const MachineFunction &MF, int FI,
                                             Register &FrameReg) const;

  /// Prefer SP when returning the base register for a frame-index reference.
  ///
  /// Same as \c getFrameIndexReference, except that the stack pointer (as
  /// opposed to the frame pointer) will be the preferred value for \p
  /// FrameReg. This is generally used for emitting statepoint or EH tables that
  /// use offsets from RSP.  If \p IgnoreSPUpdates is true, the returned
  /// offset is only guaranteed to be valid with respect to the value of SP at
  /// the end of the prologue.
  ///
  /// \param MF Function containing the frame index.
  /// \param FI Frame index being referenced.
  /// \param FrameReg Set to the preferred base register (typically SP).
  /// \param IgnoreSPUpdates If true, ignore SP updates after the prologue.
  /// \return The offset from \p FrameReg to frame index \p FI.
  virtual StackOffset
  getFrameIndexReferencePreferSP(const MachineFunction &MF, int FI,
                                 Register &FrameReg,
                                 bool IgnoreSPUpdates) const {
    // Always safe to dispatch to getFrameIndexReference.
    return getFrameIndexReference(MF, FI, FrameReg);
  }

  /// Return the offset used to reference a non-local frame index.
  ///
  /// The offset can be from either FP/BP/SP based on which base register is
  /// returned by llvm.localaddress.
  ///
  /// \param MF Function containing the frame index.
  /// \param FI Frame index being referenced.
  /// \return The offset used to reference non-local frame index \p FI.
  virtual StackOffset getNonLocalFrameIndexReference(const MachineFunction &MF,
                                                     int FI) const {
    // By default, dispatch to getFrameIndexReference. Interested targets can
    // override this.
    Register FrameReg;
    return getFrameIndexReference(MF, FI, FrameReg);
  }

  /// Return the offset from SP at function entry to the given frame index.
  ///
  /// This function serves to provide a comparable offset from a single
  /// reference point (the value of the stack-pointer at function entry) that
  /// can be used for analysis.
  ///
  /// \param MF Function containing the frame index.
  /// \param FI Frame index being referenced.
  /// \return The offset from the entry stack pointer to frame index \p FI.
  virtual StackOffset getFrameIndexReferenceFromSP(const MachineFunction &MF,
                                                   int FI) const;

  /// Return registers that must be preserved across the function.
  ///
  /// The value on exit must be the same as the value on entry. A register from
  /// this list does may not need to be saved / reloaded if the function did not
  /// use it.
  ///
  /// \param MF Function whose preserved registers are requested.
  /// \return A null-terminated list of registers that must be preserved.
  const MCPhysReg *getMustPreserveRegisters(const MachineFunction &MF) const;

  /// Determine registers that must be saved/restored in the prolog/epilog.
  ///
  /// Selects which of the registers reported by getMustPreserveRegisters()
  /// must be saved in prolog and reloaded in epilog regardless of whether or
  /// not they were modified by the function.
  ///
  /// \param MF Function being analyzed.
  /// \param CSRegs Callee-saved / must-preserve register list.
  /// \param UncondPrologCSRs Bitvector filled with registers that must spill.
  void determineUncondPrologCalleeSaves(MachineFunction &MF,
                                        const MCPhysReg *CSRegs,
                                        BitVector &UncondPrologCSRs) const;

  /// Returns the callee-saved registers as computed by determineCalleeSaves
  /// in the BitVector \p SavedRegs.
  ///
  /// \param MF Function whose callee-saves are requested.
  /// \param SavedRegs Bitvector filled with the callee-saved registers.
  virtual void getCalleeSaves(const MachineFunction &MF,
                                  BitVector &SavedRegs) const;

  /// Determine which callee-saved registers should actually be saved.
  ///
  /// The default implementation populates the \p SavedRegs bitset with all
  /// registers which are modified in the function; targets may override this
  /// function to save additional registers. This method also sets up the
  /// register scavenger ensuring there is a free register or a frameindex
  /// available. This method should not be called by any passes outside of PEI,
  /// because it may change state passed in by \p MF and \p RS. The preferred
  /// interface outside PEI is getCalleeSaves.
  ///
  /// \param MF Function being analyzed.
  /// \param SavedRegs Bitvector filled with registers that must be saved.
  /// \param RS Optional register scavenger to configure.
  virtual void determineCalleeSaves(MachineFunction &MF, BitVector &SavedRegs,
                                    RegScavenger *RS = nullptr) const;

  /// Hook called immediately before the function's frame layout is finalized.
  ///
  /// Once the frame is finalized, MO_FrameIndex operands are replaced with
  /// direct constants.  This method is optional.
  ///
  /// \param MF Function whose frame is about to be finalized.
  /// \param RS Optional register scavenger for the function.
  virtual void processFunctionBeforeFrameFinalized(MachineFunction &MF,
                                             RegScavenger *RS = nullptr) const {
  }

  /// Hook called after frame finalization but before FI operands are replaced.
  ///
  /// This method is optional.
  ///
  /// \param MF Function whose frame indices are about to be replaced.
  /// \param RS Optional register scavenger for the function.
  virtual void
  processFunctionBeforeFrameIndicesReplaced(MachineFunction &MF,
                                            RegScavenger *RS = nullptr) const {}

  /// Return the Windows EH parent-frame offset for this function.
  ///
  /// \param MF Function whose WinEH parent-frame offset is requested.
  /// \return The parent-frame offset used by Windows EH for \p MF.
  virtual unsigned getWinEHParentFrameOffset(const MachineFunction &MF) const {
    reportFatalUsageError("WinEH not implemented for this target");
  }

  /// Eliminate call-frame setup/destroy pseudo instructions.
  ///
  /// This method is called during prolog/epilog code insertion to eliminate
  /// call frame setup and destroy pseudo instructions (but only if the Target
  /// is using them).  It is responsible for eliminating these instructions,
  /// replacing them with concrete instructions.  This method need only be
  /// implemented if using call frame setup/destroy pseudo instructions.
  /// Returns an iterator pointing to the instruction after the replaced one.
  ///
  /// \param MF Function containing the pseudo instruction.
  /// \param MBB Block containing the pseudo instruction.
  /// \param MI Iterator to the call-frame pseudo to eliminate.
  /// \return An iterator to the instruction after the replaced one.
  virtual MachineBasicBlock::iterator
  eliminateCallFramePseudoInstr(MachineFunction &MF,
                                MachineBasicBlock &MBB,
                                MachineBasicBlock::iterator MI) const {
    llvm_unreachable("Call Frame Pseudo Instructions do not exist on this "
                     "target!");
  }


  /// Order the symbols in the local stack frame.
  ///
  /// The list of objects that we want to order is in \p objectsToAllocate as
  /// indices into the MachineFrameInfo. The array can be reordered in any way
  /// upon return. The contents of the array, however, may not be modified (i.e.
  /// only their order may be changed).
  /// By default, just maintain the original order.
  ///
  /// \param MF Function whose local objects are being ordered.
  /// \param objectsToAllocate Frame-index list to reorder in place.
  virtual void
  orderFrameObjects(const MachineFunction &MF,
                    SmallVectorImpl<int> &objectsToAllocate) const {
  }

  /// Check whether \p MBB can be used as a prologue for the target.
  ///
  /// The prologue will be inserted first in this basic block.
  /// This method is used by the shrink-wrapping pass to decide if
  /// \p MBB will be correctly handled by the target.
  /// As soon as the target enable shrink-wrapping without overriding
  /// this method, we assume that each basic block is a valid
  /// prologue.
  ///
  /// \param MBB Candidate prologue basic block.
  /// \return True if \p MBB is a valid prologue block for this target.
  virtual bool canUseAsPrologue(const MachineBasicBlock &MBB) const {
    return true;
  }

  /// Check whether \p MBB can be used as an epilogue for the target.
  ///
  /// The epilogue will be inserted before the first terminator of that block.
  /// This method is used by the shrink-wrapping pass to decide if
  /// \p MBB will be correctly handled by the target.
  /// As soon as the target enable shrink-wrapping without overriding
  /// this method, we assume that each basic block is a valid
  /// epilogue.
  ///
  /// \param MBB Candidate epilogue basic block.
  /// \return True if \p MBB is a valid epilogue block for this target.
  virtual bool canUseAsEpilogue(const MachineBasicBlock &MBB) const {
    return true;
  }

  /// Returns the StackID that scalable vectors should be associated with.
  ///
  /// \return The stack ID used when allocating scalable-vector objects.
  virtual TargetStackID::Value getStackIDForScalableVectors() const {
    return TargetStackID::Default;
  }

  /// Return true if the target supports allocating objects with stack ID \p ID.
  ///
  /// \param ID Stack ID to check for support.
  /// \return True if objects with stack ID \p ID can be allocated.
  virtual bool isSupportedStackID(TargetStackID::Value ID) const {
    switch (ID) {
    default:
      return false;
    case TargetStackID::Default:
    case TargetStackID::NoAlloc:
      return true;
    }
  }

  /// Check if given function is safe for not having callee saved registers.
  /// This is used when interprocedural register allocation is enabled.
  ///
  /// \param F Function being checked for no-CSR safety.
  /// \return True if \p F is safe without callee-saved registers.
  static bool isSafeForNoCSROpt(const Function &F);

  /// Check if the no-CSR optimisation is profitable for the given function.
  ///
  /// \param F Function being checked for no-CSR profitability.
  /// \return True if skipping callee-saved registers is profitable for \p F.
  virtual bool isProfitableForNoCSROpt(const Function &F) const {
    return true;
  }

  /// Return initial CFA offset value i.e. the one valid at the beginning of the
  /// function (before any stack operations).
  ///
  /// \param MF Function whose initial CFA offset is requested.
  /// \return The initial CFA offset for \p MF.
  virtual int getInitialCFAOffset(const MachineFunction &MF) const;

  /// Return initial CFA register value i.e. the one valid at the beginning of
  /// the function (before any stack operations).
  ///
  /// \param MF Function whose initial CFA register is requested.
  /// \return The initial CFA register for \p MF.
  virtual Register getInitialCFARegister(const MachineFunction &MF) const;

  /// Return the frame base information to be encoded in the DWARF subprogram
  /// debug info.
  ///
  /// \param MF Function whose DWARF frame base is requested.
  /// \return The DWARF frame-base location for \p MF.
  virtual DwarfFrameBase getDwarfFrameBase(const MachineFunction &MF) const;

  /// If frame pointer or base pointer is clobbered by an instruction, we should
  /// spill/restore it around that instruction.
  ///
  /// \param MF Function in which FP/BP spills may be inserted.
  virtual void spillFPBP(MachineFunction &MF) const {}

  /// This method is called at the end of prolog/epilog code insertion, so
  /// targets can emit remarks based on the final frame layout.
  ///
  /// \param MF Function whose final frame layout may produce remarks.
  /// \param ORE Remark emitter used to report frame-layout remarks.
  virtual void emitRemarks(const MachineFunction &MF,
                           MachineOptimizationRemarkEmitter *ORE) const {};

protected:
  /// Target-specific predicate for whether \p MF needs a frame pointer.
  ///
  /// \param MF Function being queried.
  /// \return True if \p MF needs a dedicated frame pointer.
  virtual bool hasFPImpl(const MachineFunction &MF) const = 0;
};

} // End llvm namespace

#endif
