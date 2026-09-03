//==- include/llvm/CodeGen/AccelTable.h - Accelerator Tables -----*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file contains support for writing accelerator tables.
///
/// The DWARF and Apple accelerator tables are an indirect hash table optimized
/// for null lookup rather than access to known data. The Apple accelerator
/// tables are a precursor of the newer DWARF v5 accelerator tables. Both
/// formats share common design ideas.
///
/// The Apple accelerator table are output into an on-disk format that looks
/// like this:
///
/// .------------------.
/// |  HEADER          |
/// |------------------|
/// |  BUCKETS         |
/// |------------------|
/// |  HASHES          |
/// |------------------|
/// |  OFFSETS         |
/// |------------------|
/// |  DATA            |
/// `------------------'
///
/// The header contains a magic number, version, type of hash function,
/// the number of buckets, total number of hashes, and room for a special struct
/// of data and the length of that struct.
///
/// The buckets contain an index (e.g. 6) into the hashes array. The hashes
/// section contains all of the 32-bit hash values in contiguous memory, and the
/// offsets contain the offset into the data area for the particular hash.
///
/// For a lookup example, we could hash a function name and take it modulo the
/// number of buckets giving us our bucket. From there we take the bucket value
/// as an index into the hashes table and look at each successive hash as long
/// as the hash value is still the same modulo result (bucket value) as earlier.
/// If we have a match we look at that same entry in the offsets table and grab
/// the offset in the data for our final match.
///
/// The DWARF v5 accelerator table consists of zero or more name indices that
/// are output into an on-disk format that looks like this:
///
/// .------------------.
/// |  HEADER          |
/// |------------------|
/// |  CU LIST         |
/// |------------------|
/// |  LOCAL TU LIST   |
/// |------------------|
/// |  FOREIGN TU LIST |
/// |------------------|
/// |  HASH TABLE      |
/// |------------------|
/// |  NAME TABLE      |
/// |------------------|
/// |  ABBREV TABLE    |
/// |------------------|
/// |  ENTRY POOL      |
/// `------------------'
///
/// For the full documentation please refer to the DWARF 5 standard.
///
/// This file defines the class template AccelTable, which is represents an
/// abstract view of an Accelerator table, without any notion of an on-disk
/// layout. This class is parameterized by an entry type, which should derive
/// from AccelTableData. This is the type of individual entries in the table,
/// and it should store the data necessary to emit them. AppleAccelTableData is
/// the base class for Apple Accelerator Table entries, which have a uniform
/// structure based on a sequence of Atoms. There are different sub-classes
/// derived from AppleAccelTable, which differ in the set of Atoms and how they
/// obtain their values.
///
/// An Apple Accelerator Table can be serialized by calling emitAppleAccelTable
/// function.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_ACCELTABLE_H
#define LLVM_CODEGEN_ACCELTABLE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/CodeGen/DIE.h"
#include "llvm/CodeGen/DwarfStringPoolEntry.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DJB.h"
#include "llvm/Support/Debug.h"
#include <cstdint>
#include <variant>
#include <vector>

namespace llvm {

class AsmPrinter;
/// DWARF debug info emitter that owns compile units and related metadata.
class DwarfDebug;
/// DWARF type unit used when emitting type entries into accelerator tables.
class DwarfTypeUnit;
class MCSymbol;
class raw_ostream;

/// Interface for accelerator table entry data used by AccelTable.
///
/// It serves as a base class for different values of the template argument of
/// the AccelTable class template.
class AccelTableData {
public:
  /// Virtual destructor for polymorphic AccelTableData entries.
  virtual ~AccelTableData() = default;

  /// Compare entries using their stable order key.
  ///
  /// \param Other Other entry to compare against.
  /// \return True if this entry sorts before \p Other.
  bool operator<(const AccelTableData &Other) const {
    return order() < Other.order();
  }

    // Subclasses should implement:
    // static uint32_t hash(StringRef Name);

#ifndef NDEBUG
  /// Print this entry to \p OS for debugging.
  ///
  /// \param OS Output stream.
  virtual void print(raw_ostream &OS) const = 0;
#endif
protected:
  /// Return a stable ordering key used when sorting entries.
  ///
  /// \return Stable key used to order this entry relative to others.
  virtual uint64_t order() const = 0;
};

/// Non-template base for AccelTable shared state and finalization.
///
/// Clients should not use this class directly but rather instantiate AccelTable
/// with a type derived from AccelTableData.
class AccelTableBase {
public:
  /// Hash function type that maps a name to a 32-bit hash value.
  using HashFn = uint32_t(StringRef);

  /// Represents a group of entries with identical name (and hence, hash value).
  struct HashData {
    /// Name associated with this hash group.
    DwarfStringPoolEntryRef Name;
    /// Hash value computed for \p Name.
    uint32_t HashValue;
    /// Entries that share this name and hash.
    std::vector<AccelTableData *> Values;
    /// Optional symbol labeling this hash data in the output.
    MCSymbol *Sym;

    /// Get all AccelTableData cast as a `T`.
    ///
    /// \return Range of entry pointers cast to \c T.
    template <typename T = AccelTableData *> auto getValues() const {
      static_assert(std::is_pointer<T>());
      static_assert(
          std::is_base_of<AccelTableData, std::remove_pointer_t<T>>());
      return map_range(
          Values, [](AccelTableData *Data) { return static_cast<T>(Data); });
    }

#ifndef NDEBUG
    /// Print this hash group to \p OS for debugging.
    ///
    /// \param OS Output stream.
    void print(raw_ostream &OS) const;
    /// Dump this hash group to the debug stream.
    void dump() const { print(dbgs()); }
#endif
  };
  /// List of hash groups in the table.
  using HashList = std::vector<HashData *>;
  /// Buckets, each holding the hash groups that fall into that bucket.
  using BucketList = std::vector<HashList>;

protected:
  /// Allocator for HashData and Values.
  BumpPtrAllocator Allocator;

  /// Map from name string to the corresponding hash group.
  using StringEntries = MapVector<StringRef, HashData>;
  /// Entries keyed by name.
  StringEntries Entries;

  /// Hash function used for names in this table.
  HashFn *Hash;
  /// Number of buckets after finalization.
  uint32_t BucketCount = 0;
  /// Number of unique hash values after finalization.
  uint32_t UniqueHashCount = 0;

  /// Flat list of hash groups.
  HashList Hashes;
  /// Bucketed view of hash groups.
  BucketList Buckets;

  /// Compute the number of buckets for the current set of hashes.
  LLVM_ABI void computeBucketCount();

  /// Construct a base table that hashes names with \p Hash.
  ///
  /// \param Hash Hash function used for entry names.
  AccelTableBase(HashFn *Hash) : Hash(Hash) {}

public:
  /// Finalize the table layout and assign symbols using \p Asm and \p Prefix.
  ///
  /// \param Asm Asm printer used during finalization.
  /// \param Prefix Prefix for generated symbols.
  LLVM_ABI void finalize(AsmPrinter *Asm, StringRef Prefix);
  /// Return the finalized buckets.
  ///
  /// \return Array of buckets, each holding hash groups.
  ArrayRef<HashList> getBuckets() const { return Buckets; }
  /// Return the number of buckets.
  ///
  /// \return Number of buckets after finalization.
  uint32_t getBucketCount() const { return BucketCount; }
  /// Return the number of unique hash values.
  ///
  /// \return Count of distinct hash values after finalization.
  uint32_t getUniqueHashCount() const { return UniqueHashCount; }
  /// Return the number of unique names.
  ///
  /// \return Count of unique names in the table.
  uint32_t getUniqueNameCount() const { return Entries.size(); }

#ifndef NDEBUG
  /// Print the table to \p OS for debugging.
  ///
  /// \param OS Output stream.
  void print(raw_ostream &OS) const;
  /// Dump the table to the debug stream.
  void dump() const { print(dbgs()); }
#endif

  /// AccelTableBase is non-copyable.
  ///
  /// \param Other Unused; copy construction is deleted.
  AccelTableBase(const AccelTableBase &Other) = delete;
  /// AccelTableBase is non-assignable.
  ///
  /// \param Other Unused; copy assignment is deleted.
  void operator=(const AccelTableBase &Other) = delete;
};

/// Abstract accelerator table of buckets containing HashData entries.
///
/// The class is parameterized by the type of entries it holds. The type
/// template parameter also defines the hash function to use for hashing names.
template <typename DataT> class AccelTable : public AccelTableBase {
public:
  /// Construct an empty accelerator table using \c DataT::hash.
  AccelTable() : AccelTableBase(DataT::hash) {}

  /// Add an entry named \p Name constructed from \p Args.
  ///
  /// \param Name String-pool entry for the name.
  /// \param Args Constructor arguments forwarded to \c DataT.
  template <typename... Types>
  void addName(DwarfStringPoolEntryRef Name, Types &&... Args);
  /// Remove all entries from the table.
  void clear() { Entries.clear(); }
  /// Merge all entries from \p Table into this table.
  ///
  /// \param Table Source table whose entries are added.
  void addEntries(AccelTable<DataT> &Table);
  /// Return a copy of the name-to-hash-group map.
  ///
  /// \return Copy of the name-to-hash-group map.
  const StringEntries getEntries() const { return Entries; }
};

template <typename AccelTableDataT>
template <typename... Types>
void AccelTable<AccelTableDataT>::addName(DwarfStringPoolEntryRef Name,
                                          Types &&... Args) {
  assert(Buckets.empty() && "Already finalized!");
  // If the string is in the list already then add this die to the list
  // otherwise add a new one.
  auto &It = Entries[Name.getString()];
  if (It.Values.empty()) {
    It.Name = Name;
    It.HashValue = Hash(Name.getString());
  }
  It.Values.push_back(new (Allocator)
                          AccelTableDataT(std::forward<Types>(Args)...));
}

/// Base class for Apple accelerator table entry data.
///
/// The columns in the table are defined by the static Atoms variable defined on
/// the subclasses.
class AppleAccelTableData : public AccelTableData {
public:
  /// Column descriptor for an Apple accelerator table entry.
  ///
  /// Conceptually it is a column in the accelerator consisting of a type and a
  /// specification of the form of its data.
  struct Atom {
    /// Atom Type.
    const uint16_t Type;
    /// DWARF Form.
    const uint16_t Form;

    /// Construct an atom with DWARF atom type \p Type and form \p Form.
    ///
    /// \param Type Apple accelerator atom type.
    /// \param Form DWARF form used to encode the atom value.
    constexpr Atom(uint16_t Type, uint16_t Form) : Type(Type), Form(Form) {}

#ifndef NDEBUG
    /// Print this atom to \p OS for debugging.
    ///
    /// \param OS Output stream.
    void print(raw_ostream &OS) const;
    /// Dump this atom to the debug stream.
    void dump() const { print(dbgs()); }
#endif
  };
  // Subclasses should define:
  // static constexpr Atom Atoms[];

  /// Emit this entry's atom values using \p Asm.
  ///
  /// \param Asm Asm printer used to emit the entry.
  virtual void emit(AsmPrinter *Asm) const = 0;

  /// Hash \p Buffer with the DJB hash used by Apple accelerator tables.
  ///
  /// \param Buffer Name string to hash.
  /// \return 32-bit DJB hash of \p Buffer.
  static uint32_t hash(StringRef Buffer) { return djbHash(Buffer); }
};

/// Helper class to identify an entry in DWARF5AccelTable based on their DIE
/// offset and UnitID.
struct OffsetAndUnitID {
  /// DIE offset within the unit.
  uint64_t Offset = 0;
  /// Compilation or type unit identifier.
  uint32_t UnitID = 0;
  /// True if \p UnitID refers to a type unit.
  bool IsTU = false;
  /// OffsetAndUnitID requires an explicit offset and unit id.
  OffsetAndUnitID() = delete;
  /// Construct an identifier for offset \p Offset in unit \p UnitID.
  ///
  /// \param Offset DIE offset within the unit.
  /// \param UnitID Compilation or type unit identifier.
  /// \param IsTU Whether \p UnitID refers to a type unit.
  OffsetAndUnitID(uint64_t Offset, uint32_t UnitID, bool IsTU)
      : Offset(Offset), UnitID(UnitID), IsTU(IsTU) {}
  /// Return the DIE offset.
  ///
  /// \return DIE offset within the unit.
  uint64_t offset() const { return Offset; };
  /// Return the unit identifier.
  ///
  /// \return Compilation or type unit identifier.
  uint32_t unitID() const { return UnitID; };
  /// Return true if this identifies a type-unit entry.
  ///
  /// \return True if the unit identifier refers to a type unit.
  bool isTU() const { return IsTU; }
};

/// DenseMapInfo specialization for OffsetAndUnitID keys.
template <> struct DenseMapInfo<OffsetAndUnitID> {
  /// Return a hash combining offset, unit id, and type-unit flag.
  ///
  /// \param Val Key to hash.
  /// \return Hash value for \p Val.
  static unsigned getHashValue(const OffsetAndUnitID &Val) {
    return (unsigned)llvm::hash_combine(Val.offset(), Val.unitID(), Val.IsTU);
  }
  /// Return true if \p LHS and \p RHS identify the same entry.
  ///
  /// \param LHS Left-hand key.
  /// \param RHS Right-hand key.
  /// \return True if both keys identify the same offset and unit.
  static bool isEqual(const OffsetAndUnitID &LHS, const OffsetAndUnitID &RHS) {
    return LHS.offset() == RHS.offset() && LHS.unitID() == RHS.unitID() &&
           LHS.IsTU == RHS.isTU();
  }
};

/// DIE-backed entry data for a DWARF v5 accelerator table.
///
/// Unlike the Apple Data classes, this class is just a DIE wrapper, and does
/// not know to serialize itself. The complete serialization logic is in the
/// emitDWARF5AccelTable function.
class DWARF5AccelTableData : public AccelTableData {
public:
  /// Hash \p Name with the case-folding DJB hash used by DWARF v5.
  ///
  /// \param Name Name string to hash.
  /// \return 32-bit case-folding DJB hash of \p Name.
  static uint32_t hash(StringRef Name) { return caseFoldingDjbHash(Name); }

  /// Construct entry data from DIE \p Die in unit \p UnitID.
  ///
  /// \param Die DIE referenced by this entry.
  /// \param UnitID Compilation or type unit identifier.
  /// \param IsTU Whether \p UnitID refers to a type unit.
  LLVM_ABI DWARF5AccelTableData(const DIE &Die, const uint32_t UnitID,
                                const bool IsTU);
  /// Construct entry data from an explicit DIE offset and tag.
  ///
  /// \param DieOffset Offset of the DIE within its unit.
  /// \param DefiningParentOffset Optional offset of the defining parent DIE.
  /// \param DieTag DWARF tag of the DIE.
  /// \param UnitID Compilation or type unit identifier.
  /// \param IsTU Whether \p UnitID refers to a type unit.
  DWARF5AccelTableData(const uint64_t DieOffset,
                       const std::optional<uint64_t> DefiningParentOffset,
                       const unsigned DieTag, const unsigned UnitID,
                       const bool IsTU)
      : OffsetVal(DieOffset), ParentOffset(DefiningParentOffset),
        DieTag(DieTag), AbbrevNumber(0), IsTU(IsTU), UnitID(UnitID) {}

#ifndef NDEBUG
  /// Print this entry to \p OS for debugging.
  ///
  /// \param OS Output stream.
  void print(raw_ostream &OS) const override;
#endif

  /// Return the DIE offset after normalization.
  ///
  /// \return DIE offset within its unit.
  uint64_t getDieOffset() const {
    assert(isNormalized() && "Accessing DIE Offset before normalizing.");
    return std::get<uint64_t>(OffsetVal);
  }

  /// Return the DIE offset paired with its unit identifier.
  ///
  /// \return DIE offset together with its unit id and type-unit flag.
  OffsetAndUnitID getDieOffsetAndUnitID() const {
    return {getDieOffset(), getUnitID(), isTU()};
  }

  /// Return the DWARF tag of the DIE.
  ///
  /// \return DWARF tag of the referenced DIE.
  unsigned getDieTag() const { return DieTag; }
  /// Return the compilation or type unit identifier.
  ///
  /// \return Compilation or type unit identifier for this entry.
  unsigned getUnitID() const { return UnitID; }
  /// Return true if this entry belongs to a type unit.
  ///
  /// \return True if this entry belongs to a type unit.
  bool isTU() const { return IsTU; }
  /// Replace the stored DIE pointer with its offset and parent offset.
  void normalizeDIEToOffset() {
    assert(!isNormalized() && "Accessing offset after normalizing.");
    const DIE *Entry = std::get<const DIE *>(OffsetVal);
    ParentOffset = getDefiningParentDieOffset(*Entry);
    OffsetVal = Entry->getOffset();
  }
  /// Return true if the DIE has been replaced by an explicit offset.
  ///
  /// \return True if the DIE pointer has been replaced by an offset.
  bool isNormalized() const {
    return std::holds_alternative<uint64_t>(OffsetVal);
  }

  /// Return the defining parent DIE offset, if any.
  ///
  /// \return Defining parent DIE offset, or empty if none.
  std::optional<uint64_t> getParentDieOffset() const {
    if (auto OffsetAndId = getParentDieOffsetAndUnitID())
      return OffsetAndId->offset();
    return {};
  }

  /// Return the defining parent DIE offset and unit id, if any.
  ///
  /// \return Parent offset and unit id, or empty if none.
  std::optional<OffsetAndUnitID> getParentDieOffsetAndUnitID() const {
    assert(isNormalized() && "Accessing DIE Offset before normalizing.");
    if (!ParentOffset)
      return std::nullopt;
    return OffsetAndUnitID(*ParentOffset, getUnitID(), isTU());
  }

  /// Sets AbbrevIndex for an Entry.
  ///
  /// \param AbbrevNum Abbreviation number to assign.
  void setAbbrevNumber(uint16_t AbbrevNum) { AbbrevNumber = AbbrevNum; }

  /// Returns AbbrevIndex for an Entry.
  ///
  /// \return Abbreviation number assigned for emission.
  uint16_t getAbbrevNumber() const { return AbbrevNumber; }

  /// If `Die` has a non-null parent and the parent is not a declaration,
  /// return its offset.
  ///
  /// \param Die DIE whose defining parent offset is requested.
  /// \return Defining parent DIE offset, or empty if none.
  LLVM_ABI static std::optional<uint64_t>
  getDefiningParentDieOffset(const DIE &Die);

protected:
  /// Either the DIE pointer or its normalized offset within the unit.
  std::variant<const DIE *, uint64_t> OffsetVal;
  /// Optional offset of the defining parent DIE.
  std::optional<uint64_t> ParentOffset;
  /// DWARF tag of the DIE.
  uint32_t DieTag : 16;
  /// Abbreviation number assigned for emission.
  uint32_t AbbrevNumber : 15;
  /// True if this entry belongs to a type unit.
  uint32_t IsTU : 1;
  /// Compilation or type unit identifier.
  uint32_t UnitID;
  /// Order entries by DIE offset.
  ///
  /// \return DIE offset used as the sorting key.
  uint64_t order() const override { return getDieOffset(); }
};

/// Abbreviation describing a DWARF v5 .debug_names entry form.
class DebugNamesAbbrev : public FoldingSetNode {
public:
  /// DWARF tag associated with this abbreviation.
  uint32_t DieTag;
  /// Abbreviation number assigned for emission.
  uint32_t Number;
  /// Attribute index and form pair for a .debug_names abbreviation.
  struct AttributeEncoding {
    /// DWARF index attribute encoded by this pair.
    dwarf::Index Index;
    /// DWARF form used to encode the attribute.
    dwarf::Form Form;
  };
  /// Construct an abbreviation for DIE tag \p DieTag.
  ///
  /// \param DieTag DWARF tag associated with this abbreviation.
  DebugNamesAbbrev(uint32_t DieTag) : DieTag(DieTag), Number(0) {}
  /// Add attribute encoding to an abbreviation.
  ///
  /// \param Attr Attribute index/form pair to append.
  void addAttribute(const DebugNamesAbbrev::AttributeEncoding &Attr) {
    AttrVect.push_back(Attr);
  }
  /// Set abbreviation tag index.
  ///
  /// \param AbbrevNumber Abbreviation number to assign.
  void setNumber(uint32_t AbbrevNumber) { Number = AbbrevNumber; }
  /// Get abbreviation tag index.
  ///
  /// \return Abbreviation number assigned for emission.
  uint32_t getNumber() const { return Number; }
  /// Get DIE Tag.
  ///
  /// \return DWARF tag associated with this abbreviation.
  uint32_t getDieTag() const { return DieTag; }
  /// Used to gather unique data for the abbreviation folding set.
  ///
  /// \param ID Folding set ID to profile into.
  LLVM_ABI void Profile(FoldingSetNodeID &ID) const;
  /// Returns attributes for an abbreviation.
  ///
  /// \return Attribute encodings stored in this abbreviation.
  const SmallVector<AttributeEncoding, 1> &getAttributes() const {
    return AttrVect;
  }

private:
  SmallVector<AttributeEncoding, 1> AttrVect;
};

/// Metadata describing a type unit referenced by a DWARF v5 accelerator table.
struct TypeUnitMetaInfo {
  /// Symbol for the TU section start, or the Split DWARF type signature.
  std::variant<MCSymbol *, uint64_t> LabelOrSignature;
  /// Unique identifier of the type unit.
  unsigned UniqueID;
};
/// Vector of type-unit metadata entries.
using TUVectorTy = SmallVector<TypeUnitMetaInfo, 1>;
/// DWARF v5 accelerator table specializing AccelTable for DWARF5AccelTableData.
class DWARF5AccelTable : public AccelTable<DWARF5AccelTableData> {
  // Symbols to start of all the TU sections that were generated.
  TUVectorTy TUSymbolsOrHashes;

public:
  /// Unit index paired with the attribute encoding used to reference it.
  struct UnitIndexAndEncoding {
    /// Index of the unit in the CU/TU list.
    unsigned Index;
    /// Attribute encoding used to reference the unit.
    DebugNamesAbbrev::AttributeEncoding Encoding;
  };
  /// Returns type units that were constructed.
  ///
  /// \return Type-unit metadata recorded for this table.
  const TUVectorTy &getTypeUnitsSymbols() { return TUSymbolsOrHashes; }
  /// Add a type unit start symbol.
  ///
  /// \param U Type unit whose start symbol is recorded.
  LLVM_ABI void addTypeUnitSymbol(DwarfTypeUnit &U);
  /// Add a type unit Signature.
  ///
  /// \param U Type unit whose signature is recorded.
  LLVM_ABI void addTypeUnitSignature(DwarfTypeUnit &U);
  /// Convert DIE entries to explicit offset.
  /// Needs to be called after DIE offsets are computed.
  void convertDieToOffset() {
    for (auto &Entry : Entries) {
      for (auto *Data : Entry.second.getValues<DWARF5AccelTableData *>()) {
        // For TU we normalize as each Unit is emitted.
        // So when this is invoked after CU construction we will be in mixed
        // state.
        if (!Data->isNormalized())
          Data->normalizeDIEToOffset();
      }
    }
  }

  /// Add type-unit entries from \p Table into this table.
  ///
  /// \param Table Source table whose type entries are merged in.
  void addTypeEntries(DWARF5AccelTable &Table) {
    for (auto &Entry : Table.getEntries()) {
      for (auto *Data : Entry.second.getValues<DWARF5AccelTableData *>()) {
        addName(Entry.second.Name, Data->getDieOffset(),
                Data->getParentDieOffset(), Data->getDieTag(),
                Data->getUnitID(), Data->isTU());
      }
    }
  }
};

/// Emit an Apple accelerator table for \p Contents using atom list \p Atoms.
///
/// \param Asm Asm printer used to emit the table.
/// \param Contents Abstract accelerator table contents.
/// \param Prefix Prefix for generated symbols.
/// \param SecBegin Symbol at the start of the accelerator section.
/// \param Atoms Atom descriptors defining the entry columns.
LLVM_ABI void
emitAppleAccelTableImpl(AsmPrinter *Asm, AccelTableBase &Contents,
                        StringRef Prefix, const MCSymbol *SecBegin,
                        ArrayRef<AppleAccelTableData::Atom> Atoms);

/// Emit an Apple Accelerator Table consisting of entries in the specified
/// AccelTable. The DataT template parameter should be derived from
/// AppleAccelTableData.
///
/// \param Asm Asm printer used to emit the table.
/// \param Contents Accelerator table whose entries are emitted.
/// \param Prefix Prefix for generated symbols.
/// \param SecBegin Symbol at the start of the accelerator section.
template <typename DataT>
void emitAppleAccelTable(AsmPrinter *Asm, AccelTable<DataT> &Contents,
                         StringRef Prefix, const MCSymbol *SecBegin) {
  static_assert(std::is_convertible<DataT *, AppleAccelTableData *>::value);
  emitAppleAccelTableImpl(Asm, Contents, Prefix, SecBegin, DataT::Atoms);
}

/// Emit a DWARF v5 accelerator table using compile units from \p DD.
///
/// \param Asm Asm printer used to emit the table.
/// \param Contents Accelerator table whose entries are emitted.
/// \param DD DwarfDebug instance providing compile-unit context.
/// \param CUs Compile units contributing to the table.
LLVM_ABI void
emitDWARF5AccelTable(AsmPrinter *Asm, DWARF5AccelTable &Contents,
                     const DwarfDebug &DD,
                     ArrayRef<std::unique_ptr<DwarfCompileUnit>> CUs);

/// Emit a DWARF v5 accelerator table for the given CU list.
///
/// The \p CUs contains either symbols keeping offsets to the start of
/// compilation unit, either offsets to the start of compilation unit
/// themselves.
///
/// \param Asm Asm printer used to emit the table.
/// \param Contents Accelerator table whose entries are emitted.
/// \param CUs Compilation-unit start symbols or offsets.
/// \param getIndexForEntry Callback returning the unit index/encoding for an
/// entry.
LLVM_ABI void emitDWARF5AccelTable(
    AsmPrinter *Asm, DWARF5AccelTable &Contents,
    ArrayRef<std::variant<MCSymbol *, uint64_t>> CUs,
    llvm::function_ref<std::optional<DWARF5AccelTable::UnitIndexAndEncoding>(
        const DWARF5AccelTableData &)>
        getIndexForEntry);

/// Accelerator table data implementation for simple Apple accelerator tables
/// with just a DIE reference.
class LLVM_ABI AppleAccelTableOffsetData : public AppleAccelTableData {
public:
  /// Construct entry data referencing DIE \p D.
  ///
  /// \param D DIE referenced by this entry.
  AppleAccelTableOffsetData(const DIE &D) : Die(D) {}

  /// Emit the DIE offset atom for this entry.
  ///
  /// \param Asm Asm printer used to emit the entry.
  void emit(AsmPrinter *Asm) const override;

  /// Atom list describing a single DIE-offset column.
  static constexpr Atom Atoms[] = {
      Atom(dwarf::DW_ATOM_die_offset, dwarf::DW_FORM_data4)};

#ifndef NDEBUG
  /// Print this entry to \p OS for debugging.
  ///
  /// \param OS Output stream.
  void print(raw_ostream &OS) const override;
#endif
protected:
  /// Order entries by the referenced DIE offset.
  ///
  /// \return Offset of the referenced DIE.
  uint64_t order() const override { return Die.getOffset(); }

  /// DIE referenced by this accelerator entry.
  const DIE &Die;
};

/// Accelerator table data implementation for Apple type accelerator tables.
class LLVM_ABI AppleAccelTableTypeData : public AppleAccelTableOffsetData {
public:
  /// Construct type-entry data referencing DIE \p D.
  ///
  /// \param D DIE referenced by this entry.
  AppleAccelTableTypeData(const DIE &D) : AppleAccelTableOffsetData(D) {}

  /// Emit the DIE offset, tag, and type-flag atoms for this entry.
  ///
  /// \param Asm Asm printer used to emit the entry.
  void emit(AsmPrinter *Asm) const override;

  /// Atom list describing DIE offset, tag, and type flags.
  static constexpr Atom Atoms[] = {
      Atom(dwarf::DW_ATOM_die_offset, dwarf::DW_FORM_data4),
      Atom(dwarf::DW_ATOM_die_tag, dwarf::DW_FORM_data2),
      Atom(dwarf::DW_ATOM_type_flags, dwarf::DW_FORM_data1)};

#ifndef NDEBUG
  /// Print this entry to \p OS for debugging.
  ///
  /// \param OS Output stream.
  void print(raw_ostream &OS) const override;
#endif
};

/// Accelerator table data implementation for simple Apple accelerator tables
/// with a DIE offset but no actual DIE pointer.
class LLVM_ABI AppleAccelTableStaticOffsetData : public AppleAccelTableData {
public:
  /// Construct entry data for DIE offset \p Offset.
  ///
  /// \param Offset DIE offset stored in this entry.
  AppleAccelTableStaticOffsetData(uint32_t Offset) : Offset(Offset) {}

  /// Emit the DIE offset atom for this entry.
  ///
  /// \param Asm Asm printer used to emit the entry.
  void emit(AsmPrinter *Asm) const override;

  /// Atom list describing a single DIE-offset column.
  static constexpr Atom Atoms[] = {
      Atom(dwarf::DW_ATOM_die_offset, dwarf::DW_FORM_data4)};

#ifndef NDEBUG
  /// Print this entry to \p OS for debugging.
  ///
  /// \param OS Output stream.
  void print(raw_ostream &OS) const override;
#endif
protected:
  /// Order entries by the stored DIE offset.
  ///
  /// \return Stored DIE offset.
  uint64_t order() const override { return Offset; }

  /// DIE offset stored in this entry.
  uint32_t Offset;
};

/// Accelerator table data implementation for type accelerator tables with
/// a DIE offset but no actual DIE pointer.
class LLVM_ABI AppleAccelTableStaticTypeData
    : public AppleAccelTableStaticOffsetData {
public:
  /// Construct static type-entry data for the given DIE metadata.
  ///
  /// \param Offset DIE offset stored in this entry.
  /// \param Tag DWARF tag of the type DIE.
  /// \param ObjCClassIsImplementation Whether this is an ObjC class
  /// implementation.
  /// \param QualifiedNameHash Hash of the type's qualified name.
  AppleAccelTableStaticTypeData(uint32_t Offset, uint16_t Tag,
                                bool ObjCClassIsImplementation,
                                uint32_t QualifiedNameHash)
      : AppleAccelTableStaticOffsetData(Offset),
        QualifiedNameHash(QualifiedNameHash), Tag(Tag),
        ObjCClassIsImplementation(ObjCClassIsImplementation) {}

  /// Emit the DIE offset, tag, ObjC flag, and qualified-name hash atoms.
  ///
  /// \param Asm Asm printer used to emit the entry.
  void emit(AsmPrinter *Asm) const override;

  /// Atom list describing DIE offset, tag, ObjC flag, and name hash.
  static constexpr Atom Atoms[] = {
      Atom(dwarf::DW_ATOM_die_offset, dwarf::DW_FORM_data4),
      Atom(dwarf::DW_ATOM_die_tag, dwarf::DW_FORM_data2),
      Atom(5, dwarf::DW_FORM_data1), Atom(6, dwarf::DW_FORM_data4)};

#ifndef NDEBUG
  /// Print this entry to \p OS for debugging.
  ///
  /// \param OS Output stream.
  void print(raw_ostream &OS) const override;
#endif
protected:
  /// Order entries by the stored DIE offset.
  ///
  /// \return Stored DIE offset.
  uint64_t order() const override { return Offset; }

  /// Hash of the type's qualified name.
  uint32_t QualifiedNameHash;
  /// DWARF tag of the type DIE.
  uint16_t Tag;
  /// True if this entry is an Objective-C class implementation.
  bool ObjCClassIsImplementation;
};

} // end namespace llvm

#endif // LLVM_CODEGEN_ACCELTABLE_H
