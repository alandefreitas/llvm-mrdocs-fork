//===- GOFFObjectFile.h - GOFF object file implementation -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the GOFFObjectFile class.
// Record classes and derivatives are also declared and implemented.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_GOFFOBJECTFILE_H
#define LLVM_OBJECT_GOFFOBJECTFILE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/IndexedMap.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertEBCDIC.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {

namespace object {

/// ObjectFile implementation for GOFF (Generalized Object File Format) binaries.
class LLVM_ABI GOFFObjectFile : public ObjectFile {
  friend class GOFFSymbolRef;

  IndexedMap<const uint8_t *> EsdPtrs; // Indexed by EsdId.
  SmallVector<const uint8_t *, 256> TextPtrs;

  mutable DenseMap<uint32_t, std::pair<size_t, std::unique_ptr<char[]>>>
      EsdNamesCache;

  typedef DataRefImpl SectionEntryImpl;
  // (EDID, 0)               code, r/o data section
  // (EDID,PRID)             r/w data section
  SmallVector<SectionEntryImpl, 256> SectionList;
  mutable DenseMap<uint32_t, SmallVector<uint8_t>> SectionDataCache;

  // Flattened data for all logical records (record type + continuous data
  // without headers).
  SmallVector<std::pair<GOFF::RecordType, SmallVector<uint8_t>>> FlattenedData;

  // Populate FlattenedData with all logical records.
  Error createFlattenedData();

  // Helper to get continuous data from a logical record (includes prefix +
  // continuations) Returns the number of physical records consumed (including
  // the initial record)
  Expected<unsigned> getContinuousData(SmallVectorImpl<uint8_t> &CompleteData,
                                       int DataIndex, uint16_t DataLength,
                                       const uint8_t *Record) const;

public:
  /// Flattened logical records (record type plus continuous payload bytes).
  ///
  /// \return Flattened logical records for this object file.
  const SmallVector<std::pair<GOFF::RecordType, SmallVector<uint8_t>>> &
  getFlattenedData() const {
    return FlattenedData;
  }

  /// Return the name of symbol \p Symbol.
  ///
  /// \param Symbol Symbol whose name is requested.
  /// \return The name of \p Symbol, or an error on failure.
  Expected<StringRef> getSymbolName(SymbolRef Symbol) const;

  /// Parse a GOFF object from \p Object, reporting failures via \p Err.
  ///
  /// \param Object Memory buffer holding the GOFF binary.
  /// \param Err Set on parse failure; must be checked by the caller.
  GOFFObjectFile(MemoryBufferRef Object, Error &Err);
  /// True if \p V is a GOFFObjectFile.
  ///
  /// \param V Binary to test.
  /// \return True if \p V is a GOFFObjectFile.
  static inline bool classof(const Binary *V) { return V->isGOFF(); }
  /// Iterator to the first section in this object.
  ///
  /// \return Iterator to the first section.
  section_iterator section_begin() const override;
  /// Past-the-end iterator for sections in this object.
  ///
  /// \return Iterator one past the last section.
  section_iterator section_end() const override;

  /// Number of bytes used to represent an address in this format.
  ///
  /// \return Number of bytes used to represent an address.
  uint8_t getBytesInAddress() const override { return 8; }

  /// Human-readable name of this object file format.
  ///
  /// \return Human-readable name of this object file format.
  StringRef getFileFormatName() const override { return "GOFF-SystemZ"; }

  /// Target architecture of this object file.
  ///
  /// \return Target architecture of this object file.
  Triple::ArchType getArch() const override { return Triple::systemz; }

  /// Subtarget features described by this object file.
  ///
  /// \return Subtarget features described by this object file.
  Expected<SubtargetFeatures> getFeatures() const override { return SubtargetFeatures(); }

  /// True if this is a relocatable object.
  ///
  /// \return True if this is a relocatable object.
  bool isRelocatableObject() const override { return true; }

  /// Advance \p Symb to the next symbol in this object.
  ///
  /// \param Symb Opaque symbol handle to advance.
  void moveSymbolNext(DataRefImpl &Symb) const override;
  /// Iterator to the first symbol in this object.
  ///
  /// \return Iterator to the first symbol.
  basic_symbol_iterator symbol_begin() const override;
  /// Past-the-end iterator for symbols in this object.
  ///
  /// \return Iterator one past the last symbol.
  basic_symbol_iterator symbol_end() const override;

  /// Always true; GOFF objects use 64-bit addresses.
  ///
  /// \return Always true.
  bool is64Bit() const override {
    return true;
  }

  /// True if section \p Sec has the GOFF NoLoad loading behavior.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if section \p Sec has the GOFF NoLoad loading behavior.
  bool isSectionNoLoad(DataRefImpl Sec) const;
  /// True if section \p Sec is read-only data with Initial loading behavior.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if section \p Sec is read-only data with Initial loading
  /// behavior.
  bool isSectionReadOnlyData(DataRefImpl Sec) const;
  /// Always false; GOFF fill characters are applied in getSectionContents().
  ///
  /// \param Sec Opaque section handle.
  /// \return Always false.
  bool isSectionZeroInit(DataRefImpl Sec) const;

private:
  // SymbolRef.
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;
  uint64_t getSymbolSize(DataRefImpl Symb) const;

  const uint8_t *getSymbolEsdRecord(DataRefImpl Symb) const;
  bool isSymbolUnresolved(DataRefImpl Symb) const;
  bool isSymbolIndirect(DataRefImpl Symb) const;

  // SectionRef.
  void moveSectionNext(DataRefImpl &Sec) const override;
  Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;
  uint64_t getSectionIndex(DataRefImpl Sec) const override { return Sec.d.a; }
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  bool isSectionCompressed(DataRefImpl Sec) const override { return false; }
  bool isSectionText(DataRefImpl Sec) const override;
  bool isSectionData(DataRefImpl Sec) const override;
  bool isSectionBSS(DataRefImpl Sec) const override { return false; }
  bool isSectionVirtual(DataRefImpl Sec) const override { return false; }
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override {
    return relocation_iterator(RelocationRef(Sec, this));
  }
  relocation_iterator section_rel_end(DataRefImpl Sec) const override {
    return relocation_iterator(RelocationRef(Sec, this));
  }

  const uint8_t *getSectionEdEsdRecord(DataRefImpl &Sec) const;
  const uint8_t *getSectionPrEsdRecord(DataRefImpl &Sec) const;
  const uint8_t *getSectionEdEsdRecord(uint32_t SectionIndex) const;
  const uint8_t *getSectionPrEsdRecord(uint32_t SectionIndex) const;
  uint32_t getSectionDefEsdId(DataRefImpl &Sec) const;

  // RelocationRef.
  void moveRelocationNext(DataRefImpl &Rel) const override {}
  uint64_t getRelocationOffset(DataRefImpl Rel) const override { return 0; }
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override {
    DataRefImpl Temp;
    return basic_symbol_iterator(SymbolRef(Temp, this));
  }
  uint64_t getRelocationType(DataRefImpl Rel) const override { return 0; }
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override {}
};

/// SymbolRef specialized for symbols in a GOFFObjectFile.
class GOFFSymbolRef : public SymbolRef {
public:
  /// Construct a GOFF symbol reference from SymbolRef \p B.
  ///
  /// \param B Symbol reference that must refer to a GOFFObjectFile.
  GOFFSymbolRef(const SymbolRef &B) : SymbolRef(B) {
    assert(isa<GOFFObjectFile>(SymbolRef::getObject()));
  }

  /// Returns the GOFFObjectFile that owns this symbol.
  ///
  /// \return The GOFFObjectFile that owns this symbol.
  const GOFFObjectFile *getObject() const {
    return cast<GOFFObjectFile>(BasicSymbolRef::getObject());
  }

  /// Symbol flags for this GOFF symbol.
  ///
  /// \return Symbol flags for this GOFF symbol, or an error on failure.
  Expected<uint32_t> getSymbolGOFFFlags() const {
    return getObject()->getSymbolFlags(getRawDataRefImpl());
  }

  /// High-level SymbolRef::Type for this GOFF symbol.
  ///
  /// \return High-level SymbolRef::Type for this GOFF symbol, or an error on
  /// failure.
  Expected<SymbolRef::Type> getSymbolGOFFType() const {
    return getObject()->getSymbolType(getRawDataRefImpl());
  }

  /// Return the size associated with this symbol.
  ///
  /// \return Size associated with this symbol.
  uint64_t getSize() const {
    return getObject()->getSymbolSize(getRawDataRefImpl());
  }
};

} // namespace object

} // namespace llvm

#endif
