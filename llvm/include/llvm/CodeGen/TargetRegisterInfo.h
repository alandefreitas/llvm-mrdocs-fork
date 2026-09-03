//==- CodeGen/TargetRegisterInfo.h - Target Register Information -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file describes an abstract interface used to get information about a
// target machines register file.  This information is used for a variety of
// purposed, especially register allocation.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETREGISTERINFO_H
#define LLVM_CODEGEN_TARGETREGISTERINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/RegisterBank.h"
#include "llvm/CodeGen/TargetFrameLowering.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegisterInfo.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/Printable.h"
#include <cassert>
#include <cstdint>

namespace llvm {

class BitVector;
class DIExpression;
class LiveRegMatrix;
class MachineFunction;
class MachineInstr;
class RegScavenger;
class VirtRegMap;
class LiveIntervals;
class LiveInterval;

// TODO: Remove.
using TargetRegisterClass = MCRegisterClass;

/// Extra information, not in MCRegisterDesc, about registers.
/// These are used by codegen, not by MC.
struct TargetRegisterInfoDesc {
  /// Extra cost of instructions using each register, per cost table.
  const uint8_t *CostPerUse;
  /// Number of cost values associated with each register.
  unsigned NumCosts;
  /// Whether each register belongs to an allocatable register class.
  const bool *InAllocatableClass;
};

/// Each TargetRegisterClass has a per register weight, and weight
/// limit which must be less than the limits of its pressure sets.
struct RegClassWeight {
  /// Weight contributed by one register from the class.
  unsigned RegWeight;
  /// Maximum total weight allowed for registers from the class.
  unsigned WeightLimit;
};

/// Target register information for a machine's register file.
///
/// We assume that the target defines a static array of TargetRegisterDesc
/// objects that represent all of the machine registers that the target has.
/// As such, we simply have to track a pointer to this array so that we can
/// turn register number into a register descriptor.
class LLVM_ABI TargetRegisterInfo : public MCRegisterInfo {
public:
  /// Iterator over SimpleValueType entries for a register class.
  using vt_iterator = const MVT::SimpleValueType *;
  /// Per-register-class size, spill, and value-type list metadata.
  struct RegClassInfo {
    /// Size of a register in this class, in bits.
    unsigned RegSize;
    /// Spill slot size for this class, in bits.
    unsigned SpillSize;
    /// Spill slot alignment for this class, in bits.
    unsigned SpillAlignment;
    /// Offset into the concatenated legal value-type list.
    unsigned VTListOffset;
  };

  /// SubRegCoveredBits - Emitted by tablegen: bit range covered by a subreg
  /// index, -1 in any being invalid.
  struct SubRegCoveredBits {
    /// Bit offset of the covered range, or -1 if invalid.
    uint32_t Offset;
    /// Bit size of the covered range, or -1 if invalid.
    uint32_t Size;
  };

private:
  const TargetRegisterInfoDesc *InfoDesc;     // Extra desc array for codegen
  const char *SubRegIndexStrings;             // Names of subreg indexes.
  ArrayRef<uint32_t> SubRegIndexNameOffsets;
  const SubRegCoveredBits *SubRegIdxRanges;   // Pointer to the subreg covered
                                              // bit ranges array.

  // Pointer to array of lane masks, one per sub-reg index.
  const LaneBitmask *SubRegIndexLaneMasks;

  LaneBitmask CoveringLanes;
  const RegClassInfo *const RCInfos;
  const MVT::SimpleValueType *const RCVTLists;
  unsigned HwMode;

protected:
  /// Construct target register information from TableGen-emitted tables.
  ///
  /// \param ID Extra per-register codegen descriptors.
  /// \param SubRegIndexStrings Concatenated sub-register index name strings.
  /// \param SubRegIndexNameOffsets Offsets into \p SubRegIndexStrings.
  /// \param SubRegIdxRanges Covered bit ranges for each sub-register index.
  /// \param SubRegIndexLaneMasks Lane masks for each sub-register index.
  /// \param CoveringLanes Lane mask of covering lanes.
  /// \param RCInfos Per-class size/spill/VT-list info for each HwMode.
  /// \param RCVTLists Concatenated legal value-type lists for register classes.
  /// \param Mode Hardware mode selecting the active register-class info.
  TargetRegisterInfo(const TargetRegisterInfoDesc *ID,
                     const char *SubRegIndexStrings,
                     ArrayRef<uint32_t> SubRegIndexNameOffsets,
                     const SubRegCoveredBits *SubRegIdxRanges,
                     const LaneBitmask *SubRegIndexLaneMasks,
                     LaneBitmask CoveringLanes,
                     const RegClassInfo *const RCInfos,
                     const MVT::SimpleValueType *const RCVTLists,
                     unsigned Mode = 0);

public:
  /// Destroy the target register info object.
  ~TargetRegisterInfo() override;

  /// Return the number of registers for the function. (may overestimate)
  ///
  /// \param MF Function whose supported register count is requested.
  /// \return The number of supported registers for \p MF (may overestimate).
  virtual unsigned getNumSupportedRegs(const MachineFunction &MF) const {
    return getNumRegs();
  }

  // Register numbers can represent physical registers, virtual registers, and
  // sometimes stack slots. The unsigned values are divided into these ranges:
  //
  //   0           Not a register, can be used as a sentinel.
  //   [1;2^30)    Physical registers assigned by TableGen.
  //   [2^30;2^31) Stack slots. (Rarely used.)
  //   [2^31;2^32) Virtual registers assigned by MachineRegisterInfo.
  //
  // Further sentinels can be allocated from the small negative integers.
  // DenseMapInfo<unsigned> uses -1u and -2u.

  /// Return the size in bits of a register from class RC.
  ///
  /// \param RC Register class whose register size is requested.
  /// \return The size in bits of a register from \p RC.
  TypeSize getRegSizeInBits(const TargetRegisterClass &RC) const {
    return TypeSize::getFixed(getRegClassInfo(RC).RegSize);
  }

  /// Return the size in bytes of the stack slot allocated to hold a spilled
  /// copy of a register from class RC.
  ///
  /// \param RC Register class whose spill size is requested.
  /// \return The spill slot size in bytes for a register from \p RC.
  unsigned getSpillSize(const TargetRegisterClass &RC) const {
    return getRegClassInfo(RC).SpillSize / 8;
  }

  /// Return the minimum required alignment in bytes for a spill slot for
  /// a register of this class.
  ///
  /// \param RC Register class whose spill alignment is requested.
  /// \return The minimum spill slot alignment in bytes for \p RC.
  Align getSpillAlign(const TargetRegisterClass &RC) const {
    return Align(getRegClassInfo(RC).SpillAlignment / 8);
  }

  /// Return the stack ID for spill slots holding a spilled copy of a register
  /// from this class.
  ///
  /// \param RC Register class whose spill stack ID is requested.
  /// \return The stack ID used for spill slots of \p RC.
  TargetStackID::Value getSpillStackID(const TargetRegisterClass &RC) const {
    return static_cast<TargetStackID::Value>(RC.SpillStackID);
  }

  /// Return true if the given TargetRegisterClass has the ValueType T.
  ///
  /// \param RC Register class being tested.
  /// \param T Value type that must be legal for \p RC.
  /// \return True if \p RC can represent value type \p T.
  bool isTypeLegalForClass(const TargetRegisterClass &RC, MVT T) const {
    for (auto I = legalclasstypes_begin(RC); *I != MVT::Other; ++I)
      if (MVT(*I) == T)
        return true;
    return false;
  }

  /// Return true if the given TargetRegisterClass is compatible with LLT T.
  ///
  /// \param RC Register class being tested.
  /// \param T LLT that must be legal for \p RC.
  /// \return True if \p RC is compatible with LLT \p T.
  bool isTypeLegalForClass(const TargetRegisterClass &RC, LLT T) const {
    for (auto I = legalclasstypes_begin(RC); *I != MVT::Other; ++I) {
      MVT VT(*I);
      if (VT == MVT::Untyped)
        return true;

      if (LLT(VT) == T)
        return true;
    }
    return false;
  }

  /// Loop over all of the value types that can be represented by values
  /// in the given register class.
  ///
  /// \param RC Register class whose legal value types are being enumerated.
  /// \return An iterator to the first legal value type for \p RC.
  vt_iterator legalclasstypes_begin(const TargetRegisterClass &RC) const {
    return &RCVTLists[getRegClassInfo(RC).VTListOffset];
  }

  /// Return an iterator past the last legal value type for \p RC.
  ///
  /// \param RC Register class whose legal value types are being enumerated.
  /// \return An iterator past the last legal value type for \p RC.
  vt_iterator legalclasstypes_end(const TargetRegisterClass &RC) const {
    vt_iterator I = legalclasstypes_begin(RC);
    while (*I != MVT::Other)
      ++I;
    return I;
  }

  /// Returns the Register Class of a physical register, picking the smallest
  /// register subclass that contains this physreg.
  ///
  /// \param Reg Physical register whose minimal class is requested.
  /// \return The smallest register class containing \p Reg.
  virtual const TargetRegisterClass *
  getMinimalPhysRegClass(MCRegister Reg) const = 0;

  /// Returns the common Register Class of two physical registers, picking the
  /// smallest register subclass that contains these two physregs.
  ///
  /// \param Reg1 First physical register.
  /// \param Reg2 Second physical register.
  /// \return The smallest register class containing both \p Reg1 and \p Reg2.
  const TargetRegisterClass *
  getCommonMinimalPhysRegClass(MCRegister Reg1, MCRegister Reg2) const;

  /// Return the maximal subclass of the given register class that is
  /// allocatable or NULL.
  ///
  /// \param RC Register class whose maximal allocatable subclass is requested.
  /// \return The maximal allocatable subclass of \p RC, or null.
  const TargetRegisterClass *
    getAllocatableClass(const TargetRegisterClass *RC) const;

  /// Returns a bitset indexed by register number indicating if a register is
  /// allocatable or not. If a register class is specified, returns the subset
  /// for the class.
  ///
  /// \param MF Function whose allocatable register set is requested.
  /// \param RC Optional register class that restricts the returned set.
  /// \return A bitset of allocatable registers, optionally restricted to \p RC.
  BitVector getAllocatableSet(const MachineFunction &MF,
                              const TargetRegisterClass *RC = nullptr) const;

  /// Get a list of cost values for all registers that correspond to the index
  /// returned by RegisterCostTableIndex.
  ///
  /// \param MF Function whose register cost table is selected.
  /// \return Cost values for all registers for the cost table selected by \p MF.
  ArrayRef<uint8_t> getRegisterCosts(const MachineFunction &MF) const {
    unsigned Idx = getRegisterCostTableIndex(MF);
    unsigned NumRegs = getNumRegs();
    assert(Idx < InfoDesc->NumCosts && "CostPerUse index out of bounds");

    return ArrayRef(&InfoDesc->CostPerUse[Idx * NumRegs], NumRegs);
  }

  /// Return true if the register is in the allocation of any register class.
  ///
  /// \param RegNo Physical register being tested.
  /// \return True if \p RegNo belongs to an allocatable register class.
  bool isInAllocatableClass(MCRegister RegNo) const {
    return InfoDesc->InAllocatableClass[RegNo];
  }

  /// Return the human-readable symbolic target-specific name for the specified
  /// SubRegIndex.
  ///
  /// \param SubIdx Sub-register index whose name is requested.
  /// \return The target-specific name of sub-register index \p SubIdx.
  const char *getSubRegIndexName(unsigned SubIdx) const {
    assert(SubIdx && SubIdx < getNumSubRegIndices() &&
           "This is not a subregister index");
    return SubRegIndexStrings + SubRegIndexNameOffsets[SubIdx - 1];
  }

  /// Get the size of the bit range covered by a sub-register index.
  ///
  /// If the index isn't continuous, return the sum of the sizes of its parts.
  /// If the index is used to access subregisters of different sizes, return -1.
  ///
  /// \param Idx Sub-register index whose covered bit size is requested.
  /// \return The covered bit size of \p Idx, the sum of parts if discontinuous, or -1 if sizes vary.
  unsigned getSubRegIdxSize(unsigned Idx) const;

  /// Get the offset of the bit range covered by a sub-register index.
  ///
  /// If an Offset doesn't make sense (the index isn't continuous, or is used to
  /// access sub-registers at different offsets), return -1.
  ///
  /// \param Idx Sub-register index whose covered bit offset is requested.
  /// \return The covered bit offset of \p Idx, or -1 if no single offset applies.
  unsigned getSubRegIdxOffset(unsigned Idx) const;

  /// Return a bitmask representing the parts of a register that are covered by
  /// SubIdx \see LaneBitmask.
  ///
  /// SubIdx == 0 is allowed, it has the lane mask ~0u.
  ///
  /// \param SubIdx Sub-register index whose lane mask is requested.
  /// \return The lane mask covered by \p SubIdx.
  LaneBitmask getSubRegIndexLaneMask(unsigned SubIdx) const {
    assert(SubIdx < getNumSubRegIndices() && "This is not a subregister index");
    return SubRegIndexLaneMasks[SubIdx];
  }

  /// Try to find one or more subregister indexes to cover \p LaneMask.
  ///
  /// If this is possible, returns true and appends the best matching set of
  /// indexes to \p Indexes. If this is not possible, returns false.
  ///
  /// \param RC Register class whose sub-register indexes are considered.
  /// \param LaneMask Lane mask that should be covered.
  /// \param Indexes Output vector receiving covering sub-register indexes.
  /// \return True if covering indexes were found and appended to \p Indexes.
  bool getCoveringSubRegIndexes(const TargetRegisterClass *RC,
                                LaneBitmask LaneMask,
                                SmallVectorImpl<unsigned> &Indexes) const;

  /// Return the lane mask of lanes that completely cover their sub-registers.
  ///
  /// The lane masks returned by getSubRegIndexLaneMask() above can only be
  /// used to determine if sub-registers overlap - they can't be used to
  /// determine if a set of sub-registers completely cover another
  /// sub-register.
  ///
  /// The X86 general purpose registers have two lanes corresponding to the
  /// sub_8bit and sub_8bit_hi sub-registers. Both sub_32bit and sub_16bit have
  /// lane masks '3', but the sub_16bit sub-register doesn't fully cover the
  /// sub_32bit sub-register.
  ///
  /// On the other hand, the ARM NEON lanes fully cover their registers: The
  /// dsub_0 sub-register is completely covered by the ssub_0 and ssub_1 lanes.
  /// This is related to the CoveredBySubRegs property on register definitions.
  ///
  /// This function returns a bit mask of lanes that completely cover their
  /// sub-registers. More precisely, given:
  ///
  ///   Covering = getCoveringLanes();
  ///   MaskA = getSubRegIndexLaneMask(SubA);
  ///   MaskB = getSubRegIndexLaneMask(SubB);
  ///
  /// If (MaskA & ~(MaskB & Covering)) == 0, then SubA is completely covered by
  /// SubB.
  ///
  /// \return A bit mask of lanes that completely cover their sub-registers.
  LaneBitmask getCoveringLanes() const { return CoveringLanes; }

  /// Returns true if the two registers are equal or alias each other.
  /// The registers may be virtual registers.
  ///
  /// \param RegA First register.
  /// \param RegB Second register.
  /// \return True if \p RegA and \p RegB are equal or alias each other.
  bool regsOverlap(Register RegA, Register RegB) const {
    if (RegA == RegB)
      return true;
    if (RegA.isPhysical() && RegB.isPhysical())
      return MCRegisterInfo::regsOverlap(RegA.asMCReg(), RegB.asMCReg());
    return false;
  }

  /// Returns true if the two subregisters are equal or overlap.
  /// The registers may be virtual registers.
  ///
  /// \param RegA First register.
  /// \param SubA Sub-register index of \p RegA.
  /// \param RegB Second register.
  /// \param SubB Sub-register index of \p RegB.
  /// \return True if the two subregisters are equal or overlap.
  bool checkSubRegInterference(Register RegA, unsigned SubA, Register RegB,
                               unsigned SubB) const;

  /// Returns true if Reg contains RegUnit.
  ///
  /// \param Reg Physical register being tested.
  /// \param RegUnit Register unit that may be contained in \p Reg.
  /// \return True if \p Reg contains \p RegUnit.
  bool hasRegUnit(MCRegister Reg, MCRegUnit RegUnit) const {
    return llvm::is_contained(regunits(Reg), RegUnit);
  }

  /// Walk a copy-like chain back to the original source register.
  ///
  /// Returns the original SrcReg unless it is the target of a copy-like
  /// operation, in which case we chain backwards through all such operations to
  /// the ultimate source register.  If a physical register is encountered, we
  /// stop the search.
  ///
  /// \param SrcReg Register at which the copy-like walk begins.
  /// \param MRI Machine register info used to follow definitions.
  /// \return The ultimate source register at the end of the copy-like chain.
  virtual Register lookThruCopyLike(Register SrcReg,
                                    const MachineRegisterInfo *MRI) const;

  /// Walk a single-use copy-like chain back to the original source register.
  ///
  /// Find the original SrcReg unless it is the target of a copy-like operation,
  /// in which case we chain backwards through all such operations to the
  /// ultimate source register. If a physical register is encountered, we stop
  /// the search. Return the original SrcReg if all the definitions in the chain
  /// only have one user and not a physical register.
  ///
  /// \param SrcReg Register at which the copy-like walk begins.
  /// \param MRI Machine register info used to follow definitions.
  /// \return The original source register if the chain is single-use; otherwise \p SrcReg.
  virtual Register
  lookThruSingleUseCopyChain(Register SrcReg,
                             const MachineRegisterInfo *MRI) const;

  /// Return the target callee-saved register list for \p MF.
  ///
  /// Return a null-terminated list of all of the callee-saved registers on
  /// this target. The register should be in the order of desired callee-save
  /// stack frame offset. The first register is closest to the incoming stack
  /// pointer if stack grows down, and vice versa.
  /// Notice: This function does not take into account disabled CSRs.
  ///         In most cases you will want to use instead the function
  ///         getCalleeSavedRegs that is implemented in MachineRegisterInfo.
  ///
  /// \param MF Function whose callee-saved registers are requested.
  /// \return A null-terminated list of callee-saved registers for \p MF.
  virtual const MCPhysReg*
  getCalleeSavedRegs(const MachineFunction *MF) const = 0;

  /// Return the IPRA callee-saved register list for \p MF.
  ///
  /// Return a null-terminated list of all of the callee-saved registers on
  /// this target when IPRA is on. The list should include any non-allocatable
  /// registers that the backend uses and assumes will be saved by all calling
  /// conventions. This is typically the ISA-standard frame pointer, but could
  /// include the thread pointer, TOC pointer, or base pointer for different
  /// targets.
  ///
  /// \param MF Function whose IPRA callee-saved registers are requested.
  /// \return A null-terminated list of IPRA callee-saved registers, or null.
  virtual const MCPhysReg *getIPRACSRegs(const MachineFunction *MF) const {
    return nullptr;
  }

  /// Return the call-preserved register mask for a calling convention.
  ///
  /// Return a mask of call-preserved registers for the given calling convention
  /// on the current function. The mask should include all call-preserved
  /// aliases. This is used by the register allocator to determine which
  /// registers can be live across a call.
  ///
  /// The mask is an array containing (TRI::getNumRegs()+31)/32 entries.
  /// A set bit indicates that all bits of the corresponding register are
  /// preserved across the function call.  The bit mask is expected to be
  /// sub-register complete, i.e. if A is preserved, so are all its
  /// sub-registers.
  ///
  /// Bits are numbered from the LSB, so the bit for physical register Reg can
  /// be found as (Mask[Reg / 32] >> Reg % 32) & 1.
  ///
  /// A NULL pointer means that no register mask will be used, and call
  /// instructions should use implicit-def operands to indicate call clobbered
  /// registers.
  ///
  /// \param MF Function whose call-preserved mask is requested.
  /// \param CC Calling convention whose preserved mask is requested.
  /// \return A call-preserved register mask for \p CC, or null.
  virtual const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                               CallingConv::ID CC) const {
    // The default mask clobbers everything.  All targets should override.
    return nullptr;
  }

  /// Return a register mask for the registers preserved by the unwinder,
  /// or nullptr if no custom mask is needed.
  ///
  /// \param MF Function whose EH-pad preserved mask is requested.
  /// \return An EH-pad preserved register mask, or null if none is needed.
  virtual const uint32_t *
  getCustomEHPadPreservedMask(const MachineFunction &MF) const {
    return nullptr;
  }

  /// Return a register mask that clobbers everything.
  ///
  /// \return A register mask that clobbers every register.
  virtual const uint32_t *getNoPreservedMask() const {
    llvm_unreachable("target does not provide no preserved mask");
  }

  /// Return registers clobbered inside a call to \p MF.
  ///
  /// Return a list of all of the registers which are clobbered "inside" a call
  /// to the given function. For example, these might be needed for PLT
  /// sequences of long-branch veneers.
  ///
  /// \param MF Function whose intra-call clobbers are requested.
  /// \return The registers clobbered inside a call to \p MF.
  virtual ArrayRef<MCPhysReg>
  getIntraCallClobberedRegs(const MachineFunction *MF) const {
    return {};
  }

  /// Return true if all bits that are set in mask \p mask0 are also set in
  /// \p mask1.
  ///
  /// \param mask0 Subset candidate register mask.
  /// \param mask1 Superset candidate register mask.
  /// \return True if every bit set in \p mask0 is also set in \p mask1.
  bool regmaskSubsetEqual(const uint32_t *mask0, const uint32_t *mask1) const;

  /// Return all the call-preserved register masks defined for this target.
  ///
  /// \return All call-preserved register masks defined for this target.
  virtual ArrayRef<const uint32_t *> getRegMasks() const = 0;
  /// Return the names of the call-preserved register masks for this target.
  ///
  /// \return The names of the call-preserved register masks for this target.
  virtual ArrayRef<const char *> getRegMaskNames() const = 0;

  /// Return the set of registers reserved for \p MF.
  ///
  /// Returns a bitset indexed by physical register number indicating if a
  /// register is a special register that has particular uses and should be
  /// considered unavailable at all times, e.g. stack pointer, return address.
  /// A reserved register:
  /// - is not allocatable
  /// - is considered always live
  /// - is ignored by liveness tracking
  /// It is often necessary to reserve the super registers of a reserved
  /// register as well, to avoid them getting allocated indirectly. You may use
  /// markSuperRegs() and checkAllSuperRegsMarked() in this case.
  ///
  /// \param MF Function whose reserved registers are requested.
  /// \return A bitset of reserved physical registers for \p MF.
  virtual BitVector getReservedRegs(const MachineFunction &MF) const = 0;

  /// Return an explanation for why \p PhysReg is reserved, if available.
  ///
  /// Returns either a string explaining why the given register is reserved for
  /// this function, or an empty optional if no explanation has been written.
  /// The absence of an explanation does not mean that the register is not
  /// reserved (meaning, you should check that PhysReg is in fact reserved
  /// before calling this).
  ///
  /// \param MF Function whose reserved-register explanation is queried.
  /// \param PhysReg Physical register whose reservation is explained.
  /// \return An explanation string if available, or an empty optional.
  virtual std::optional<std::string>
  explainReservedReg(const MachineFunction &MF, MCRegister PhysReg) const {
    return {};
  }

  /// Returns false if we can't guarantee that Physreg, specified as an IR asm
  /// clobber constraint, will be preserved across the statement.
  ///
  /// \param MF Function in which the asm clobber applies.
  /// \param PhysReg Physical register named by the clobber constraint.
  /// \return False if \p PhysReg cannot be guaranteed preserved across an IR asm clobber.
  virtual bool isAsmClobberable(const MachineFunction &MF,
                                MCRegister PhysReg) const {
    return true;
  }

  /// Returns true if PhysReg cannot be written to in inline asm statements.
  ///
  /// \param MF Function in which the inline-asm constraint applies.
  /// \param PhysReg Physical register being tested.
  /// \return True if \p PhysReg cannot be written in inline asm.
  virtual bool isInlineAsmReadOnlyReg(const MachineFunction &MF,
                                      MCRegister PhysReg) const {
    return false;
  }

  /// Returns true if PhysReg is unallocatable and constant throughout the
  /// function.  Used by MachineRegisterInfo::isConstantPhysReg().
  ///
  /// \param PhysReg Physical register being tested.
  /// \return True if \p PhysReg is unallocatable and constant throughout the function.
  virtual bool isConstantPhysReg(MCRegister PhysReg) const { return false; }

  /// Returns true if the register class is considered divergent.
  ///
  /// \param RC Register class being tested.
  /// \return True if \p RC is considered divergent.
  virtual bool isDivergentRegClass(const TargetRegisterClass *RC) const {
    return false;
  }

  /// Returns true if the register is considered uniform.
  ///
  /// \param MRI Machine register info for the function.
  /// \param RBI Register bank info for the function.
  /// \param Reg Register being tested for uniformity.
  /// \return True if \p Reg is considered uniform.
  virtual bool isUniformReg(const MachineRegisterInfo &MRI,
                            const RegisterBankInfo &RBI, Register Reg) const {
    return false;
  }

  /// Returns true if MachineLoopInfo should analyze the given physreg
  /// for loop invariance.
  ///
  /// \param R Physical register being considered for invariance analysis.
  /// \return True if MachineLoopInfo should analyze \p R for loop invariance.
  virtual bool shouldAnalyzePhysregInMachineLoopInfo(MCRegister R) const {
    return false;
  }

  /// Return true if \p PhysReg is restored before any use after modification.
  ///
  /// Physical registers that may be modified within a function but are
  /// guaranteed to be restored before any uses. This is useful for targets that
  /// have call sequences where a GOT register may be updated by the caller
  /// prior to a call and is guaranteed to be restored (also by the caller)
  /// after the call.
  ///
  /// \param PhysReg Physical register being tested.
  /// \param MF Function in which the register is classified.
  /// \return True if \p PhysReg is restored before any use after modification.
  virtual bool isCallerPreservedPhysReg(MCRegister PhysReg,
                                        const MachineFunction &MF) const {
    return false;
  }

  /// This is a wrapper around getCallPreservedMask().
  /// Return true if the register is preserved after the call.
  ///
  /// \param PhysReg Physical register being tested.
  /// \param MF Function whose calling convention is queried.
  /// \return True if \p PhysReg is preserved after a call.
  virtual bool isCalleeSavedPhysReg(MCRegister PhysReg,
                                    const MachineFunction &MF) const;

  /// Returns true if PhysReg can be used as an argument to a function.
  ///
  /// \param MF Function in which the register is classified.
  /// \param PhysReg Physical register being tested.
  /// \return True if \p PhysReg can be used as a function argument.
  virtual bool isArgumentRegister(const MachineFunction &MF,
                                  MCRegister PhysReg) const {
    return false;
  }

  /// Returns true if PhysReg is a fixed register.
  ///
  /// \param MF Function in which the register is classified.
  /// \param PhysReg Physical register being tested.
  /// \return True if \p PhysReg is a fixed register.
  virtual bool isFixedRegister(const MachineFunction &MF,
                               MCRegister PhysReg) const {
    return false;
  }

  /// Returns true if PhysReg is a general purpose register.
  ///
  /// \param MF Function in which the register is classified.
  /// \param PhysReg Physical register being tested.
  /// \return True if \p PhysReg is a general purpose register.
  virtual bool isGeneralPurposeRegister(const MachineFunction &MF,
                                        MCRegister PhysReg) const {
    return false;
  }

  /// Returns true if RC is a class/subclass of general purpose register.
  ///
  /// \param RC Register class being tested.
  /// \return True if \p RC is a general purpose register class or subclass.
  virtual bool
  isGeneralPurposeRegisterClass(const TargetRegisterClass *RC) const {
    return false;
  }

  /// Adjust a stackmap/patchpoint live-out mask before it is emitted.
  ///
  /// Prior to adding the live-out mask to a stackmap or patchpoint instruction,
  /// provide the target the opportunity to adjust it (mainly to remove
  /// pseudo-registers that should be ignored).
  ///
  /// \param Mask Live-out register mask that may be edited in place.
  virtual void adjustStackMapLiveOutMask(uint32_t *Mask) const {}

  /// Return a subclass of the register class \p A so that each register in it
  /// has a sub-register of sub-register index \p Idx which is in the register
  /// class \p B.
  ///
  /// TableGen will synthesize missing A sub-classes.
  ///
  /// \param A Super-register class being specialized.
  /// \param B Required class of the \p Idx sub-registers.
  /// \param Idx Sub-register index that must land in \p B.
  /// \return A subclass of \p A whose \p Idx sub-registers are in \p B.
  virtual const TargetRegisterClass *
  getMatchingSuperRegClass(const TargetRegisterClass *A,
                           const TargetRegisterClass *B, unsigned Idx) const;

  /// Find a common register class that can accomodate both the source and
  /// destination operands of a copy-like instruction:
  ///
  /// DefRC:DefSubReg = COPY SrcRC:SrcSubReg
  ///
  /// This is a generalized form of getMatchingSuperRegClass,
  /// getCommonSuperRegClass, and getCommonSubClass which handles 0, 1, or 2
  /// subregister indexes. Those utilities should be preferred if the number of
  /// non-0 subregister indexes is known.
  ///
  /// \param DefRC Register class of the defined value.
  /// \param DefSubReg Sub-register index of the defined value.
  /// \param SrcRC Register class of the source value.
  /// \param SrcSubReg Sub-register index of the source value.
  /// \return A common register class for the copy-like operands, or null.
  const TargetRegisterClass *
  findCommonRegClass(const TargetRegisterClass *DefRC, unsigned DefSubReg,
                     const TargetRegisterClass *SrcRC,
                     unsigned SrcSubReg) const;

  /// Return true if this copy-like source is preferable to an earlier use.
  ///
  /// For a copy-like instruction that defines a register of class DefRC with
  /// subreg index DefSubReg, reading from another source with class SrcRC and
  /// subregister SrcSubReg return true if this is a preferable copy
  /// instruction or an earlier use should be used.
  ///
  /// \param DefRC Register class of the defined value.
  /// \param DefSubReg Sub-register index of the defined value.
  /// \param SrcRC Register class of the source value.
  /// \param SrcSubReg Sub-register index of the source value.
  /// \return True if this copy source is preferable to an earlier use.
  virtual bool shouldRewriteCopySrc(const TargetRegisterClass *DefRC,
                                    unsigned DefSubReg,
                                    const TargetRegisterClass *SrcRC,
                                    unsigned SrcSubReg) const {
    // If this source does not incur a cross register bank copy, use it.
    return findCommonRegClass(DefRC, DefSubReg, SrcRC, SrcSubReg) != nullptr;
  }

  /// Return the largest legal subclass of \p RC that supports subreg \p Idx.
  ///
  /// Returns the largest legal sub-class of \p RC that supports the
  /// sub-register index \p Idx. If no such sub-class exists, return NULL. If
  /// all registers in RC already have an Idx sub-register, return RC.
  ///
  /// TableGen generates a version of this function that is good enough in most
  /// cases.  Targets can override if they have constraints that TableGen
  /// doesn't understand.  For example, the x86 sub_8bit sub-register index is
  /// supported by the full GR32 register class in 64-bit mode, but only by the
  /// GR32_ABCD regiister class in 32-bit mode.
  ///
  /// TableGen will synthesize missing RC sub-classes.
  ///
  /// \param RC Register class whose supporting subclass is requested.
  /// \param Idx Sub-register index that must be supported.
  /// \return The largest subclass of \p RC supporting \p Idx, or null.
  virtual const TargetRegisterClass *
  getSubClassWithSubReg(const TargetRegisterClass *RC, unsigned Idx) const {
    assert(Idx == 0 && "Target has no sub-registers");
    return RC;
  }

  /// Returns the register class of all sub-registers of \p SuperRC obtained by
  /// applying the sub-register index \p SubRegIdx.
  ///
  /// TableGen *may not* synthesize the missing sub-register classes, so this
  /// function may return null even if SubRegIdx can be applied to all registers
  /// in SuperRC, i.e., even if
  /// isSubRegValidForRegClass(SuperRC, SubRegIdx) is true.
  ///
  /// \param SuperRC Super-register class whose sub-registers are queried.
  /// \param SubRegIdx Sub-register index applied to members of \p SuperRC.
  /// \return The register class of \p SubRegIdx sub-registers of \p SuperRC, or null.
  virtual const TargetRegisterClass *
  getSubRegisterClass(const TargetRegisterClass *SuperRC,
                      unsigned SubRegIdx) const {
    return nullptr;
  }

  /// Return true if every register in \p RC supports sub-register \p Idx.
  ///
  /// Returns true if sub-register \p Idx can be used with register class \p RC.
  /// Idx is valid if the largest subclass of RC that supports sub-register
  /// index Idx is same as RC. That is, every physical register in RC supports
  /// sub-register index Idx.
  ///
  /// \param RC Register class being tested.
  /// \param Idx Sub-register index being tested.
  /// \return True if every register in \p RC supports sub-register \p Idx.
  bool isSubRegValidForRegClass(const TargetRegisterClass *RC,
                                unsigned Idx) const {
    return getSubClassWithSubReg(RC, Idx) == RC;
  }

  /// Return the subregister index you get from composing
  /// two subregister indices.
  ///
  /// The special null sub-register index composes as the identity.
  ///
  /// If R:a:b is the same register as R:c, then composeSubRegIndices(a, b)
  /// returns c. Note that composeSubRegIndices does not tell you about illegal
  /// compositions. If R does not have a subreg a, or R:a does not have a subreg
  /// b, composeSubRegIndices doesn't tell you.
  ///
  /// The ARM register Q0 has two D subregs dsub_0:D0 and dsub_1:D1. It also has
  /// ssub_0:S0 - ssub_3:S3 subregs.
  /// If you compose subreg indices dsub_1, ssub_0 you get ssub_2.
  ///
  /// \param a First sub-register index.
  /// \param b Second sub-register index.
  /// \return The composed sub-register index of \p a and \p b.
  unsigned composeSubRegIndices(unsigned a, unsigned b) const {
    if (!a) return b;
    if (!b) return a;
    return composeSubRegIndicesImpl(a, b);
  }

  /// Return a subregister index that will compose to give you the subregister
  /// index.
  ///
  /// Finds a subregister index x such that composeSubRegIndices(a, x) ==
  /// b. Note that this relationship does not hold if
  /// reverseComposeSubRegIndices returns the null subregister.
  ///
  /// The special null sub-register index composes as the identity.
  ///
  /// \param a First sub-register index in the composition.
  /// \param b Desired composed sub-register index.
  /// \return A sub-register index that composes with \p a to yield \p b.
  unsigned reverseComposeSubRegIndices(unsigned a, unsigned b) const {
    if (!a)
      return b;
    if (!b)
      return a;
    return reverseComposeSubRegIndicesImpl(a, b);
  }

  /// Transforms a LaneMask computed for one subregister to the lanemask that
  /// would have been computed when composing the subsubregisters with IdxA
  /// first. @sa composeSubRegIndices()
  ///
  /// \param IdxA Sub-register index applied first.
  /// \param Mask Lane mask computed for the composed sub-registers.
  /// \return The lane mask after composing through \p IdxA.
  LaneBitmask composeSubRegIndexLaneMask(unsigned IdxA,
                                         LaneBitmask Mask) const {
    if (!IdxA)
      return Mask;
    return composeSubRegIndexLaneMaskImpl(IdxA, Mask);
  }

  /// Reverse-compose a virtual-register lane mask through subreg index \p IdxA.
  ///
  /// Transform a lanemask given for a virtual register to the corresponding
  /// lanemask before using subregister with index \p IdxA. This is the reverse
  /// of composeSubRegIndexLaneMask(), assuming Mask is a valie lane mask (no
  /// invalid bits set) the following holds:
  /// X0 = composeSubRegIndexLaneMask(Idx, Mask)
  /// X1 = reverseComposeSubRegIndexLaneMask(Idx, X0)
  /// => X1 == Mask
  ///
  /// \param IdxA Sub-register index previously applied to the mask.
  /// \param LaneMask Lane mask to transform.
  /// \return The lane mask before applying sub-register \p IdxA.
  LaneBitmask reverseComposeSubRegIndexLaneMask(unsigned IdxA,
                                                LaneBitmask LaneMask) const {
    if (!IdxA)
      return LaneMask;
    return reverseComposeSubRegIndexLaneMaskImpl(IdxA, LaneMask);
  }

  /// Debugging helper: dump register in human readable form to dbgs() stream.
  ///
  /// \param Reg Register to dump.
  /// \param SubRegIndex Optional sub-register index to include.
  /// \param TRI Optional target register info used for naming.
  static void dumpReg(Register Reg, unsigned SubRegIndex = 0,
                      const TargetRegisterInfo *TRI = nullptr);

  /// Return the target-defined base register class for physical register \p Reg.
  ///
  /// This is the register class with the lowest BaseClassOrder containing the
  /// register. Will be nullptr if the register is not in any base register
  /// class.
  ///
  /// \param Reg Physical register whose base class is requested.
  /// \return The base register class containing \p Reg, or null.
  virtual const TargetRegisterClass *getPhysRegBaseClass(MCRegister Reg) const {
    return nullptr;
  }

protected:
  /// Overridden by TableGen in targets that have sub-registers.
  ///
  /// \param A First sub-register index.
  /// \param B Second sub-register index.
  /// \return The composed sub-register index of \p A and \p B.
  virtual unsigned composeSubRegIndicesImpl(unsigned A, unsigned B) const {
    llvm_unreachable("Target has no sub-registers");
  }

  /// Overridden by TableGen in targets that have sub-registers.
  ///
  /// \param A First sub-register index.
  /// \param B Second sub-register index.
  /// \return A sub-register index that composes with \p A to yield \p B.
  virtual unsigned reverseComposeSubRegIndicesImpl(unsigned A,
                                                   unsigned B) const {
    llvm_unreachable("Target has no sub-registers");
  }

  /// Overridden by TableGen in targets that have sub-registers.
  ///
  /// \param IdxA Sub-register index applied first.
  /// \param Mask Lane mask computed for the composed sub-registers.
  /// \return The lane mask after composing through \p IdxA.
  virtual LaneBitmask
  composeSubRegIndexLaneMaskImpl(unsigned IdxA, LaneBitmask Mask) const {
    llvm_unreachable("Target has no sub-registers");
  }

  /// Reverse-compose a lane mask through a sub-register index.
  ///
  /// \param IdxA Sub-register index previously applied to the mask.
  /// \param LaneMask Lane mask to transform.
  /// \return The lane mask before applying sub-register \p IdxA.
  virtual LaneBitmask reverseComposeSubRegIndexLaneMaskImpl(
      unsigned IdxA, LaneBitmask LaneMask) const {
    llvm_unreachable("Target has no sub-registers");
  }

  /// Return the register cost table index for \p MF.
  ///
  /// This implementation is sufficient for most architectures and can be
  /// overriden by targets in case there are multiple cost values associated
  /// with each register.
  ///
  /// \param MF Function whose register cost table index is requested.
  /// \return The register cost table index for \p MF.
  virtual unsigned getRegisterCostTableIndex(const MachineFunction &MF) const {
    return 0;
  }

public:
  /// Find a common super-register class if it exists.
  ///
  /// Find a register class, SuperRC and two sub-register indices, PreA and
  /// PreB, such that:
  ///
  ///   1. PreA + SubA == PreB + SubB  (using composeSubRegIndices()), and
  ///
  ///   2. For all Reg in SuperRC: Reg:PreA in RCA and Reg:PreB in RCB, and
  ///
  ///   3. SuperRC->getSize() >= max(RCA->getSize(), RCB->getSize()).
  ///
  /// SuperRC will be chosen such that no super-class of SuperRC satisfies the
  /// requirements, and there is no register class with a smaller spill size
  /// that satisfies the requirements.
  ///
  /// SubA and SubB must not be 0. Use getMatchingSuperRegClass() instead.
  ///
  /// Either of the PreA and PreB sub-register indices may be returned as 0. In
  /// that case, the returned register class will be a sub-class of the
  /// corresponding argument register class.
  ///
  /// The function returns NULL if no register class can be found.
  ///
  /// \param RCA First register class.
  /// \param SubA Sub-register index into members of \p RCA.
  /// \param RCB Second register class.
  /// \param SubB Sub-register index into members of \p RCB.
  /// \param PreA Set to the composed sub-register index for \p RCA.
  /// \param PreB Set to the composed sub-register index for \p RCB.
  /// \return The common super-register class, or null if none exists.
  const TargetRegisterClass*
  getCommonSuperRegClass(const TargetRegisterClass *RCA, unsigned SubA,
                         const TargetRegisterClass *RCB, unsigned SubB,
                         unsigned &PreA, unsigned &PreB) const;

  //===--------------------------------------------------------------------===//
  // Register Class Information
  //
protected:
  /// Return tablegen-emitted info for register class \p RC.
  ///
  /// \param RC Register class whose info record is requested.
  /// \return The TableGen-emitted info record for \p RC.
  const RegClassInfo &getRegClassInfo(const TargetRegisterClass &RC) const {
    return RCInfos[getNumRegClasses() * HwMode + RC.getID()];
  }

public:
  /// Returns the register class associated with the enumeration value.
  /// See class MCOperandInfo.
  ///
  /// \param i Register-class enumeration value.
  /// \return The register class for enumeration value \p i.
  const TargetRegisterClass *getRegClass(unsigned i) const {
    return &MCRegisterInfo::getRegClass(i);
  }

  /// Find the largest common subclass of A and B.
  /// Return NULL if there is no common subclass.
  ///
  /// \param A First register class.
  /// \param B Second register class.
  /// \return The largest common subclass of \p A and \p B, or null.
  const TargetRegisterClass *
  getCommonSubClass(const TargetRegisterClass *A,
                    const TargetRegisterClass *B) const;

  /// Returns a TargetRegisterClass used for pointer values.
  /// If a target supports multiple different pointer register classes,
  /// kind specifies which one is indicated.
  ///
  /// \param Kind Selector among multiple pointer register classes.
  /// \return The register class used for pointer values of \p Kind.
  virtual const TargetRegisterClass *
  getPointerRegClass(unsigned Kind = 0) const {
    llvm_unreachable("Target didn't implement getPointerRegClass!");
  }

  /// Return a legal register class for copying values of class \p RC.
  ///
  /// Returns a legal register class to copy a register in the specified class
  /// to or from. If it is possible to copy the register directly without using
  /// a cross register class copy, return the specified RC. Returns NULL if it
  /// is not possible to copy between two registers of the specified class.
  ///
  /// \param RC Register class being copied to or from.
  /// \return A legal register class for copying values of \p RC, or null.
  virtual const TargetRegisterClass *
  getCrossCopyRegClass(const TargetRegisterClass *RC) const {
    return RC;
  }

  /// Return the largest legal same-spill-size super-class of \p RC.
  ///
  /// Returns the largest super class of RC that is legal to use in the current
  /// sub-target and has the same spill size. The returned register class can be
  /// used to create virtual registers which means that all its registers can be
  /// copied and spilled.
  ///
  /// \param RC Register class whose legal super-class is requested.
  /// \param MF Function whose subtarget constrains the legal classes.
  /// \return The largest legal same-spill-size super-class of \p RC.
  virtual const TargetRegisterClass *
  getLargestLegalSuperClass(const TargetRegisterClass *RC,
                            const MachineFunction &MF) const {
    /// The default implementation is very conservative and doesn't allow the
    /// register allocator to inflate register classes.
    return RC;
  }

  /// Return the register-pressure high-water mark for \p RC.
  ///
  /// Return the register pressure "high water mark" for the specific register
  /// class. The scheduler is in high register pressure mode (for the specific
  /// register class) if it goes over the limit.
  ///
  /// Note: this is the old register pressure model that relies on a manually
  /// specified representative register class per value type.
  ///
  /// \param RC Register class whose pressure limit is requested.
  /// \param MF Function whose scheduling pressure limits are queried.
  /// \return The register pressure high-water mark for \p RC.
  virtual unsigned getRegPressureLimit(const TargetRegisterClass *RC,
                                       MachineFunction &MF) const {
    return 0;
  }

  /// Return a heuristic score for increasing pressure set \p PSetID.
  ///
  /// Return a heuristic for the machine scheduler to compare the profitability
  /// of increasing one register pressure set versus another. The scheduler will
  /// prefer increasing the register pressure of the set which returns the
  /// largest value for this function.
  ///
  /// \param MF Function whose scheduling heuristics are queried.
  /// \param PSetID Pressure-set identifier being scored.
  /// \return A heuristic score for increasing pressure set \p PSetID.
  virtual unsigned getRegPressureSetScore(const MachineFunction &MF,
                                          unsigned PSetID) const {
    return PSetID;
  }

  /// Get the weight in units of pressure for this register class.
  ///
  /// \param RC Register class whose pressure weight is requested.
  /// \return The pressure weight for register class \p RC.
  virtual const RegClassWeight &getRegClassWeight(
    const TargetRegisterClass *RC) const = 0;

  /// Returns size in bits of a phys/virtual/generic register.
  ///
  /// \param Reg Register whose size is requested.
  /// \param MRI Machine register info used for virtual/generic registers.
  /// \return The size in bits of \p Reg.
  TypeSize getRegSizeInBits(Register Reg, const MachineRegisterInfo &MRI) const;

  /// Get the weight in units of pressure for this register unit.
  ///
  /// \param RegUnit Register unit whose pressure weight is requested.
  /// \return The pressure weight of register unit \p RegUnit.
  virtual unsigned getRegUnitWeight(MCRegUnit RegUnit) const = 0;

  /// Get the number of dimensions of register pressure.
  ///
  /// \return The number of register pressure dimensions.
  virtual unsigned getNumRegPressureSets() const = 0;

  /// Get the name of this register unit pressure set.
  ///
  /// \param Idx Pressure-set index whose name is requested.
  /// \return The name of pressure set \p Idx.
  virtual const char *getRegPressureSetName(unsigned Idx) const = 0;

  /// Get the register unit pressure limit for this dimension.
  /// This limit must be adjusted dynamically for reserved registers.
  ///
  /// \param MF Function whose pressure limits may be adjusted.
  /// \param Idx Pressure-set index whose limit is requested.
  /// \return The register unit pressure limit for dimension \p Idx.
  virtual unsigned getRegPressureSetLimit(const MachineFunction &MF,
                                          unsigned Idx) const = 0;

  /// Get the register class for this pressure set with the largest
  /// `RegClassWeight::WeightLimit`.
  ///
  /// \param Idx Pressure-set index whose largest register class is requested.
  /// \return The register class for \p Idx with the largest weight limit.
  virtual const TargetRegisterClass *
  getLargestRegClassForRegPressureSet(unsigned Idx) const = 0;

  /// Get the dimensions of register pressure impacted by this register class.
  /// Returns a -1 terminated array of pressure set IDs.
  ///
  /// \param RC Register class whose pressure sets are requested.
  /// \return A -1-terminated array of pressure set IDs impacted by \p RC.
  virtual const int *getRegClassPressureSets(
    const TargetRegisterClass *RC) const = 0;

  /// Get the dimensions of register pressure impacted by this register unit.
  /// Returns a -1 terminated array of pressure set IDs.
  ///
  /// \param RegUnit Register unit whose pressure sets are requested.
  /// \return A -1-terminated array of pressure set IDs impacted by \p RegUnit.
  virtual const int *getRegUnitPressureSets(MCRegUnit RegUnit) const = 0;

  /// Get the scale factor of spill weight for this register class.
  ///
  /// \param RC Register class whose spill-weight scale is requested.
  /// \return The spill weight scale factor for \p RC.
  virtual float getSpillWeightScaleFactor(const TargetRegisterClass *RC) const;

  /// Return the preferred allocation order for registers in \p RC.
  ///
  /// Returns the preferred order for allocating registers from this register
  /// class in MF. The raw order comes directly from the .td file and may
  /// include reserved registers that are not allocatable. Register allocators
  /// should also make sure to allocate callee-saved registers only after all
  /// the volatiles are used. The RegisterClassInfo class provides filtered
  /// allocation orders with callee-saved registers moved to the end.
  ///
  /// The MachineFunction argument can be used to tune the allocatable
  /// registers based on the characteristics of the function, subtarget, or
  /// other criteria.
  ///
  /// By default, this method returns all registers in the class.
  ///
  /// \param RC Register class whose raw allocation order is requested.
  /// \param MF Function for which the allocation order may be tuned.
  /// \param Rev Whether to reverse the allocation order.
  /// \return The preferred allocation order for registers in \p RC.
  virtual ArrayRef<MCPhysReg>
  getRawAllocationOrder(const TargetRegisterClass &RC,
                        const MachineFunction &MF,
                        bool Rev = false) const {
    return RC.getRegisters();
  }

  /// Get preferred physical registers to try first for \p VirtReg.
  ///
  /// Get a list of 'hint' registers that the register allocator should try
  /// first when allocating a physical register for the virtual register
  /// VirtReg. These registers are effectively moved to the front of the
  /// allocation order. If true is returned, regalloc will try to only use
  /// hints to the greatest extent possible even if it means spilling.
  ///
  /// The Order argument is the allocation order for VirtReg's register class
  /// as returned from RegisterClassInfo::getOrder(). The hint registers must
  /// come from Order, and they must not be reserved.
  ///
  /// The default implementation of this function will only add target
  /// independent register allocation hints. Targets that override this
  /// function should typically call this default implementation as well and
  /// expect to see generic copy hints added.
  ///
  /// \param VirtReg Virtual register needing allocation hints.
  /// \param Order Allocation order for \p VirtReg's register class.
  /// \param Hints Output list of preferred physical registers.
  /// \param MF Function that owns \p VirtReg.
  /// \param VRM Optional virtual-to-physical register map.
  /// \param Matrix Optional live register matrix.
  /// \return True if allocation should prefer the hint registers exclusively when possible.
  virtual bool
  getRegAllocationHints(Register VirtReg, ArrayRef<MCPhysReg> Order,
                        SmallVectorImpl<MCPhysReg> &Hints,
                        const MachineFunction &MF,
                        const VirtRegMap *VRM = nullptr,
                        const LiveRegMatrix *Matrix = nullptr) const;

  /// Update register allocation hints after \p Reg changes to \p NewReg.
  ///
  /// A callback to allow target a chance to update register allocation hints
  /// when a register is "changed" (e.g. coalesced) to another register. e.g. On
  /// ARM, some virtual registers should target register pairs, if one of pair
  /// is coalesced to another register, the allocation hint of the other half of
  /// the pair should be changed to point to the new register.
  ///
  /// \param Reg Original register whose hints may need updating.
  /// \param NewReg Replacement register that \p Reg changed into.
  /// \param MF Function whose allocation hints are updated.
  virtual void updateRegAllocHint(Register Reg, Register NewReg,
                                  MachineFunction &MF) const {
    // Do nothing.
  }

  /// Return true if local live ranges should be allocated in reverse order.
  ///
  /// Allow the target to reverse allocation order of local live ranges. This
  /// will generally allocate shorter local live ranges first. For targets with
  /// many registers, this could reduce regalloc compile time by a large factor.
  /// It is disabled by default for three reasons:
  /// (1) Top-down allocation is simpler and easier to debug for targets that
  /// don't benefit from reversing the order.
  /// (2) Bottom-up allocation could result in poor evicition decisions on some
  /// targets affecting the performance of compiled code.
  /// (3) Bottom-up allocation is no longer guaranteed to optimally color.
  ///
  /// \return True if local live ranges should be allocated in reverse order.
  virtual bool reverseLocalAssignment() const { return false; }

  /// Return the cost of using a callee-saved register for the first time.
  ///
  /// Allow the target to override the cost of using a callee-saved register for
  /// the first time. Default value of 0 means we will use a callee-saved
  /// register if it is available.
  ///
  /// \param MF Function whose first CSR-use cost is queried.
  /// \return The cost of using a callee-saved register for the first time.
  virtual unsigned getCSRFirstUseCost(const MachineFunction &MF) const {
    return 0;
  }
  /// FIXME: We should deprecate this usage.
  ///
  /// \return The CSR cost value.
  virtual unsigned getCSRCost() const { return 0; }

  /// Scale the CSRFirstUseCost with this number.
  /// The scale is a percentage (e.g., 30 means 30% of the base cost).
  /// Target can tune and override this default value.
  ///
  /// \param MF Function whose CSR cost scale is queried.
  /// \return The percentage scale applied to CSR first-use cost.
  virtual unsigned getCSRCostScale(const MachineFunction &MF) const {
    return 30;
  }

  /// Returns true if the target requires (and can make use of) the register
  /// scavenger.
  ///
  /// \param MF Function whose register-scavenging need is queried.
  /// \return True if the target requires the register scavenger.
  virtual bool requiresRegisterScavenging(const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the target wants to use frame pointer based accesses to
  /// spill to the scavenger emergency spill slot.
  ///
  /// \param MF Function whose scavenger addressing preference is queried.
  /// \return True if scavenger emergency spills should use FP-based addressing.
  virtual bool useFPForScavengingIndex(const MachineFunction &MF) const {
    return true;
  }

  /// Returns true if the target requires post PEI scavenging of registers for
  /// materializing frame index constants.
  ///
  /// \param MF Function whose frame-index scavenging need is queried.
  /// \return True if post-PEI scavenging is required for frame index constants.
  virtual bool requiresFrameIndexScavenging(const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the target requires using the RegScavenger directly for
  /// frame elimination despite using requiresFrameIndexScavenging.
  ///
  /// \param MF Function whose frame-index replacement scavenging need is queried.
  /// \return True if RegScavenger must be used directly for frame elimination.
  virtual bool requiresFrameIndexReplacementScavenging(
      const MachineFunction &MF) const {
    return false;
  }

  /// Returns true if the target wants the LocalStackAllocation pass to be run
  /// and virtual base registers used for more efficient stack access.
  ///
  /// \param MF Function whose virtual base-register requirement is queried.
  /// \return True if virtual base registers should be used for stack access.
  virtual bool requiresVirtualBaseRegisters(const MachineFunction &MF) const {
    return false;
  }

  /// Return true if the target already reserved a spill slot for \p Reg.
  ///
  /// Return true if target has reserved a spill slot in the stack frame of the
  /// given function for the specified register. e.g. On x86, if the frame
  /// register is required, the first fixed stack object is reserved as its
  /// spill slot. This tells PEI not to create a new stack frame object for the
  /// given register. It should be called only after determineCalleeSaves().
  ///
  /// \param MF Function whose reserved spill slots are queried.
  /// \param Reg Register that may already have a reserved spill slot.
  /// \param FrameIdx Set to the reserved frame index when one exists.
  /// \return True if \p Reg already has a reserved spill slot in \p MF.
  virtual bool hasReservedSpillSlot(const MachineFunction &MF, Register Reg,
                                    int &FrameIdx) const {
    return false;
  }

  /// Returns true if the live-ins should be tracked after register allocation.
  ///
  /// \param MF Function whose post-regalloc liveness tracking is queried.
  /// \return True if live-ins should be tracked after register allocation.
  virtual bool trackLivenessAfterRegAlloc(const MachineFunction &MF) const {
    return true;
  }

  /// True if the stack can be realigned for the target.
  ///
  /// \param MF Function whose stack realignment capability is queried.
  /// \return True if the stack can be realigned for \p MF.
  virtual bool canRealignStack(const MachineFunction &MF) const;

  /// True if storage within the function requires the stack pointer to be
  /// aligned more than the normal calling convention calls for.
  ///
  /// \param MF Function whose stack alignment requirement is queried.
  /// \return True if storage in \p MF requires extra stack alignment.
  virtual bool shouldRealignStack(const MachineFunction &MF) const;

  /// True if stack realignment is required and still possible.
  ///
  /// \param MF Function whose stack realignment state is queried.
  /// \return True if stack realignment is required and still possible.
  bool hasStackRealignment(const MachineFunction &MF) const {
    return shouldRealignStack(MF) && canRealignStack(MF);
  }

  /// Get the offset from the referenced frame index in the instruction,
  /// if there is one.
  ///
  /// \param MI Instruction that may reference a frame index.
  /// \param Idx Operand index of the frame-index reference.
  /// \return The frame-index offset encoded in \p MI, or 0.
  virtual int64_t getFrameIndexInstrOffset(const MachineInstr *MI,
                                           int Idx) const {
    return 0;
  }

  /// Return true if a frame-index reference prefers a non-FP/SP base register.
  ///
  /// Returns true if the instruction's frame index reference would be better
  /// served by a base register other than FP or SP. Used by
  /// LocalStackFrameAllocation to determine which frame index references it
  /// should create new base registers for.
  ///
  /// \param MI Instruction containing the frame-index reference.
  /// \param Offset Offset associated with the frame-index reference.
  /// \return True if the frame-index reference prefers a non-FP/SP base register.
  virtual bool needsFrameBaseReg(MachineInstr *MI, int64_t Offset) const {
    return false;
  }

  /// Insert defining instruction(s) for a pointer to FrameIdx before
  /// insertion point I. Return materialized frame pointer.
  ///
  /// \param MBB Basic block that receives the materializing instructions.
  /// \param FrameIdx Frame index being materialized.
  /// \param Offset Additional offset from the frame index.
  /// \return The materialized frame pointer register.
  virtual Register materializeFrameBaseRegister(MachineBasicBlock *MBB,
                                                int FrameIdx,
                                                int64_t Offset) const {
    llvm_unreachable("materializeFrameBaseRegister does not exist on this "
                     "target");
  }

  /// Resolve a frame index operand of an instruction
  /// to reference the indicated base register plus offset instead.
  ///
  /// \param MI Instruction whose frame-index operand is rewritten.
  /// \param BaseReg Base register that replaces the frame index.
  /// \param Offset Immediate offset added to \p BaseReg.
  virtual void resolveFrameIndex(MachineInstr &MI, Register BaseReg,
                                 int64_t Offset) const {
    llvm_unreachable("resolveFrameIndex does not exist on this target");
  }

  /// Determine whether a given base register plus offset immediate is
  /// encodable to resolve a frame index.
  ///
  /// \param MI Instruction whose addressing mode is checked.
  /// \param BaseReg Base register being combined with \p Offset.
  /// \param Offset Immediate offset from \p BaseReg.
  /// \return True if \p BaseReg plus \p Offset can encode the frame index.
  virtual bool isFrameOffsetLegal(const MachineInstr *MI, Register BaseReg,
                                  int64_t Offset) const {
    llvm_unreachable("isFrameOffsetLegal does not exist on this target");
  }

  /// Gets the DWARF expression opcodes for \p Offset.
  ///
  /// \param Offset Stack offset to encode.
  /// \param Ops Output vector receiving the DWARF opcodes.
  virtual void getOffsetOpcodes(const StackOffset &Offset,
                                SmallVectorImpl<uint64_t> &Ops) const;

  /// Prepends a DWARF expression for \p Offset to DIExpression \p Expr.
  ///
  /// \param Expr DIExpression that receives the prepended offset ops.
  /// \param PrependFlags Flags controlling how the expression is prepended.
  /// \param Offset Stack offset encoded into the DWARF expression.
  /// \return A DIExpression with \p Offset prepended according to \p PrependFlags.
  DIExpression *
  prependOffsetExpression(const DIExpression *Expr, unsigned PrependFlags,
                          const StackOffset &Offset) const;

  /// Return the DWARF register number for virtual register \p RegNum.
  ///
  /// \param RegNum Virtual register whose DWARF number is requested.
  /// \param isEH Whether an EH-specific DWARF register number is needed.
  /// \return The DWARF register number for virtual register \p RegNum.
  virtual int64_t getDwarfRegNumForVirtReg(Register RegNum, bool isEH) const {
    llvm_unreachable("getDwarfRegNumForVirtReg does not exist on this target");
  }

  /// Spill \p Reg so the register scavenger can reuse it.
  ///
  /// Return true if the register was spilled, false otherwise. If this function
  /// does not spill the register, the scavenger will instead spill it to the
  /// emergency spill slot.
  ///
  /// \param MBB Basic block containing the scavenger use site.
  /// \param I Insertion point for spill code.
  /// \param UseMI Instruction that needs the scavenged register.
  /// \param RC Register class required by the scavenger.
  /// \param Reg Register being spilled for scavenging.
  /// \return True if \p Reg was spilled for the scavenger.
  virtual bool saveScavengerRegister(MachineBasicBlock &MBB,
                                     MachineBasicBlock::iterator I,
                                     MachineBasicBlock::iterator &UseMI,
                                     const TargetRegisterClass *RC,
                                     Register Reg) const {
    return false;
  }

  /// Return true if frame indices should be processed in reverse block order.
  ///
  /// Process frame indices in reverse block order. This changes the behavior of
  /// the RegScavenger passed to eliminateFrameIndex. If this is true targets
  /// should scavengeRegisterBackwards in eliminateFrameIndex. New targets
  /// should prefer reverse scavenging behavior.
  /// TODO: Remove this when all targets return true.
  /// \return True if frame indices should be processed in reverse block order.
  virtual bool eliminateFrameIndicesBackwards() const { return true; }

  /// Eliminate an abstract frame index operand from the current instruction.
  ///
  /// This method must be overriden to eliminate abstract frame indices from
  /// instructions which may use them. The instruction referenced by the
  /// iterator contains an MO_FrameIndex operand which must be eliminated by
  /// this method. This method may modify or replace the specified instruction,
  /// as long as it keeps the iterator pointing at the finished product.
  /// SPAdj is the SP adjustment due to call frame setup instruction.
  /// FIOperandNum is the FI operand number.
  /// Returns true if the current instruction was removed and the iterator
  /// is not longer valid
  ///
  /// \param MI Iterator referencing the instruction with the frame index.
  /// \param SPAdj Stack-pointer adjustment from call-frame setup.
  /// \param FIOperandNum Operand index of the MO_FrameIndex being eliminated.
  /// \param RS Optional register scavenger for temporary registers.
  /// \return True if the current instruction was removed and the iterator is no longer valid.
  virtual bool eliminateFrameIndex(MachineBasicBlock::iterator MI,
                                   int SPAdj, unsigned FIOperandNum,
                                   RegScavenger *RS = nullptr) const = 0;

  /// Return the assembly name for \p Reg.
  ///
  /// \param Reg Physical register whose assembly name is requested.
  /// \return The assembly name for physical register \p Reg.
  virtual StringRef getRegAsmName(MCRegister Reg) const {
    // FIXME: We are assuming that the assembly name is equal to the TableGen
    // name converted to lower case
    //
    // The TableGen name is the name of the definition for this register in the
    // target's tablegen files.  For example, the TableGen name of
    // def EAX : Register <...>; is "EAX"
    return StringRef(getName(Reg));
  }

  //===--------------------------------------------------------------------===//
  /// Subtarget Hooks

  /// Return true if SrcRC and DstRC may morph into NewRC during coalescing.
  ///
  /// \param MI Copy-like instruction being considered for coalescing.
  /// \param SrcRC Register class of the source operand.
  /// \param SubReg Sub-register index of the source operand.
  /// \param DstRC Register class of the destination operand.
  /// \param DstSubReg Sub-register index of the destination operand.
  /// \param NewRC Candidate register class after coalescing.
  /// \param LIS Live interval analysis for the function.
  /// \return True if \p SrcRC and \p DstRC may morph into \p NewRC during coalescing.
  virtual bool shouldCoalesce(MachineInstr *MI,
                              const TargetRegisterClass *SrcRC,
                              unsigned SubReg,
                              const TargetRegisterClass *DstRC,
                              unsigned DstSubReg,
                              const TargetRegisterClass *NewRC,
                              LiveIntervals &LIS) const
  { return true; }

  /// Decide whether region splitting should run for \p VirtReg.
  ///
  /// Region split has a high compile time cost especially for large live range.
  /// This method is used to decide whether or not \p VirtReg should go through
  /// this expensive splitting heuristic.
  ///
  /// \param MF Function that owns \p VirtReg.
  /// \param VirtReg Live interval being considered for region splitting.
  /// \return True if region splitting should run for \p VirtReg.
  virtual bool shouldRegionSplitForVirtReg(const MachineFunction &MF,
                                           const LiveInterval &VirtReg) const;

  /// Decide whether last-chance recoloring should run for \p VirtReg.
  ///
  /// Last chance recoloring has a high compile time cost especially for targets
  /// with a lot of registers. This method is used to decide whether or not
  /// \p VirtReg should go through this expensive heuristic. When this target
  /// hook is hit, by returning false, there is a high chance that the register
  /// allocation will fail altogether (usually with "ran out of registers").
  /// That said, this error usually points to another problem in the
  /// optimization pipeline.
  ///
  /// \param MF Function that owns \p VirtReg.
  /// \param VirtReg Live interval being considered for last-chance recoloring.
  /// \return True if last-chance recoloring should run for \p VirtReg.
  virtual bool
  shouldUseLastChanceRecoloringForVirtReg(const MachineFunction &MF,
                                          const LiveInterval &VirtReg) const {
    return true;
  }

  /// Prefer register-class allocation priority over live-range globalness.
  ///
  /// When prioritizing live ranges in register allocation, if this hook returns
  /// true then the AllocationPriority of the register class will be treated as
  /// more important than whether the range is local to a basic block or global.
  ///
  /// \param MF Function whose allocation priority policy is queried.
  /// \return True if register-class priority outweighs live-range globalness.
  virtual bool
  regClassPriorityTrumpsGlobalness(const MachineFunction &MF) const {
    return false;
  }

  //===--------------------------------------------------------------------===//
  /// Debug information queries.

  /// Return the register used as a base for the current stack frame.
  ///
  /// \param MF Function whose frame register is requested.
  /// \return The register used as the base for the current stack frame.
  virtual Register getFrameRegister(const MachineFunction &MF) const = 0;

  /// Mark a register and all its aliases as reserved in the given set.
  ///
  /// \param RegisterSet Bitset updated with \p Reg and its aliases.
  /// \param Reg Physical register whose aliases are marked.
  void markSuperRegs(BitVector &RegisterSet, MCRegister Reg) const;

  /// Returns true if for every register in the set all super registers are part
  /// of the set as well.
  ///
  /// \param RegisterSet Bitset of registers to validate.
  /// \param Exceptions Registers allowed to miss marked super-registers.
  /// \return True if every register in the set has all its super-registers marked.
  bool checkAllSuperRegsMarked(const BitVector &RegisterSet,
      ArrayRef<MCPhysReg> Exceptions = ArrayRef<MCPhysReg>()) const;

  /// Return a constrained register class for operand \p MO, if any.
  ///
  /// \param MO Machine operand whose constrained class is requested.
  /// \param MRI Machine register info for the owning function.
  /// \return A constrained register class for \p MO, or null.
  virtual const TargetRegisterClass *
  getConstrainedRegClassForOperand(const MachineOperand &MO,
                                   const MachineRegisterInfo &MRI) const {
    return nullptr;
  }

  /// Return true if \p Reg should be treated like a callee-saved register.
  ///
  /// Some targets have non-allocatable registers that aren't technically part
  /// of the explicit callee saved register list, but should be handled as such
  /// in certain cases.
  ///
  /// \param Reg Physical register being tested.
  /// \return True if \p Reg should be treated like a callee-saved register.
  virtual bool isNonallocatableRegisterCalleeSave(MCRegister Reg) const {
    return false;
  }

  /// Return true if \p Reg is a placeholder for a late-assigned frame register.
  ///
  /// Some targets delay assigning the frame until late and use a placeholder to
  /// represent it earlier. This method can be used to identify the frame
  /// register placeholder.
  ///
  /// \param Reg Physical register being tested.
  /// \return True if \p Reg is a placeholder for a late-assigned frame register.
  virtual bool isVirtualFrameRegister(MCRegister Reg) const { return false; }

  /// Return the numeric value of the named virtual-register flag, if known.
  ///
  /// \param Name Virtual-register flag name to look up.
  /// \return The numeric value of the named virtual-register flag, if known.
  virtual std::optional<uint8_t> getVRegFlagValue(StringRef Name) const {
    return {};
  }

  /// Return the virtual-register flag names associated with \p Reg.
  ///
  /// \param Reg Virtual register whose flags are queried.
  /// \param MF Function that owns \p Reg.
  /// \return The virtual-register flag names associated with \p Reg.
  virtual SmallVector<StringLiteral>
  getVRegFlagsOfReg(Register Reg, const MachineFunction &MF) const {
    return {};
  }

  /// Return whether \p LLVMReg should be ignored for CodeView debug info.
  ///
  /// Useful when there is known to be no available mapping.
  ///
  /// \param LLVMReg Physical register being considered for CodeView emission.
  /// \return True if \p LLVMReg should be ignored for CodeView debug info.
  virtual bool isIgnoredCVReg(MCRegister LLVMReg) const { return false; }
};

//===----------------------------------------------------------------------===//
//                           SuperRegClassIterator
//===----------------------------------------------------------------------===//
/// Iterate over possible super-register classes for a register class.
///
/// The iterator visits a list of pairs (Idx, Mask) corresponding to the
/// possible classes of super-registers.
///
/// Each bit mask will have at least one set bit, and each set bit in Mask
/// corresponds to a SuperRC such that:
///
///   For all Reg in SuperRC: Reg:Idx is in RC.
///
/// The iterator can include (0, RC->getSubClassMask()) as the first entry which
/// also satisfies the above requirement, assuming Reg:0 == Reg.
class SuperRegClassIterator {
  const unsigned RCMaskWords;
  unsigned SubReg = 0;
  const uint16_t *Idx;
  const uint32_t *Mask;

public:
  /// Create a SuperRegClassIterator that visits all the super-register classes
  /// of RC. When IncludeSelf is set, also include the (0, sub-classes) entry.
  ///
  /// \param RC Register class whose super-register classes are visited.
  /// \param TRI Target register info used to size the class masks.
  /// \param IncludeSelf Whether to include the (0, sub-classes) entry first.
  SuperRegClassIterator(const TargetRegisterClass *RC,
                        const TargetRegisterInfo *TRI,
                        bool IncludeSelf = false)
    : RCMaskWords((TRI->getNumRegClasses() + 31) / 32),
      Idx(RC->getSuperRegIndices()), Mask(RC->getSubClassMask()) {
    if (!IncludeSelf)
      ++*this;
  }

  /// Returns true if this iterator is still pointing at a valid entry.
  ///
  /// \return True if the iterator still points at a valid entry.
  bool isValid() const { return Idx; }

  /// Returns the current sub-register index.
  ///
  /// \return The current sub-register index.
  unsigned getSubReg() const { return SubReg; }

  /// Returns the bit mask of register classes that getSubReg() projects into
  /// RC.
  /// See TargetRegisterClass::getSubClassMask() for how to use it.
  ///
  /// \return The bit mask of register classes that getSubReg() projects into RC.
  const uint32_t *getMask() const { return Mask; }

  /// Advance iterator to the next entry.
  void operator++() {
    assert(isValid() && "Cannot move iterator past end.");
    Mask += RCMaskWords;
    SubReg = *Idx++;
    if (!SubReg)
      Idx = nullptr;
  }
};

//===----------------------------------------------------------------------===//
//                           BitMaskClassIterator
//===----------------------------------------------------------------------===//
/// Iterator over register classes selected by a bitmask.
///
/// This class encapsulates the logic to iterate over bitmasks returned by the
/// various RegClass related APIs. E.g., this class can be used to iterate over
/// the subclasses provided by TargetRegisterClass::getSubClassMask or
/// SuperRegClassIterator::getMask.
class BitMaskClassIterator {
  /// Total number of register classes.
  const unsigned NumRegClasses;
  /// Base index of CurrentChunk.
  /// In other words, the number of bit we read to get at the
  /// beginning of that chunck.
  unsigned Base = 0;
  /// Adjust base index of CurrentChunk.
  /// Base index + how many bit we read within CurrentChunk.
  unsigned Idx = 0;
  /// Current register class ID.
  unsigned ID = 0;
  /// Mask we are iterating over.
  const uint32_t *Mask;
  /// Current chunk of the Mask we are traversing.
  uint32_t CurrentChunk;

  /// Move ID to the next set bit.
  void moveToNextID() {
    // If the current chunk of memory is empty, move to the next one,
    // while making sure we do not go pass the number of register
    // classes.
    while (!CurrentChunk) {
      // Move to the next chunk.
      Base += 32;
      if (Base >= NumRegClasses) {
        ID = NumRegClasses;
        return;
      }
      CurrentChunk = *++Mask;
      Idx = Base;
    }
    // Otherwise look for the first bit set from the right
    // (representation of the class ID is big endian).
    // See getSubClassMask for more details on the representation.
    unsigned Offset = llvm::countr_zero(CurrentChunk);
    // Add the Offset to the adjusted base number of this chunk: Idx.
    // This is the ID of the register class.
    ID = Idx + Offset;

    // Consume the zeros, if any, and the bit we just read
    // so that we are at the right spot for the next call.
    // Do not do Offset + 1 because Offset may be 31 and 32
    // will be UB for the shift, though in that case we could
    // have make the chunk being equal to 0, but that would
    // have introduced a if statement.
    moveNBits(Offset);
    moveNBits(1);
  }

  /// Move \p NumBits Bits forward in CurrentChunk.
  void moveNBits(unsigned NumBits) {
    assert(NumBits < 32 && "Undefined behavior spotted!");
    // Consume the bit we read for the next call.
    CurrentChunk >>= NumBits;
    // Adjust the base for the chunk.
    Idx += NumBits;
  }

public:
  /// Create a BitMaskClassIterator that visits all the register classes
  /// represented by \p Mask.
  ///
  /// \pre \p Mask != nullptr
  ///
  /// \param Mask Bitmask of register class IDs to iterate.
  /// \param TRI Target register info providing the class count.
  BitMaskClassIterator(const uint32_t *Mask, const TargetRegisterInfo &TRI)
      : NumRegClasses(TRI.getNumRegClasses()), Mask(Mask), CurrentChunk(*Mask) {
    // Move to the first ID.
    moveToNextID();
  }

  /// Returns true if this iterator is still pointing at a valid entry.
  ///
  /// \return True if the iterator still points at a valid entry.
  bool isValid() const { return getID() != NumRegClasses; }

  /// Returns the current register class ID.
  ///
  /// \return The current register class ID.
  unsigned getID() const { return ID; }

  /// Advance iterator to the next entry.
  void operator++() {
    assert(isValid() && "Cannot move iterator past end.");
    moveToNextID();
  }
};

/// Functor that maps a virtual register to its dense index.
///
/// This is useful when building IndexedMaps keyed on virtual registers.
struct VirtReg2IndexFunctor {
  /// Argument type accepted by the functor.
  using argument_type = Register;
  /// Return the dense index of virtual register \p Reg.
  ///
  /// \param Reg Virtual register whose index is requested.
  /// \return The dense index of virtual register \p Reg.
  unsigned operator()(Register Reg) const { return Reg.virtRegIndex(); }
};

/// Prints virtual and physical registers with or without a TRI instance.
///
/// The format is:
///   %noreg          - NoRegister
///   %5              - a virtual register.
///   %5:sub_8bit     - a virtual register with sub-register index (with TRI).
///   %eax            - a physical register
///   %physreg17      - a physical register when no TRI instance given.
///
/// Usage: OS << printReg(Reg, TRI, SubRegIdx) << '\n';
///
/// \param Reg Register to print.
/// \param TRI Target register info used for naming, or null.
/// \param SubIdx Optional sub-register index to append.
/// \param MRI Optional machine register info for richer naming.
  /// \return A Printable object that prints \p Reg.
LLVM_ABI Printable printReg(Register Reg,
                            const TargetRegisterInfo *TRI = nullptr,
                            unsigned SubIdx = 0,
                            const MachineRegisterInfo *MRI = nullptr);

/// Create Printable object to print register units on a \ref raw_ostream.
///
/// Register units are named after their root registers:
///
///   al      - Single root.
///   fp0~st7 - Dual roots.
///
/// Usage: OS << printRegUnit(Unit, TRI) << '\n';
///
/// \param Unit Register unit to print.
/// \param TRI Target register info used for naming.
  /// \return A Printable object that prints \p Unit.
LLVM_ABI Printable printRegUnit(MCRegUnit Unit, const TargetRegisterInfo *TRI);

/// Create Printable object to print virtual registers and physical
/// registers on a \ref raw_ostream.
///
/// \param VRegOrUnit Virtual register or register unit to print.
/// \param TRI Target register info used for naming, or null.
  /// \return A Printable object that prints \p VRegOrUnit.
LLVM_ABI Printable printVRegOrUnit(VirtRegOrUnit VRegOrUnit,
                                   const TargetRegisterInfo *TRI);

/// Create Printable object to print register classes or register banks
/// on a \ref raw_ostream.
///
/// \param Reg Register whose class or bank is printed.
/// \param RegInfo Machine register info used to look up class or bank.
/// \param TRI Target register info, or null.
  /// \return A Printable object that prints the class or bank of \p Reg.
LLVM_ABI Printable printRegClassOrBank(Register Reg,
                                       const MachineRegisterInfo &RegInfo,
                                       const TargetRegisterInfo *TRI);

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETREGISTERINFO_H
