//===- DWARFAcceleratorTable.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFACCELERATORTABLE_H
#define LLVM_DEBUGINFO_DWARF_DWARFACCELERATORTABLE_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/DebugInfo/DWARF/DWARFDataExtractor.h"
#include "llvm/DebugInfo/DWARF/DWARFFormValue.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <utility>

namespace llvm {

class raw_ostream;
class ScopedPrinter;

/// Common base for Apple and DWARF 5 accelerator tables.
///
/// The accelerator tables are designed to allow efficient random access
/// (using a symbol name as a key) into debug info by providing an index of the
/// debug info DIEs. This class implements the common functionality of Apple and
/// DWARF 5 accelerator tables.
/// TODO: Generalize the rest of the AppleAcceleratorTable interface and move it
/// to this class.
class LLVM_ABI DWARFAcceleratorTable {
protected:
  /// Contents of the accelerator table section.
  DWARFDataExtractor AccelSection;
  /// Contents of the string table section referenced by the accelerator table.
  DataExtractor StringSection;

public:
  /// An abstract class representing a single entry in the accelerator tables.
  class Entry {
  protected:
    /// Raw form values stored in this accelerator entry.
    SmallVector<DWARFFormValue, 3> Values;

    /// Default-construct an empty accelerator entry.
    Entry() = default;

    // Make these protected so only (final) subclasses can be copied around.
    /// Copy-construct an accelerator entry.
    ///
    /// \param Other Entry to copy.
    Entry(const Entry &Other) = default;
    /// Move-construct an accelerator entry.
    ///
    /// \param Other Entry to move from.
    Entry(Entry &&Other) = default;
    /// Copy-assign an accelerator entry.
    ///
    /// \param Other Entry to copy-assign from.
    /// \return Reference to this entry.
    Entry &operator=(const Entry &Other) = default;
    /// Move-assign an accelerator entry.
    ///
    /// \param Other Entry to move-assign from.
    /// \return Reference to this entry.
    Entry &operator=(Entry &&Other) = default;
    /// Destroy an accelerator entry.
    ~Entry() = default;


  public:
    /// Returns the compilation unit offset for this entry, if any.
    ///
    /// Returns the Offset of the Compilation Unit associated with this
    /// Accelerator Entry or std::nullopt if the Compilation Unit offset is not
    /// recorded in this Accelerator Entry.
    ///
    /// \return Compilation unit offset, or std::nullopt if not recorded.
    virtual std::optional<uint64_t> getCUOffset() const = 0;

    /// Returns the Offset of the Type Unit associated with this
    /// Accelerator Entry or std::nullopt if the Type Unit offset is not
    /// recorded in this Accelerator Entry.
    ///
    /// \return Local type unit offset, or std::nullopt if not recorded.
    virtual std::optional<uint64_t> getLocalTUOffset() const {
      // Default return for accelerator tables that don't support type units.
      return std::nullopt;
    }

    /// Returns the foreign type unit signature for this entry, if any.
    ///
    /// Returns the type signature of the Type Unit associated with this
    /// Accelerator Entry or std::nullopt if the Type Unit offset is not
    /// recorded in this Accelerator Entry.
    ///
    /// \return Foreign type unit signature, or std::nullopt if not recorded.
    virtual std::optional<uint64_t> getForeignTUTypeSignature() const {
      // Default return for accelerator tables that don't support type units.
      return std::nullopt;
    }

    /// Returns the Tag of the Debug Info Entry associated with this
    /// Accelerator Entry or std::nullopt if the Tag is not recorded in this
    /// Accelerator Entry.
    ///
    /// \return DWARF tag of the associated DIE, or std::nullopt if not recorded.
    virtual std::optional<dwarf::Tag> getTag() const = 0;

    /// Returns the raw form values stored in this accelerator entry.
    ///
    /// In general, these can only be interpreted with the help of the metadata
    /// in the owning Accelerator Table.
    ///
    /// \return Array of raw form values stored in this entry.
    ArrayRef<DWARFFormValue> getValues() const { return Values; }
  };

  /// Construct an accelerator table over the given accelerator and string
  /// sections.
  ///
  /// \param AccelSection Accelerator table section contents.
  /// \param StringSection String table section referenced by the accelerator
  /// table.
  DWARFAcceleratorTable(const DWARFDataExtractor &AccelSection,
                        DataExtractor StringSection)
      : AccelSection(AccelSection), StringSection(StringSection) {}
  /// Destroy this accelerator table.
  virtual ~DWARFAcceleratorTable();

  /// Parse the accelerator table from AccelSection.
  ///
  /// \return Success, or an error if the table could not be parsed.
  virtual Error extract() = 0;
  /// Dump this accelerator table to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  virtual void dump(raw_ostream &OS) const = 0;

  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is not supported.
  DWARFAcceleratorTable(const DWARFAcceleratorTable &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  void operator=(const DWARFAcceleratorTable &Other) = delete;
};

/// This implements the Apple accelerator table format, a precursor of the
/// DWARF 5 accelerator table format.
class LLVM_ABI AppleAcceleratorTable : public DWARFAcceleratorTable {
  struct Header {
    uint32_t Magic;
    uint16_t Version;
    uint16_t HashFunction;
    uint32_t BucketCount;
    uint32_t HashCount;
    uint32_t HeaderDataLength;

    LLVM_ABI void dump(ScopedPrinter &W) const;
  };

  struct HeaderData {
    using AtomType = uint16_t;
    using Form = dwarf::Form;

    uint64_t DIEOffsetBase;
    SmallVector<std::pair<AtomType, Form>, 3> Atoms;

    LLVM_ABI std::optional<uint64_t>
    extractOffset(std::optional<DWARFFormValue> Value) const;
  };

  Header Hdr;
  HeaderData HdrData;
  dwarf::FormParams FormParams;
  uint32_t HashDataEntryLength;
  bool IsValid = false;

  /// Returns true if we should continue scanning for entries or false if we've
  /// reached the last (sentinel) entry of encountered a parsing error.
  bool dumpName(ScopedPrinter &W, SmallVectorImpl<DWARFFormValue> &AtomForms,
                uint64_t *DataOffset) const;

  /// Reads an uint32_t from the accelerator table at Offset, which is
  /// incremented by the number of bytes read.
  std::optional<uint32_t> readU32FromAccel(uint64_t &Offset,
                                           bool UseRelocation = false) const;

  /// Reads a StringRef from the string table at Offset.
  std::optional<StringRef>
  readStringFromStrSection(uint64_t StringSectionOffset) const;

  /// Return the offset into the section where the Buckets begin.
  uint64_t getBucketBase() const { return sizeof(Hdr) + Hdr.HeaderDataLength; }

  /// Return the offset into the section where the I-th bucket is.
  uint64_t getIthBucketBase(uint32_t I) const {
    return getBucketBase() + I * 4;
  }

  /// Return the offset into the section where the hash list begins.
  uint64_t getHashBase() const { return getBucketBase() + getNumBuckets() * 4; }

  /// Return the offset into the section where the I-th hash is.
  std::optional<uint64_t> getIthHashBase(uint32_t I) const {
    if (I < Hdr.HashCount)
      return getHashBase() + I * 4;
    return std::nullopt;
  }

  /// Return the offset into the section where the offset list begins.
  uint64_t getOffsetBase() const { return getHashBase() + getNumHashes() * 4; }

  /// Return the offset into the section where the table entries begin.
  uint64_t getEntriesBase() const {
    return getOffsetBase() + getNumHashes() * 4;
  }

  /// Return the offset into the section where the I-th offset is.
  std::optional<uint64_t> getIthOffsetBase(uint32_t I) const {
    if (I < Hdr.HashCount)
      return getOffsetBase() + I * 4;
    return std::nullopt;
  }

  /// Returns the index of the bucket where a hypothetical Hash would be.
  uint32_t hashToBucketIdx(uint32_t Hash) const {
    return Hash % getNumBuckets();
  }

  /// Returns true iff a hypothetical Hash would be assigned to the BucketIdx-th
  /// bucket.
  bool wouldHashBeInBucket(uint32_t Hash, uint32_t BucketIdx) const {
    return hashToBucketIdx(Hash) == BucketIdx;
  }

  /// Reads the contents of the I-th bucket, that is, the index in the hash list
  /// where the hashes corresponding to this bucket begin.
  std::optional<uint32_t> readIthBucket(uint32_t I) const {
    uint64_t Offset = getIthBucketBase(I);
    return readU32FromAccel(Offset);
  }

  /// Reads the I-th hash in the hash list.
  std::optional<uint32_t> readIthHash(uint32_t I) const {
    std::optional<uint64_t> OptOffset = getIthHashBase(I);
    if (OptOffset)
      return readU32FromAccel(*OptOffset);
    return std::nullopt;
  }

  /// Reads the I-th offset in the offset list.
  std::optional<uint32_t> readIthOffset(uint32_t I) const {
    std::optional<uint64_t> OptOffset = getIthOffsetBase(I);
    if (OptOffset)
      return readU32FromAccel(*OptOffset);
    return std::nullopt;
  }

  /// Reads a string offset from the accelerator table at Offset, which is
  /// incremented by the number of bytes read.
  std::optional<uint32_t> readStringOffsetAt(uint64_t &Offset) const {
    return readU32FromAccel(Offset, /*UseRelocation*/ true);
  }

  /// Scans through all Hashes in the BucketIdx-th bucket, attempting to find
  /// HashToFind. If it is found, its index in the list of hashes is returned.
  std::optional<uint32_t> idxOfHashInBucket(uint32_t HashToFind,
                                            uint32_t BucketIdx) const;

public:
  /// Apple-specific implementation of an Accelerator Entry.
  class LLVM_ABI Entry final : public DWARFAcceleratorTable::Entry {
    const AppleAcceleratorTable &Table;

    Entry(const AppleAcceleratorTable &Table);
    void extract(uint64_t *Offset);

  public:
    /// Compilation unit offset for this entry, if recorded.
    ///
    /// \return Compilation unit offset, or std::nullopt if not recorded.
    std::optional<uint64_t> getCUOffset() const override;

    /// Returns the DIE section offset for this entry, if recorded.
    ///
    /// Returns the Section Offset of the Debug Info Entry associated with this
    /// Accelerator Entry or std::nullopt if the DIE offset is not recorded in
    /// this Accelerator Entry. The returned offset is relative to the start of
    /// the Section containing the DIE.
    ///
    /// \return DIE section offset, or std::nullopt if not recorded.
    std::optional<uint64_t> getDIESectionOffset() const;

    /// DWARF tag of the DIE described by this entry, if recorded.
    ///
    /// \return DWARF tag of the DIE, or std::nullopt if not recorded.
    std::optional<dwarf::Tag> getTag() const override;

    /// Returns the value of the Atom in this Accelerator Entry, if the Entry
    /// contains such Atom.
    ///
    /// \param Atom Atom type to look up in this entry.
    /// \return Form value for \p Atom, or std::nullopt if not present.
    std::optional<DWARFFormValue> lookup(HeaderData::AtomType Atom) const;

    friend class AppleAcceleratorTable;
    /// An iterator for Entries all having the same string as key.
    friend class ValueIterator;
  };

  /// An iterator for Entries all having the same string as key.
  class SameNameIterator
      : public iterator_facade_base<SameNameIterator, std::forward_iterator_tag,
                                    Entry> {
    Entry Current;
    uint64_t Offset = 0;

  public:
    /// Construct a new iterator for the entries at \p DataOffset.
    ///
    /// \param AccelTable Accelerator table providing entry layout metadata.
    /// \param DataOffset Offset of the first hash-data entry for this name.
    LLVM_ABI SameNameIterator(const AppleAcceleratorTable &AccelTable,
                              uint64_t DataOffset);

    /// Return the accelerator entry at the current data offset.
    ///
    /// \return Reference to the accelerator entry at the current offset.
    const Entry &operator*() {
      uint64_t OffsetCopy = Offset;
      Current.extract(&OffsetCopy);
      return Current;
    }
    /// Advance to the next hash-data entry for the same name.
    ///
    /// \return Reference to this iterator after advancing.
    SameNameIterator &operator++() {
      Offset += Current.Table.getHashDataEntryLength();
      return *this;
    }
    /// Compare two same-name iterators for equality.
    ///
    /// \param A First iterator to compare.
    /// \param B Second iterator to compare.
    /// \return True if both iterators refer to the same data offset.
    friend bool operator==(const SameNameIterator &A,
                           const SameNameIterator &B) {
      return A.Offset == B.Offset;
    }
  };

  /// An accelerator entry paired with the string-table offset of its key.
  struct EntryWithName {
    /// Construct an empty named entry bound to \p Table.
    ///
    /// \param Table Accelerator table providing entry layout metadata.
    EntryWithName(const AppleAcceleratorTable &Table)
        : BaseEntry(Table), StrOffset(0) {}

    /// Read the entry key from the string section, if the offset is valid.
    ///
    /// \return Entry key string, or std::nullopt if the offset is invalid.
    std::optional<StringRef> readName() const {
      return BaseEntry.Table.readStringFromStrSection(StrOffset);
    }

    /// Accelerator entry for this name.
    Entry BaseEntry;
    /// Offset of this entry's name in the string section.
    uint32_t StrOffset;
  };

  /// An iterator for all entries in the table.
  class Iterator
      : public iterator_facade_base<Iterator, std::forward_iterator_tag,
                                    EntryWithName> {
    constexpr static auto EndMarker = std::numeric_limits<uint64_t>::max();

    EntryWithName Current;
    uint32_t OffsetIdx = 0;
    uint64_t Offset = EndMarker;
    uint32_t NumEntriesToCome = 0;

    void setToEnd() { Offset = EndMarker; }
    bool isEnd() const { return Offset == EndMarker; }
    const AppleAcceleratorTable &getTable() const {
      return Current.BaseEntry.Table;
    }

    /// Reads the next Entry in the table, populating `Current`.
    /// If not possible (e.g. end of the section), becomes the end iterator.
    LLVM_ABI void prepareNextEntryOrEnd();

    /// Reads the next string pointer and the entry count for that string,
    /// populating `NumEntriesToCome`.
    /// If not possible (e.g. end of the section), becomes the end iterator.
    /// If `Offset` is zero, then the next valid string offset will be fetched
    /// from the Offsets array, otherwise it will continue to parse the current
    /// entry's strings.
    void prepareNextStringOrEnd();

  public:
    /// Construct an iterator over all entries, optionally positioned at end.
    ///
    /// \param Table Accelerator table being iterated.
    /// \param SetEnd If true, construct an end iterator.
    LLVM_ABI Iterator(const AppleAcceleratorTable &Table, bool SetEnd = false);

    /// Advance to the next entry in the table.
    ///
    /// \return Reference to this iterator after advancing.
    Iterator &operator++() {
      prepareNextEntryOrEnd();
      return *this;
    }
    /// Compare two table iterators for equality.
    ///
    /// \param It Iterator to compare against.
    /// \return True if both iterators refer to the same table offset.
    bool operator==(const Iterator &It) const { return Offset == It.Offset; }
    /// Return the current entry together with its name.
    ///
    /// \return Current entry paired with its name.
    const EntryWithName &operator*() const {
      assert(!isEnd() && "dereferencing end iterator");
      return Current;
    }
  };

  /// Construct an Apple accelerator table over the given sections.
  ///
  /// \param AccelSection Accelerator table section contents.
  /// \param StringSection String table section referenced by the accelerator
  /// table.
  AppleAcceleratorTable(const DWARFDataExtractor &AccelSection,
                        DataExtractor StringSection)
      : DWARFAcceleratorTable(AccelSection, StringSection) {}

  /// Parse the Apple accelerator table from the accelerator section.
  ///
  /// \return Success, or an error if the table could not be parsed.
  Error extract() override;
  /// Number of hash buckets in this table.
  ///
  /// \return Number of hash buckets in this table.
  uint32_t getNumBuckets() const;
  /// Number of hashes in this table.
  ///
  /// \return Number of hashes in this table.
  uint32_t getNumHashes() const;
  /// Size in bytes of the fixed header.
  ///
  /// \return Size in bytes of the fixed header.
  uint32_t getSizeHdr() const;
  /// Length in bytes of the header data payload.
  ///
  /// \return Length in bytes of the header data payload.
  uint32_t getHeaderDataLength() const;

  /// Returns the size of one HashData entry.
  ///
  /// \return Size in bytes of one HashData entry.
  uint32_t getHashDataEntryLength() const { return HashDataEntryLength; }

  /// Return the Atom description, which can be used to interpret the raw values
  /// of the Accelerator Entries in this table.
  ///
  /// \return Array of atom type and form pairs for this table.
  ArrayRef<std::pair<HeaderData::AtomType, HeaderData::Form>> getAtomsDesc();

  /// Returns true iff `AtomTy` is one of the atoms available in Entries of this
  /// table.
  ///
  /// \param AtomTy Atom type to look for in this table's atom list.
  /// \return True if \p AtomTy is present in this table's atom list.
  bool containsAtomType(HeaderData::AtomType AtomTy) const {
    return is_contained(make_first_range(HdrData.Atoms), AtomTy);
  }

  /// Validate that atom forms in the header are consistent and supported.
  ///
  /// \return True if atom forms in the header are consistent and supported.
  bool validateForms();

  /// Return information related to the DWARF DIE we're looking for when
  /// performing a lookup by name.
  ///
  /// \param HashDataOffset an offset into the hash data table
  /// \returns <DieOffset, DieTag>
  /// DieOffset is the offset into the .debug_info section for the DIE
  /// related to the input hash data offset.
  /// DieTag is the tag of the DIE
  std::pair<uint64_t, dwarf::Tag> readAtoms(uint64_t *HashDataOffset);
  /// Dump this accelerator table to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  void dump(raw_ostream &OS) const override;

  /// Look up all entries in the accelerator table matching \c Key.
  ///
  /// \param Key Name to search for in this accelerator table.
  /// \return Iterator range over all entries matching \p Key.
  iterator_range<SameNameIterator> equal_range(StringRef Key) const;

  /// Lookup all entries in the accelerator table.
  ///
  /// \return Iterator range over every entry in this accelerator table.
  auto entries() const {
    return make_range(Iterator(*this), Iterator(*this, /*SetEnd*/ true));
  }
};

/// Parser for the DWARF v5 .debug_names accelerator section.
///
/// .debug_names section consists of one or more units. Each unit starts with a
/// header, which is followed by a list of compilation units, local and foreign
/// type units.
///
/// These may be followed by an (optional) hash lookup table, which consists of
/// an array of buckets and hashes similar to the apple tables above. The only
/// difference is that the hashes array is 1-based, and consequently an empty
/// bucket is denoted by 0 and not UINT32_MAX.
///
/// Next is the name table, which consists of an array of names and array of
/// entry offsets. This is different from the apple tables, which store names
/// next to the actual entries.
///
/// The structure of the entries is described by an abbreviations table, which
/// comes after the name table. Unlike the apple tables, which have a uniform
/// entry structure described in the header, each .debug_names entry may have
/// different index attributes (DW_IDX_???) attached to it.
///
/// The last segment consists of a list of entries, which is a 0-terminated list
/// referenced by the name table and interpreted with the help of the
/// abbreviation table.
class LLVM_ABI DWARFDebugNames : public DWARFAcceleratorTable {
public:
  class NameIndex;
  class NameIterator;
  class ValueIterator;

  /// DWARF v5 Name Index header.
  struct Header {
    /// Length of this name index unit, excluding the length field itself.
    uint64_t UnitLength;
    /// DWARF 32/64 format of this name index.
    dwarf::DwarfFormat Format;
    /// DWARF version number of this name index.
    uint16_t Version;
    /// Number of compilation units covered by this name index.
    uint32_t CompUnitCount;
    /// Number of local type units covered by this name index.
    uint32_t LocalTypeUnitCount;
    /// Number of foreign type units covered by this name index.
    uint32_t ForeignTypeUnitCount;
    /// Number of buckets in the optional hash lookup table.
    uint32_t BucketCount;
    /// Number of names in the name table.
    uint32_t NameCount;
    /// Size in bytes of the abbreviations table.
    uint32_t AbbrevTableSize;
    /// Size in bytes of the augmentation string.
    uint32_t AugmentationStringSize;
    /// Producer-specific augmentation string.
    SmallString<8> AugmentationString;

    /// Parse this header from \p AS starting at \p Offset.
    ///
    /// \param AS Accelerator section data to parse from.
    /// \param Offset Offset into \p AS; advanced past the header on success.
    /// \return Success, or an error if the header could not be parsed.
    LLVM_ABI Error extract(const DWARFDataExtractor &AS, uint64_t *Offset);
    /// Dump this header to \p W.
    ///
    /// \param W Printer used for dump output.
    LLVM_ABI void dump(ScopedPrinter &W) const;
  };

  /// Index attribute and its encoding.
  struct AttributeEncoding {
    /// DWARF index attribute (DW_IDX_*).
    dwarf::Index Index;
    /// Form used to encode the attribute value.
    dwarf::Form Form;

    /// Construct an attribute encoding from index and form.
    ///
    /// \param Index DWARF index attribute (DW_IDX_*).
    /// \param Form Form used to encode the attribute value.
    constexpr AttributeEncoding(dwarf::Index Index, dwarf::Form Form)
        : Index(Index), Form(Form) {}

    /// Compare two attribute encodings for equality.
    ///
    /// \param LHS First attribute encoding to compare.
    /// \param RHS Second attribute encoding to compare.
    /// \return True if both encodings have the same index and form.
    friend bool operator==(const AttributeEncoding &LHS,
                           const AttributeEncoding &RHS) {
      return LHS.Index == RHS.Index && LHS.Form == RHS.Form;
    }
  };

  /// Abbreviation describing the encoding of Name Index entries.
  struct Abbrev {
    uint64_t AbbrevOffset; ///< Abbreviation offset in the .debug_names section
    uint32_t Code;         ///< Abbreviation code
    dwarf::Tag Tag; ///< Dwarf Tag of the described entity.
    std::vector<AttributeEncoding> Attributes; ///< List of index attributes.

    /// Construct an abbreviation from code, tag, offset, and attributes.
    ///
    /// \param Code Abbreviation code.
    /// \param Tag DWARF tag of the described entity.
    /// \param AbbrevOffset Abbreviation offset in the .debug_names section.
    /// \param Attributes List of index attribute encodings.
    Abbrev(uint32_t Code, dwarf::Tag Tag, uint64_t AbbrevOffset,
           std::vector<AttributeEncoding> Attributes)
        : AbbrevOffset(AbbrevOffset), Code(Code), Tag(Tag),
          Attributes(std::move(Attributes)) {}

    /// Dump this abbreviation to \p W.
    ///
    /// \param W Printer used for dump output.
    LLVM_ABI void dump(ScopedPrinter &W) const;
  };

  /// DWARF v5-specific implementation of an Accelerator Entry.
  class LLVM_ABI Entry final : public DWARFAcceleratorTable::Entry {
    const NameIndex *NameIdx;
    const Abbrev *Abbr;

    Entry(const NameIndex &NameIdx, const Abbrev &Abbr);

  public:
    /// Name index that owns this accelerator entry.
    ///
    /// \return Pointer to the owning Name Index.
    const NameIndex *getNameIndex() const { return NameIdx; }
    /// Compilation unit offset for this entry, if recorded.
    ///
    /// \return Compilation unit offset, or std::nullopt if not recorded.
    std::optional<uint64_t> getCUOffset() const override;
    /// Local type unit offset for this entry, if recorded.
    ///
    /// \return Local type unit offset, or std::nullopt if not recorded.
    std::optional<uint64_t> getLocalTUOffset() const override;
    /// Foreign type unit signature for this entry, if recorded.
    ///
    /// \return Foreign type unit signature, or std::nullopt if not recorded.
    std::optional<uint64_t> getForeignTUTypeSignature() const override;
    /// DWARF tag of the DIE described by this entry.
    ///
    /// \return DWARF tag of the DIE described by this entry.
    std::optional<dwarf::Tag> getTag() const override { return tag(); }

    /// Return the related compilation unit offset for type-unit entries.
    ///
    /// Special function that will return the related CU offset needed type
    /// units. This gets used to find the .dwo file that originated the entries
    /// for a given type unit.
    ///
    /// \return Related compilation unit offset, or std::nullopt if unavailable.
    std::optional<uint64_t> getRelatedCUOffset() const;

    /// Returns the compilation unit index for this entry, if any.
    ///
    /// Returns the Index into the Compilation Unit list of the owning Name
    /// Index or std::nullopt if this Accelerator Entry does not have an
    /// associated Compilation Unit. It is up to the user to verify that the
    /// returned Index is valid in the owning NameIndex (or use getCUOffset(),
    /// which will handle that check itself). Note that entries in NameIndexes
    /// which index just a single Compilation Unit are implicitly associated
    /// with that unit, so this function will return 0 even without an explicit
    /// DW_IDX_compile_unit attribute, unless there is a DW_IDX_type_unit
    /// attribute.
    ///
    /// \return Compilation unit index, or std::nullopt if none is associated.
    std::optional<uint64_t> getCUIndex() const;

    /// Returns the related compilation unit index for this entry.
    ///
    /// Similar functionality to getCUIndex() but without the DW_IDX_type_unit
    /// restriction. This allows us to get the associated a compilation unit
    /// index for an entry that is a type unit.
    ///
    /// \return Related compilation unit index, or std::nullopt if unavailable.
    std::optional<uint64_t> getRelatedCUIndex() const;

    /// Returns the type unit index for this entry, if any.
    ///
    /// Returns the index of the Type Unit of the owning Name Index or
    /// std::nullopt if this Accelerator Entry does not have an associated Type
    /// Unit. It is up to the user to verify that the returned Index is a valid
    /// index in the owning NameIndex (or use getLocalTUOffset(), which will
    /// handle that check itself).
    ///
    /// \return Type unit index, or std::nullopt if none is associated.
    std::optional<uint64_t> getTUIndex() const;

    /// .debug_names-specific getter, which always succeeds (DWARF v5 index
    /// entries always have a tag).
    ///
    /// \return DWARF tag of the DIE described by this entry.
    dwarf::Tag tag() const { return Abbr->Tag; }

    /// Returns the Offset of the DIE within the containing CU or TU.
    ///
    /// \return DIE offset within the containing CU or TU, or std::nullopt.
    std::optional<uint64_t> getDIEUnitOffset() const;

    /// Returns true if this Entry has information about its parent DIE (i.e. if
    /// it has an IDX_parent attribute)
    ///
    /// \return True if this entry has an IDX_parent attribute.
    bool hasParentInformation() const;

    /// Returns the accelerator entry for this DIE's parent, if present.
    ///
    /// Returns the Entry corresponding to the parent of the DIE represented by
    /// `this` Entry. If the parent is not in the table, nullopt is returned.
    /// Precondition: hasParentInformation() == true.
    /// An error is returned for ill-formed tables.
    ///
    /// \return Parent accelerator entry, std::nullopt if absent, or an error.
    Expected<std::optional<DWARFDebugNames::Entry>> getParentDIEEntry() const;

    /// Return the Abbreviation that can be used to interpret the raw values of
    /// this Accelerator Entry.
    ///
    /// \return Abbreviation describing the encoding of this entry.
    const Abbrev &getAbbrev() const { return *Abbr; }

    /// Returns the value of the Index Attribute in this Accelerator Entry, if
    /// the Entry contains such Attribute.
    ///
    /// \param Index Index attribute to look up in this entry.
    /// \return Form value for \p Index, or std::nullopt if not present.
    std::optional<DWARFFormValue> lookup(dwarf::Index Index) const;

    /// Dump this accelerator entry to \p W.
    ///
    /// \param W Printer used for dump output.
    void dump(ScopedPrinter &W) const;
    /// Dump the parent-index attribute value \p FormValue to \p W.
    ///
    /// \param W Printer used for dump output.
    /// \param FormValue Parent-index form value to dump.
    void dumpParentIdx(ScopedPrinter &W, const DWARFFormValue &FormValue) const;

    friend class NameIndex;
    friend class ValueIterator;
  };

  /// Error returned by NameIndex::getEntry to report it has reached the end of
  /// the entry list.
  class LLVM_ABI SentinelError : public ErrorInfo<SentinelError> {
  public:
    /// ErrorInfo ID for SentinelError.
    static char ID;

    /// Write a human-readable description of this error to \p OS.
    ///
    /// \param OS Output stream to write the description to.
    void log(raw_ostream &OS) const override { OS << "Sentinel"; }
    /// Convert this sentinel error to a std::error_code.
    ///
    /// \return Std::error_code corresponding to this sentinel error.
    std::error_code convertToErrorCode() const override;
  };

private:
  /// DenseMapInfo for struct Abbrev.
  struct AbbrevMapInfo {
    static unsigned getHashValue(uint32_t Code) {
      return DenseMapInfo<uint32_t>::getHashValue(Code);
    }
    static unsigned getHashValue(const Abbrev &Abbr) {
      return getHashValue(Abbr.Code);
    }
    static bool isEqual(uint32_t LHS, const Abbrev &RHS) {
      return LHS == RHS.Code;
    }
    static bool isEqual(const Abbrev &LHS, const Abbrev &RHS) {
      return LHS.Code == RHS.Code;
    }
  };

public:
  /// A single entry in the Name Table (DWARF v5 sect. 6.1.1.4.6) of the Name
  /// Index.
  class NameTableEntry {
    DataExtractor StrData;

    uint32_t Index;
    uint64_t StringOffset;
    uint64_t EntryOffset;

  public:
    /// Construct a name table entry from string data and offsets.
    ///
    /// \param StrData String section data used to resolve the name.
    /// \param Index One-based index of this name in the parent Name Index.
    /// \param StringOffset Offset of the name in the string section.
    /// \param EntryOffset Offset of the first entry in the entry pool.
    NameTableEntry(const DataExtractor &StrData, uint32_t Index,
                   uint64_t StringOffset, uint64_t EntryOffset)
        : StrData(StrData), Index(Index), StringOffset(StringOffset),
          EntryOffset(EntryOffset) {}

    /// Return the index of this name in the parent Name Index.
    ///
    /// \return One-based index of this name in the parent Name Index.
    uint32_t getIndex() const { return Index; }

    /// Returns the offset of the name of the described entities.
    ///
    /// \return Offset of the name in the string section.
    uint64_t getStringOffset() const { return StringOffset; }

    /// Return the string referenced by this name table entry or nullptr if the
    /// string offset is not valid.
    ///
    /// \return C string for this name, or nullptr if the offset is invalid.
    const char *getString() const {
      uint64_t Off = StringOffset;
      return StrData.getCStr(&Off);
    }

    /// Returns true if this entry's name equals Target.
    ///
    /// This is more efficient in hot code paths that do not need the length of
    /// the name.
    ///
    /// \param Target Name to compare against this entry.
    /// \return True if this entry's name equals \p Target.
    bool sameNameAs(StringRef Target) const {
      // Note: this is not the name, but the rest of debug_str starting from
      // name. This handles corrupt data (non-null terminated) without
      // overrunning the buffer.
      StringRef Data = StrData.getData().substr(StringOffset);
      size_t TargetSize = Target.size();
      return Data.size() > TargetSize && !Data[TargetSize] &&
             strncmp(Data.data(), Target.data(), TargetSize) == 0;
    }

    /// Returns the offset of the first Entry in the list.
    ///
    /// \return Offset of the first entry in the entry pool for this name.
    uint64_t getEntryOffset() const { return EntryOffset; }
  };

  /// Offsets for the start of various important tables from the start of the
  /// section.
  struct DWARFDebugNamesOffsets {
    /// Offset of the compilation unit list.
    uint64_t CUsBase;
    /// Offset of the bucket array.
    uint64_t BucketsBase;
    /// Offset of the hash array.
    uint64_t HashesBase;
    /// Offset of the string offsets array.
    uint64_t StringOffsetsBase;
    /// Offset of the entry offsets array.
    uint64_t EntryOffsetsBase;
    /// Offset of the entry pool.
    uint64_t EntriesBase;
  };

  /// Represents a single accelerator table within the DWARF v5 .debug_names
  /// section.
  class NameIndex {
    DenseSet<Abbrev, AbbrevMapInfo> Abbrevs;
    struct Header Hdr;
    const DWARFDebugNames &Section;

    // Base of the whole unit and of various important tables, as offsets from
    // the start of the section.
    uint64_t Base;
    DWARFDebugNamesOffsets Offsets;

    void dumpCUs(ScopedPrinter &W) const;
    void dumpLocalTUs(ScopedPrinter &W) const;
    void dumpForeignTUs(ScopedPrinter &W) const;
    void dumpAbbreviations(ScopedPrinter &W) const;
    bool dumpEntry(ScopedPrinter &W, uint64_t *Offset) const;
    void dumpName(ScopedPrinter &W, const NameTableEntry &NTE,
                  std::optional<uint32_t> Hash) const;
    void dumpBucket(ScopedPrinter &W, uint32_t Bucket) const;

    Expected<AttributeEncoding> extractAttributeEncoding(uint64_t *Offset);

    Expected<std::vector<AttributeEncoding>>
    extractAttributeEncodings(uint64_t *Offset);

    Expected<Abbrev> extractAbbrev(uint64_t *Offset);

  public:
    /// Construct a Name Index covering the unit that starts at \p Base.
    ///
    /// \param Section Owning .debug_names accelerator section.
    /// \param Base Section offset of the start of this name index unit.
    NameIndex(const DWARFDebugNames &Section, uint64_t Base)
        : Section(Section), Base(Base) {}

    /// Returns Hdr field
    ///
    /// \return Parsed header of this name index.
    Header getHeader() const { return Hdr; }

    /// Returns Offsets field
    ///
    /// \return Section offsets of the tables within this name index.
    DWARFDebugNamesOffsets getOffsets() const { return Offsets; }

    /// Reads offset of compilation unit CU. CU is 0-based.
    ///
    /// \param CU Zero-based compilation unit index.
    /// \return Section offset of the compilation unit at index \p CU.
    LLVM_ABI uint64_t getCUOffset(uint32_t CU) const;
    /// Number of compilation units covered by this name index.
    ///
    /// \return Number of compilation units in this name index.
    uint32_t getCUCount() const { return Hdr.CompUnitCount; }

    /// Reads offset of local type unit TU, TU is 0-based.
    ///
    /// \param TU Zero-based local type unit index.
    /// \return Section offset of the local type unit at index \p TU.
    LLVM_ABI uint64_t getLocalTUOffset(uint32_t TU) const;
    /// Number of local type units covered by this name index.
    ///
    /// \return Number of local type units in this name index.
    uint32_t getLocalTUCount() const { return Hdr.LocalTypeUnitCount; }

    /// Reads signature of foreign type unit TU. TU is 0-based.
    ///
    /// \param TU Zero-based foreign type unit index.
    /// \return Type signature of the foreign type unit at index \p TU.
    LLVM_ABI uint64_t getForeignTUSignature(uint32_t TU) const;
    /// Number of foreign type units covered by this name index.
    ///
    /// \return Number of foreign type units in this name index.
    uint32_t getForeignTUCount() const { return Hdr.ForeignTypeUnitCount; }

    /// Reads the bucket array entry for the given zero-based bucket.
    ///
    /// The returned value is a (1-based) index into the Names, StringOffsets and
    /// EntryOffsets arrays. The input Bucket index is 0-based.
    ///
    /// \param Bucket Zero-based bucket index.
    /// \return One-based index into the name/hash arrays for that bucket.
    LLVM_ABI uint32_t getBucketArrayEntry(uint32_t Bucket) const;
    /// Number of buckets in the optional hash lookup table.
    ///
    /// \return Number of buckets in the hash lookup table.
    uint32_t getBucketCount() const { return Hdr.BucketCount; }

    /// Reads an entry in the Hash Array for the given Index. The input Index
    /// is 1-based.
    ///
    /// \param Index One-based hash array index.
    /// \return Hash value stored at the given one-based index.
    LLVM_ABI uint32_t getHashArrayEntry(uint32_t Index) const;

    /// Reads the name table entry for the given one-based index.
    ///
    /// The Name Table consists of two arrays -- String Offsets and Entry
    /// Offsets. The returned offsets are relative to the starts of respective
    /// sections. Input Index is 1-based.
    ///
    /// \param Index One-based name table index.
    /// \return Name table entry at the given one-based index.
    LLVM_ABI NameTableEntry getNameTableEntry(uint32_t Index) const;

    /// Number of names in this name index.
    ///
    /// \return Number of names in this name index.
    uint32_t getNameCount() const { return Hdr.NameCount; }

    /// Abbreviations describing encodings of entries in this name index.
    ///
    /// \return Set of abbreviations used by entries in this name index.
    const DenseSet<Abbrev, AbbrevMapInfo> &getAbbrevs() const {
      return Abbrevs;
    }

    /// Parse the accelerator entry at \p Offset and advance \p Offset.
    ///
    /// \param Offset Section offset of the entry; advanced past the parsed
    /// entry on success.
    /// \return Parsed accelerator entry, or an error on failure.
    LLVM_ABI Expected<Entry> getEntry(uint64_t *Offset) const;

    /// Returns the Entry at the relative `Offset` from the start of the Entry
    /// pool.
    ///
    /// \param Offset Offset relative to the start of the entry pool.
    /// \return Parsed accelerator entry, or an error on failure.
    Expected<Entry> getEntryAtRelativeOffset(uint64_t Offset) const {
      auto OffsetFromSection = Offset + this->Offsets.EntriesBase;
      return getEntry(&OffsetFromSection);
    }

    /// Look up all entries in this Name Index matching \c Key.
    ///
    /// \param Key Name to search for in this name index.
    /// \return Iterator range over matching accelerator entries.
    LLVM_ABI iterator_range<ValueIterator> equal_range(StringRef Key) const;

    /// Iterator to the first name table entry.
    ///
    /// \return Name iterator positioned at the first name table entry.
    NameIterator begin() const { return NameIterator(this, 1); }
    /// Iterator past the last name table entry.
    ///
    /// \return Name iterator past the last name table entry.
    NameIterator end() const { return NameIterator(this, getNameCount() + 1); }

    /// Parse this name index from the accelerator section.
    ///
    /// \return Success, or an error if this name index could not be parsed.
    LLVM_ABI Error extract();
    /// Section offset of the start of this name index unit.
    ///
    /// \return Section offset of the start of this name index unit.
    uint64_t getUnitOffset() const { return Base; }
    /// Section offset of the unit that follows this name index.
    ///
    /// \return Section offset of the unit that follows this name index.
    uint64_t getNextUnitOffset() const {
      return Base + dwarf::getUnitLengthFieldByteSize(Hdr.Format) +
             Hdr.UnitLength;
    }
    /// Dump this name index to \p W.
    ///
    /// \param W Printer used for dump output.
    LLVM_ABI void dump(ScopedPrinter &W) const;

    friend class DWARFDebugNames;
  };

  /// Iterator over accelerator entries matching a specific name key.
  class ValueIterator {
  public:
    /// Iterator category tag for this value iterator.
    using iterator_category = std::input_iterator_tag;
    /// Accelerator entry type yielded by this iterator.
    using value_type = Entry;
    /// Distance type between value iterators.
    using difference_type = std::ptrdiff_t;
    /// Pointer type for accelerator entries.
    using pointer = value_type *;
    /// Reference type for accelerator entries.
    using reference = value_type &;

  private:
    /// The Name Index we are currently iterating through. The implementation
    /// relies on the fact that this can also be used as an iterator into the
    /// "NameIndices" vector in the Accelerator section.
    const NameIndex *CurrentIndex = nullptr;

    /// Whether this is a local iterator (searches in CurrentIndex only) or not
    /// (searches all name indices).
    bool IsLocal;

    std::optional<Entry> CurrentEntry;
    uint64_t DataOffset = 0; ///< Offset into the section.
    std::string Key;         ///< The Key we are searching for.
    std::optional<uint32_t> Hash; ///< Hash of Key, if it has been computed.

    bool getEntryAtCurrentOffset();
    std::optional<uint64_t> findEntryOffsetInCurrentIndex();
    bool findInCurrentIndex();
    void searchFromStartOfCurrentIndex();
    LLVM_ABI void next();

    /// Set the iterator to the "end" state.
    void setEnd() { *this = ValueIterator(); }

  public:
    /// Creates a begin iterator over all entries matching Key.
    ///
    /// The iterator will run through all Name Indexes in the section in
    /// sequence.
    ///
    /// \param AccelTable Accelerator table whose name indices are searched.
    /// \param Key Name to match while iterating entries.
    LLVM_ABI ValueIterator(const DWARFDebugNames &AccelTable, StringRef Key);

    /// Create a "begin" iterator for looping over all entries in a specific
    /// Name Index. Other indices in the section will not be visited.
    ///
    /// \param NI Name index to search.
    /// \param Key Name to match while iterating entries.
    LLVM_ABI ValueIterator(const NameIndex &NI, StringRef Key);

    /// Construct an end-of-range sentinel value iterator.
    ValueIterator() = default;

    /// Return the current matching accelerator entry.
    ///
    /// \return Reference to the current matching accelerator entry.
    const Entry &operator*() const { return *CurrentEntry; }
    /// Advance to the next matching accelerator entry.
    ///
    /// \return Reference to this iterator after advancing.
    ValueIterator &operator++() {
      next();
      return *this;
    }
    /// Advance to the next matching entry, returning the previous position.
    ///
    /// \param Unused Unused postfix-discriminator parameter.
    /// \return Copy of the iterator before advancing.
    ValueIterator operator++(int Unused) {
      ValueIterator I = *this;
      next();
      return I;
    }

    /// Compare two value iterators for equality.
    ///
    /// \param A First iterator to compare.
    /// \param B Second iterator to compare.
    /// \return True if both iterators refer to the same index and offset.
    friend bool operator==(const ValueIterator &A, const ValueIterator &B) {
      return A.CurrentIndex == B.CurrentIndex && A.DataOffset == B.DataOffset;
    }
    /// Compare two value iterators for inequality.
    ///
    /// \param A First iterator to compare.
    /// \param B Second iterator to compare.
    /// \return True if the iterators refer to different positions.
    friend bool operator!=(const ValueIterator &A, const ValueIterator &B) {
      return !(A == B);
    }
  };

  /// Iterator over NameTableEntry values in a single Name Index.
  class NameIterator {

    /// The Name Index we are iterating through.
    const NameIndex *CurrentIndex;

    /// The current name in the Name Index.
    uint32_t CurrentName;

    void next() {
      assert(CurrentName <= CurrentIndex->getNameCount());
      ++CurrentName;
    }

  public:
    /// Size type used for random-access indexing.
    using size_type = size_t;
    /// Iterator category tag for this name iterator.
    using iterator_category = std::input_iterator_tag;
    /// Name table entry type yielded by this iterator.
    using value_type = NameTableEntry;
    /// Distance type between name iterators.
    using difference_type = uint32_t;
    /// Pointer type for name table entries.
    using pointer = NameTableEntry *;
    /// Reference type for name table entries (returned by value).
    using reference = NameTableEntry; // We return entries by value.

    /// Creates an iterator whose initial position is name CurrentName in
    /// CurrentIndex.
    ///
    /// \param CurrentIndex Name index being iterated.
    /// \param CurrentName One-based name table index for the starting position.
    NameIterator(const NameIndex *CurrentIndex, uint32_t CurrentName)
        : CurrentIndex(CurrentIndex), CurrentName(CurrentName) {}

    /// Return the name table entry at the current position.
    ///
    /// \return Name table entry at the current iterator position.
    NameTableEntry operator*() const {
      return CurrentIndex->getNameTableEntry(CurrentName);
    }
    /// Advance to the next name table entry.
    ///
    /// \return Reference to this iterator after advancing.
    NameIterator &operator++() {
      next();
      return *this;
    }
    /// Advance to the next name table entry, returning the previous position.
    ///
    /// \param Unused Unused postfix-discriminator parameter.
    /// \return Copy of the iterator before advancing.
    NameIterator operator++(int Unused) {
      NameIterator I = *this;
      next();
      return I;
    }
    /// Accesses entry at specific index (1-based internally, 0-based
    /// externally). For example how this is used in parallelForEach.
    ///
    /// \param idx Zero-based index into the name table.
    /// \return Name table entry at the given zero-based index.
    reference operator[](size_type idx) {
      return CurrentIndex->getNameTableEntry(idx + 1);
    }
    /// Computes difference between iterators (used in parallelForEach).
    ///
    /// \param other Iterator to subtract from this one.
    /// \return Distance in name-table positions between \p other and this.
    difference_type operator-(const NameIterator &other) const {
      assert(CurrentIndex == other.CurrentIndex);
      return this->CurrentName - other.CurrentName;
    }

    /// Compare two name iterators for equality.
    ///
    /// \param A First iterator to compare.
    /// \param B Second iterator to compare.
    /// \return True if both iterators refer to the same name index position.
    friend bool operator==(const NameIterator &A, const NameIterator &B) {
      return A.CurrentIndex == B.CurrentIndex && A.CurrentName == B.CurrentName;
    }
    /// Compare two name iterators for inequality.
    ///
    /// \param A First iterator to compare.
    /// \param B Second iterator to compare.
    /// \return True if the iterators refer to different positions.
    friend bool operator!=(const NameIterator &A, const NameIterator &B) {
      return !(A == B);
    }
  };

private:
  SmallVector<NameIndex, 0> NameIndices;
  DenseMap<uint64_t, const NameIndex *> UnitOffsetToNameIndex;

public:
  /// Construct a .debug_names parser over the given accelerator and string
  /// sections.
  ///
  /// \param AccelSection Accelerator table section contents.
  /// \param StringSection String table section referenced by the accelerator
  /// table.
  DWARFDebugNames(const DWARFDataExtractor &AccelSection,
                  DataExtractor StringSection)
      : DWARFAcceleratorTable(AccelSection, StringSection) {}

  /// Parse the .debug_names section into name indices.
  ///
  /// \return Success, or an error if the section could not be parsed.
  Error extract() override;
  /// Dump all name indices in this section to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  void dump(raw_ostream &OS) const override;

  /// Look up all entries in the accelerator table matching \c Key.
  ///
  /// \param Key Name to search for across all name indices.
  /// \return Iterator range over all matching accelerator entries.
  iterator_range<ValueIterator> equal_range(StringRef Key) const;

  /// Const iterator over name indices in this section.
  using const_iterator = SmallVector<NameIndex, 0>::const_iterator;
  /// Iterator to the first name index.
  ///
  /// \return Const iterator to the first name index.
  const_iterator begin() const { return NameIndices.begin(); }
  /// Iterator past the last name index.
  ///
  /// \return Const iterator past the last name index.
  const_iterator end() const { return NameIndices.end(); }

  /// Return the Name Index covering the compile unit or local type unit at
  /// UnitOffset, or nullptr if there is no Name Index covering that unit.
  ///
  /// \param UnitOffset Section offset of the compile or local type unit.
  /// \return Pointer to the covering Name Index, or nullptr if none covers
  /// that unit.
  const NameIndex *getCUOrTUNameIndex(uint64_t UnitOffset);
};

namespace dwarf {
/// Calculates the starting offsets for tables within a .debug_names unit.
///
/// \param EndOfHeaderOffset Offset immediately after the name index header.
/// \param Hdr Parsed name index header used to size the following tables.
/// \return Offsets of the CU list, buckets, hashes, strings, and entry pool.
LLVM_ABI DWARFDebugNames::DWARFDebugNamesOffsets
findDebugNamesOffsets(uint64_t EndOfHeaderOffset,
                      const DWARFDebugNames::Header &Hdr);
}

/// Strips template parameters from a templated function name.
///
/// If \p Name is the name of a templated function that includes template
/// parameters, returns a substring of \p Name containing no template
/// parameters.
/// E.g.: StripTemplateParameters("foo<int>") = "foo".
///
/// \param Name Possibly templated function name to strip.
/// \return Name without template parameters, or std::nullopt if none apply.
LLVM_ABI std::optional<StringRef> StripTemplateParameters(StringRef Name);

/// Components of an Objective-C method selector name.
struct ObjCSelectorNames {
  /// For "-[A(Category) method:]", this would be "method:"
  StringRef Selector;
  /// For "-[A(Category) method:]", this would be "A(category)"
  StringRef ClassName;
  /// For "-[A(Category) method:]", this would be "A"
  std::optional<StringRef> ClassNameNoCategory;
  /// For "-[A(Category) method:]", this would be "A method:"
  std::optional<std::string> MethodNameNoCategory;
};

/// Parses an Objective-C selector name into its component parts.
///
/// If \p Name is the AT_name of a DIE which refers to an Objective-C selector,
/// returns an instance of ObjCSelectorNames. The Selector and ClassName fields
/// are guaranteed to be non-empty in the result.
///
/// \param Name DIE AT_name to parse as an Objective-C selector.
/// \return Parsed selector components, or std::nullopt if \p Name is not a
/// selector.
LLVM_ABI std::optional<ObjCSelectorNames>
getObjCNamesIfSelector(StringRef Name);

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFACCELERATORTABLE_H
