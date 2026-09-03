//===- MC/MCRegisterInfo.h - Target Register Description --------*- C++ -*-===//
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

#ifndef LLVM_MC_MCREGISTERINFO_H
#define LLVM_MC_MCREGISTERINFO_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/MC/LaneBitmask.h"
#include "llvm/MC/MCRegister.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <cstdint>
#include <iterator>
#include <utility>

namespace llvm {

class MCRegUnitIterator;
class MCSubRegIterator;
class MCSuperRegIterator;

/// MCRegisterClass - Base class of TargetRegisterClass.
class MCRegisterClass {
public:
  /// Iterator over physical registers in this class.
  using iterator = const MCPhysReg*;
  /// Const iterator over physical registers in this class.
  using const_iterator = const MCPhysReg*;

  // TODO: reorder fields to reduce memory usage.
  const uint32_t RegsOff;   ///< Relative offset to MCPhysReg array.
  const uint32_t RegSetOff; ///< Relative offset to uint8_t array.
  const uint32_t NameIdx; ///< Index of this class's name in the string table.
  const uint32_t RegSizeInBits; ///< Size in bits of registers in this class.
  const uint16_t RegsSize; ///< Number of registers in this class.
  /// Register denoted by first bit in RegSet.
  const MCPhysReg RegSetBegin;
  const uint16_t RegSetSize; ///< Size of the membership bitset in bits.
  const uint16_t ID; ///< Unique ID of this register class.
  const uint8_t CopyCost; ///< Cost of copying between registers in this class.
  const bool Allocatable; ///< True if virtual registers may use this class.
  const bool BaseClass; ///< True if this class has a defined BaseClassOrder.

  const uint32_t SubClassMaskOff;    ///< Relative offset to uint32_t array.
  const uint32_t SuperRegIndicesOff; ///< Relative offset to MCPhysReg array.
  /// Combined lane mask of all registers in this class.
  const LaneBitmask LaneMask;
  /// Classes with a higher priority value are assigned first by register
  /// allocators using a greedy heuristic. The value is in the range [0,31].
  const uint8_t AllocationPriority;

  // Change allocation priority heuristic used by greedy.
  /// True to use the global allocation priority heuristic.
  const bool GlobalPriority;

  /// Configurable target specific flags.
  const uint8_t TSFlags;
  /// Stack ID used when spilling registers from this class.
  const uint8_t SpillStackID;
  /// Whether the class supports two (or more) disjunct subregister indices.
  const bool HasDisjunctSubRegs;
  /// Whether a combination of subregisters can cover every register in the
  /// class. See also the CoveredBySubRegs description in Target.td.
  const bool CoveredBySubRegs;
  const uint32_t SuperClassesOff; ///< Relative offset to unsigned array.
  /// Number of entries in the super-classes list.
  const uint16_t SuperClassesSize;

  /// getID() - Return the register class ID number.
  ///
  /// \return Unique ID of this register class.
  unsigned getID() const { return ID; }

  /// begin/end - Return all of the registers in this class.
  ///
  /// \return Iterator to the first register in this class.
  iterator begin() const {
    return reinterpret_cast<iterator>(reinterpret_cast<const char *>(this) +
                                      RegsOff);
  }
  /// Return an iterator past the last register in this class.
  ///
  /// \return Past-the-end iterator over registers in this class.
  iterator end() const { return begin() + RegsSize; }

  /// getNumRegs - Return the number of registers in this class.
  ///
  /// \return The number of registers in this class.
  unsigned getNumRegs() const { return RegsSize; }

  /// getRegister - Return the specified register in the class.
  ///
  /// \param i Index of the register within this class.
  /// \return The register at index \p i in this class.
  MCRegister getRegister(unsigned i) const {
    assert(i < getNumRegs() && "Register number out of range!");
    return begin()[i];
  }

  /// Return all physical registers in this class.
  ///
  /// \return Array of physical registers in this class.
  ArrayRef<MCPhysReg> getRegisters() const {
    return ArrayRef(begin(), RegsSize);
  }

  /// contains - Return true if the specified register is included in this
  /// register class.  This does not include virtual registers.
  ///
  /// \param Reg Physical register to test for membership.
  /// \return True if \p Reg is a member of this class.
  bool contains(MCRegister Reg) const {
    unsigned RegSetIdx = Reg.id() - RegSetBegin;
    if (RegSetIdx >= RegSetSize)
      return false;
    unsigned InByte = RegSetIdx % 8;
    unsigned Byte = RegSetIdx / 8;
    const uint8_t *RegSet = reinterpret_cast<const uint8_t *>(this) + RegSetOff;
    return (RegSet[Byte] & (1 << InByte)) != 0;
  }

  /// contains - Return true if both registers are in this class.
  ///
  /// \param Reg1 First physical register to test.
  /// \param Reg2 Second physical register to test.
  /// \return True if both registers are members of this class.
  bool contains(MCRegister Reg1, MCRegister Reg2) const {
    return contains(Reg1) && contains(Reg2);
  }

  /// Return the size of the physical register in bits if we are able to
  /// determine it.
  ///
  /// This always returns zero for registers of targets that use HW modes, as
  /// we need more information to determine the size of registers in such
  /// cases. Use TargetRegisterInfo to cover them.
  ///
  /// \return Register size in bits, or zero when undetermined.
  unsigned getSizeInBits() const { return RegSizeInBits; }

  /// Return the cost of copying a value between two registers in this class.
  ///
  /// A negative number means the register class is very expensive to copy
  /// e.g. status flag register classes.
  ///
  /// \return Copy cost for registers in this class.
  uint8_t getCopyCost() const { return CopyCost; }

  /// Return true if copying this register class is very expensive or impossible.
  ///
  /// \return true if register class is very expensive to copy e.g. status flag
  /// register classes.
  bool expensiveOrImpossibleToCopy() const {
    return CopyCost == std::numeric_limits<uint8_t>::max();
  }

  /// isAllocatable - Return true if this register class may be used to create
  /// virtual registers.
  ///
  /// \return True if virtual registers may use this class.
  bool isAllocatable() const { return Allocatable; }

  /// Return true if this register class has a defined BaseClassOrder.
  ///
  /// \return True if this class has a defined BaseClassOrder.
  bool isBaseClass() const { return BaseClass; }

  /// Return true if the specified TargetRegisterClass
  /// is a proper sub-class of this TargetRegisterClass.
  ///
  /// \param RC Candidate sub-class to test.
  /// \return True if \p RC is a proper sub-class of this class.
  bool hasSubClass(const MCRegisterClass *RC) const {
    return RC != this && hasSubClassEq(RC);
  }

  /// Returns true if RC is a sub-class of or equal to this class.
  ///
  /// \param RC Candidate sub-class to test.
  /// \return True if \p RC is a sub-class of or equal to this class.
  bool hasSubClassEq(const MCRegisterClass *RC) const {
    unsigned ID = RC->getID();
    return (getSubClassMask()[ID / 32] >> (ID % 32)) & 1;
  }

  /// Return true if the specified MCRegisterClass is a
  /// proper super-class of this MCRegisterClass.
  ///
  /// \param RC Candidate super-class to test.
  /// \return True if \p RC is a proper super-class of this class.
  bool hasSuperClass(const MCRegisterClass *RC) const {
    return RC->hasSubClass(this);
  }

  /// Returns true if RC is a super-class of or equal to this class.
  ///
  /// \param RC Candidate super-class to test.
  /// \return True if \p RC is a super-class of or equal to this class.
  bool hasSuperClassEq(const MCRegisterClass *RC) const {
    return RC->hasSubClassEq(this);
  }

  /// Returns a bit vector of subclasses, including this one.
  ///
  /// The vector is indexed by class IDs.
  ///
  /// To use it, consider the returned array as a chunk of memory that
  /// contains an array of bits of size NumRegClasses. Each 32-bit chunk
  /// contains a bitset of the ID of the subclasses in big-endian style.
  ///
  /// I.e., the representation of the memory from left to right at the
  /// bit level looks like:
  /// [31 30 ... 1 0] [ 63 62 ... 33 32] ...
  ///                     [ XXX NumRegClasses NumRegClasses - 1 ... ]
  /// Where the number represents the class ID and XXX bits that
  /// should be ignored.
  ///
  /// See the implementation of hasSubClassEq for an example of how it
  /// can be used.
  ///
  /// \return Pointer to the subclass bit mask array.
  const uint32_t *getSubClassMask() const {
    return reinterpret_cast<const uint32_t *>(
        reinterpret_cast<const char *>(this) + SubClassMaskOff);
  }

  /// Return the 0-terminated list of super-register indices into this class.
  ///
  /// The list has an entry for each Idx such that:
  ///
  ///   There exists SuperRC where:
  ///     For all Reg in SuperRC:
  ///       this->contains(Reg:Idx)
  ///
  /// \return Zero-terminated array of super-register indices.
  const uint16_t *getSuperRegIndices() const {
    return reinterpret_cast<const uint16_t *>(
        reinterpret_cast<const char *>(this) + SuperRegIndicesOff);
  }

  /// Return the list of super-classes of this register class.
  ///
  /// The classes are ordered by ID which is also a topological ordering from
  /// large to small classes. The list does NOT include the current class.
  ///
  /// \return Super-class IDs ordered from large to small, excluding this class.
  ArrayRef<unsigned> superclasses() const {
    const unsigned *SuperClasses = reinterpret_cast<const unsigned *>(
        reinterpret_cast<const char *>(this) + SuperClassesOff);
    return ArrayRef(SuperClasses, SuperClassesSize);
  }

  /// Return the combination of all lane masks of registers in this class.
  ///
  /// The lane masks of the registers are the combination of all lane masks of
  /// their subregisters. Returns 1 if there are no subregisters.
  ///
  /// \return Combined lane mask for this class.
  LaneBitmask getLaneMask() const { return LaneMask; }
};

/// Contiguous storage layout for TableGen-emitted register class data.
template <unsigned RegClassCount, unsigned RegCount, unsigned BitSetSize,
          unsigned SubClassMaskSize, unsigned SuperRegIdxSeqSize,
          unsigned SuperClassSize>
struct MCRegisterClassStorage {
  MCRegisterClass Classes[RegClassCount]; ///< Register class descriptors.
  MCPhysReg Regs[RegCount];               ///< Flat register list for all classes.
  uint8_t BitSets[BitSetSize];            ///< Membership bitsets for classes.
  uint32_t SubClassMasks[SubClassMaskSize]; ///< Subclass bitmasks for classes.
  uint16_t SuperRegIdxSeqs[SuperRegIdxSeqSize]; ///< Super-reg index sequences.
  // Avoid zero-sized arrays.
  unsigned SuperClasses[SuperClassSize > 0 ? SuperClassSize : 1]; ///< Super-class ID lists.
};

/// Description of a single physical register.
///
/// The SubRegs field is a zero terminated array of registers that are
/// sub-registers of the specific register, e.g. AL, AH are sub-registers of
/// AX. The SuperRegs field is a zero terminated array of registers that are
/// super-registers of the specific register, e.g. RAX, EAX, are
/// super-registers of AX.
struct MCRegisterDesc {
  uint32_t Name;      ///< Offset of the printable name in the string table.
  uint32_t SubRegs;   ///< Offset into DiffLists of the sub-register set.
  uint32_t SuperRegs; ///< Offset into DiffLists of the super-register set.

  /// Offset into SubRegIndices for each sub-register in SubRegs.
  uint32_t SubRegIndices;

  /// Encoded first register unit and DiffLists offset for this register.
  ///
  /// The low bits hold the first regunit number; the high bits hold an offset
  /// into DiffLists. See MCRegUnitIterator.
  uint32_t RegUnits;

  /// Index into list with lane mask sequences. The sequence contains a lanemask
  /// for every register unit.
  uint16_t RegUnitLaneMasks;

  /// True if this register is a constant register.
  bool IsConstant;

  /// True if this register is artificial.
  bool IsArtificial;
};

/// Interface to TableGen-generated physical register data for a target.
///
/// Targets define a static array of MCRegisterDesc objects for every machine
/// register. This class holds a pointer to that array so register numbers can
/// be turned into descriptors.
///
/// Note this class is designed to be a base class of TargetRegisterInfo, which
/// is the interface used by codegen. However, specific targets *should never*
/// specialize this class. MCRegisterInfo should only contain getters to access
/// TableGen generated physical register data. It must not be extended with
/// virtual methods.
///
class LLVM_ABI MCRegisterInfo {
public:
  /// Iterator over register classes in this target.
  using regclass_iterator = const MCRegisterClass *;

  /// DwarfLLVMRegPair - Emitted by tablegen so Dwarf<->LLVM reg mappings can be
  /// performed with a binary search.
  struct DwarfLLVMRegPair {
    unsigned FromReg; ///< Source register number in the mapping.
    unsigned ToReg;   ///< Destination register number in the mapping.

    /// Order pairs by \c FromReg for binary search.
    /// \param RHS Other pair to compare against.
    /// \return True if this pair's FromReg is less than \p RHS.FromReg.
    bool operator<(DwarfLLVMRegPair RHS) const { return FromReg < RHS.FromReg; }
  };

private:
  const MCRegisterDesc *Desc;                 // Pointer to the descriptor array
  unsigned NumRegs;                           // Number of entries in the array
  MCRegister RAReg;                           // Return address register
  MCRegister PCReg;                           // Program counter register
  const MCRegisterClass *Classes;             // Pointer to the regclass array
  unsigned NumClasses;                        // Number of entries in the array
  unsigned NumRegUnits;                       // Number of regunits.
  const MCPhysReg (*RegUnitRoots)[2];         // Pointer to regunit root table.
  const int16_t *DiffLists;                   // Pointer to the difflists array
  const LaneBitmask *RegUnitMaskSequences;    // Pointer to lane mask sequences
                                              // for register units.
  const char *RegStrings;                     // Pointer to the string table.
  const char *RegClassStrings;                // Pointer to the class strings.
  const uint16_t *SubRegIndices;              // Pointer to the subreg lookup
                                              // array.
  unsigned NumSubRegIndices;                  // Number of subreg indices.
  const uint16_t *RegEncodingTable;           // Pointer to array of register
                                              // encodings.
  const unsigned (*RegUnitIntervals)[2]; // Pointer to regunit interval table.

  unsigned L2DwarfRegsSize;
  unsigned EHL2DwarfRegsSize;
  unsigned Dwarf2LRegsSize;
  unsigned EHDwarf2LRegsSize;
  const DwarfLLVMRegPair *L2DwarfRegs;        // LLVM to Dwarf regs mapping
  const DwarfLLVMRegPair *EHL2DwarfRegs;      // LLVM to Dwarf regs mapping EH
  const DwarfLLVMRegPair *Dwarf2LRegs;        // Dwarf to LLVM regs mapping
  const DwarfLLVMRegPair *EHDwarf2LRegs;      // Dwarf to LLVM regs mapping EH
  DenseMap<MCRegister, int> L2SEHRegs;        // LLVM to SEH regs mapping
  DenseMap<MCRegister, int> L2CVRegs;         // LLVM to CV regs mapping

  mutable std::vector<std::vector<MCPhysReg>> RegAliasesCache;
  ArrayRef<MCPhysReg> getCachedAliasesOf(MCRegister R) const;

  /// Iterator class that can traverse the differentially encoded values in
  /// DiffLists. Don't use this class directly, use one of the adaptors below.
  class DiffListIterator
      : public iterator_facade_base<DiffListIterator, std::forward_iterator_tag,
                                    unsigned> {
    unsigned Val = 0;
    const int16_t *List = nullptr;

  public:
    /// Constructs an invalid iterator, which is also the end iterator.
    /// Call init() to point to something useful.
    DiffListIterator() = default;

    /// Point the iterator to InitVal, decoding subsequent values from DiffList.
    void init(unsigned InitVal, const int16_t *DiffList) {
      Val = InitVal;
      List = DiffList;
    }

    /// Returns true if this iterator is not yet at the end.
    bool isValid() const { return List; }

    /// Dereference the iterator to get the value at the current position.
    const unsigned &operator*() const { return Val; }

    using DiffListIterator::iterator_facade_base::operator++;
    /// Pre-increment to move to the next position.
    DiffListIterator &operator++() {
      assert(isValid() && "Cannot move off the end of the list.");
      int16_t D = *List++;
      Val += D;
      // The end of the list is encoded as a 0 differential.
      if (!D)
        List = nullptr;
      return *this;
    }

    bool operator==(const DiffListIterator &Other) const {
      return List == Other.List;
    }
  };

public:
  /// Return an iterator range over all sub-registers of \p Reg, excluding \p
  /// Reg.
  /// \param Reg Register whose sub-registers are enumerated.
  /// \return Range of sub-registers of \p Reg, excluding \p Reg.
  iterator_range<MCSubRegIterator> subregs(MCRegister Reg) const;

  /// Return an iterator range over all sub-registers of \p Reg, including \p
  /// Reg.
  /// \param Reg Register whose sub-registers are enumerated.
  /// \return Inclusive range of sub-registers of \p Reg.
  iterator_range<MCSubRegIterator> subregs_inclusive(MCRegister Reg) const;

  /// Return an iterator range over all super-registers of \p Reg, excluding \p
  /// Reg.
  /// \param Reg Register whose super-registers are enumerated.
  /// \return Range of super-registers of \p Reg, excluding \p Reg.
  iterator_range<MCSuperRegIterator> superregs(MCRegister Reg) const;

  /// Return an iterator range over all super-registers of \p Reg, including \p
  /// Reg.
  /// \param Reg Register whose super-registers are enumerated.
  /// \return Inclusive range of super-registers of \p Reg.
  iterator_range<MCSuperRegIterator> superregs_inclusive(MCRegister Reg) const;

  /// Return an iterator range over all sub- and super-registers of \p Reg,
  /// including \p Reg.
  /// \param Reg Register whose sub- and super-registers are enumerated.
  /// \return Inclusive range of sub- and super-registers of \p Reg.
  detail::concat_range<const MCPhysReg, iterator_range<MCSubRegIterator>,
                       iterator_range<MCSuperRegIterator>>
  sub_and_superregs_inclusive(MCRegister Reg) const;

  /// Returns an iterator range over all regunits.
  ///
  /// \return Iterator range covering every register unit.
  iota_range<MCRegUnit> regunits() const;

  /// Returns an iterator range over all regunits for \p Reg.
  /// \param Reg Register whose register units are enumerated.
  /// \return Iterator range of register units for \p Reg.
  iterator_range<MCRegUnitIterator> regunits(MCRegister Reg) const;

  // These iterators are allowed to sub-class DiffListIterator and access
  // internal list pointers.
  friend class MCSubRegIterator;
  friend class MCSubRegIndexIterator;
  friend class MCSuperRegIterator;
  friend class MCRegUnitIterator;
  friend class MCRegUnitMaskIterator;
  friend class MCRegUnitRootIterator;
  friend class MCRegAliasIterator;

  /// Destroy the register info.
  virtual ~MCRegisterInfo() = default;

  /// Initialize MCRegisterInfo, called by TableGen
  /// auto-generated routines. *DO NOT USE*.
  ///
  /// \param D Array of register descriptors.
  /// \param NR Number of registers in \p D.
  /// \param RA Return-address register number.
  /// \param PC Program-counter register number.
  /// \param C Array of register classes.
  /// \param NC Number of register classes in \p C.
  /// \param RURoots Table of root registers for each register unit.
  /// \param NRU Number of register units.
  /// \param DL Differentially encoded DiffLists for sub/super/unit walks.
  /// \param RUMS Lane-mask sequences for register units.
  /// \param Strings String table of register names.
  /// \param ClassStrings String table of register class names.
  /// \param SubIndices Sub-register index lookup table.
  /// \param NumIndices Number of sub-register indices.
  /// \param RET Per-register encoding table.
  /// \param RUI Optional per-register unit interval table.
  void InitMCRegisterInfo(const MCRegisterDesc *D, unsigned NR, unsigned RA,
                          unsigned PC, const MCRegisterClass *C, unsigned NC,
                          const MCPhysReg (*RURoots)[2], unsigned NRU,
                          const int16_t *DL, const LaneBitmask *RUMS,
                          const char *Strings, const char *ClassStrings,
                          const uint16_t *SubIndices, unsigned NumIndices,
                          const uint16_t *RET,
                          const unsigned (*RUI)[2] = nullptr) {
    Desc = D;
    NumRegs = NR;
    RAReg = RA;
    PCReg = PC;
    Classes = C;
    DiffLists = DL;
    RegUnitMaskSequences = RUMS;
    RegStrings = Strings;
    RegClassStrings = ClassStrings;
    NumClasses = NC;
    RegUnitRoots = RURoots;
    NumRegUnits = NRU;
    SubRegIndices = SubIndices;
    NumSubRegIndices = NumIndices;
    RegEncodingTable = RET;
    RegUnitIntervals = RUI;

    // Initialize DWARF register mapping variables
    EHL2DwarfRegs = nullptr;
    EHL2DwarfRegsSize = 0;
    L2DwarfRegs = nullptr;
    L2DwarfRegsSize = 0;
    EHDwarf2LRegs = nullptr;
    EHDwarf2LRegsSize = 0;
    Dwarf2LRegs = nullptr;
    Dwarf2LRegsSize = 0;

    RegAliasesCache.resize(NumRegs);
  }

  /// Used to initialize LLVM register to Dwarf
  /// register number mapping. Called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  ///
  /// \param Map Sorted array of LLVM-to-Dwarf register pairs.
  /// \param Size Number of entries in \p Map.
  /// \param isEH True to install the EH numbering; false for debug.
  void mapLLVMRegsToDwarfRegs(const DwarfLLVMRegPair *Map, unsigned Size,
                              bool isEH) {
    if (isEH) {
      EHL2DwarfRegs = Map;
      EHL2DwarfRegsSize = Size;
    } else {
      L2DwarfRegs = Map;
      L2DwarfRegsSize = Size;
    }
  }

  /// Used to initialize Dwarf register to LLVM
  /// register number mapping. Called by TableGen auto-generated routines.
  /// *DO NOT USE*.
  ///
  /// \param Map Sorted array of Dwarf-to-LLVM register pairs.
  /// \param Size Number of entries in \p Map.
  /// \param isEH True to install the EH numbering; false for debug.
  void mapDwarfRegsToLLVMRegs(const DwarfLLVMRegPair *Map, unsigned Size,
                              bool isEH) {
    if (isEH) {
      EHDwarf2LRegs = Map;
      EHDwarf2LRegsSize = Size;
    } else {
      Dwarf2LRegs = Map;
      Dwarf2LRegsSize = Size;
    }
  }

  /// Map an LLVM register number to an SEH register number.
  ///
  /// By default the SEH register number is just the same as the LLVM register
  /// number.
  /// FIXME: TableGen these numbers. Currently this requires target specific
  /// initialization code.
  ///
  /// \param LLVMReg LLVM physical register to map.
  /// \param SEHReg SEH register number to associate with \p LLVMReg.
  void mapLLVMRegToSEHReg(MCRegister LLVMReg, int SEHReg) {
    L2SEHRegs[LLVMReg] = SEHReg;
  }

  /// Map an LLVM register number to a CodeView register number.
  ///
  /// \param LLVMReg LLVM physical register to map.
  /// \param CVReg CodeView register number to associate with \p LLVMReg.
  void mapLLVMRegToCVReg(MCRegister LLVMReg, int CVReg) {
    L2CVRegs[LLVMReg] = CVReg;
  }

  /// This method should return the register where the return
  /// address can be found.
  ///
  /// \return The return-address register.
  MCRegister getRARegister() const {
    return RAReg;
  }

  /// Return the register which is the program counter.
  ///
  /// \return The program counter register.
  MCRegister getProgramCounter() const {
    return PCReg;
  }

  /// Return the descriptor for physical register \p Reg.
  ///
  /// \param Reg Physical register whose descriptor is requested.
  /// \return Descriptor for physical register \p Reg.
  const MCRegisterDesc &operator[](MCRegister Reg) const {
    assert(Reg.id() < NumRegs &&
           "Attempting to access record for invalid register number!");
    return Desc[Reg.id()];
  }

  /// Provide a get method, equivalent to [], but more useful with a
  /// pointer to this object.
  ///
  /// \param Reg Physical register whose descriptor is requested.
  /// \return Descriptor for physical register \p Reg.
  const MCRegisterDesc &get(MCRegister Reg) const {
    return operator[](Reg);
  }

  /// Returns the physical register number of sub-register "Index"
  /// for physical register RegNo. Return zero if the sub-register does not
  /// exist.
  ///
  /// \param Reg Super-register to project.
  /// \param Idx Sub-register index to apply.
  /// \return The sub-register, or zero if it does not exist.
  MCRegister getSubReg(MCRegister Reg, unsigned Idx) const;

  /// Return a super-register of the specified register
  /// Reg so its sub-register of index SubIdx is Reg.
  ///
  /// \param Reg Sub-register that must appear at \p SubIdx.
  /// \param SubIdx Sub-register index in the returned super-register.
  /// \param RC Register class the super-register must belong to.
  /// \return A matching super-register in \p RC, or zero if none.
  MCRegister getMatchingSuperReg(MCRegister Reg, unsigned SubIdx,
                                 const MCRegisterClass *RC) const;

  /// For a given register pair, return the sub-register index
  /// if the second register is a sub-register of the first. Return zero
  /// otherwise.
  ///
  /// \param RegNo Candidate super-register.
  /// \param SubRegNo Candidate sub-register of \p RegNo.
  /// \return The sub-register index, or zero if not a sub-register.
  unsigned getSubRegIndex(MCRegister RegNo, MCRegister SubRegNo) const;

  /// Return the human-readable symbolic target-specific name for the
  /// specified physical register.
  ///
  /// \param RegNo Physical register whose name is requested.
  /// \return Null-terminated name of \p RegNo.
  const char *getName(MCRegister RegNo) const {
    return RegStrings + get(RegNo).Name;
  }

  /// Returns true if the given register is constant.
  ///
  /// \param RegNo Physical register to query.
  /// \return True if \p RegNo is a constant register.
  bool isConstant(MCRegister RegNo) const { return get(RegNo).IsConstant; }

  /// Return true if the given register is artificial.
  ///
  /// An artificial register represents a regunit that is not separately
  /// addressable but still needs to be modelled, such as the top 16-bits of a
  /// 32-bit GPR.
  ///
  /// \param RegNo Physical register to query.
  /// \return True if \p RegNo is artificial.
  bool isArtificial(MCRegister RegNo) const { return get(RegNo).IsArtificial; }

  /// Return true when the given register unit is considered artificial.
  ///
  /// Register units are considered artificial when at least one of the root
  /// registers is artificial.
  ///
  /// \param Unit Register unit to query.
  /// \return True if \p Unit is artificial.
  bool isArtificialRegUnit(MCRegUnit Unit) const;

  /// Return the number of registers this target has (useful for
  /// sizing arrays holding per register information)
  ///
  /// \return The number of physical registers.
  unsigned getNumRegs() const {
    return NumRegs;
  }

  /// Return the number of sub-register indices understood by the target.
  ///
  /// Index 0 is reserved for the no-op sub-register, while 1 to
  /// getNumSubRegIndices() - 1 represent real sub-registers.
  ///
  /// \return The number of sub-register indices.
  unsigned getNumSubRegIndices() const {
    return NumSubRegIndices;
  }

  /// Return the number of (native) register units in the target.
  ///
  /// Register units are numbered from 0 to getNumRegUnits() - 1. They can be
  /// accessed through MCRegUnitIterator defined below.
  ///
  /// \return The number of register units.
  unsigned getNumRegUnits() const {
    return NumRegUnits;
  }

  /// Map a target register to an equivalent dwarf register number.
  ///
  /// Returns -1 if there is no equivalent value. The second parameter allows
  /// targets to use different numberings for EH info and debugging info.
  ///
  /// \param Reg Target register to map.
  /// \param isEH True to use the EH numbering; false for debug.
  /// \return The DWARF register number, or -1 if unmapped.
  virtual int64_t getDwarfRegNum(MCRegister Reg, bool isEH) const;

  /// Map a dwarf register back to a target register. Returns std::nullopt if
  /// there is no mapping.
  ///
  /// \param RegNum Dwarf register number to map.
  /// \param isEH True to use the EH numbering; false for debug.
  /// \return The LLVM register, or std::nullopt if unmapped.
  std::optional<MCRegister> getLLVMRegNum(uint64_t RegNum, bool isEH) const;

  /// Map a target EH register number to an equivalent DWARF register
  /// number.
  ///
  /// \param RegNum Dwarf EH register number to convert.
  /// \return The corresponding DWARF register number.
  int64_t getDwarfRegNumFromDwarfEHRegNum(uint64_t RegNum) const;

  /// Map a target register to an equivalent SEH register
  /// number.  Returns LLVM register number if there is no equivalent value.
  ///
  /// \param Reg Target register to map.
  /// \return The SEH register number, or the LLVM number if unmapped.
  int getSEHRegNum(MCRegister Reg) const;

  /// Map a target register to an equivalent CodeView register
  /// number.
  ///
  /// \param Reg Target register to map.
  /// \return The CodeView register number for \p Reg.
  int getCodeViewRegNum(MCRegister Reg) const;

  /// Return an iterator to the first register class.
  ///
  /// \return Iterator to the first register class.
  regclass_iterator regclass_begin() const { return Classes; }
  /// Return an iterator past the last register class.
  ///
  /// \return Past-the-end iterator over register classes.
  regclass_iterator regclass_end() const { return Classes+NumClasses; }
  /// Return a range over all register classes.
  ///
  /// \return Iterator range covering every register class.
  iterator_range<regclass_iterator> regclasses() const {
    return make_range(regclass_begin(), regclass_end());
  }

  /// Return the number of register classes defined by the target.
  ///
  /// \return The number of register classes.
  unsigned getNumRegClasses() const {
    return (unsigned)(regclass_end()-regclass_begin());
  }

  /// Returns the register class associated with the enumeration
  /// value.  See class MCOperandInfo.
  ///
  /// \param i Register class ID to look up.
  /// \return The register class with ID \p i.
  const MCRegisterClass& getRegClass(unsigned i) const {
    assert(i < getNumRegClasses() && "Register Class ID out of range");
    return Classes[i];
  }

  /// Return the name of register class \p Class.
  ///
  /// \param Class Register class whose name is requested.
  /// \return Null-terminated name of \p Class.
  const char *getRegClassName(const MCRegisterClass *Class) const {
    return RegClassStrings + Class->NameIdx;
  }

   /// Returns the encoding for Reg
  ///
  /// \param Reg Physical register whose encoding is requested.
  /// \return The target encoding value for \p Reg.
  uint16_t getEncodingValue(MCRegister Reg) const {
    assert(Reg.id() < NumRegs &&
           "Attempting to get encoding for invalid register number!");
    return RegEncodingTable[Reg.id()];
  }

  /// Returns true if RegB is a sub-register of RegA.
  ///
  /// \param RegA Candidate super-register.
  /// \param RegB Candidate sub-register of \p RegA.
  /// \return True if \p RegB is a sub-register of \p RegA.
  bool isSubRegister(MCRegister RegA, MCRegister RegB) const {
    return isSuperRegister(RegB, RegA);
  }

  /// Returns true if RegB is a super-register of RegA.
  ///
  /// \param RegA Candidate sub-register.
  /// \param RegB Candidate super-register of \p RegA.
  /// \return True if \p RegB is a super-register of \p RegA.
  bool isSuperRegister(MCRegister RegA, MCRegister RegB) const;

  /// Returns true if RegB is a sub-register of RegA or if RegB == RegA.
  ///
  /// \param RegA Candidate super-register.
  /// \param RegB Candidate sub-register of \p RegA, or equal to \p RegA.
  /// \return True if \p RegB is a sub-register of \p RegA, or equal.
  bool isSubRegisterEq(MCRegister RegA, MCRegister RegB) const {
    return isSuperRegisterEq(RegB, RegA);
  }

  /// Returns true if RegB is a super-register of RegA or if
  /// RegB == RegA.
  ///
  /// \param RegA Candidate sub-register.
  /// \param RegB Candidate super-register of \p RegA, or equal to \p RegA.
  /// \return True if \p RegB is a super-register of \p RegA, or equal.
  bool isSuperRegisterEq(MCRegister RegA, MCRegister RegB) const {
    return RegA == RegB || isSuperRegister(RegA, RegB);
  }

  /// Returns true if RegB is a super-register or sub-register of RegA
  /// or if RegB == RegA.
  ///
  /// \param RegA First register in the relationship.
  /// \param RegB Second register in the relationship.
  /// \return True if \p RegB is a super- or sub-register of \p RegA, or equal.
  bool isSuperOrSubRegisterEq(MCRegister RegA, MCRegister RegB) const {
    return isSubRegisterEq(RegA, RegB) || isSuperRegister(RegA, RegB);
  }

  /// Returns true if the two registers are equal or alias each other.
  ///
  /// \param RegA First register to compare.
  /// \param RegB Second register to compare.
  /// \return True if \p RegA and \p RegB are equal or alias.
  bool regsOverlap(MCRegister RegA, MCRegister RegB) const;

  /// Returns true if this target uses regunit intervals.
  ///
  /// \return True if a regunit interval table is available.
  bool hasRegUnitIntervals() const { return RegUnitIntervals != nullptr; }

  /// Returns an iterator range over all native regunits in the RegUnitInterval
  /// table for \p Reg.
  ///
  /// \param Reg Register whose unit interval is requested.
  /// \return Range of register unit indices for \p Reg.
  iota_range<unsigned> regunits_interval(MCRegister Reg) const {
    assert(hasRegUnitIntervals() &&
           "Target does not support regunit intervals");
    assert(Reg.id() < NumRegs && "Invalid register number");
    return seq<unsigned>(RegUnitIntervals[Reg.id()][0],
                         RegUnitIntervals[Reg.id()][1]);
  }
};

//===----------------------------------------------------------------------===//
//                          Register List Iterators
//===----------------------------------------------------------------------===//

// MCRegisterInfo provides lists of super-registers, sub-registers, and
// aliasing registers. Use these iterator classes to traverse the lists.

/// MCSubRegIterator enumerates all sub-registers of Reg.
/// If IncludeSelf is set, Reg itself is included in the list.
class MCSubRegIterator
    : public iterator_adaptor_base<MCSubRegIterator,
                                   MCRegisterInfo::DiffListIterator,
                                   std::forward_iterator_tag, const MCPhysReg> {
  // Cache the current value, so that we can return a reference to it.
  MCPhysReg Val;

public:
  /// Constructs an end iterator.
  MCSubRegIterator() = default;

  /// Construct an iterator over the sub-registers of \p Reg.
  ///
  /// \param Reg Physical register whose sub-registers are enumerated.
  /// \param MCRI Register info that owns the DiffLists and descriptors.
  /// \param IncludeSelf If true, include \p Reg itself as the first element.
  MCSubRegIterator(MCRegister Reg, const MCRegisterInfo *MCRI,
                   bool IncludeSelf = false) {
    assert(Reg.isPhysical());
    I.init(Reg.id(), MCRI->DiffLists + MCRI->get(Reg).SubRegs);
    // Initially, the iterator points to Reg itself.
    Val = MCPhysReg(*I);
    if (!IncludeSelf)
      ++*this;
  }

  /// Dereference to get the current sub-register.
  ///
  /// \return The current sub-register.
  const MCPhysReg &operator*() const { return Val; }

  /// Bring the postfix increment operator into scope.
  using iterator_adaptor_base::operator++;
  /// Pre-increment to move to the next sub-register.
  ///
  /// \return Reference to this iterator after advancing.
  MCSubRegIterator &operator++() {
    Val = MCPhysReg(*++I);
    return *this;
  }

  /// Returns true if this iterator is not yet at the end.
  ///
  /// \return True if more sub-registers remain.
  bool isValid() const { return I.isValid(); }
};

/// Iterator that enumerates the sub-registers of a Reg and the associated
/// sub-register indices.
class MCSubRegIndexIterator {
  MCSubRegIterator SRIter;
  const uint16_t *SRIndex;

public:
  /// Constructs an iterator that traverses subregisters and their
  /// associated subregister indices.
  ///
  /// \param Reg Physical register whose subregisters are enumerated.
  /// \param MCRI Register info that owns the DiffLists and index tables.
  MCSubRegIndexIterator(MCRegister Reg, const MCRegisterInfo *MCRI)
    : SRIter(Reg, MCRI) {
    SRIndex = MCRI->SubRegIndices + MCRI->get(Reg).SubRegIndices;
  }

  /// Returns current sub-register.
  ///
  /// \return The current sub-register.
  MCRegister getSubReg() const {
    return *SRIter;
  }

  /// Returns sub-register index of the current sub-register.
  ///
  /// \return The sub-register index of the current sub-register.
  unsigned getSubRegIndex() const {
    return *SRIndex;
  }

  /// Returns true if this iterator is not yet at the end.
  ///
  /// \return True if more sub-registers remain.
  bool isValid() const { return SRIter.isValid(); }

  /// Moves to the next position.
  ///
  /// \return Reference to this iterator after advancing.
  MCSubRegIndexIterator &operator++() {
    ++SRIter;
    ++SRIndex;
    return *this;
  }
};

/// MCSuperRegIterator enumerates all super-registers of Reg.
/// If IncludeSelf is set, Reg itself is included in the list.
class MCSuperRegIterator
    : public iterator_adaptor_base<MCSuperRegIterator,
                                   MCRegisterInfo::DiffListIterator,
                                   std::forward_iterator_tag, const MCPhysReg> {
  // Cache the current value, so that we can return a reference to it.
  MCPhysReg Val;

public:
  /// Constructs an end iterator.
  MCSuperRegIterator() = default;

  /// Construct an iterator over the super-registers of \p Reg.
  ///
  /// \param Reg Physical register whose super-registers are enumerated.
  /// \param MCRI Register info that owns the DiffLists and descriptors.
  /// \param IncludeSelf If true, include \p Reg itself as the first element.
  MCSuperRegIterator(MCRegister Reg, const MCRegisterInfo *MCRI,
                     bool IncludeSelf = false) {
    assert(Reg.isPhysical());
    I.init(Reg.id(), MCRI->DiffLists + MCRI->get(Reg).SuperRegs);
    // Initially, the iterator points to Reg itself.
    Val = MCPhysReg(*I);
    if (!IncludeSelf)
      ++*this;
  }

  /// Dereference to get the current super-register.
  ///
  /// \return The current super-register.
  const MCPhysReg &operator*() const { return Val; }

  /// Bring the postfix increment operator into scope.
  using iterator_adaptor_base::operator++;
  /// Pre-increment to move to the next super-register.
  ///
  /// \return Reference to this iterator after advancing.
  MCSuperRegIterator &operator++() {
    Val = MCPhysReg(*++I);
    return *this;
  }

  /// Returns true if this iterator is not yet at the end.
  ///
  /// \return True if more super-registers remain.
  bool isValid() const { return I.isValid(); }
};

// Definition for isSuperRegister. Put it down here since it needs the
// iterator defined above in addition to the MCRegisterInfo class itself.
inline bool MCRegisterInfo::isSuperRegister(MCRegister RegA,
                                            MCRegister RegB) const {
  return is_contained(superregs(RegA), RegB);
}

//===----------------------------------------------------------------------===//
//                               Register Units
//===----------------------------------------------------------------------===//

/// MCRegUnitIterator enumerates a list of register units for Reg.
///
/// The list is in ascending numerical order.
class MCRegUnitIterator
    : public iterator_adaptor_base<MCRegUnitIterator,
                                   MCRegisterInfo::DiffListIterator,
                                   std::forward_iterator_tag, const MCRegUnit> {
  // The value must be kept in sync with RegisterInfoEmitter.cpp.
  static constexpr unsigned RegUnitBits = 12;
  // Cache the current value, so that we can return a reference to it.
  MCRegUnit Val;

public:
  /// Constructs an end iterator.
  MCRegUnitIterator() = default;

  /// Construct an iterator over the register units of \p Reg.
  ///
  /// \param Reg Physical register whose units are enumerated.
  /// \param MCRI Register info that owns the DiffLists and descriptors.
  MCRegUnitIterator(MCRegister Reg, const MCRegisterInfo *MCRI) {
    assert(Reg.isPhysical());
    // Decode the RegUnits MCRegisterDesc field.
    unsigned RU = MCRI->get(Reg).RegUnits;
    unsigned FirstRU = RU & ((1u << RegUnitBits) - 1);
    unsigned Offset = RU >> RegUnitBits;
    I.init(FirstRU, MCRI->DiffLists + Offset);
    Val = MCRegUnit(*I);
  }

  /// Dereference to get the current register unit.
  ///
  /// \return The current register unit.
  const MCRegUnit &operator*() const { return Val; }

  /// Bring the postfix increment operator into scope.
  using iterator_adaptor_base::operator++;
  /// Pre-increment to move to the next register unit.
  ///
  /// \return Reference to this iterator after advancing.
  MCRegUnitIterator &operator++() {
    Val = MCRegUnit(*++I);
    return *this;
  }

  /// Returns true if this iterator is not yet at the end.
  ///
  /// \return True if more register units remain.
  bool isValid() const { return I.isValid(); }
};

/// MCRegUnitMaskIterator enumerates a list of register units and their
/// associated lane masks for Reg. The register units are in ascending
/// numerical order.
class MCRegUnitMaskIterator {
  MCRegUnitIterator RUIter;
  const LaneBitmask *MaskListIter;

public:
  /// Construct an end iterator.
  MCRegUnitMaskIterator() = default;

  /// Construct an iterator over the register units and lane masks of \p Reg.
  ///
  /// \param Reg Physical register whose units and lane masks are enumerated.
  /// \param MCRI Register info that owns the unit and mask tables.
  MCRegUnitMaskIterator(MCRegister Reg, const MCRegisterInfo *MCRI)
    : RUIter(Reg, MCRI) {
      uint16_t Idx = MCRI->get(Reg).RegUnitLaneMasks;
      MaskListIter = &MCRI->RegUnitMaskSequences[Idx];
  }

  /// Returns a (RegUnit, LaneMask) pair.
  ///
  /// \return The current register unit and its lane mask.
  std::pair<MCRegUnit, LaneBitmask> operator*() const {
    return std::make_pair(*RUIter, *MaskListIter);
  }

  /// Returns true if this iterator is not yet at the end.
  ///
  /// \return True if more register units remain.
  bool isValid() const { return RUIter.isValid(); }

  /// Moves to the next position.
  ///
  /// \return Reference to this iterator after advancing.
  MCRegUnitMaskIterator &operator++() {
    ++MaskListIter;
    ++RUIter;
    return *this;
  }
};

// Each register unit has one or two root registers. The complete set of
// registers containing a register unit is the union of the roots and their
// super-registers. All registers aliasing Unit can be visited like this:
//
//   for (MCRegUnitRootIterator RI(Unit, MCRI); RI.isValid(); ++RI) {
//     for (MCSuperRegIterator SI(*RI, MCRI, true); SI.isValid(); ++SI)
//       visit(*SI);
//    }

/// MCRegUnitRootIterator enumerates the root registers of a register unit.
class MCRegUnitRootIterator {
  uint16_t Reg0 = 0;
  uint16_t Reg1 = 0;

public:
  /// Construct an end (invalid) iterator.
  MCRegUnitRootIterator() = default;

  /// Construct an iterator over the root registers of \p RegUnit.
  ///
  /// \param RegUnit Register unit whose roots are enumerated.
  /// \param MCRI Register info that owns the root table.
  MCRegUnitRootIterator(MCRegUnit RegUnit, const MCRegisterInfo *MCRI) {
    assert(static_cast<unsigned>(RegUnit) < MCRI->getNumRegUnits() &&
           "Invalid register unit");
    Reg0 = MCRI->RegUnitRoots[static_cast<unsigned>(RegUnit)][0];
    Reg1 = MCRI->RegUnitRoots[static_cast<unsigned>(RegUnit)][1];
  }

  /// Dereference to get the current root register.
  ///
  /// \return The current root register number.
  unsigned operator*() const {
    return Reg0;
  }

  /// Check if the iterator is at the end of the list.
  ///
  /// \return True if more root registers remain.
  bool isValid() const {
    return Reg0;
  }

  /// Preincrement to move to the next root register.
  ///
  /// \return Reference to this iterator after advancing.
  MCRegUnitRootIterator &operator++() {
    assert(isValid() && "Cannot move off the end of the list.");
    Reg0 = Reg1;
    Reg1 = 0;
    return *this;
  }
};

/// MCRegAliasIterator enumerates all registers aliasing Reg.
class MCRegAliasIterator {
private:
  const MCPhysReg *It = nullptr;
  const MCPhysReg *End = nullptr;

public:
  /// Construct an iterator over registers that alias \p Reg.
  ///
  /// \param Reg Register whose aliases are enumerated.
  /// \param MCRI Register info used to look up the alias cache.
  /// \param IncludeSelf If true, include \p Reg itself in the iteration.
  MCRegAliasIterator(MCRegister Reg, const MCRegisterInfo *MCRI,
                     bool IncludeSelf) {
    ArrayRef<MCPhysReg> Cache = MCRI->getCachedAliasesOf(Reg);
    assert(Cache.back() == Reg);
    It = Cache.begin();
    End = Cache.end();
    if (!IncludeSelf)
      --End;
  }

  /// Return true if this iterator is not yet at the end.
  ///
  /// \return True if more aliases remain.
  bool isValid() const { return It != End; }

  /// Dereference to get the current aliasing register.
  ///
  /// \return The current aliasing physical register.
  MCRegister operator*() const { return *It; }

  /// Pre-increment to move to the next aliasing register.
  ///
  /// \return Reference to this iterator after advancing.
  MCRegAliasIterator &operator++() {
    assert(isValid() && "Cannot move off the end of the list.");
    ++It;
    return *this;
  }
};

inline iterator_range<MCSubRegIterator>
MCRegisterInfo::subregs(MCRegister Reg) const {
  return make_range({Reg, this, /*IncludeSelf=*/false}, MCSubRegIterator());
}

inline iterator_range<MCSubRegIterator>
MCRegisterInfo::subregs_inclusive(MCRegister Reg) const {
  return make_range({Reg, this, /*IncludeSelf=*/true}, MCSubRegIterator());
}

inline iterator_range<MCSuperRegIterator>
MCRegisterInfo::superregs(MCRegister Reg) const {
  return make_range({Reg, this, /*IncludeSelf=*/false}, MCSuperRegIterator());
}

inline iterator_range<MCSuperRegIterator>
MCRegisterInfo::superregs_inclusive(MCRegister Reg) const {
  return make_range({Reg, this, /*IncludeSelf=*/true}, MCSuperRegIterator());
}

inline detail::concat_range<const MCPhysReg, iterator_range<MCSubRegIterator>,
                            iterator_range<MCSuperRegIterator>>
MCRegisterInfo::sub_and_superregs_inclusive(MCRegister Reg) const {
  return concat<const MCPhysReg>(subregs_inclusive(Reg), superregs(Reg));
}

inline iota_range<MCRegUnit> MCRegisterInfo::regunits() const {
  return enum_seq(static_cast<MCRegUnit>(0),
                  static_cast<MCRegUnit>(getNumRegUnits()),
                  force_iteration_on_noniterable_enum);
}

inline iterator_range<MCRegUnitIterator>
MCRegisterInfo::regunits(MCRegister Reg) const {
  return make_range({Reg, this}, MCRegUnitIterator());
}

} // end namespace llvm

#endif // LLVM_MC_MCREGISTERINFO_H
