//==- llvm/CodeGen/MachineMemOperand.h - MachineMemOperand class -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MachineMemOperand class, which is a
// description of a memory reference. It is used to help track dependencies
// in the backend.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEMEMOPERAND_H
#define LLVM_CODEGEN_MACHINEMEMOPERAND_H

#include "llvm/ADT/BitmaskEnum.h"
#include "llvm/ADT/PointerUnion.h"
#include "llvm/Analysis/MemoryLocation.h"
#include "llvm/CodeGen/PseudoSourceValue.h"
#include "llvm/CodeGenTypes/LowLevelType.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Value.h" // PointerLikeTypeTraits<Value*>
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {

class MDNode;
class raw_ostream;
class MachineFunction;
class ModuleSlotTracker;
class TargetInstrInfo;

/// Discriminated union of pointer information for memory operands.
///
/// Relates memory operands back to LLVM IR or to virtual locations (such as
/// frame indices) that are exposed during codegen.
struct MachinePointerInfo {
  /// This is the IR pointer value for the access, or it is null if unknown.
  PointerUnion<const Value *, const PseudoSourceValue *> V;

  /// Offset - This is an offset from the base Value*.
  int64_t Offset;

  /// Address space of the pointer, or 0 if unknown.
  unsigned AddrSpace = 0;

  /// Target-specific stack identifier for the referenced object.
  uint8_t StackID;

  /// Construct from an IR pointer value and optional offset and stack ID.
  /// \param v IR pointer value; may be null.
  /// \param offset Byte offset from the base value.
  /// \param ID Target-specific stack identifier.
  explicit MachinePointerInfo(const Value *v, int64_t offset = 0,
                              uint8_t ID = 0)
      : V(v), Offset(offset), StackID(ID) {
    AddrSpace = v ? v->getType()->getPointerAddressSpace() : 0;
  }

  /// Construct from a PseudoSourceValue and optional offset and stack ID.
  /// \param v Pseudo source value; may be null.
  /// \param offset Byte offset from the base value.
  /// \param ID Target-specific stack identifier.
  explicit MachinePointerInfo(const PseudoSourceValue *v, int64_t offset = 0,
                              uint8_t ID = 0)
      : V(v), Offset(offset), StackID(ID) {
    AddrSpace = v ? v->getAddressSpace() : 0;
  }

  /// Construct a pointer with unknown base value in \p AddressSpace.
  /// \param AddressSpace LLVM IR address space number.
  /// \param offset Byte offset from the unknown base.
  explicit MachinePointerInfo(unsigned AddressSpace = 0, int64_t offset = 0)
      : V((const Value *)nullptr), Offset(offset), AddrSpace(AddressSpace),
        StackID(0) {}

  /// Construct from a pointer union of IR or pseudo source values.
  /// \param v Base pointer as an IR Value or PseudoSourceValue.
  /// \param offset Byte offset from the base value.
  /// \param ID Target-specific stack identifier.
  explicit MachinePointerInfo(
    PointerUnion<const Value *, const PseudoSourceValue *> v,
    int64_t offset = 0,
    uint8_t ID = 0)
    : V(v), Offset(offset), StackID(ID) {
    if (V) {
      if (const auto *ValPtr = dyn_cast_if_present<const Value *>(V))
        AddrSpace = ValPtr->getType()->getPointerAddressSpace();
      else
        AddrSpace = cast<const PseudoSourceValue *>(V)->getAddressSpace();
    }
  }

  /// Return a copy of this pointer info with \p O added to the offset.
  /// \param O Additional byte offset to apply.
  /// \return A MachinePointerInfo with the adjusted offset.
  MachinePointerInfo getWithOffset(int64_t O) const {
    if (V.isNull())
      return MachinePointerInfo(AddrSpace, Offset + O);
    if (isa<const Value *>(V))
      return MachinePointerInfo(cast<const Value *>(V), Offset + O, StackID);
    return MachinePointerInfo(cast<const PseudoSourceValue *>(V), Offset + O,
                              StackID);
  }

  /// Return true if memory region [V, V+Offset+Size) is known to be
  /// dereferenceable.
  /// \param Size Size in bytes of the region starting at V+Offset.
  /// \param C LLVM context used for IR queries.
  /// \param DL Data layout used to evaluate dereferenceability.
  /// \return True if the region is known dereferenceable.
  LLVM_ABI bool isDereferenceable(unsigned Size, LLVMContext &C,
                                  const DataLayout &DL) const;

  /// Return the LLVM IR address space number that this pointer points into.
  /// \return The address space number of this pointer.
  LLVM_ABI unsigned getAddrSpace() const;

  /// Return a MachinePointerInfo record that refers to the constant pool.
  /// \param MF Machine function whose constant pool is referenced.
  /// \return MachinePointerInfo referring to the constant pool.
  LLVM_ABI static MachinePointerInfo getConstantPool(MachineFunction &MF);

  /// Return a MachinePointerInfo record that refers to the specified
  /// FrameIndex.
  /// \param MF Machine function owning the frame index.
  /// \param FI Frame index of the referenced stack object.
  /// \param Offset Byte offset within the stack object.
  /// \return MachinePointerInfo referring to the fixed stack object.
  LLVM_ABI static MachinePointerInfo getFixedStack(MachineFunction &MF, int FI,
                                                   int64_t Offset = 0);

  /// Return a MachinePointerInfo record that refers to a jump table entry.
  /// \param MF Machine function whose jump table is referenced.
  /// \return MachinePointerInfo referring to the jump table.
  LLVM_ABI static MachinePointerInfo getJumpTable(MachineFunction &MF);

  /// Return a MachinePointerInfo record that refers to a GOT entry.
  /// \param MF Machine function whose GOT is referenced.
  /// \return MachinePointerInfo referring to the GOT.
  LLVM_ABI static MachinePointerInfo getGOT(MachineFunction &MF);

  /// Stack pointer relative access.
  /// \param MF Machine function whose stack is referenced.
  /// \param Offset Byte offset from the stack pointer.
  /// \param ID Target-specific stack identifier.
  /// \return MachinePointerInfo referring to a stack-pointer-relative location.
  LLVM_ABI static MachinePointerInfo getStack(MachineFunction &MF,
                                              int64_t Offset, uint8_t ID = 0);

  /// Stack memory without other information.
  /// \param MF Machine function whose stack is referenced.
  /// \return MachinePointerInfo referring to unknown stack memory.
  LLVM_ABI static MachinePointerInfo getUnknownStack(MachineFunction &MF);
};

/// LLVM IR metadata carried by a MachineMemOperand.
struct MMOMetadata {
  /// Alias-analysis metadata nodes for the memory access.
  AAMDNodes AAInfo;
  /// Range metadata describing the possible values loaded.
  const MDNode *Ranges = nullptr;
  /// Cache-hint metadata for the memory access.
  const MDNode *MemCacheHint = nullptr;

  /// Construct empty metadata with no AA, range, or cache-hint nodes.
  MMOMetadata() = default;
  /// Construct metadata from alias-analysis info and optional range/cache nodes.
  /// \param AAInfo Alias-analysis metadata for the access.
  /// \param Ranges Optional range metadata node.
  /// \param MemCacheHint Optional cache-hint metadata node.
  MMOMetadata(const AAMDNodes &AAInfo, const MDNode *Ranges = nullptr,
              const MDNode *MemCacheHint = nullptr)
      : AAInfo(AAInfo), Ranges(Ranges), MemCacheHint(MemCacheHint) {}
};

//===----------------------------------------------------------------------===//
/// Description of a memory reference used in the backend.
///
/// Instead of holding a StoreInst or LoadInst, this class holds the address
/// Value of the reference along with a byte size and offset. This allows it
/// to describe lowered loads and stores. Also, the special PseudoSourceValue
/// objects can be used to represent loads and stores to memory locations
/// that aren't explicit in the regular LLVM IR.
///
class MachineMemOperand {
public:
  /// Flags values. These may be or'd together.
  enum Flags : uint16_t {
    /// No flags set.
    MONone = 0,
    /// The memory access reads data.
    MOLoad = 1u << 0,
    /// The memory access writes data.
    MOStore = 1u << 1,
    /// The memory access is volatile.
    MOVolatile = 1u << 2,
    /// The memory access is non-temporal.
    MONonTemporal = 1u << 3,
    /// The memory access is dereferenceable (i.e., doesn't trap).
    MODereferenceable = 1u << 4,
    /// The memory access always returns the same value (or traps).
    MOInvariant = 1u << 5,

    // Reserved for use by target-specific passes.
    // Targets may override getSerializableMachineMemOperandTargetFlags() to
    // enable MIR serialization/parsing of these flags.  If more of these flags
    // are added, the MIR printing/parsing code will need to be updated as well.
    MOTargetFlag1 = 1u << 6,
    MOTargetFlag2 = 1u << 7,
    MOTargetFlag3 = 1u << 8,
    MOTargetFlag4 = 1u << 9,

    LLVM_MARK_AS_BITMASK_ENUM(/* LargestFlag = */ MOTargetFlag4)
  };

private:
  /// Atomic information for this memory operation.
  struct MachineAtomicInfo {
    /// Synchronization scope ID for this memory operation.
    unsigned SSID : 8;            // SyncScope::ID
    /// Atomic ordering requirements for this memory operation. For cmpxchg
    /// atomic operations, atomic ordering requirements when store occurs.
    unsigned Ordering : 4;        // enum AtomicOrdering
    /// For cmpxchg atomic operations, atomic ordering requirements when store
    /// does not occur.
    unsigned FailureOrdering : 4; // enum AtomicOrdering
  };

  MachinePointerInfo PtrInfo;

  /// Track the memory type of the access. An access size which is unknown or
  /// too large to be represented by LLT should use the invalid LLT.
  LLT MemoryType;

  Flags FlagVals;
  Align BaseAlign;
  MachineAtomicInfo AtomicInfo;
  AAMDNodes AAInfo;
  const MDNode *Ranges;
  const MDNode *MemCacheHint;

public:
  /// Construct a MachineMemOperand with pointer info, flags, size, and alignment.
  ///
  /// For atomic operations the synchronization scope and atomic ordering
  /// requirements must also be specified. For cmpxchg atomic operations the
  /// atomic ordering requirements when store does not occur must also be
  /// specified.
  /// \param PtrInfo Base pointer information for the access.
  /// \param Flags Memory operand flags (load/store/volatile/etc.).
  /// \param TS Size of the memory access.
  /// \param A Base alignment of the access.
  /// \param Metadata Optional AA, range, and cache-hint metadata.
  /// \param SSID Synchronization scope for atomic operations.
  /// \param Ordering Success (or only) atomic ordering.
  /// \param FailureOrdering Failure ordering for cmpxchg; otherwise unused.
  LLVM_ABI
  MachineMemOperand(MachinePointerInfo PtrInfo, Flags Flags, LocationSize TS,
                    Align A, const MMOMetadata &Metadata = MMOMetadata(),
                    SyncScope::ID SSID = SyncScope::System,
                    AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
                    AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic);
  /// Construct a MachineMemOperand from pointer info, flags, \p Type, and
  /// alignment. Size is taken from \p Type.
  /// \param PtrInfo Base pointer information for the access.
  /// \param Flags Memory operand flags (load/store/volatile/etc.).
  /// \param Type Memory type of the access; size is derived from it.
  /// \param A Base alignment of the access.
  /// \param Metadata Optional AA, range, and cache-hint metadata.
  /// \param SSID Synchronization scope for atomic operations.
  /// \param Ordering Success (or only) atomic ordering.
  /// \param FailureOrdering Failure ordering for cmpxchg; otherwise unused.
  LLVM_ABI
  MachineMemOperand(MachinePointerInfo PtrInfo, Flags Flags, LLT Type, Align A,
                    const MMOMetadata &Metadata = MMOMetadata(),
                    SyncScope::ID SSID = SyncScope::System,
                    AtomicOrdering Ordering = AtomicOrdering::NotAtomic,
                    AtomicOrdering FailureOrdering = AtomicOrdering::NotAtomic);

  /// Return the pointer information for this memory operand.
  /// \return Reference to the MachinePointerInfo for this operand.
  const MachinePointerInfo &getPointerInfo() const { return PtrInfo; }

  /// Return the base address of the memory access as an IR Value, if any.
  ///
  /// This may either be a normal LLVM IR Value, or one of the special values
  /// used in CodeGen. Special values are those obtained via
  /// PseudoSourceValue::getFixedStack(int), PseudoSourceValue::getStack, and
  /// other PseudoSourceValue member functions which return objects which stand
  /// for frame/stack pointer relative references and other special references
  /// which are not representable in the high-level IR.
  /// \return The IR Value base address, or nullptr if none.
  const Value *getValue() const {
    return dyn_cast_if_present<const Value *>(PtrInfo.V);
  }

  /// Return the base address as a PseudoSourceValue, if any.
  /// \return The PseudoSourceValue base, or nullptr if none.
  const PseudoSourceValue *getPseudoValue() const {
    return dyn_cast_if_present<const PseudoSourceValue *>(PtrInfo.V);
  }

  /// Return the base address as an opaque pointer, or null if unknown.
  /// \return Opaque pointer to the base value, or nullptr if unknown.
  const void *getOpaqueValue() const { return PtrInfo.V.getOpaqueValue(); }

  /// Return the raw flags of the source value, \see Flags.
  /// \return The memory operand flags bitfield.
  Flags getFlags() const { return FlagVals; }

  /// Bitwise OR the current flags with the given flags.
  /// \param f Flags to combine into the existing flag set.
  void setFlags(Flags f) { FlagVals |= f; }

  /// For normal values, this is a byte offset added to the base address.
  /// For PseudoSourceValue::FPRel values, this is the FrameIndex number.
  /// \return The byte offset or frame index associated with the access.
  int64_t getOffset() const { return PtrInfo.Offset; }

  /// Return the LLVM IR address space of the memory access.
  /// \return The address space number of the pointer.
  unsigned getAddrSpace() const { return PtrInfo.getAddrSpace(); }

  /// Return the memory type of the memory reference. This should only be relied
  /// on for GlobalISel G_* operation legalization.
  /// \return The LLT of the memory access.
  LLT getMemoryType() const { return MemoryType; }

  /// Return the size in bytes of the memory reference.
  /// \return Precise size in bytes, or beforeOrAfterPointer if unknown.
  LocationSize getSize() const {
    return MemoryType.isValid()
               ? LocationSize::precise(MemoryType.getSizeInBytes())
               : LocationSize::beforeOrAfterPointer();
  }

  /// Return the size in bits of the memory reference.
  /// \return Precise size in bits, or beforeOrAfterPointer if unknown.
  LocationSize getSizeInBits() const {
    return MemoryType.isValid()
               ? LocationSize::precise(MemoryType.getSizeInBits())
               : LocationSize::beforeOrAfterPointer();
  }

  /// Return the low-level type of the memory access.
  /// \return The LLT describing the memory access.
  LLT getType() const {
    return MemoryType;
  }

  /// Return the minimum known alignment in bytes of the actual memory
  /// reference.
  /// \return The alignment of the memory reference including offset.
  LLVM_ABI Align getAlign() const;

  /// Return the minimum known alignment in bytes of the base address, without
  /// the offset.
  /// \return The base alignment of the access.
  Align getBaseAlign() const { return BaseAlign; }

  /// Return the AA tags for the memory reference.
  /// \return The alias-analysis metadata for this access.
  AAMDNodes getAAInfo() const { return AAInfo; }

  /// Return the range tag for the memory reference.
  /// \return The range metadata node, or nullptr if none.
  const MDNode *getRanges() const { return Ranges; }

  /// Return the cache hint metadata for the memory reference.
  /// \return The cache-hint metadata node, or nullptr if none.
  const MDNode *getMemCacheHint() const { return MemCacheHint; }

  /// Returns the synchronization scope ID for this memory operation.
  /// \return The synchronization scope ID.
  SyncScope::ID getSyncScopeID() const {
    return static_cast<SyncScope::ID>(AtomicInfo.SSID);
  }

  /// Return the atomic ordering requirements for this memory operation. For
  /// cmpxchg atomic operations, return the atomic ordering requirements when
  /// store occurs.
  /// \return The success (or only) atomic ordering.
  AtomicOrdering getSuccessOrdering() const {
    return static_cast<AtomicOrdering>(AtomicInfo.Ordering);
  }

  /// For cmpxchg atomic operations, return the atomic ordering requirements
  /// when store does not occur.
  /// \return The failure atomic ordering for cmpxchg.
  AtomicOrdering getFailureOrdering() const {
    return static_cast<AtomicOrdering>(AtomicInfo.FailureOrdering);
  }

  /// Return an atomic ordering at least as strong as success and failure.
  ///
  /// For operations other than cmpxchg, this is equivalent to
  /// getSuccessOrdering().
  /// \return The merged atomic ordering of success and failure.
  AtomicOrdering getMergedOrdering() const {
    return getMergedAtomicOrdering(getSuccessOrdering(), getFailureOrdering());
  }

  /// Return true if this memory operand describes a load.
  /// \return True if the MOLoad flag is set.
  bool isLoad() const { return FlagVals & MOLoad; }
  /// Return true if this memory operand describes a store.
  /// \return True if the MOStore flag is set.
  bool isStore() const { return FlagVals & MOStore; }
  /// Return true if this memory access is volatile.
  /// \return True if the MOVolatile flag is set.
  bool isVolatile() const { return FlagVals & MOVolatile; }
  /// Return true if this memory access is non-temporal.
  /// \return True if the MONonTemporal flag is set.
  bool isNonTemporal() const { return FlagVals & MONonTemporal; }
  /// Return true if this memory access is known dereferenceable.
  /// \return True if the MODereferenceable flag is set.
  bool isDereferenceable() const { return FlagVals & MODereferenceable; }
  /// Return true if this memory access is invariant.
  /// \return True if the MOInvariant flag is set.
  bool isInvariant() const { return FlagVals & MOInvariant; }

  /// Returns true if this operation has an atomic ordering requirement of
  /// unordered or higher, false otherwise.
  /// \return True if the success ordering is unordered or stronger.
  bool isAtomic() const {
    return getSuccessOrdering() != AtomicOrdering::NotAtomic;
  }

  /// Return true if this access has no ordering beyond normal aliasing.
  ///
  /// Volatile and (ordered) atomic memory operations can't be reordered.
  /// \return True if the access is unordered (and not volatile).
  bool isUnordered() const {
    return (getSuccessOrdering() == AtomicOrdering::NotAtomic ||
            getSuccessOrdering() == AtomicOrdering::Unordered) &&
           !isVolatile();
  }

  /// Update alignment from \p MMO when it is stricter.
  ///
  /// This must only be used when the new alignment applies to all users of this
  /// MachineMemOperand.
  /// \param MMO Operand whose alignment may replace this one's if greater.
  LLVM_ABI void refineAlignment(const MachineMemOperand *MMO);

  /// Change the SourceValue for this MachineMemOperand. This should only be
  /// used when an object is being relocated and all references to it are being
  /// updated.
  /// \param NewSV New IR value that becomes the base address.
  void setValue(const Value *NewSV) { PtrInfo.V = NewSV; }
  /// Change the PseudoSourceValue for this MachineMemOperand.
  ///
  /// This should only be used when an object is being relocated and all
  /// references to it are being updated.
  /// \param NewSV New pseudo source value that becomes the base address.
  void setValue(const PseudoSourceValue *NewSV) { PtrInfo.V = NewSV; }
  /// Set the byte offset from the base pointer for this memory operand.
  /// \param NewOffset New byte offset from the base pointer.
  void setOffset(int64_t NewOffset) { PtrInfo.Offset = NewOffset; }

  /// Reset the tracked memory type.
  /// \param NewTy New low-level type for the memory access.
  void setType(LLT NewTy) {
    MemoryType = NewTy;
  }

  /// Unset the tracked range metadata.
  void clearRanges() { Ranges = nullptr; }

  /// Unset the cache hint metadata.
  void clearMemCacheHint() { MemCacheHint = nullptr; }

  /// Support for operator<<.
  /// @{
  /// Print this memory operand to \p OS.
  /// \param OS Stream to print to.
  /// \param MST Module slot tracker for IR value numbering.
  /// \param SSNs Collector for sync-scope names referenced while printing.
  /// \param Context LLVM context used when printing metadata.
  /// \param MFI Optional machine frame info for stack object names.
  /// \param TII Optional target instr info for target-specific printing.
  LLVM_ABI void print(raw_ostream &OS, ModuleSlotTracker &MST,
                      SmallVectorImpl<StringRef> &SSNs,
                      const LLVMContext &Context, const MachineFrameInfo *MFI,
                      const TargetInstrInfo *TII) const;
  /// @}

  /// Return true if two memory operands compare equal.
  /// \param LHS Left-hand memory operand.
  /// \param RHS Right-hand memory operand.
  /// \return True if the operands compare equal.
  friend bool operator==(const MachineMemOperand &LHS,
                         const MachineMemOperand &RHS) {
    return LHS.getValue() == RHS.getValue() &&
           LHS.getPseudoValue() == RHS.getPseudoValue() &&
           LHS.getSize() == RHS.getSize() &&
           LHS.getOffset() == RHS.getOffset() &&
           LHS.getFlags() == RHS.getFlags() &&
           LHS.getAAInfo() == RHS.getAAInfo() &&
           LHS.getRanges() == RHS.getRanges() &&
           LHS.getMemCacheHint() == RHS.getMemCacheHint() &&
           LHS.getAlign() == RHS.getAlign() &&
           LHS.getAddrSpace() == RHS.getAddrSpace() &&
           LHS.getSuccessOrdering() == RHS.getSuccessOrdering() &&
           LHS.getFailureOrdering() == RHS.getFailureOrdering() &&
           LHS.getSyncScopeID() == RHS.getSyncScopeID();
  }

  /// Return true if two memory operands are not equal.
  /// \param LHS Left-hand memory operand.
  /// \param RHS Right-hand memory operand.
  /// \return True if the operands are not equal.
  friend bool operator!=(const MachineMemOperand &LHS,
                         const MachineMemOperand &RHS) {
    return !(LHS == RHS);
  }
};

} // End llvm namespace

#endif
