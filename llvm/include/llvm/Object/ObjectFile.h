//===- ObjectFile.h - File format independent object file -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares a file format independent ObjectFile class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_OBJECTFILE_H
#define LLVM_OBJECT_OBJECTFILE_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/Magic.h"
#include "llvm/BinaryFormat/Swift.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/SymbolicFile.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBufferRef.h"
#include "llvm/TargetParser/Triple.h"
#include <cassert>
#include <cstdint>
#include <memory>

namespace llvm {

/// Describes CPU/feature bits selected for a target (defined in TargetParser).
class SubtargetFeatures;

namespace object {

/// COFF object file.
class COFFObjectFile;
/// Mach-O object file.
class MachOObjectFile;
/// Format-independent object file base class.
class ObjectFile;
/// Reference to a single section in an ObjectFile.
class SectionRef;
/// Reference to a single symbol in an ObjectFile.
class SymbolRef;
/// Iterator over SymbolRef symbols in an ObjectFile.
class symbol_iterator;
/// WebAssembly object file.
class WasmObjectFile;
/// DirectX container object file.
class DXContainerObjectFile;

/// Content iterator specialized for SectionRef.
using section_iterator = content_iterator<SectionRef>;

/// Predicate that selects which sections a SectionFilter should yield.
typedef std::function<bool(const SectionRef &)> SectionFilterPredicate;
/// This is a value type class that represents a single relocation in the list
/// of relocations in the object file.
class RelocationRef {
  DataRefImpl RelocationPimpl;
  const ObjectFile *OwningObject = nullptr;

public:
  /// Default-construct an empty relocation reference.
  RelocationRef() = default;
  /// Construct a RelocationRef for \p RelocationP in \p Owner.
  ///
  /// \param RelocationP Opaque format-specific relocation handle.
  /// \param Owner Object file that owns the relocation.
  RelocationRef(DataRefImpl RelocationP, const ObjectFile *Owner);

  /// True if this relocation refers to the same entry as \p Other.
  ///
  /// \param Other Relocation to compare against.
  /// \return True if the relocations refer to the same entry.
  bool operator==(const RelocationRef &Other) const;

  /// Advance to the next relocation in the owning section.
  void moveNext();

  /// Byte offset of this relocation within its section.
  ///
  /// \return The byte offset of this relocation within its section.
  uint64_t getOffset() const;
  /// Symbol this relocation refers to, if any.
  ///
  /// \return An iterator to the referenced symbol.
  symbol_iterator getSymbol() const;
  /// Relocation type encoding specific to the object format.
  ///
  /// \return The format-specific relocation type encoding.
  uint64_t getType() const;

  /// Get a string that represents the type of this relocation.
  ///
  /// This is for display purposes only.
  ///
  /// \param Result Buffer that receives the type name.
  void getTypeName(SmallVectorImpl<char> &Result) const;

  /// Opaque format-specific handle for this relocation.
  ///
  /// \return The opaque DataRefImpl for this relocation.
  DataRefImpl getRawDataRefImpl() const;
  /// Returns the ObjectFile that owns this relocation.
  ///
  /// \return Pointer to the owning ObjectFile.
  const ObjectFile *getObject() const;
};

/// Iterator over RelocationRef entries in a section.
using relocation_iterator = content_iterator<RelocationRef>;

/// This is a value type class that represents a single section in the list of
/// sections in the object file.
class SectionRef {
  friend class SymbolRef;

  DataRefImpl SectionPimpl;
  const ObjectFile *OwningObject = nullptr;

public:
  /// Default-construct an empty section reference.
  SectionRef() = default;
  /// Construct a SectionRef for \p SectionP in \p Owner.
  ///
  /// \param SectionP Opaque format-specific section handle.
  /// \param Owner Object file that owns the section.
  SectionRef(DataRefImpl SectionP, const ObjectFile *Owner);

  /// True if this section refers to the same entry as \p Other.
  ///
  /// \param Other Section to compare against.
  /// \return True if the sections refer to the same entry.
  bool operator==(const SectionRef &Other) const;
  /// True if this section refers to a different entry than \p Other.
  ///
  /// \param Other Section to compare against.
  /// \return True if the sections refer to different entries.
  bool operator!=(const SectionRef &Other) const;
  /// Order sections by their opaque DataRefImpl for use in ordered containers.
  ///
  /// \param Other Section to compare against.
  /// \return True if this section orders before \p Other.
  bool operator<(const SectionRef &Other) const;

  /// Advance to the next section in the owning object file.
  void moveNext();

  /// Section name, or an error if unavailable.
  ///
  /// \return The section name, or an error if unavailable.
  Expected<StringRef> getName() const;
  /// Virtual address at which this section is loaded.
  ///
  /// \return The virtual load address of this section.
  uint64_t getAddress() const;
  /// Index of this section within the object file.
  ///
  /// \return The section index within the object file.
  uint64_t getIndex() const;
  /// Size of this section in bytes.
  ///
  /// \return The size of this section in bytes.
  uint64_t getSize() const;
  /// Raw contents of this section, or an error if unavailable.
  ///
  /// \return The section contents, or an error if unavailable.
  Expected<StringRef> getContents() const;

  /// Get the alignment of this section.
  ///
  /// \return The alignment of this section.
  Align getAlignment() const;

  /// True if this section's contents are compressed.
  ///
  /// \return True if this section's contents are compressed.
  bool isCompressed() const;
  /// Whether this section contains instructions.
  ///
  /// \return True if this section contains instructions.
  bool isText() const;
  /// Whether this section contains data, not instructions.
  ///
  /// \return True if this section contains data, not instructions.
  bool isData() const;
  /// Whether this section contains BSS uninitialized data.
  ///
  /// \return True if this section contains BSS uninitialized data.
  bool isBSS() const;
  /// True if the section's contents are not present in the object image.
  ///
  /// \return True if the section's contents are not present in the object image.
  bool isVirtual() const;
  /// True if this section holds LLVM IR bitcode.
  ///
  /// \return True if this section holds LLVM IR bitcode.
  bool isBitcode() const;
  /// True if this section's contents were stripped from the object.
  ///
  /// \return True if this section's contents were stripped from the object.
  bool isStripped() const;

  /// True if this section counts as Berkeley text (allocatable code/rodata).
  ///
  /// \return True if this section counts as Berkeley text.
  bool isBerkeleyText() const;
  /// True if this section counts as Berkeley data (allocatable data, not text).
  ///
  /// \return True if this section counts as Berkeley data.
  bool isBerkeleyData() const;

  /// Whether this section is a debug section.
  ///
  /// \return True if this section is a debug section.
  bool isDebugSection() const;

  /// True if this section contains symbol \p S.
  ///
  /// \param S Symbol to test for membership in this section.
  /// \return True if this section contains \p S.
  LLVM_ABI bool containsSymbol(SymbolRef S) const;

  /// Iterator to the first relocation in this section.
  ///
  /// \return An iterator to the first relocation in this section.
  relocation_iterator relocation_begin() const;
  /// Past-the-end iterator for relocations in this section.
  ///
  /// \return A past-the-end iterator for relocations in this section.
  relocation_iterator relocation_end() const;
  /// Range of relocations in this section.
  ///
  /// \return An iterator range over relocations in this section.
  iterator_range<relocation_iterator> relocations() const {
    return make_range(relocation_begin(), relocation_end());
  }

  /// Returns the related section if this section contains relocations. The
  /// returned section may or may not have applied its relocations.
  ///
  /// \return An iterator to the relocated section, or an error if none.
  Expected<section_iterator> getRelocatedSection() const;

  /// Opaque format-specific handle for this section.
  ///
  /// \return The opaque DataRefImpl for this section.
  DataRefImpl getRawDataRefImpl() const;
  /// Returns the ObjectFile that owns this section.
  ///
  /// \return Pointer to the owning ObjectFile.
  const ObjectFile *getObject() const;
};

/// An address paired with the section that contains it.
struct SectionedAddress {
  /// Sentinel section index meaning no section is associated.
  const static uint64_t UndefSection = UINT64_MAX;

  /// Address within the section (or absolute when SectionIndex is UndefSection).
  uint64_t Address = 0;
  /// Section index for Address, or UndefSection for an absolute address.
  uint64_t SectionIndex = UndefSection;
};

/// Order by section index, then by address within the section.
///
/// \param LHS Left-hand sectioned address.
/// \param RHS Right-hand sectioned address.
/// \return True if \p LHS orders before \p RHS.
inline bool operator<(const SectionedAddress &LHS,
                      const SectionedAddress &RHS) {
  return std::tie(LHS.SectionIndex, LHS.Address) <
         std::tie(RHS.SectionIndex, RHS.Address);
}

/// True if both sectioned addresses have the same section index and address.
///
/// \param LHS Left-hand sectioned address.
/// \param RHS Right-hand sectioned address.
/// \return True if \p LHS and \p RHS have the same section index and address.
inline bool operator==(const SectionedAddress &LHS,
                       const SectionedAddress &RHS) {
  return std::tie(LHS.SectionIndex, LHS.Address) ==
         std::tie(RHS.SectionIndex, RHS.Address);
}

/// Print \p Addr to \p OS as a human-readable sectioned address.
///
/// \param OS Stream to write to.
/// \param Addr Sectioned address to print.
/// \return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const SectionedAddress &Addr);

/// This is a value type class that represents a single symbol in the list of
/// symbols in the object file.
class SymbolRef : public BasicSymbolRef {
  friend class SectionRef;

public:
  /// High-level classification of a symbol's kind.
  enum Type {
    ST_Unknown, ///< Type not specified.
    ST_Other,   ///< Other or unspecified symbol kind.
    ST_Data,    ///< Data symbol.
    ST_Debug,   ///< Debug symbol.
    ST_File,    ///< File symbol.
    ST_Function ///< Function symbol.
  };

  /// Default-construct an empty symbol reference.
  SymbolRef() = default;
  /// Construct a SymbolRef for \p SymbolP in \p Owner.
  ///
  /// \param SymbolP Opaque format-specific symbol handle.
  /// \param Owner Object file that owns the symbol.
  SymbolRef(DataRefImpl SymbolP, const ObjectFile *Owner);
  /// Construct a SymbolRef from a BasicSymbolRef owned by an ObjectFile.
  ///
  /// \param B Basic symbol reference to wrap; its owner must be an ObjectFile.
  SymbolRef(const BasicSymbolRef &B) : BasicSymbolRef(B) {
    assert(isa<ObjectFile>(BasicSymbolRef::getObject()));
  }

  /// Returns the symbol's name.
  ///
  /// \return The symbol's name, or an error if unavailable.
  Expected<StringRef> getName() const;
  /// Returns the symbol virtual address (i.e. address at which it will be
  /// mapped).
  ///
  /// \return The symbol's virtual address, or an error if unavailable.
  Expected<uint64_t> getAddress() const;

  /// Return the value of the symbol depending on the object this can be an
  /// offset or a virtual address.
  ///
  /// \return The symbol value (offset or virtual address), or an error.
  Expected<uint64_t> getValue() const;

  /// Get the alignment of this symbol as the actual value (not log 2).
  ///
  /// \return The symbol alignment as the actual value (not log 2).
  uint32_t getAlignment() const;
  /// Size of a common symbol, or 0 if this is not a common symbol.
  ///
  /// \return The common symbol size, or 0 if this is not a common symbol.
  uint64_t getCommonSize() const;
  /// High-level symbol kind (data, function, file, etc.), or an error.
  ///
  /// \return The high-level symbol kind, or an error if unavailable.
  Expected<SymbolRef::Type> getType() const;

  /// Get section this symbol is defined in reference to. Result is
  /// section_end() if it is undefined or is an absolute symbol.
  ///
  /// \return An iterator to the defining section, or section_end() /
  ///         an error for undefined or absolute symbols.
  Expected<section_iterator> getSection() const;

  /// Returns the ObjectFile that owns this symbol.
  ///
  /// \return Pointer to the owning ObjectFile.
  const ObjectFile *getObject() const;
};

/// Iterator over SymbolRef symbols in an ObjectFile.
class symbol_iterator : public basic_symbol_iterator {
public:
  /// Construct from a SymbolRef.
  ///
  /// \param Sym Symbol to wrap as an iterator position.
  symbol_iterator(SymbolRef Sym) : basic_symbol_iterator(Sym) {}
  /// Construct from a basic_symbol_iterator owned by an ObjectFile.
  ///
  /// \param B Basic symbol iterator to promote to a SymbolRef iterator.
  symbol_iterator(const basic_symbol_iterator &B)
      : basic_symbol_iterator(SymbolRef(B->getRawDataRefImpl(),
                                        cast<ObjectFile>(B->getObject()))) {}

  /// Access the current SymbolRef.
  ///
  /// \return A pointer to the current SymbolRef.
  const SymbolRef *operator->() const {
    const BasicSymbolRef &P = basic_symbol_iterator::operator *();
    return static_cast<const SymbolRef*>(&P);
  }

  /// Dereference to the current SymbolRef.
  ///
  /// \return A reference to the current SymbolRef.
  const SymbolRef &operator*() const {
    const BasicSymbolRef &P = basic_symbol_iterator::operator *();
    return static_cast<const SymbolRef&>(P);
  }
};

/// This class is the base class for all object file types. Concrete instances
/// of this object are created by createObjectFile, which figures out which type
/// to create.
class LLVM_ABI ObjectFile : public SymbolicFile {
  virtual void anchor();

protected:
  /// Construct an ObjectFile of \p Type backed by \p Source.
  ///
  /// \param Type Binary type identifier for this object format.
  /// \param Source Memory buffer holding the object file contents.
  ObjectFile(unsigned int Type, MemoryBufferRef Source);

  /// Pointer to the start of this object's memory-mapped contents.
  ///
  /// \return A pointer to the start of the memory-mapped contents.
  const uint8_t *base() const {
    return reinterpret_cast<const uint8_t *>(Data.getBufferStart());
  }

  // These functions are for SymbolRef to call internally. The main goal of
  // this is to allow SymbolRef::SymbolPimpl to point directly to the symbol
  // entry in the memory mapped object file. SymbolPimpl cannot contain any
  // virtual functions because then it could not point into the memory mapped
  // file.
  //
  // Implementations assume that the DataRefImpl is valid and has not been
  // modified externally. It's UB otherwise.
  friend class SymbolRef;

  /// Name of symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The symbol name, or an error if unavailable.
  virtual Expected<StringRef> getSymbolName(DataRefImpl Symb) const = 0;
  /// Print the name of symbol \p Symb to \p OS.
  ///
  /// \param OS Stream to write the symbol name to.
  /// \param Symb Opaque symbol handle.
  /// \return Error::success() on success, or an error if printing fails.
  Error printSymbolName(raw_ostream &OS,
                                  DataRefImpl Symb) const override;
  /// Virtual address of symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The symbol's virtual address, or an error if unavailable.
  virtual Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const = 0;
  /// Format-specific symbol value (address or offset) for \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The format-specific symbol value (address or offset).
  virtual uint64_t getSymbolValueImpl(DataRefImpl Symb) const = 0;
  /// Alignment of symbol \p Symb as the actual value (not log 2).
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The symbol alignment as the actual value (not log 2).
  virtual uint32_t getSymbolAlignment(DataRefImpl Symb) const;
  /// Size of common symbol \p Symb (format-specific implementation).
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The size of the common symbol in bytes.
  virtual uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const = 0;
  /// Classify symbol \p Symb (data, function, file, etc.).
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The high-level symbol kind, or an error if unavailable.
  virtual Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const = 0;
  /// Section defining symbol \p Symb, or section_end() if undefined/absolute.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return An iterator to the defining section, or section_end() /
  ///         an error for undefined or absolute symbols.
  virtual Expected<section_iterator>
  getSymbolSection(DataRefImpl Symb) const = 0;

  // Same as above for SectionRef.
  friend class SectionRef;

  /// Advance \p Sec to the next section in this object.
  ///
  /// \param Sec Opaque section handle to advance.
  virtual void moveSectionNext(DataRefImpl &Sec) const = 0;
  /// Name of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return The section name, or an error if unavailable.
  virtual Expected<StringRef> getSectionName(DataRefImpl Sec) const = 0;
  /// Virtual load address of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return The virtual load address of the section.
  virtual uint64_t getSectionAddress(DataRefImpl Sec) const = 0;
  /// Index of section \p Sec within this object.
  ///
  /// \param Sec Opaque section handle.
  /// \return The index of the section within this object.
  virtual uint64_t getSectionIndex(DataRefImpl Sec) const = 0;
  /// Size in bytes of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return The size of the section in bytes.
  virtual uint64_t getSectionSize(DataRefImpl Sec) const = 0;
  /// Raw contents of section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return The section contents, or an error if unavailable.
  virtual Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const = 0;
  /// Alignment of section \p Sec in bytes.
  ///
  /// \param Sec Opaque section handle.
  /// \return The section alignment in bytes.
  virtual uint64_t getSectionAlignment(DataRefImpl Sec) const = 0;
  /// True if section \p Sec is compressed.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section is compressed.
  virtual bool isSectionCompressed(DataRefImpl Sec) const = 0;
  /// True if section \p Sec contains executable code.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section contains executable code.
  virtual bool isSectionText(DataRefImpl Sec) const = 0;
  /// True if the section contains initialized data (not text).
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section contains initialized data (not text).
  virtual bool isSectionData(DataRefImpl Sec) const = 0;
  /// True if section \p Sec is BSS (zero-initialized, no file contents).
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section is BSS.
  virtual bool isSectionBSS(DataRefImpl Sec) const = 0;
  /// True if section \p Sec's contents are absent from the object image.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section's contents are absent from the object image.
  virtual bool isSectionVirtual(DataRefImpl Sec) const = 0;
  /// True if section \p Sec holds LLVM IR bitcode.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section holds LLVM IR bitcode.
  virtual bool isSectionBitcode(DataRefImpl Sec) const;
  /// True if section \p Sec's contents were stripped.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section's contents were stripped.
  virtual bool isSectionStripped(DataRefImpl Sec) const;
  /// True if section \p Sec counts as Berkeley text (allocatable code/rodata).
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section counts as Berkeley text.
  virtual bool isBerkeleyText(DataRefImpl Sec) const;
  /// True if section \p Sec counts as Berkeley data (allocatable data, not text).
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section counts as Berkeley data.
  virtual bool isBerkeleyData(DataRefImpl Sec) const;
  /// True if section \p Sec is a debug info section.
  ///
  /// \param Sec Opaque section handle.
  /// \return True if the section is a debug info section.
  virtual bool isDebugSection(DataRefImpl Sec) const;
  /// Iterator to the first relocation in section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return An iterator to the first relocation in the section.
  virtual relocation_iterator section_rel_begin(DataRefImpl Sec) const = 0;
  /// Past-the-end iterator for relocations in section \p Sec.
  ///
  /// \param Sec Opaque section handle.
  /// \return A past-the-end iterator for relocations in the section.
  virtual relocation_iterator section_rel_end(DataRefImpl Sec) const = 0;
  /// Section that relocations in \p Sec apply to, if any.
  ///
  /// \param Sec Opaque section handle.
  /// \return An iterator to the relocated section, or an error if none.
  virtual Expected<section_iterator> getRelocatedSection(DataRefImpl Sec) const;

  // Same as above for RelocationRef.
  friend class RelocationRef;
  /// Advance \p Rel to the next relocation.
  ///
  /// \param Rel Opaque relocation handle to advance.
  virtual void moveRelocationNext(DataRefImpl &Rel) const = 0;
  /// Byte offset of relocation \p Rel within its section.
  ///
  /// \param Rel Opaque relocation handle.
  /// \return The byte offset of the relocation within its section.
  virtual uint64_t getRelocationOffset(DataRefImpl Rel) const = 0;
  /// Symbol referenced by relocation \p Rel.
  ///
  /// \param Rel Opaque relocation handle.
  /// \return An iterator to the symbol referenced by the relocation.
  virtual symbol_iterator getRelocationSymbol(DataRefImpl Rel) const = 0;
  /// Format-specific type encoding of relocation \p Rel.
  ///
  /// \param Rel Opaque relocation handle.
  /// \return The format-specific relocation type encoding.
  virtual uint64_t getRelocationType(DataRefImpl Rel) const = 0;
  /// Append a display name for relocation \p Rel's type to \p Result.
  ///
  /// \param Rel Opaque relocation handle.
  /// \param Result Buffer that receives the type name.
  virtual void getRelocationTypeName(DataRefImpl Rel,
                                     SmallVectorImpl<char> &Result) const = 0;

  /// Map a Swift reflection section name to its reflection section kind.
  ///
  /// \param SectionName Reflection section name to map.
  /// \return The matching Swift reflection section kind, or unknown.
  virtual llvm::binaryformat::Swift5ReflectionSectionKind
  mapReflectionSectionNameToEnumValue(StringRef SectionName) const {
    return llvm::binaryformat::Swift5ReflectionSectionKind::unknown;
  };

  /// Return the format-specific value of symbol \p Symb.
  ///
  /// \param Symb Opaque symbol handle.
  /// \return The format-specific symbol value, or an error if unavailable.
  Expected<uint64_t> getSymbolValue(DataRefImpl Symb) const;

public:
  /// Deleted; ObjectFile requires a type and buffer.
  ObjectFile() = delete;
  /// Deleted copy constructor.
  ///
  /// \param other Unused; copying is not allowed.
  ObjectFile(const ObjectFile &other) = delete;
  /// ObjectFile is not copy-assignable.
  ///
  /// \param other Unused; assignment is not allowed.
  ObjectFile &operator=(const ObjectFile &other) = delete;

  /// Size of common symbol \p Symb in bytes.
  ///
  /// \param Symb Opaque symbol handle; must have the SF_Common flag.
  /// \return The size of the common symbol in bytes.
  uint64_t getCommonSymbolSize(DataRefImpl Symb) const {
    Expected<uint32_t> SymbolFlagsOrErr = getSymbolFlags(Symb);
    if (!SymbolFlagsOrErr)
      // TODO: Actually report errors helpfully.
      report_fatal_error(SymbolFlagsOrErr.takeError());
    assert(*SymbolFlagsOrErr & SymbolRef::SF_Common);
    return getCommonSymbolSizeImpl(Symb);
  }

  /// Sections that hold dynamic relocations (empty by default).
  ///
  /// \return A vector of sections that hold dynamic relocations.
  virtual std::vector<SectionRef> dynamic_relocation_sections() const {
    return std::vector<SectionRef>();
  }

  /// Iterator range type over SymbolRef symbols in this object.
  using symbol_iterator_range = iterator_range<symbol_iterator>;
  /// Range over all symbols in this object file.
  ///
  /// \return An iterator range over all symbols in this object file.
  symbol_iterator_range symbols() const {
    return symbol_iterator_range(symbol_begin(), symbol_end());
  }

  /// Iterator to the first section in this object.
  ///
  /// \return An iterator to the first section in this object.
  virtual section_iterator section_begin() const = 0;
  /// Past-the-end iterator for sections in this object.
  ///
  /// \return A past-the-end iterator for sections in this object.
  virtual section_iterator section_end() const = 0;

  /// Iterator range over this object's sections.
  using section_iterator_range = iterator_range<section_iterator>;
  /// Range over all sections in this object file.
  ///
  /// \return An iterator range over all sections in this object file.
  section_iterator_range sections() const {
    return section_iterator_range(section_begin(), section_end());
  }

  /// True if this object contains debug information sections.
  ///
  /// \return True if this object contains debug information sections.
  virtual bool hasDebugInfo() const;

  /// The number of bytes used to represent an address in this object
  ///        file format.
  ///
  /// \return The number of bytes used to represent an address.
  virtual uint8_t getBytesInAddress() const = 0;

  /// Human-readable name of this object file format.
  ///
  /// \return The human-readable name of this object file format.
  virtual StringRef getFileFormatName() const = 0;
  /// Target architecture of this object file.
  ///
  /// \return The target architecture of this object file.
  virtual Triple::ArchType getArch() const = 0;
  /// Operating system this object targets, or UnknownOS if unspecified.
  ///
  /// \return The target OS, or UnknownOS if unspecified.
  virtual Triple::OSType getOS() const { return Triple::UnknownOS; }
  /// Subtarget features described by this object file.
  ///
  /// \return The subtarget features, or an error if unavailable.
  virtual Expected<SubtargetFeatures> getFeatures() const = 0;
  /// CPU name encoded in the object, if the format provides one.
  ///
  /// \return The CPU name, or std::nullopt if the format provides none.
  virtual std::optional<StringRef> tryGetCPUName() const {
    return std::nullopt;
  };
  /// Set ARM sub-architecture fields on \p TheTriple from this object.
  ///
  /// \param TheTriple Triple to update with ARM sub-arch information.
  virtual void setARMSubArch(Triple &TheTriple) const { }
  /// Entry point address, or an error if the format has none.
  ///
  /// \return The entry point address, or an error if the format has none.
  virtual Expected<uint64_t> getStartAddress() const {
    return errorCodeToError(object_error::parse_failed);
  };

  /// Create a triple from the data in this object file.
  ///
  /// \return A Triple constructed from this object's architecture and OS.
  Triple makeTriple() const;

  /// Maps a debug section name to a standard DWARF section name.
  ///
  /// \param Name Debug section name to map.
  /// \return The corresponding standard DWARF section name.
  virtual StringRef mapDebugSectionName(StringRef Name) const { return Name; }

  /// True if this is a relocatable object (.o/.obj).
  ///
  /// \return True if this is a relocatable object (.o/.obj).
  virtual bool isRelocatableObject() const = 0;

  /// True if the reflection section can be stripped by the linker.
  ///
  /// \param ReflectionSectionKind Swift reflection section kind to check.
  /// \return True if the reflection section can be stripped by the linker.
  bool isReflectionSectionStrippable(
      llvm::binaryformat::Swift5ReflectionSectionKind ReflectionSectionKind)
      const;

  /// Create an ObjectFile from the file at \p ObjectPath.
  ///
  /// \param ObjectPath Path to the object file. ObjectPath.isObject must
  ///        return true.
  /// \returns Pointer to ObjectFile subclass to handle this type of object.
  static Expected<OwningBinary<ObjectFile>>
  createObjectFile(StringRef ObjectPath);

  /// Create an ObjectFile from \p Object of known \p Type; optionally run initContent.
  ///
  /// \param Object Memory buffer holding the object file.
  /// \param Type Detected or requested file magic / format.
  /// \param InitContent If true, initialize format-specific content eagerly.
  /// \return A unique_ptr to an ObjectFile subclass, or an error on failure.
  static Expected<std::unique_ptr<ObjectFile>>
  createObjectFile(MemoryBufferRef Object, llvm::file_magic Type,
                   bool InitContent = true);
  /// Create an ObjectFile from \p Object, autodetecting the file type.
  ///
  /// \param Object Memory buffer holding the object file.
  /// \return A unique_ptr to an ObjectFile subclass, or an error on failure.
  static Expected<std::unique_ptr<ObjectFile>>
  createObjectFile(MemoryBufferRef Object) {
    return createObjectFile(Object, llvm::file_magic::unknown);
  }

  /// True if \p v is an ObjectFile (type ID between StartObjects and EndObjects).
  ///
  /// \param v Binary to test.
  /// \return True if \p v is an ObjectFile.
  static bool classof(const Binary *v) {
    return v->isObject();
  }

  /// Create a COFF ObjectFile from \p Object.
  ///
  /// \param Object Memory buffer holding a COFF object.
  /// \return A unique_ptr to a COFFObjectFile, or an error on failure.
  static Expected<std::unique_ptr<COFFObjectFile>>
  createCOFFObjectFile(MemoryBufferRef Object);

  /// Create an XCOFF ObjectFile from \p Object of the given \p FileType.
  ///
  /// \param Object Memory buffer holding an XCOFF object.
  /// \param FileType XCOFF file type / magic identifying the variant.
  /// \return A unique_ptr to an XCOFF ObjectFile, or an error on failure.
  static Expected<std::unique_ptr<ObjectFile>>
  createXCOFFObjectFile(MemoryBufferRef Object, unsigned FileType);

  /// Create an ELF ObjectFile from \p Object.
  ///
  /// \param Object Memory buffer holding an ELF object.
  /// \param InitContent If true, initialize format-specific content eagerly.
  /// \return A unique_ptr to an ELF ObjectFile, or an error on failure.
  static Expected<std::unique_ptr<ObjectFile>>
  createELFObjectFile(MemoryBufferRef Object, bool InitContent = true);

  /// Create a Mach-O ObjectFile from \p Object.
  ///
  /// Optional universal-binary fields select a slice when applicable.
  ///
  /// \param Object Memory buffer holding a Mach-O object or slice.
  /// \param UniversalCputype CPU type when selecting a universal-binary slice.
  /// \param UniversalIndex Index of the slice within a universal binary.
  /// \param MachOFilesetEntryOffset Offset of the fileset entry, if any.
  /// \return A unique_ptr to a MachOObjectFile, or an error on failure.
  static Expected<std::unique_ptr<MachOObjectFile>>
  createMachOObjectFile(MemoryBufferRef Object, uint32_t UniversalCputype = 0,
                        uint32_t UniversalIndex = 0,
                        size_t MachOFilesetEntryOffset = 0);

  /// Create a GOFF ObjectFile from \p Object.
  ///
  /// \param Object Memory buffer holding a GOFF object.
  /// \return A unique_ptr to a GOFF ObjectFile, or an error on failure.
  static Expected<std::unique_ptr<ObjectFile>>
  createGOFFObjectFile(MemoryBufferRef Object);

  /// Create a WebAssembly ObjectFile from \p Object.
  ///
  /// \param Object Memory buffer holding a Wasm object.
  /// \return A unique_ptr to a WasmObjectFile, or an error on failure.
  static Expected<std::unique_ptr<WasmObjectFile>>
  createWasmObjectFile(MemoryBufferRef Object);

  /// Create a DirectX container ObjectFile from \p Object.
  ///
  /// \param Object Memory buffer holding a DXContainer object.
  /// \return A unique_ptr to a DXContainerObjectFile, or an error on failure.
  static Expected<std::unique_ptr<DXContainerObjectFile>>
  createDXContainerObjectFile(MemoryBufferRef Object);
};

/// A filtered iterator for SectionRefs that skips sections based on some given
/// predicate.
class SectionFilterIterator {
public:
  /// Construct a filter iterator starting at \p Begin and stopping at \p End.
  ///
  /// Skips sections for which \p Pred returns false.
  ///
  /// \param Pred Predicate that selects which sections to yield.
  /// \param Begin First section to consider.
  /// \param End Past-the-end section iterator.
  SectionFilterIterator(SectionFilterPredicate Pred,
                        const section_iterator &Begin,
                        const section_iterator &End)
      : Predicate(std::move(Pred)), Iterator(Begin), End(End) {
    scanPredicate();
  }
  /// Dereference to the current matching SectionRef.
  ///
  /// \return A reference to the current matching SectionRef.
  const SectionRef &operator*() const { return *Iterator; }
  /// Advance to the next section that matches the filter predicate.
  ///
  /// \return A reference to this iterator after advancing.
  SectionFilterIterator &operator++() {
    ++Iterator;
    scanPredicate();
    return *this;
  }
  /// True if this iterator and \p Other are at different positions.
  ///
  /// \param Other Iterator to compare against.
  /// \return True if the iterators are at different positions.
  bool operator!=(const SectionFilterIterator &Other) const {
    return Iterator != Other.Iterator;
  }

private:
  void scanPredicate() {
    while (Iterator != End && !Predicate(*Iterator)) {
      ++Iterator;
    }
  }
  SectionFilterPredicate Predicate;
  section_iterator Iterator;
  section_iterator End;
};

/// Creates an iterator range of SectionFilterIterators for a given Object and
/// predicate.
class SectionFilter {
public:
  /// Filter \p Obj's sections with predicate \p Pred.
  ///
  /// \param Pred Predicate that selects which sections to yield.
  /// \param Obj Object file whose sections are filtered.
  SectionFilter(SectionFilterPredicate Pred, const ObjectFile &Obj)
      : Predicate(std::move(Pred)), Object(Obj) {}
  /// Iterator to the first section matching the filter predicate.
  ///
  /// \return An iterator to the first matching section.
  SectionFilterIterator begin() {
    return SectionFilterIterator(Predicate, Object.section_begin(),
                                 Object.section_end());
  }
  /// Past-the-end iterator for the filtered section range.
  ///
  /// \return A past-the-end iterator for the filtered section range.
  SectionFilterIterator end() {
    return SectionFilterIterator(Predicate, Object.section_end(),
                                 Object.section_end());
  }

private:
  SectionFilterPredicate Predicate;
  const ObjectFile &Object;
};

// Inline function definitions.
inline SymbolRef::SymbolRef(DataRefImpl SymbolP, const ObjectFile *Owner)
    : BasicSymbolRef(SymbolP, Owner) {}

inline Expected<StringRef> SymbolRef::getName() const {
  return getObject()->getSymbolName(getRawDataRefImpl());
}

inline Expected<uint64_t> SymbolRef::getAddress() const {
  return getObject()->getSymbolAddress(getRawDataRefImpl());
}

inline Expected<uint64_t> SymbolRef::getValue() const {
  return getObject()->getSymbolValue(getRawDataRefImpl());
}

inline uint32_t SymbolRef::getAlignment() const {
  return getObject()->getSymbolAlignment(getRawDataRefImpl());
}

inline uint64_t SymbolRef::getCommonSize() const {
  return getObject()->getCommonSymbolSize(getRawDataRefImpl());
}

inline Expected<section_iterator> SymbolRef::getSection() const {
  return getObject()->getSymbolSection(getRawDataRefImpl());
}

inline Expected<SymbolRef::Type> SymbolRef::getType() const {
  return getObject()->getSymbolType(getRawDataRefImpl());
}

inline const ObjectFile *SymbolRef::getObject() const {
  const SymbolicFile *O = BasicSymbolRef::getObject();
  return cast<ObjectFile>(O);
}

/// SectionRef
inline SectionRef::SectionRef(DataRefImpl SectionP,
                              const ObjectFile *Owner)
  : SectionPimpl(SectionP)
  , OwningObject(Owner) {}

inline bool SectionRef::operator==(const SectionRef &Other) const {
  return OwningObject == Other.OwningObject &&
         SectionPimpl == Other.SectionPimpl;
}

inline bool SectionRef::operator!=(const SectionRef &Other) const {
  return !(*this == Other);
}

inline bool SectionRef::operator<(const SectionRef &Other) const {
  assert(OwningObject == Other.OwningObject);
  return SectionPimpl < Other.SectionPimpl;
}

inline void SectionRef::moveNext() {
  return OwningObject->moveSectionNext(SectionPimpl);
}

inline Expected<StringRef> SectionRef::getName() const {
  return OwningObject->getSectionName(SectionPimpl);
}

inline uint64_t SectionRef::getAddress() const {
  return OwningObject->getSectionAddress(SectionPimpl);
}

inline uint64_t SectionRef::getIndex() const {
  return OwningObject->getSectionIndex(SectionPimpl);
}

inline uint64_t SectionRef::getSize() const {
  return OwningObject->getSectionSize(SectionPimpl);
}

inline Expected<StringRef> SectionRef::getContents() const {
  Expected<ArrayRef<uint8_t>> Res =
      OwningObject->getSectionContents(SectionPimpl);
  if (!Res)
    return Res.takeError();
  return StringRef(reinterpret_cast<const char *>(Res->data()), Res->size());
}

inline Align SectionRef::getAlignment() const {
  return MaybeAlign(OwningObject->getSectionAlignment(SectionPimpl))
      .valueOrOne();
}

inline bool SectionRef::isCompressed() const {
  return OwningObject->isSectionCompressed(SectionPimpl);
}

inline bool SectionRef::isText() const {
  return OwningObject->isSectionText(SectionPimpl);
}

inline bool SectionRef::isData() const {
  return OwningObject->isSectionData(SectionPimpl);
}

inline bool SectionRef::isBSS() const {
  return OwningObject->isSectionBSS(SectionPimpl);
}

inline bool SectionRef::isVirtual() const {
  return OwningObject->isSectionVirtual(SectionPimpl);
}

inline bool SectionRef::isBitcode() const {
  return OwningObject->isSectionBitcode(SectionPimpl);
}

inline bool SectionRef::isStripped() const {
  return OwningObject->isSectionStripped(SectionPimpl);
}

inline bool SectionRef::isBerkeleyText() const {
  return OwningObject->isBerkeleyText(SectionPimpl);
}

inline bool SectionRef::isBerkeleyData() const {
  return OwningObject->isBerkeleyData(SectionPimpl);
}

inline bool SectionRef::isDebugSection() const {
  return OwningObject->isDebugSection(SectionPimpl);
}

inline relocation_iterator SectionRef::relocation_begin() const {
  return OwningObject->section_rel_begin(SectionPimpl);
}

inline relocation_iterator SectionRef::relocation_end() const {
  return OwningObject->section_rel_end(SectionPimpl);
}

inline Expected<section_iterator> SectionRef::getRelocatedSection() const {
  return OwningObject->getRelocatedSection(SectionPimpl);
}

inline DataRefImpl SectionRef::getRawDataRefImpl() const {
  return SectionPimpl;
}

inline const ObjectFile *SectionRef::getObject() const {
  return OwningObject;
}

/// RelocationRef
inline RelocationRef::RelocationRef(DataRefImpl RelocationP,
                              const ObjectFile *Owner)
  : RelocationPimpl(RelocationP)
  , OwningObject(Owner) {}

inline bool RelocationRef::operator==(const RelocationRef &Other) const {
  return RelocationPimpl == Other.RelocationPimpl;
}

inline void RelocationRef::moveNext() {
  return OwningObject->moveRelocationNext(RelocationPimpl);
}

inline uint64_t RelocationRef::getOffset() const {
  return OwningObject->getRelocationOffset(RelocationPimpl);
}

inline symbol_iterator RelocationRef::getSymbol() const {
  return OwningObject->getRelocationSymbol(RelocationPimpl);
}

inline uint64_t RelocationRef::getType() const {
  return OwningObject->getRelocationType(RelocationPimpl);
}

inline void RelocationRef::getTypeName(SmallVectorImpl<char> &Result) const {
  return OwningObject->getRelocationTypeName(RelocationPimpl, Result);
}

inline DataRefImpl RelocationRef::getRawDataRefImpl() const {
  return RelocationPimpl;
}

inline const ObjectFile *RelocationRef::getObject() const {
  return OwningObject;
}

} // end namespace object

/// DenseMapInfo specialization so SectionRef can be used as a DenseMap key.
template <> struct DenseMapInfo<object::SectionRef> {
  /// Return true if \p A and \p B refer to the same section.
  ///
  /// \param A First section reference.
  /// \param B Second section reference.
  /// \return True if \p A and \p B refer to the same section.
  static bool isEqual(const object::SectionRef &A,
                      const object::SectionRef &B) {
    return A == B;
  }
  /// Compute a hash value for section \p Sec.
  ///
  /// \param Sec Section reference to hash.
  /// \return A hash value for \p Sec suitable for DenseMap.
  static unsigned getHashValue(const object::SectionRef &Sec) {
    object::DataRefImpl Raw = Sec.getRawDataRefImpl();
    return hash_combine(Raw.p, Raw.d.a, Raw.d.b);
  }
};

} // end namespace llvm

#endif // LLVM_OBJECT_OBJECTFILE_H
