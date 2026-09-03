//===-- CodeGen/MachineFrameInfo.h - Abstract Stack Frame Rep. --*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// The file defines the MachineFrameInfo class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEFRAMEINFO_H
#define LLVM_CODEGEN_MACHINEFRAMEINFO_H

#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/Register.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <vector>

namespace llvm {
class raw_ostream;
class MachineFunction;
class MachineBasicBlock;
class BitVector;
class AllocaInst;

/// Tracks where a callee-saved register is spilled in the current frame.
///
/// Callee-saved registers can also be saved to a different register rather
/// than on the stack by setting DstReg instead of FrameIdx.
class CalleeSavedInfo {
  MCRegister Reg;
  union {
    /// Frame index of the stack slot that holds the callee-saved register.
    int FrameIdx;
    /// Alternate register that holds the spilled callee-saved register.
    unsigned DstReg;
  };
  /// Flag indicating whether the register is actually restored in the epilog.
  /// In most cases, if a register is saved, it is also restored. There are
  /// some situations, though, when this is not the case. For example, the
  /// LR register on ARM is usually saved, but on exit from the function its
  /// saved value may be loaded directly into PC. Since liveness tracking of
  /// physical registers treats callee-saved registers are live outside of
  /// the function, LR would be treated as live-on-exit, even though in these
  /// scenarios it is not. This flag is added to indicate that the saved
  /// register described by this object is not restored in the epilog.
  /// The long-term solution is to model the liveness of callee-saved registers
  /// by implicit uses on the return instructions, however, the required
  /// changes in the ARM backend would be quite extensive.
  bool Restored = true;
  /// Flag indicating whether the register is spilled to stack or another
  /// register.
  bool SpilledToReg = false;

public:
  /// Construct callee-saved info for register \p R at optional frame index
  /// \p FI.
  ///
  /// \param R Callee-saved register being tracked.
  /// \param FI Frame index of the spill slot, or 0 if unused.
  explicit CalleeSavedInfo(MCRegister R, int FI = 0) : Reg(R), FrameIdx(FI) {}

  /// Return the callee-saved register being tracked.
  ///
  /// @return The callee-saved register being tracked.
  MCRegister getReg()                      const { return Reg; }
  /// Return the frame index of the spill slot.
  ///
  /// @return The frame index of the spill slot.
  int getFrameIdx()                        const { return FrameIdx; }
  /// Return the destination register when spilled to another register.
  ///
  /// @return The destination register when spilled to another register.
  MCRegister getDstReg()                   const { return DstReg; }
  /// Set the callee-saved register being tracked.
  ///
  /// \param R New callee-saved register.
  void setReg(MCRegister R) { Reg = R; }
  /// Set the frame index of the spill slot and mark the save as stack-based.
  ///
  /// \param FI Frame index of the spill slot.
  void setFrameIdx(int FI) {
    FrameIdx = FI;
    SpilledToReg = false;
  }
  /// Set the destination register and mark the save as register-based.
  ///
  /// \param SpillReg Register that holds the spilled value.
  void setDstReg(MCRegister SpillReg) {
    DstReg = SpillReg.id();
    SpilledToReg = true;
  }
  /// Return true if the register is restored in the epilog.
  ///
  /// @return True if the register is restored in the epilog.
  bool isRestored()                        const { return Restored; }
  /// Set whether the register is restored in the epilog.
  ///
  /// \param R True if the register is restored.
  void setRestored(bool R)                       { Restored = R; }
  /// Return true if the register is spilled to another register.
  ///
  /// @return True if the register is spilled to another register.
  bool isSpilledToReg()                    const { return SpilledToReg; }
};

/// Map from basic blocks to the callee-saved registers saved or restored there.
using SaveRestorePoints =
    DenseMap<MachineBasicBlock *, std::vector<CalleeSavedInfo>>;

/// Represents an abstract stack frame until prolog/epilog code is inserted.
///
/// This class is key to allowing stack frame representation optimizations,
/// such as frame pointer elimination.  It also allows more mundane (but still
/// important) optimizations, such as reordering of abstract objects on the
/// stack frame.
///
/// To support this, the class assigns unique integer identifiers to stack
/// objects requested clients.  These identifiers are negative integers for
/// fixed stack objects (such as arguments passed on the stack) or nonnegative
/// for objects that may be reordered.  Instructions which refer to stack
/// objects use a special MO_FrameIndex operand to represent these frame
/// indexes.
///
/// Because this class keeps track of all references to the stack frame, it
/// knows when a variable sized object is allocated on the stack.  This is the
/// sole condition which prevents frame pointer elimination, which is an
/// important optimization on register-poor architectures.  Because original
/// variable sized alloca's in the source program are the only source of
/// variable sized stack objects, it is safe to decide whether there will be
/// any variable sized objects before all stack objects are known (for
/// example, register allocator spill code never needs variable sized
/// objects).
///
/// When prolog/epilog code emission is performed, the final stack frame is
/// built and the machine instructions are modified to refer to the actual
/// stack offsets of the object, eliminating all MO_FrameIndex operands from
/// the program.
class MachineFrameInfo {
public:
  /// Stack Smashing Protection (SSP) rules require that vulnerable stack
  /// allocations are located close the stack protector.
  enum SSPLayoutKind {
    SSPLK_None,       ///< Did not trigger a stack protector.  No effect on data
                      ///< layout.
    SSPLK_LargeArray, ///< Array or nested array >= SSP-buffer-size.  Closest
                      ///< to the stack protector.
    SSPLK_SmallArray, ///< Array or nested array < SSP-buffer-size. 2nd closest
                      ///< to the stack protector.
    SSPLK_AddrOf      ///< The address of this allocation is exposed and
                      ///< triggered protection.  3rd closest to the protector.
  };

private:
  // Represent a single object allocated on the stack.
  struct StackObject {
    // The offset of this object from the stack pointer on entry to
    // the function.  This field has no meaning for a variable sized element.
    int64_t SPOffset;

    // The size of this object on the stack. 0 means a variable sized object,
    // ~0ULL means a dead object.
    uint64_t Size;

    // The required alignment of this stack slot.
    Align Alignment;

    // If true, the value of the stack object is set before
    // entering the function and is not modified inside the function. By
    // default, fixed objects are immutable unless marked otherwise.
    bool isImmutable;

    // If true the stack object is used as spill slot. It
    // cannot alias any other memory objects.
    bool isSpillSlot;

    /// If true, this stack slot is used to spill a value (could be deopt
    /// and/or GC related) over a statepoint. We know that the address of the
    /// slot can't alias any LLVM IR value.  This is very similar to a Spill
    /// Slot, but is created by statepoint lowering is SelectionDAG, not the
    /// register allocator.
    bool isStatepointSpillSlot = false;

    /// If true, this stack slot is used for spilling a callee saved register
    /// in the calling convention of the containing function.
    bool isCalleeSaved = false;

    /// Identifier for stack memory type analagous to address space. If this is
    /// non-0, the meaning is target defined. Offsets cannot be directly
    /// compared between objects with different stack IDs. The object may not
    /// necessarily reside in the same contiguous memory block as other stack
    /// objects. Objects with differing stack IDs should not be merged or
    /// replaced substituted for each other.
    //
    /// It is assumed a target uses consecutive, increasing stack IDs starting
    /// from 1.
    uint8_t StackID;

    /// If this stack object is originated from an Alloca instruction
    /// this value saves the original IR allocation. Can be NULL.
    const AllocaInst *Alloca;

    // If true, the object was mapped into the local frame
    // block and doesn't need additional handling for allocation beyond that.
    bool PreAllocated = false;

    // If true, an LLVM IR value might point to this object.
    // Normally, spill slots and fixed-offset objects don't alias IR-accessible
    // objects, but there are exceptions (on PowerPC, for example, some byval
    // arguments have ABI-prescribed offsets).
    bool isAliased;

    /// If true, the object has been zero-extended.
    bool isZExt = false;

    /// If true, the object has been sign-extended.
    bool isSExt = false;

    uint8_t SSPLayout = SSPLK_None;

    StackObject(uint64_t Size, Align Alignment, int64_t SPOffset,
                bool IsImmutable, bool IsSpillSlot, const AllocaInst *Alloca,
                bool IsAliased, uint8_t StackID = 0)
        : SPOffset(SPOffset), Size(Size), Alignment(Alignment),
          isImmutable(IsImmutable), isSpillSlot(IsSpillSlot), StackID(StackID),
          Alloca(Alloca), isAliased(IsAliased) {}
  };

  /// The alignment of the stack.
  Align StackAlignment;

  /// Can the stack be realigned. This can be false if the target does not
  /// support stack realignment, or if the user asks us not to realign the
  /// stack. In this situation, overaligned allocas are all treated as dynamic
  /// allocations and the target must handle them as part of DYNAMIC_STACKALLOC
  /// lowering. All non-alloca stack objects have their alignment clamped to the
  /// base ABI stack alignment.
  /// FIXME: There is room for improvement in this case, in terms of
  /// grouping overaligned allocas into a "secondary stack frame" and
  /// then only use a single alloca to allocate this frame and only a
  /// single virtual register to access it. Currently, without such an
  /// optimization, each such alloca gets its own dynamic realignment.
  bool StackRealignable;

  /// Whether the function has the \c alignstack attribute.
  bool ForcedRealign;

  /// The list of stack objects allocated.
  std::vector<StackObject> Objects;

  /// This contains the number of fixed objects contained on
  /// the stack.  Because fixed objects are stored at a negative index in the
  /// Objects list, this is also the index to the 0th object in the list.
  unsigned NumFixedObjects = 0;

  /// This boolean keeps track of whether any variable
  /// sized objects have been allocated yet.
  bool HasVarSizedObjects = false;

  /// This boolean keeps track of whether there is a call
  /// to builtin \@llvm.frameaddress.
  bool FrameAddressTaken = false;

  /// This boolean keeps track of whether there is a call
  /// to builtin \@llvm.returnaddress.
  bool ReturnAddressTaken = false;

  /// This boolean keeps track of whether there is a call
  /// to builtin \@llvm.experimental.stackmap.
  bool HasStackMap = false;

  /// This boolean keeps track of whether there is a call
  /// to builtin \@llvm.experimental.patchpoint.
  bool HasPatchPoint = false;

  /// The prolog/epilog code inserter calculates the final stack
  /// offsets for all of the fixed size objects, updating the Objects list
  /// above.  It then updates StackSize to contain the number of bytes that need
  /// to be allocated on entry to the function.
  uint64_t StackSize = 0;

  /// The amount that a frame offset needs to be adjusted to
  /// have the actual offset from the stack/frame pointer.  The exact usage of
  /// this is target-dependent, but it is typically used to adjust between
  /// SP-relative and FP-relative offsets.  E.G., if objects are accessed via
  /// SP then OffsetAdjustment is zero; if FP is used, OffsetAdjustment is set
  /// to the distance between the initial SP and the value in FP.  For many
  /// targets, this value is only used when generating debug info (via
  /// TargetRegisterInfo::getFrameIndexReference); when generating code, the
  /// corresponding adjustments are performed directly.
  int64_t OffsetAdjustment = 0;

  /// The prolog/epilog code inserter may process objects that require greater
  /// alignment than the default alignment the target provides.
  /// To handle this, MaxAlignment is set to the maximum alignment
  /// needed by the objects on the current frame.  If this is greater than the
  /// native alignment maintained by the compiler, dynamic alignment code will
  /// be needed.
  ///
  Align MaxAlignment;

  /// Set to true if this function adjusts the stack -- e.g.,
  /// when calling another function. This is only valid during and after
  /// prolog/epilog code insertion.
  bool AdjustsStack = false;

  /// Set to true if this function has any function calls.
  bool HasCalls = false;

  /// Frame-pointer policy for this function to avoid repeated attribute
  /// lookups in hot paths.
  FramePointerKind FramePointerPolicy = FramePointerKind::None;

  /// The frame index for the stack protector.
  int StackProtectorIdx = -1;

  /// The frame index for the function context. Used for SjLj exceptions.
  int FunctionContextIdx = -1;

  /// This contains the size of the largest call frame if the target uses frame
  /// setup/destroy pseudo instructions (as defined in the TargetFrameInfo
  /// class).  This information is important for frame pointer elimination.
  /// It is only valid during and after prolog/epilog code insertion.
  uint64_t MaxCallFrameSize = ~UINT64_C(0);

  /// The number of bytes of callee saved registers that the target wants to
  /// report for the current function in the CodeView S_FRAMEPROC record.
  unsigned CVBytesOfCalleeSavedRegisters = 0;

  /// The prolog/epilog code inserter fills in this vector with each
  /// callee saved register saved in either the frame or a different
  /// register.  Beyond its use by the prolog/ epilog code inserter,
  /// this data is used for debug info and exception handling.
  std::vector<CalleeSavedInfo> CSInfo;

  /// Has CSInfo been set yet?
  bool CSIValid = false;

  /// References to frame indices which are mapped
  /// into the local frame allocation block. <FrameIdx, LocalOffset>
  SmallVector<std::pair<int, int64_t>, 32> LocalFrameObjects;

  /// Size of the pre-allocated local frame block.
  int64_t LocalFrameSize = 0;

  /// Required alignment of the local object blob, which is the strictest
  /// alignment of any object in it.
  Align LocalFrameMaxAlign;

  /// Whether the local object blob needs to be allocated together. If not,
  /// PEI should ignore the isPreAllocated flags on the stack objects and
  /// just allocate them normally.
  bool UseLocalStackAllocationBlock = false;

  /// True if the function dynamically adjusts the stack pointer through some
  /// opaque mechanism like inline assembly or Win32 EH.
  bool HasOpaqueSPAdjustment = false;

  /// True if the function contains operations which will lower down to
  /// instructions which manipulate the stack pointer.
  bool HasCopyImplyingStackAdjustment = false;

  /// True if the function contains a call to the llvm.vastart intrinsic.
  bool HasVAStart = false;

  /// True if this is a varargs function that contains a musttail call.
  bool HasMustTailInVarArgFunc = false;

  /// True if this function contains a tail call. If so immutable objects like
  /// function arguments are no longer so. A tail call *can* override fixed
  /// stack objects like arguments so we can't treat them as immutable.
  bool HasTailCall = false;

  /// Not empty, if shrink-wrapping found a better place for the prologue.
  SaveRestorePoints SavePoints;
  /// Not empty, if shrink-wrapping found a better place for the epilogue.
  SaveRestorePoints RestorePoints;

  /// Size of the UnsafeStack Frame
  uint64_t UnsafeStackSize = 0;

public:
  /// Construct frame info with the given stack alignment constraints.
  ///
  /// \param StackAlignment Default ABI stack alignment.
  /// \param StackRealignable Whether the stack may be realigned.
  /// \param ForcedRealign Whether the function forces stack realignment.
  explicit MachineFrameInfo(Align StackAlignment, bool StackRealignable,
                            bool ForcedRealign)
      : StackAlignment(StackAlignment),
        StackRealignable(StackRealignable), ForcedRealign(ForcedRealign) {}

  /// MachineFrameInfo is non-copyable.
  ///
  /// \param RHS Unused; copy construction is deleted.
  MachineFrameInfo(const MachineFrameInfo &RHS) = delete;

  /// Return true if the stack may be realigned.
  ///
  /// @return True if the stack may be realigned.
  bool isStackRealignable() const { return StackRealignable; }

  /// Return true if there are any stack objects in this function.
  ///
  /// @return True if there are any stack objects in this function.
  bool hasStackObjects() const { return !Objects.empty(); }

  /// Return true if the stack frame contains any variable-sized objects.
  ///
  /// This method may be called any time after instruction selection is
  /// complete.
  ///
  /// @return True if the stack frame contains any variable-sized objects.
  bool hasVarSizedObjects() const { return HasVarSizedObjects; }

  /// Return the index for the stack protector object.
  ///
  /// @return The index for the stack protector object.
  int getStackProtectorIndex() const { return StackProtectorIdx; }
  /// Set the frame index of the stack protector object.
  ///
  /// \param I Frame index of the stack protector, or -1 if none.
  void setStackProtectorIndex(int I) { StackProtectorIdx = I; }
  /// Return true if a stack protector frame index has been set.
  ///
  /// @return True if a stack protector frame index has been set.
  bool hasStackProtectorIndex() const { return StackProtectorIdx != -1; }

  /// Return the index for the function context object.
  /// This object is used for SjLj exceptions.
  ///
  /// @return The index for the function context object.
  int getFunctionContextIndex() const { return FunctionContextIdx; }
  /// Set the frame index of the SjLj function context object.
  ///
  /// \param I Frame index of the function context, or -1 if none.
  void setFunctionContextIndex(int I) { FunctionContextIdx = I; }
  /// Return true if a function context frame index has been set.
  ///
  /// @return True if a function context frame index has been set.
  bool hasFunctionContextIndex() const { return FunctionContextIdx != -1; }

  /// This method may be called any time after instruction
  /// selection is complete to determine if there is a call to
  /// \@llvm.frameaddress in this function.
  ///
  /// @return True if there is a call to \@llvm.frameaddress in this function.
  bool isFrameAddressTaken() const { return FrameAddressTaken; }
  /// Record whether \@llvm.frameaddress is taken in this function.
  ///
  /// \param T True if the frame address is taken.
  void setFrameAddressIsTaken(bool T) { FrameAddressTaken = T; }

  /// This method may be called any time after
  /// instruction selection is complete to determine if there is a call to
  /// \@llvm.returnaddress in this function.
  ///
  /// @return True if there is a call to \@llvm.returnaddress in this function.
  bool isReturnAddressTaken() const { return ReturnAddressTaken; }
  /// Record whether \@llvm.returnaddress is taken in this function.
  ///
  /// \param s True if the return address is taken.
  void setReturnAddressIsTaken(bool s) { ReturnAddressTaken = s; }

  /// This method may be called any time after instruction
  /// selection is complete to determine if there is a call to builtin
  /// \@llvm.experimental.stackmap.
  ///
  /// @return True if there is a call to \@llvm.experimental.stackmap.
  bool hasStackMap() const { return HasStackMap; }
  /// Record whether this function contains an \@llvm.experimental.stackmap.
  ///
  /// \param s True if a stack map is present.
  void setHasStackMap(bool s = true) { HasStackMap = s; }

  /// This method may be called any time after instruction
  /// selection is complete to determine if there is a call to builtin
  /// \@llvm.experimental.patchpoint.
  ///
  /// @return True if there is a call to \@llvm.experimental.patchpoint.
  bool hasPatchPoint() const { return HasPatchPoint; }
  /// Record whether this function contains an \@llvm.experimental.patchpoint.
  ///
  /// \param s True if a patchpoint is present.
  void setHasPatchPoint(bool s = true) { HasPatchPoint = s; }

  /// Return true if this function requires a split stack prolog.
  ///
  /// This remains true even if the function uses no stack space. It is only
  /// meaningful for functions where MachineFunction::shouldSplitStack()
  /// returns true.
  ///
  /// @return True if this function requires a split stack prolog.
  //
  // For non-leaf functions we have to allow for the possibility that the call
  // is to a non-split function, as in PR37807. This function could also take
  // the address of a non-split function. When the linker tries to adjust its
  // non-existent prologue, it would fail with an error. Mark the object file so
  // that such failures are not errors. See this Go language bug-report
  // https://go-review.googlesource.com/c/go/+/148819/
  bool needsSplitStackProlog() const {
    return getStackSize() != 0 || hasTailCall();
  }

  /// Return the minimum frame object index.
  ///
  /// @return The minimum frame object index.
  int getObjectIndexBegin() const { return -NumFixedObjects; }

  /// Return one past the maximum frame object index.
  ///
  /// @return One past the maximum frame object index.
  int getObjectIndexEnd() const { return (int)Objects.size()-NumFixedObjects; }

  /// Return the number of fixed objects.
  ///
  /// @return The number of fixed objects.
  unsigned getNumFixedObjects() const { return NumFixedObjects; }

  /// Return the number of objects.
  ///
  /// @return The number of objects.
  unsigned getNumObjects() const { return Objects.size(); }

  /// Map a frame index into the local object block.
  ///
  /// \param ObjectIndex Frame index being mapped into the local block.
  /// \param Offset Offset of the object within the local block.
  void mapLocalFrameObject(int ObjectIndex, int64_t Offset) {
    LocalFrameObjects.push_back(std::pair<int, int64_t>(ObjectIndex, Offset));
    Objects[ObjectIndex + NumFixedObjects].PreAllocated = true;
  }

  /// Get the local offset mapping for an object.
  ///
  /// \param i Index into the local frame object map.
  /// @return The local offset mapping for object \p i.
  std::pair<int, int64_t> getLocalFrameObjectMap(int i) const {
    assert (i >= 0 && (unsigned)i < LocalFrameObjects.size() &&
            "Invalid local object reference!");
    return LocalFrameObjects[i];
  }

  /// Return the number of objects allocated into the local object block.
  ///
  /// @return The number of objects allocated into the local object block.
  int64_t getLocalFrameObjectCount() const { return LocalFrameObjects.size(); }

  /// Set the size of the local object blob.
  ///
  /// \param sz Size in bytes of the local object blob.
  void setLocalFrameSize(int64_t sz) { LocalFrameSize = sz; }

  /// Get the size of the local object blob.
  ///
  /// @return The size of the local object blob.
  int64_t getLocalFrameSize() const { return LocalFrameSize; }

  /// Set the required alignment of the local object blob.
  ///
  /// This is the strictest alignment of any object in the blob.
  ///
  /// \param Alignment Required alignment of the local object blob.
  void setLocalFrameMaxAlign(Align Alignment) {
    LocalFrameMaxAlign = Alignment;
  }

  /// Return the required alignment of the local object blob.
  ///
  /// @return The required alignment of the local object blob.
  Align getLocalFrameMaxAlign() const { return LocalFrameMaxAlign; }

  /// Get whether the local allocation blob should be allocated together or
  /// let PEI allocate the locals in it directly.
  ///
  /// @return True if the local allocation blob should be allocated together.
  bool getUseLocalStackAllocationBlock() const {
    return UseLocalStackAllocationBlock;
  }

  /// Set whether the local allocation blob should be allocated together.
  ///
  /// When false, PEI allocates the locals in it directly.
  ///
  /// \param v True to allocate the local blob as a single block.
  void setUseLocalStackAllocationBlock(bool v) {
    UseLocalStackAllocationBlock = v;
  }

  /// Return true if the object was pre-allocated into the local block.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the object was pre-allocated into the local block.
  bool isObjectPreAllocated(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].PreAllocated;
  }

  /// Return the size of the specified object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return The size of the specified object.
  int64_t getObjectSize(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].Size;
  }

  /// Change the size of the specified stack object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param Size New size of the object in bytes.
  void setObjectSize(int ObjectIdx, int64_t Size) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].Size = Size;
  }

  /// Return the alignment of the specified stack object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return The alignment of the specified stack object.
  Align getObjectAlign(int ObjectIdx) const {
    assert(unsigned(ObjectIdx + NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx + NumFixedObjects].Alignment;
  }

  /// Return true if this stack ID should be considered in MaxAlignment.
  ///
  /// \param StackID Stack identifier to check.
  /// @return True if this stack ID should be considered in MaxAlignment.
  bool contributesToMaxAlignment(uint8_t StackID) {
    return StackID == TargetStackID::Default ||
           StackID == TargetStackID::ScalableVector ||
           StackID == TargetStackID::ScalablePredicateVector;
  }

  /// Return true if the object uses a scalable-vector stack ID.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the object uses a scalable-vector stack ID.
  bool hasScalableStackID(int ObjectIdx) const {
    uint8_t StackID = getStackID(ObjectIdx);
    return isScalableStackID(StackID);
  }

  /// Return true if \p StackID identifies a scalable-vector stack.
  ///
  /// \param StackID Stack identifier to check.
  /// @return True if \p StackID identifies a scalable-vector stack.
  bool isScalableStackID(uint8_t StackID) const {
    return StackID == TargetStackID::ScalableVector ||
           StackID == TargetStackID::ScalablePredicateVector;
  }

  /// Change the alignment of the specified stack object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param Alignment New alignment requirement for the object.
  void setObjectAlignment(int ObjectIdx, Align Alignment) {
    assert(unsigned(ObjectIdx + NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx + NumFixedObjects].Alignment = Alignment;

    // Only ensure max alignment for the default and scalable vector stack.
    uint8_t StackID = getStackID(ObjectIdx);
    if (contributesToMaxAlignment(StackID))
      ensureMaxAlignment(Alignment);
  }

  /// Return the underlying Alloca of the specified
  /// stack object if it exists. Returns 0 if none exists.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return The underlying Alloca of the object, or nullptr if none exists.
  const AllocaInst* getObjectAllocation(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].Alloca;
  }

  /// Remove the underlying Alloca of the specified stack object if it
  /// exists. This generally should not be used and is for reduction tooling.
  ///
  /// \param ObjectIdx Frame index of the object.
  void clearObjectAllocation(int ObjectIdx) {
    assert(unsigned(ObjectIdx + NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx + NumFixedObjects].Alloca = nullptr;
  }

  /// Return the assigned stack offset of the specified object
  /// from the incoming stack pointer.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return The assigned stack offset from the incoming stack pointer.
  int64_t getObjectOffset(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    assert(!isDeadObjectIndex(ObjectIdx) &&
           "Getting frame offset for a dead object?");
    return Objects[ObjectIdx+NumFixedObjects].SPOffset;
  }

  /// Return true if the object has been zero-extended.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the object has been zero-extended.
  bool isObjectZExt(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isZExt;
  }

  /// Set whether the object has been zero-extended.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param IsZExt True if the object is zero-extended.
  void setObjectZExt(int ObjectIdx, bool IsZExt) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isZExt = IsZExt;
  }

  /// Return true if the object has been sign-extended.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the object has been sign-extended.
  bool isObjectSExt(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isSExt;
  }

  /// Set whether the object has been sign-extended.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param IsSExt True if the object is sign-extended.
  void setObjectSExt(int ObjectIdx, bool IsSExt) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isSExt = IsSExt;
  }

  /// Set the stack frame offset of the specified object. The
  /// offset is relative to the stack pointer on entry to the function.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param SPOffset Offset from the incoming stack pointer.
  void setObjectOffset(int ObjectIdx, int64_t SPOffset) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    assert(!isDeadObjectIndex(ObjectIdx) &&
           "Setting frame offset for a dead object?");
    Objects[ObjectIdx+NumFixedObjects].SPOffset = SPOffset;
  }

  /// Return the SSP layout kind for the specified object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return The SSP layout kind for the specified object.
  SSPLayoutKind getObjectSSPLayout(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return (SSPLayoutKind)Objects[ObjectIdx+NumFixedObjects].SSPLayout;
  }

  /// Set the SSP layout kind for the specified object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param Kind SSP layout classification for the object.
  void setObjectSSPLayout(int ObjectIdx, SSPLayoutKind Kind) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    assert(!isDeadObjectIndex(ObjectIdx) &&
           "Setting SSP layout for a dead object?");
    Objects[ObjectIdx+NumFixedObjects].SSPLayout = Kind;
  }

  /// Return the number of bytes allocated for fixed-size frame objects.
  ///
  /// This is only valid after Prolog/Epilog code insertion has finalized the
  /// stack frame layout.
  ///
  /// @return The number of bytes allocated for fixed-size frame objects.
  uint64_t getStackSize() const { return StackSize; }

  /// Set the size of the stack.
  ///
  /// \param Size Total stack size in bytes.
  void setStackSize(uint64_t Size) { StackSize = Size; }

  /// Estimate and return the size of the stack frame.
  ///
  /// \param MF Function whose stack size is estimated.
  /// @return Estimated size of the stack frame.
  LLVM_ABI uint64_t estimateStackSize(const MachineFunction &MF) const;

  /// Return the correction for frame offsets.
  ///
  /// @return The correction for frame offsets.
  int64_t getOffsetAdjustment() const { return OffsetAdjustment; }

  /// Set the correction for frame offsets.
  ///
  /// \param Adj Offset adjustment between SP-relative and FP-relative forms.
  void setOffsetAdjustment(int64_t Adj) { OffsetAdjustment = Adj; }

  /// Return alignment of this function's frame.
  ///
  /// @return Alignment of this function's frame.
  Align getMaxAlign() const { return MaxAlignment; }

  /// Overwrite alignment of this function's frame.
  ///
  /// \param Alignment Maximum frame alignment to record.
  void setMaxAlign(Align Alignment) { MaxAlignment = Alignment; }

  /// Make sure the function's frame is at least Align bytes aligned.
  ///
  /// \param Alignment Minimum frame alignment to ensure.
  LLVM_ABI void ensureMaxAlignment(Align Alignment);

  /// Return true if stack realignment is forced by function attributes or if
  /// the stack alignment.
  ///
  /// @return True if stack realignment is forced or required by MaxAlignment.
  bool shouldRealignStack() const {
    return ForcedRealign || MaxAlignment > StackAlignment;
  }

  /// Return true if this function adjusts the stack -- e.g.,
  /// when calling another function. This is only valid during and after
  /// prolog/epilog code insertion.
  ///
  /// @return True if this function adjusts the stack.
  bool adjustsStack() const { return AdjustsStack; }
  /// Record whether this function adjusts the stack.
  ///
  /// \param V True if the function adjusts the stack.
  void setAdjustsStack(bool V) { AdjustsStack = V; }

  /// Return true if the current function has any function calls.
  ///
  /// @return True if the current function has any function calls.
  bool hasCalls() const { return HasCalls; }
  /// Record whether the current function has any function calls.
  ///
  /// \param V True if the function contains calls.
  void setHasCalls(bool V) { HasCalls = V; }

  /// Return the frame-pointer policy for this function.
  ///
  /// @return The frame-pointer policy for this function.
  FramePointerKind getFramePointerPolicy() const { return FramePointerPolicy; }
  /// Set the frame-pointer policy for this function.
  ///
  /// \param Kind Frame-pointer policy to apply.
  void setFramePointerPolicy(FramePointerKind Kind) {
    FramePointerPolicy = Kind;
  }

  /// Returns true if the function contains opaque dynamic stack adjustments.
  ///
  /// @return True if the function contains opaque dynamic stack adjustments.
  bool hasOpaqueSPAdjustment() const { return HasOpaqueSPAdjustment; }
  /// Record whether the function contains opaque dynamic stack adjustments.
  ///
  /// \param B True if opaque SP adjustments are present.
  void setHasOpaqueSPAdjustment(bool B) { HasOpaqueSPAdjustment = B; }

  /// Returns true if the function contains operations which will lower down to
  /// instructions which manipulate the stack pointer.
  ///
  /// @return True if copies imply a stack-pointer adjustment.
  bool hasCopyImplyingStackAdjustment() const {
    return HasCopyImplyingStackAdjustment;
  }
  /// Record whether copies imply a stack-pointer adjustment.
  ///
  /// \param B True if such copies are present.
  void setHasCopyImplyingStackAdjustment(bool B) {
    HasCopyImplyingStackAdjustment = B;
  }

  /// Returns true if the function calls the llvm.va_start intrinsic.
  ///
  /// @return True if the function calls the llvm.va_start intrinsic.
  bool hasVAStart() const { return HasVAStart; }
  /// Record whether the function calls llvm.va_start.
  ///
  /// \param B True if llvm.va_start is present.
  void setHasVAStart(bool B) { HasVAStart = B; }

  /// Returns true if the function is variadic and contains a musttail call.
  ///
  /// @return True if the function is variadic and contains a musttail call.
  bool hasMustTailInVarArgFunc() const { return HasMustTailInVarArgFunc; }
  /// Record whether this varargs function contains a musttail call.
  ///
  /// \param B True if a musttail call is present in a varargs function.
  void setHasMustTailInVarArgFunc(bool B) { HasMustTailInVarArgFunc = B; }

  /// Returns true if the function contains a tail call.
  ///
  /// @return True if the function contains a tail call.
  bool hasTailCall() const { return HasTailCall; }
  /// Record whether the function contains a tail call.
  ///
  /// \param V True if a tail call is present.
  void setHasTailCall(bool V = true) { HasTailCall = V; }

  /// Compute the maximum size of a call frame for this function.
  ///
  /// This only works for targets defining
  /// TargetInstrInfo::getCallFrameSetupOpcode(), getCallFrameDestroyOpcode(),
  /// and getFrameSize(). This is usually computed by the prologue epilogue
  /// inserter but some targets may call this to compute it earlier. If
  /// FrameSDOps is passed, the frame instructions in the MF will be inserted
  /// into it.
  ///
  /// \param MF Function whose call-frame size is computed.
  /// \param FrameSDOps Optional list to receive call-frame setup/destroy
  ///        iterators.
  LLVM_ABI void computeMaxCallFrameSize(
      MachineFunction &MF,
      std::vector<MachineBasicBlock::iterator> *FrameSDOps = nullptr);

  /// Return the maximum size of a call frame that must be allocated.
  ///
  /// This is only available if CallFrameSetup/Destroy pseudo instructions are
  /// used by the target, and then only during or after prolog/epilog code
  /// insertion.
  ///
  /// @return The maximum size of a call frame that must be allocated.
  uint64_t getMaxCallFrameSize() const {
    // TODO: Enable this assert when targets are fixed.
    //assert(isMaxCallFrameSizeComputed() && "MaxCallFrameSize not computed yet");
    if (!isMaxCallFrameSizeComputed())
      return 0;
    return MaxCallFrameSize;
  }
  /// Return true if the maximum call-frame size has been computed.
  ///
  /// @return True if the maximum call-frame size has been computed.
  bool isMaxCallFrameSizeComputed() const {
    return MaxCallFrameSize != ~UINT64_C(0);
  }
  /// Set the maximum size of an outgoing call frame.
  ///
  /// \param S Maximum call-frame size in bytes.
  void setMaxCallFrameSize(uint64_t S) { MaxCallFrameSize = S; }

  /// Returns how many bytes of callee-saved registers the target pushed in the
  /// prologue. Only used for debug info.
  ///
  /// @return Bytes of callee-saved registers pushed in the prologue.
  unsigned getCVBytesOfCalleeSavedRegisters() const {
    return CVBytesOfCalleeSavedRegisters;
  }
  /// Set how many bytes of callee-saved registers to report for CodeView.
  ///
  /// \param S Number of bytes of callee-saved registers.
  void setCVBytesOfCalleeSavedRegisters(unsigned S) {
    CVBytesOfCalleeSavedRegisters = S;
  }

  /// Create a new object at a fixed location on the stack.
  ///
  /// All fixed objects should be created before other objects are created for
  /// efficiency. By default, fixed objects are not pointed to by LLVM IR
  /// values. This returns an index with a negative value.
  ///
  /// \param Size Size of the object in bytes.
  /// \param SPOffset Offset from the stack pointer on entry.
  /// \param IsImmutable True if the object is not modified in the function.
  /// \param isAliased True if an LLVM IR value might point to the object.
  /// @return Frame index with a negative value for the new fixed object.
  LLVM_ABI int CreateFixedObject(uint64_t Size, int64_t SPOffset,
                                 bool IsImmutable, bool isAliased = false);

  /// Create a spill slot at a fixed location on the stack.
  /// Returns an index with a negative value.
  ///
  /// \param Size Size of the spill slot in bytes.
  /// \param SPOffset Offset from the stack pointer on entry.
  /// \param IsImmutable True if the spill slot is immutable.
  /// @return Frame index with a negative value for the spill slot.
  LLVM_ABI int CreateFixedSpillStackObject(uint64_t Size, int64_t SPOffset,
                                           bool IsImmutable = false);

  /// Returns true if the specified index corresponds to a fixed stack object.
  ///
  /// \param ObjectIdx Frame index to test.
  /// @return True if the specified index corresponds to a fixed stack object.
  bool isFixedObjectIndex(int ObjectIdx) const {
    return ObjectIdx < 0 && (ObjectIdx >= -(int)NumFixedObjects);
  }

  /// Returns true if the specified index corresponds
  /// to an object that might be pointed to by an LLVM IR value.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if an LLVM IR value might point to the object.
  bool isAliasedObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isAliased;
  }

  /// Set "maybe pointed to by an LLVM IR value" for an object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param IsAliased True if an LLVM IR value might point to the object.
  void setIsAliasedObjectIndex(int ObjectIdx, bool IsAliased) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isAliased = IsAliased;
  }

  /// Returns true if the specified index corresponds to an immutable object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the specified index corresponds to an immutable object.
  bool isImmutableObjectIndex(int ObjectIdx) const {
    // Tail calling functions can clobber their function arguments.
    if (HasTailCall)
      return false;
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isImmutable;
  }

  /// Marks the immutability of an object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param IsImmutable True if the object is immutable.
  void setIsImmutableObjectIndex(int ObjectIdx, bool IsImmutable) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isImmutable = IsImmutable;
  }

  /// Returns true if the specified index corresponds to a spill slot.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the specified index corresponds to a spill slot.
  bool isSpillSlotObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isSpillSlot;
  }

  /// Return true if the index is a statepoint spill slot.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the index is a statepoint spill slot.
  bool isStatepointSpillSlotObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].isStatepointSpillSlot;
  }

  /// Return true if the index is a callee-saved spill slot.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the index is a callee-saved spill slot.
  bool isCalleeSavedObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx + NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx + NumFixedObjects].isCalleeSaved;
  }

  /// Mark whether the index is a callee-saved spill slot.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param IsCalleeSaved True if the slot spills a callee-saved register.
  void setIsCalleeSavedObjectIndex(int ObjectIdx, bool IsCalleeSaved) {
    assert(unsigned(ObjectIdx + NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx + NumFixedObjects].isCalleeSaved = IsCalleeSaved;
  }

  /// Return the stack ID associated with the specified object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \see StackID
  /// @return The stack ID associated with the specified object.
  uint8_t getStackID(int ObjectIdx) const {
    return Objects[ObjectIdx+NumFixedObjects].StackID;
  }

  /// Set the stack ID associated with the specified object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// \param ID Stack identifier to assign.
  /// \see StackID
  void setStackID(int ObjectIdx, uint8_t ID) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].StackID = ID;
    // If ID > 0, MaxAlignment may now be overly conservative.
    // If ID == 0, MaxAlignment will need to be updated separately.
  }

  /// Returns true if the specified index corresponds to a dead object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the specified index corresponds to a dead object.
  bool isDeadObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx+NumFixedObjects].Size == ~0ULL;
  }

  /// Returns true if the specified index corresponds to a variable sized
  /// object.
  ///
  /// \param ObjectIdx Frame index of the object.
  /// @return True if the specified index corresponds to a variable sized object.
  bool isVariableSizedObjectIndex(int ObjectIdx) const {
    assert(unsigned(ObjectIdx + NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    return Objects[ObjectIdx + NumFixedObjects].Size == 0;
  }

  /// Mark the specified object as a statepoint spill slot.
  ///
  /// \param ObjectIdx Frame index of the object.
  void markAsStatepointSpillSlotObjectIndex(int ObjectIdx) {
    assert(unsigned(ObjectIdx+NumFixedObjects) < Objects.size() &&
           "Invalid Object Idx!");
    Objects[ObjectIdx+NumFixedObjects].isStatepointSpillSlot = true;
    assert(isStatepointSpillSlotObjectIndex(ObjectIdx) && "inconsistent");
  }

  /// Create a new statically sized stack object, returning
  /// a nonnegative identifier to represent it.
  ///
  /// \param Size Size of the object in bytes.
  /// \param Alignment Required alignment of the object.
  /// \param isSpillSlot True if the object is used as a spill slot.
  /// \param Alloca Optional originating AllocaInst, or nullptr.
  /// \param ID Stack identifier for the object.
  /// @return A nonnegative identifier for the new stack object.
  LLVM_ABI int CreateStackObject(uint64_t Size, Align Alignment,
                                 bool isSpillSlot,
                                 const AllocaInst *Alloca = nullptr,
                                 uint8_t ID = 0);

  /// Create a new statically sized stack object that represents a spill slot,
  /// returning a nonnegative identifier to represent it.
  ///
  /// \param Size Size of the spill slot in bytes.
  /// \param Alignment Required alignment of the spill slot.
  /// \param StackID Stack identifier for the spill slot.
  /// @return A nonnegative identifier for the new spill slot.
  LLVM_ABI int
  CreateSpillStackObject(uint64_t Size, Align Alignment,
                         TargetStackID::Value StackID = TargetStackID::Default);

  /// Remove or mark dead a statically sized stack object.
  ///
  /// \param ObjectIdx Frame index of the object to remove.
  void RemoveStackObject(int ObjectIdx) {
    // Mark it dead.
    Objects[ObjectIdx+NumFixedObjects].Size = ~0ULL;
  }

  /// Create a variable-sized stack object and return its frame index.
  ///
  /// This must be called whenever a variable sized object is created, whether
  /// or not the index returned is actually used.
  ///
  /// \param Alignment Required alignment of the variable-sized object.
  /// \param Alloca Originating AllocaInst, if any.
  /// @return Frame index of the variable-sized stack object.
  LLVM_ABI int CreateVariableSizedObject(Align Alignment,
                                         const AllocaInst *Alloca);

  /// Returns a reference to call saved info vector for the current function.
  ///
  /// @return A const reference to the callee-saved info vector.
  const std::vector<CalleeSavedInfo> &getCalleeSavedInfo() const {
    return CSInfo;
  }
  /// Returns a mutable reference to call saved info for the current function.
  ///
  /// @return A mutable reference to the callee-saved info vector.
  std::vector<CalleeSavedInfo> &getCalleeSavedInfo() { return CSInfo; }

  /// Used by prolog/epilog inserter to set the function's callee saved
  /// information.
  ///
  /// \param CSI Callee-saved register descriptors for this function.
  void setCalleeSavedInfo(std::vector<CalleeSavedInfo> CSI) {
    CSInfo = std::move(CSI);
  }

  /// Has the callee saved info been calculated yet?
  ///
  /// @return True if the callee-saved info has been calculated.
  bool isCalleeSavedInfoValid() const { return CSIValid; }

  /// Record whether the callee-saved info has been calculated.
  ///
  /// \param v True if callee-saved info is valid.
  void setCalleeSavedInfoValid(bool v) { CSIValid = v; }

  /// Return the shrink-wrapping restore points for this function.
  ///
  /// @return The shrink-wrapping restore points for this function.
  const SaveRestorePoints &getRestorePoints() const { return RestorePoints; }

  /// Return the shrink-wrapping save points for this function.
  ///
  /// @return The shrink-wrapping save points for this function.
  const SaveRestorePoints &getSavePoints() const { return SavePoints; }

  /// Set the shrink-wrapping save points for this function.
  ///
  /// \param NewSavePoints Map of blocks to callee-saved info to save.
  void setSavePoints(SaveRestorePoints NewSavePoints) {
    SavePoints = std::move(NewSavePoints);
  }

  /// Set the shrink-wrapping restore points for this function.
  ///
  /// \param NewRestorePoints Map of blocks to callee-saved info to restore.
  void setRestorePoints(SaveRestorePoints NewRestorePoints) {
    RestorePoints = std::move(NewRestorePoints);
  }

  /// Clear all shrink-wrapping save points.
  void clearSavePoints() { SavePoints.clear(); }
  /// Clear all shrink-wrapping restore points.
  void clearRestorePoints() { RestorePoints.clear(); }

  /// Return the size of the unsafe stack frame.
  ///
  /// @return The size of the unsafe stack frame.
  uint64_t getUnsafeStackSize() const { return UnsafeStackSize; }
  /// Set the size of the unsafe stack frame.
  ///
  /// \param Size Unsafe stack size in bytes.
  void setUnsafeStackSize(uint64_t Size) { UnsafeStackSize = Size; }

  /// Return a set of physical registers that are pristine.
  ///
  /// Pristine registers hold a value that is useless to the current function,
  /// but that must be preserved - they are callee saved registers that are not
  /// saved.
  ///
  /// Before the PrologueEpilogueInserter has placed the CSR spill code, this
  /// method always returns an empty set.
  ///
  /// \param MF Function whose pristine registers are queried.
  /// @return A set of physical registers that are pristine.
  LLVM_ABI BitVector getPristineRegs(const MachineFunction &MF) const;

  /// Used by the MachineFunction printer to print information about
  /// stack objects. Implemented in MachineFunction.cpp.
  ///
  /// \param MF Function whose frame info is printed.
  /// \param OS Output stream to write to.
  LLVM_ABI void print(const MachineFunction &MF, raw_ostream &OS) const;

  /// Print the function frame info to stderr.
  ///
  /// \param MF Function whose frame info is dumped.
  LLVM_ABI void dump(const MachineFunction &MF) const;
};

} // End llvm namespace

#endif
