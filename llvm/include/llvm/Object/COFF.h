//===- COFF.h - COFF object file implementation -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the COFFObjectFile class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_COFF_H
#define LLVM_OBJECT_COFF_H

#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/CVDebugRecord.h"
#include "llvm/Object/Error.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/ConvertUTF.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/TargetParser/SubtargetFeature.h"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <system_error>

namespace llvm {

template <typename T> class ArrayRef;

namespace object {

class Arm64XRelocRef;
class BaseRelocRef;
class DelayImportDirectoryEntryRef;
class DynamicRelocRef;
class ExportDirectoryEntryRef;
class ImportDirectoryEntryRef;
class ImportedSymbolRef;
class ResourceSectionRef;

/// Iterator over COFF import directory table entries.
using import_directory_iterator = content_iterator<ImportDirectoryEntryRef>;
/// Iterator over COFF delay-import directory table entries.
using delay_import_directory_iterator =
    content_iterator<DelayImportDirectoryEntryRef>;
/// Iterator over COFF export directory table entries.
using export_directory_iterator = content_iterator<ExportDirectoryEntryRef>;
/// Iterator over symbols listed in an import lookup or address table.
using imported_symbol_iterator = content_iterator<ImportedSymbolRef>;
/// Iterator over PE base relocation entries.
using base_reloc_iterator = content_iterator<BaseRelocRef>;
/// Iterator over PE dynamic relocation table entries.
using dynamic_reloc_iterator = content_iterator<DynamicRelocRef>;
/// Iterator over Arm64X dynamic fixup relocation entries.
using arm64x_reloc_iterator = content_iterator<Arm64XRelocRef>;

/// The DOS compatible header at the front of all PE/COFF executables.
struct dos_header {
  char                 Magic[2]; ///< DOS signature, typically "MZ".
  support::ulittle16_t UsedBytesInTheLastPage; ///< Bytes used in the last file page.
  support::ulittle16_t FileSizeInPages; ///< File size in 512-byte pages.
  support::ulittle16_t NumberOfRelocationItems; ///< Count of DOS relocation entries.
  support::ulittle16_t HeaderSizeInParagraphs; ///< Header size in 16-byte paragraphs.
  support::ulittle16_t MinimumExtraParagraphs; ///< Minimum extra paragraphs needed.
  support::ulittle16_t MaximumExtraParagraphs; ///< Maximum extra paragraphs requested.
  support::ulittle16_t InitialRelativeSS; ///< Initial SS relative to the load segment.
  support::ulittle16_t InitialSP; ///< Initial stack pointer value.
  support::ulittle16_t Checksum; ///< DOS checksum (often unused).
  support::ulittle16_t InitialIP; ///< Initial instruction pointer.
  support::ulittle16_t InitialRelativeCS; ///< Initial CS relative to the load segment.
  support::ulittle16_t AddressOfRelocationTable; ///< File offset of the DOS relocation table.
  support::ulittle16_t OverlayNumber; ///< Overlay number (zero for main program).
  support::ulittle16_t Reserved[4]; ///< Reserved words.
  support::ulittle16_t OEMid; ///< OEM identifier.
  support::ulittle16_t OEMinfo; ///< OEM-specific information.
  support::ulittle16_t Reserved2[10]; ///< Additional reserved words.
  support::ulittle32_t AddressOfNewExeHeader; ///< File offset of the PE/COFF header.
};

/// Standard COFF file header (IMAGE_FILE_HEADER).
struct coff_file_header {
  support::ulittle16_t Machine; ///< Target machine type (IMAGE_FILE_MACHINE_*).
  support::ulittle16_t NumberOfSections; ///< Number of sections in the file.
  support::ulittle32_t TimeDateStamp; ///< Time-date stamp of the file.
  support::ulittle32_t PointerToSymbolTable; ///< File offset of the COFF symbol table.
  support::ulittle32_t NumberOfSymbols; ///< Number of entries in the symbol table.
  support::ulittle16_t SizeOfOptionalHeader; ///< Size of the optional PE header, if any.
  support::ulittle16_t Characteristics; ///< Image characteristics flags.

  /// Return true if this header describes a short import library.
  /// @return True if this header describes a short import library.
  bool isImportLibrary() const { return NumberOfSections == 0xffff; }
};

/// Extended COFF bigobj file header for objects with many sections.
struct coff_bigobj_file_header {
  support::ulittle16_t Sig1; ///< First signature word (must be 0).
  support::ulittle16_t Sig2; ///< Second signature word (must be 0xFFFF).
  support::ulittle16_t Version; ///< Bigobj format version.
  support::ulittle16_t Machine; ///< Target machine type (IMAGE_FILE_MACHINE_*).
  support::ulittle32_t TimeDateStamp; ///< Time-date stamp of the file.
  uint8_t              UUID[16]; ///< Class ID identifying the bigobj format.
  support::ulittle32_t unused1; ///< Reserved; must be zero.
  support::ulittle32_t unused2; ///< Reserved; must be zero.
  support::ulittle32_t unused3; ///< Reserved; must be zero.
  support::ulittle32_t unused4; ///< Reserved; must be zero.
  support::ulittle32_t NumberOfSections; ///< Number of sections in the file.
  support::ulittle32_t PointerToSymbolTable; ///< File offset of the COFF symbol table.
  support::ulittle32_t NumberOfSymbols; ///< Number of entries in the symbol table.
};

/// The 32-bit PE header that follows the COFF header.
struct pe32_header {
  support::ulittle16_t Magic; ///< PE optional-header magic (PE32).
  uint8_t MajorLinkerVersion; ///< Major linker version number.
  uint8_t MinorLinkerVersion; ///< Minor linker version number.
  support::ulittle32_t SizeOfCode; ///< Total size of all code sections.
  support::ulittle32_t SizeOfInitializedData; ///< Total size of initialized data.
  support::ulittle32_t SizeOfUninitializedData; ///< Total size of uninitialized data.
  support::ulittle32_t AddressOfEntryPoint; ///< RVA of the entry point.
  support::ulittle32_t BaseOfCode; ///< RVA of the start of the code section.
  support::ulittle32_t BaseOfData; ///< RVA of the start of the data section.
  support::ulittle32_t ImageBase; ///< Preferred base address of the image.
  support::ulittle32_t SectionAlignment; ///< Section alignment in memory.
  support::ulittle32_t FileAlignment; ///< Section alignment in the file.
  support::ulittle16_t MajorOperatingSystemVersion; ///< Major required OS version.
  support::ulittle16_t MinorOperatingSystemVersion; ///< Minor required OS version.
  support::ulittle16_t MajorImageVersion; ///< Major image version.
  support::ulittle16_t MinorImageVersion; ///< Minor image version.
  support::ulittle16_t MajorSubsystemVersion; ///< Major subsystem version.
  support::ulittle16_t MinorSubsystemVersion; ///< Minor subsystem version.
  support::ulittle32_t Win32VersionValue; ///< Reserved Win32 version field (must be 0).
  support::ulittle32_t SizeOfImage; ///< Size of the image as loaded in memory.
  support::ulittle32_t SizeOfHeaders; ///< Combined size of headers and section table.
  support::ulittle32_t CheckSum; ///< Image checksum.
  support::ulittle16_t Subsystem; ///< Windows subsystem required to run this image.
  // FIXME: This should be DllCharacteristics.
  support::ulittle16_t DLLCharacteristics; ///< DLL characteristic flags.
  support::ulittle32_t SizeOfStackReserve; ///< Size of stack to reserve.
  support::ulittle32_t SizeOfStackCommit; ///< Size of stack to commit.
  support::ulittle32_t SizeOfHeapReserve; ///< Size of local heap to reserve.
  support::ulittle32_t SizeOfHeapCommit; ///< Size of local heap to commit.
  support::ulittle32_t LoaderFlags; ///< Obsolete loader flags (must be 0).
  // FIXME: This should be NumberOfRvaAndSizes.
  support::ulittle32_t NumberOfRvaAndSize; ///< Number of data-directory entries.
};

/// The 64-bit PE header that follows the COFF header.
struct pe32plus_header {
  support::ulittle16_t Magic; ///< PE optional-header magic (PE32+).
  uint8_t MajorLinkerVersion; ///< Major linker version number.
  uint8_t MinorLinkerVersion; ///< Minor linker version number.
  support::ulittle32_t SizeOfCode; ///< Total size of all code sections.
  support::ulittle32_t SizeOfInitializedData; ///< Total size of initialized data.
  support::ulittle32_t SizeOfUninitializedData; ///< Total size of uninitialized data.
  support::ulittle32_t AddressOfEntryPoint; ///< RVA of the entry point.
  support::ulittle32_t BaseOfCode; ///< RVA of the start of the code section.
  support::ulittle64_t ImageBase; ///< Preferred base address of the image.
  support::ulittle32_t SectionAlignment; ///< Section alignment in memory.
  support::ulittle32_t FileAlignment; ///< Section alignment in the file.
  support::ulittle16_t MajorOperatingSystemVersion; ///< Major required OS version.
  support::ulittle16_t MinorOperatingSystemVersion; ///< Minor required OS version.
  support::ulittle16_t MajorImageVersion; ///< Major image version.
  support::ulittle16_t MinorImageVersion; ///< Minor image version.
  support::ulittle16_t MajorSubsystemVersion; ///< Major subsystem version.
  support::ulittle16_t MinorSubsystemVersion; ///< Minor subsystem version.
  support::ulittle32_t Win32VersionValue; ///< Reserved Win32 version field (must be 0).
  support::ulittle32_t SizeOfImage; ///< Size of the image as loaded in memory.
  support::ulittle32_t SizeOfHeaders; ///< Combined size of headers and section table.
  support::ulittle32_t CheckSum; ///< Image checksum.
  support::ulittle16_t Subsystem; ///< Windows subsystem required to run this image.
  support::ulittle16_t DLLCharacteristics; ///< DLL characteristic flags.
  support::ulittle64_t SizeOfStackReserve; ///< Size of stack to reserve.
  support::ulittle64_t SizeOfStackCommit; ///< Size of stack to commit.
  support::ulittle64_t SizeOfHeapReserve; ///< Size of local heap to reserve.
  support::ulittle64_t SizeOfHeapCommit; ///< Size of local heap to commit.
  support::ulittle32_t LoaderFlags; ///< Obsolete loader flags (must be 0).
  support::ulittle32_t NumberOfRvaAndSize; ///< Number of data-directory entries.
};

/// PE data-directory entry describing an RVA/size pair.
struct data_directory {
  support::ulittle32_t RelativeVirtualAddress; ///< RVA of the directory data.
  support::ulittle32_t Size; ///< Size in bytes of the directory data.
};

/// PE debug-directory entry (IMAGE_DEBUG_DIRECTORY).
struct debug_directory {
  support::ulittle32_t Characteristics; ///< Reserved characteristics flags.
  support::ulittle32_t TimeDateStamp; ///< Time-date stamp of the debug data.
  support::ulittle16_t MajorVersion; ///< Major version of the debug data format.
  support::ulittle16_t MinorVersion; ///< Minor version of the debug data format.
  support::ulittle32_t Type; ///< Debug type (IMAGE_DEBUG_TYPE_*).
  support::ulittle32_t SizeOfData; ///< Size of the debug data.
  support::ulittle32_t AddressOfRawData; ///< RVA of the debug data when loaded.
  support::ulittle32_t PointerToRawData; ///< File offset of the debug data.
};

/// Import lookup / address table entry for PE32 or PE32+.
template <typename IntTy>
struct import_lookup_table_entry {
  IntTy Data; ///< Raw ILT/IAT entry bits (ordinal or hint/name RVA).

  /// Return true if this entry imports by ordinal.
  /// @return True if this entry imports by ordinal.
  bool isOrdinal() const { return Data < 0; }

  /// Return the import ordinal when \c isOrdinal() is true.
  /// @return The import ordinal when \c isOrdinal() is true.
  uint16_t getOrdinal() const {
    assert(isOrdinal() && "ILT entry is not an ordinal!");
    return Data & 0xFFFF;
  }

  /// Return the hint/name table RVA when importing by name.
  /// @return The hint/name table RVA when importing by name.
  uint32_t getHintNameRVA() const {
    assert(!isOrdinal() && "ILT entry is not a Hint/Name RVA!");
    return Data & 0xFFFFFFFF;
  }
};

/// 32-bit import lookup table entry.
using import_lookup_table_entry32 =
    import_lookup_table_entry<support::little32_t>;
/// 64-bit import lookup table entry.
using import_lookup_table_entry64 =
    import_lookup_table_entry<support::little64_t>;

/// Delay-load import directory table entry.
struct delay_import_directory_table_entry {
  // dumpbin reports this field as "Characteristics" instead of "Attributes".
  support::ulittle32_t Attributes; ///< Delay-load attributes / characteristics.
  support::ulittle32_t Name; ///< RVA of the delay-loaded DLL name.
  support::ulittle32_t ModuleHandle; ///< RVA of the module handle storage.
  support::ulittle32_t DelayImportAddressTable; ///< RVA of the delay IAT.
  support::ulittle32_t DelayImportNameTable; ///< RVA of the delay import name table.
  support::ulittle32_t BoundDelayImportTable; ///< RVA of the bound delay-import table.
  support::ulittle32_t UnloadDelayImportTable; ///< RVA of the unload delay-import table.
  support::ulittle32_t TimeStamp; ///< Time stamp of the bound DLL, if bound.
};

/// Export directory table (IMAGE_EXPORT_DIRECTORY).
struct export_directory_table_entry {
  support::ulittle32_t ExportFlags; ///< Reserved export flags (must be 0).
  support::ulittle32_t TimeDateStamp; ///< Time-date stamp of the export data.
  support::ulittle16_t MajorVersion; ///< Major version number.
  support::ulittle16_t MinorVersion; ///< Minor version number.
  support::ulittle32_t NameRVA; ///< RVA of the DLL name string.
  support::ulittle32_t OrdinalBase; ///< Starting ordinal number for exports.
  support::ulittle32_t AddressTableEntries; ///< Number of entries in the EAT.
  support::ulittle32_t NumberOfNamePointers; ///< Number of name-pointer / ordinal entries.
  support::ulittle32_t ExportAddressTableRVA; ///< RVA of the export address table.
  support::ulittle32_t NamePointerRVA; ///< RVA of the export name pointer table.
  support::ulittle32_t OrdinalTableRVA; ///< RVA of the export ordinal table.
};

/// Export address table entry: either an export RVA or a forwarder RVA.
union export_address_table_entry {
  support::ulittle32_t ExportRVA; ///< RVA of the exported symbol when not forwarded.
  support::ulittle32_t ForwarderRVA; ///< RVA of the forwarder string when forwarded.
};

/// RVA entry in the export name pointer table.
using export_name_pointer_table_entry = support::ulittle32_t;
/// Ordinal entry in the export ordinal table.
using export_ordinal_table_entry = support::ulittle16_t;

/// Offset into the COFF string table for a long symbol name.
struct StringTableOffset {
  support::ulittle32_t Zeroes; ///< Must be zero when the name is in the string table.
  support::ulittle32_t Offset; ///< Byte offset of the name in the string table.
};

/// COFF symbol table entry with 16- or 32-bit section numbers.
template <typename SectionNumberType>
struct coff_symbol {
  /// Symbol name: either an inline short name or a string-table offset.
  union {
    char ShortName[COFF::NameSize]; ///< Inline name when at most eight bytes.
    StringTableOffset Offset; ///< String-table reference for longer names.
  } Name; ///< Symbol name storage.

  support::ulittle32_t Value; ///< Symbol value, meaning depends on storage class.
  SectionNumberType SectionNumber; ///< One-based section number, or a special value.

  support::ulittle16_t Type; ///< Base and complex type packed into 16 bits.

  uint8_t StorageClass; ///< Storage class (IMAGE_SYM_CLASS_*).
  uint8_t NumberOfAuxSymbols; ///< Number of following auxiliary symbol records.
};

/// COFF symbol with a 16-bit section number field.
using coff_symbol16 = coff_symbol<support::ulittle16_t>;
/// COFF symbol with a 32-bit section number field (bigobj).
using coff_symbol32 = coff_symbol<support::ulittle32_t>;

/// Common prefix shared by coff_symbol16 and coff_symbol32.
struct coff_symbol_generic {
  /// Symbol name: either an inline short name or a string-table offset.
  union {
    char ShortName[COFF::NameSize]; ///< Inline name when at most eight bytes.
    StringTableOffset Offset; ///< String-table reference for longer names.
  } Name; ///< Symbol name storage.
  support::ulittle32_t Value; ///< Symbol value, meaning depends on storage class.
};

struct coff_aux_section_definition;
struct coff_aux_weak_external;

/// Lightweight reference to a 16- or 32-bit COFF symbol table entry.
class COFFSymbolRef {
public:
  /// Construct an empty symbol reference.
  COFFSymbolRef() = default;
  /// Construct a reference to 16-bit COFF symbol \p CS.
  /// \param CS Pointer to a coff_symbol16 record.
  COFFSymbolRef(const coff_symbol16 *CS) : CS16(CS) {}
  /// Construct a reference to 32-bit COFF symbol \p CS.
  /// \param CS Pointer to a coff_symbol32 record.
  COFFSymbolRef(const coff_symbol32 *CS) : CS32(CS) {}

  /// Return a raw pointer to the underlying symbol record.
  /// @return A raw pointer to the underlying symbol record.
  const void *getRawPtr() const {
    return CS16 ? static_cast<const void *>(CS16) : CS32;
  }

  /// Return the common symbol-prefix view of this record.
  /// @return The common symbol-prefix view of this record.
  const coff_symbol_generic *getGeneric() const {
    if (CS16)
      return reinterpret_cast<const coff_symbol_generic *>(CS16);
    return reinterpret_cast<const coff_symbol_generic *>(CS32);
  }

  /// Order symbol references by underlying record address.
  /// \param A Left-hand symbol reference.
  /// \param B Right-hand symbol reference.
  /// @return True if \p A points to a lower address than \p B.
  friend bool operator<(COFFSymbolRef A, COFFSymbolRef B) {
    return A.getRawPtr() < B.getRawPtr();
  }

  /// Return true if this references a bigobj (32-bit) symbol.
  /// @return True if this references a bigobj (32-bit) symbol.
  bool isBigObj() const {
    if (CS16)
      return false;
    if (CS32)
      return true;
    llvm_unreachable("COFFSymbolRef points to nothing!");
  }

  /// Return the inline eight-byte short name field.
  /// @return The inline eight-byte short name field.
  const char *getShortName() const {
    return CS16 ? CS16->Name.ShortName : CS32->Name.ShortName;
  }

  /// Return the string-table offset when the name is not short.
  /// @return The string-table offset when the name is not short.
  const StringTableOffset &getStringTableOffset() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->Name.Offset : CS32->Name.Offset;
  }

  /// Return the symbol value field.
  /// @return The symbol value field.
  uint32_t getValue() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->Value : CS32->Value;
  }

  /// Return the section number, with reserved values as negatives for 16-bit.
  /// @return The section number, with reserved values as negatives for 16-bit.
  int32_t getSectionNumber() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    if (CS16) {
      // Reserved sections are returned as negative numbers.
      if (CS16->SectionNumber <= COFF::MaxNumberOfSections16)
        return CS16->SectionNumber;
      return static_cast<int16_t>(CS16->SectionNumber);
    }
    return static_cast<int32_t>(CS32->SectionNumber);
  }

  /// Return the packed symbol type field.
  /// @return The packed symbol type field.
  uint16_t getType() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->Type : CS32->Type;
  }

  /// Return the symbol storage class.
  /// @return The symbol storage class.
  uint8_t getStorageClass() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->StorageClass : CS32->StorageClass;
  }

  /// Return the number of following auxiliary symbol records.
  /// @return The number of following auxiliary symbol records.
  uint8_t getNumberOfAuxSymbols() const {
    assert(isSet() && "COFFSymbolRef points to nothing!");
    return CS16 ? CS16->NumberOfAuxSymbols : CS32->NumberOfAuxSymbols;
  }

  /// Return the base type nibble of the symbol type.
  /// @return The base type nibble of the symbol type.
  uint8_t getBaseType() const { return getType() & 0x0F; }

  /// Return the complex type nibble of the symbol type.
  /// @return The complex type nibble of the symbol type.
  uint8_t getComplexType() const {
    return (getType() & 0xF0) >> COFF::SCT_COMPLEX_TYPE_SHIFT;
  }

  /// Return the first auxiliary record reinterpreted as type \p T.
  /// @return The first auxiliary record reinterpreted as type \p T.
  template <typename T> const T *getAux() const {
    return CS16 ? reinterpret_cast<const T *>(CS16 + 1)
                : reinterpret_cast<const T *>(CS32 + 1);
  }

  /// Return the section-definition auxiliary record, if present.
  /// @return The section-definition auxiliary record, if present.
  const coff_aux_section_definition *getSectionDefinition() const {
    if (!getNumberOfAuxSymbols() ||
        getStorageClass() != COFF::IMAGE_SYM_CLASS_STATIC)
      return nullptr;
    return getAux<coff_aux_section_definition>();
  }

  /// Return the weak-external auxiliary record, if present.
  /// @return The weak-external auxiliary record, if present.
  const coff_aux_weak_external *getWeakExternal() const {
    if (!getNumberOfAuxSymbols() ||
        getStorageClass() != COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL)
      return nullptr;
    return getAux<coff_aux_weak_external>();
  }

  /// Return true if this is an absolute symbol.
  /// @return True if this is an absolute symbol.
  bool isAbsolute() const {
    return getSectionNumber() == -1;
  }

  /// Return true if this symbol has external storage class.
  /// @return True if this symbol has external storage class.
  bool isExternal() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_EXTERNAL;
  }

  /// Return true if this is a common (communal) symbol.
  /// @return True if this is a common (communal) symbol.
  bool isCommon() const {
    return isExternal() && getSectionNumber() == COFF::IMAGE_SYM_UNDEFINED &&
           getValue() != 0;
  }

  /// Return true if this is an undefined external symbol.
  /// @return True if this is an undefined external symbol.
  bool isUndefined() const {
    return isExternal() && getSectionNumber() == COFF::IMAGE_SYM_UNDEFINED &&
           getValue() == 0;
  }

  /// Return true if this is a section symbol with no section number.
  /// @return True if this is a section symbol with no section number.
  bool isEmptySectionDeclaration() const {
    return isSection() && getSectionNumber() == COFF::IMAGE_SYM_UNDEFINED;
  }

  /// Return true if this is a weak external symbol.
  /// @return True if this is a weak external symbol.
  bool isWeakExternal() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_WEAK_EXTERNAL;
  }

  /// Return true if this symbol describes a function definition.
  /// @return True if this symbol describes a function definition.
  bool isFunctionDefinition() const {
    return isExternal() && getBaseType() == COFF::IMAGE_SYM_TYPE_NULL &&
           getComplexType() == COFF::IMAGE_SYM_DTYPE_FUNCTION &&
           !COFF::isReservedSectionNumber(getSectionNumber());
  }

  /// Return true if this is a function line-number symbol.
  /// @return True if this is a function line-number symbol.
  bool isFunctionLineInfo() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_FUNCTION;
  }

  /// Return true if this symbol is undefined or a weak external.
  /// @return True if this symbol is undefined or a weak external.
  bool isAnyUndefined() const {
    return isUndefined() || isWeakExternal();
  }

  /// Return true if this is a file-record symbol.
  /// @return True if this is a file-record symbol.
  bool isFileRecord() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_FILE;
  }

  /// Return true if this is a section symbol.
  /// @return True if this is a section symbol.
  bool isSection() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_SECTION;
  }

  /// Return true if this symbol is followed by a section-definition aux record.
  /// @return True if this symbol is followed by a section-definition aux record.
  bool isSectionDefinition() const {
    // C++/CLI creates external ABS symbols for non-const appdomain globals.
    // These are also followed by an auxiliary section definition.
    bool isAppdomainGlobal =
        getStorageClass() == COFF::IMAGE_SYM_CLASS_EXTERNAL &&
        getSectionNumber() == COFF::IMAGE_SYM_ABSOLUTE;
    bool isOrdinarySection = getStorageClass() == COFF::IMAGE_SYM_CLASS_STATIC;
    if (!getNumberOfAuxSymbols())
      return false;
    return isAppdomainGlobal || isOrdinarySection;
  }

  /// Return true if this is a CLR token symbol.
  /// @return True if this is a CLR token symbol.
  bool isCLRToken() const {
    return getStorageClass() == COFF::IMAGE_SYM_CLASS_CLR_TOKEN;
  }

private:
  bool isSet() const { return CS16 || CS32; }

  const coff_symbol16 *CS16 = nullptr;
  const coff_symbol32 *CS32 = nullptr;
};

/// COFF section header (IMAGE_SECTION_HEADER).
struct coff_section {
  char Name[COFF::NameSize]; ///< Section name (or slash-encoded string-table offset).
  support::ulittle32_t VirtualSize; ///< Size of the section when loaded.
  support::ulittle32_t VirtualAddress; ///< RVA of the section when loaded.
  support::ulittle32_t SizeOfRawData; ///< Size of the section's raw data in the file.
  support::ulittle32_t PointerToRawData; ///< File offset of the section's raw data.
  support::ulittle32_t PointerToRelocations; ///< File offset of the relocation table.
  support::ulittle32_t PointerToLinenumbers; ///< File offset of the line-number table.
  support::ulittle16_t NumberOfRelocations; ///< Number of relocation entries.
  support::ulittle16_t NumberOfLinenumbers; ///< Number of line-number entries.
  support::ulittle32_t Characteristics; ///< Section flags (IMAGE_SCN_*).

  /// Return true if the real relocation count is stored in the first reloc.
  ///
  /// Returns true if the actual number of relocations is stored in
  /// VirtualAddress field of the first relocation table entry.
  /// @return True if the real relocation count is stored in the first reloc.
  bool hasExtendedRelocations() const {
    return (Characteristics & COFF::IMAGE_SCN_LNK_NRELOC_OVFL) &&
           NumberOfRelocations == UINT16_MAX;
  }

  /// Return the section alignment in bytes decoded from Characteristics.
  /// @return The section alignment in bytes decoded from Characteristics.
  uint32_t getAlignment() const {
    // The IMAGE_SCN_TYPE_NO_PAD bit is a legacy way of getting to
    // IMAGE_SCN_ALIGN_1BYTES.
    if (Characteristics & COFF::IMAGE_SCN_TYPE_NO_PAD)
      return 1;

    // Bit [20:24] contains section alignment. 0 means use a default alignment
    // of 16.
    uint32_t Shift = (Characteristics >> 20) & 0xF;
    if (Shift > 0)
      return 1U << (Shift - 1);
    return 16;
  }
};

/// COFF relocation table entry.
struct coff_relocation {
  support::ulittle32_t VirtualAddress; ///< Section-relative address to relocate.
  support::ulittle32_t SymbolTableIndex; ///< Index of the symbol used as the reloc target.
  support::ulittle16_t Type; ///< Relocation type (machine-specific).
};

/// Auxiliary record for a function-definition symbol.
struct coff_aux_function_definition {
  support::ulittle32_t TagIndex; ///< Symbol-table index of the related .bf symbol.
  support::ulittle32_t TotalSize; ///< Size of the function in bytes.
  support::ulittle32_t PointerToLinenumber; ///< File pointer to the line-number entries.
  support::ulittle32_t PointerToNextFunction; ///< Symbol index of the next function.
  char Unused1[2]; ///< Unused padding.
};

static_assert(sizeof(coff_aux_function_definition) == 18,
              "auxiliary entry must be 18 bytes");

/// Auxiliary record for begin-function (.bf) and end-function (.ef) symbols.
struct coff_aux_bf_and_ef_symbol {
  char Unused1[4]; ///< Unused padding.
  support::ulittle16_t Linenumber; ///< Source line number associated with the symbol.
  char Unused2[6]; ///< Unused padding.
  support::ulittle32_t PointerToNextFunction; ///< Symbol index of the next function.
  char Unused3[2]; ///< Unused padding.
};

static_assert(sizeof(coff_aux_bf_and_ef_symbol) == 18,
              "auxiliary entry must be 18 bytes");

/// Auxiliary record for a weak-external symbol.
struct coff_aux_weak_external {
  support::ulittle32_t TagIndex; ///< Symbol-table index of the paired strong symbol.
  support::ulittle32_t Characteristics; ///< Weak-external characteristics flags.
  char Unused1[10]; ///< Unused padding.
};

static_assert(sizeof(coff_aux_weak_external) == 18,
              "auxiliary entry must be 18 bytes");

/// Auxiliary record for a section-definition symbol.
struct coff_aux_section_definition {
  support::ulittle32_t Length; ///< Section length in bytes.
  support::ulittle16_t NumberOfRelocations; ///< Number of relocations in the section.
  support::ulittle16_t NumberOfLinenumbers; ///< Number of line numbers in the section.
  support::ulittle32_t CheckSum; ///< Section checksum for COMDAT deduplication.
  support::ulittle16_t NumberLowPart; ///< Low 16 bits of the related section number.
  uint8_t              Selection; ///< COMDAT selection type.
  uint8_t              Unused; ///< Unused byte.
  support::ulittle16_t NumberHighPart; ///< High 16 bits of the related section number.
  /// Return the related section number, combining high/low parts for bigobj.
  /// \param IsBigObj True when reading a bigobj symbol table.
  /// @return The related section number, combining high/low parts for bigobj.
  int32_t getNumber(bool IsBigObj) const {
    uint32_t Number = static_cast<uint32_t>(NumberLowPart);
    if (IsBigObj)
      Number |= static_cast<uint32_t>(NumberHighPart) << 16;
    return static_cast<int32_t>(Number);
  }
};

static_assert(sizeof(coff_aux_section_definition) == 18,
              "auxiliary entry must be 18 bytes");

/// Auxiliary record for a CLR token symbol.
struct coff_aux_clr_token {
  uint8_t              AuxType; ///< Auxiliary type discriminator.
  uint8_t              Reserved; ///< Reserved; must be zero.
  support::ulittle32_t SymbolTableIndex; ///< Index of the related symbol table entry.
  char                 MBZ[12]; ///< Must be zero.
};

static_assert(sizeof(coff_aux_clr_token) == 18,
              "auxiliary entry must be 18 bytes");

/// Header of a COFF short import library member.
struct coff_import_header {
  support::ulittle16_t Sig1; ///< First signature word (must be 0).
  support::ulittle16_t Sig2; ///< Second signature word (must be 0xFFFF).
  support::ulittle16_t Version; ///< Import header version.
  support::ulittle16_t Machine; ///< Target machine type.
  support::ulittle32_t TimeDateStamp; ///< Time-date stamp.
  support::ulittle32_t SizeOfData; ///< Size of the following name strings.
  support::ulittle16_t OrdinalHint; ///< Ordinal or hint for the import.
  support::ulittle16_t TypeInfo; ///< Packed import type and name type.

  /// Return the import type bits from TypeInfo.
  /// @return The import type bits from TypeInfo.
  int getType() const { return TypeInfo & 0x3; }
  /// Return the import name-type bits from TypeInfo.
  /// @return The import name-type bits from TypeInfo.
  int getNameType() const { return (TypeInfo >> 2) & 0x7; }
};

/// Import directory table entry (IMAGE_IMPORT_DESCRIPTOR).
struct coff_import_directory_table_entry {
  support::ulittle32_t ImportLookupTableRVA; ///< RVA of the import lookup table.
  support::ulittle32_t TimeDateStamp; ///< Time stamp of the bound DLL, if bound.
  support::ulittle32_t ForwarderChain; ///< Index of the first forwarder reference.
  support::ulittle32_t NameRVA; ///< RVA of the imported DLL name.
  support::ulittle32_t ImportAddressTableRVA; ///< RVA of the import address table.

  /// Return true if this is the null terminator entry.
  /// @return True if this is the null terminator entry.
  bool isNull() const {
    return ImportLookupTableRVA == 0 && TimeDateStamp == 0 &&
           ForwarderChain == 0 && NameRVA == 0 && ImportAddressTableRVA == 0;
  }
};

/// Thread-local storage directory (IMAGE_TLS_DIRECTORY).
template <typename IntTy>
struct coff_tls_directory {
  IntTy StartAddressOfRawData; ///< VA of the start of the TLS template.
  IntTy EndAddressOfRawData; ///< VA of the end of the TLS template.
  IntTy AddressOfIndex; ///< VA of the TLS index assigned by the loader.
  IntTy AddressOfCallBacks; ///< VA of the null-terminated TLS callback array.
  support::ulittle32_t SizeOfZeroFill; ///< Size of zero-fill after the template.
  support::ulittle32_t Characteristics; ///< TLS characteristics, including alignment.

  /// Return the TLS alignment in bytes decoded from Characteristics.
  /// @return The TLS alignment in bytes decoded from Characteristics.
  uint32_t getAlignment() const {
    // Bit [20:24] contains section alignment.
    uint32_t Shift = (Characteristics & COFF::IMAGE_SCN_ALIGN_MASK) >> 20;
    if (Shift > 0)
      return 1U << (Shift - 1);
    return 0;
  }

  /// Encode alignment \p Align into the Characteristics field.
  /// \param Align Alignment in bytes; must be zero or a power of two.
  void setAlignment(uint32_t Align) {
    uint32_t AlignBits = 0;
    if (Align) {
      assert(llvm::isPowerOf2_32(Align) && "alignment is not a power of 2");
      assert(llvm::Log2_32(Align) <= 13 && "alignment requested is too large");
      AlignBits = (llvm::Log2_32(Align) + 1) << 20;
    }
    Characteristics =
        (Characteristics & ~COFF::IMAGE_SCN_ALIGN_MASK) | AlignBits;
  }
};

/// 32-bit TLS directory.
using coff_tls_directory32 = coff_tls_directory<support::little32_t>;
/// 64-bit TLS directory.
using coff_tls_directory64 = coff_tls_directory<support::little64_t>;

/// Frame-pointer type encoded in FPO data.
enum class frame_type : uint16_t {
  Fpo = 0, ///< Frame pointer omitted; use FPO data.
  Trap = 1, ///< Trap frame.
  Tss = 2, ///< TSS frame.
  NonFpo = 3, ///< Non-FPO frame with a frame pointer.
};

/// Code integrity fields within a load configuration directory.
struct coff_load_config_code_integrity {
  support::ulittle16_t Flags; ///< Code integrity flags.
  support::ulittle16_t Catalog; ///< Catalog index or identifier.
  support::ulittle32_t CatalogOffset; ///< Offset of the catalog data.
  support::ulittle32_t Reserved; ///< Reserved; must be zero.
};

/// 32-bit load config (IMAGE_LOAD_CONFIG_DIRECTORY32)
struct coff_load_configuration32 {
  support::ulittle32_t Size; ///< Size of this structure.
  support::ulittle32_t TimeDateStamp; ///< Time-date stamp.
  support::ulittle16_t MajorVersion; ///< Major version.
  support::ulittle16_t MinorVersion; ///< Minor version.
  support::ulittle32_t GlobalFlagsClear; ///< Global flags to clear.
  support::ulittle32_t GlobalFlagsSet; ///< Global flags to set.
  support::ulittle32_t CriticalSectionDefaultTimeout; ///< Default critical-section timeout.
  support::ulittle32_t DeCommitFreeBlockThreshold; ///< Free-block decommit threshold.
  support::ulittle32_t DeCommitTotalFreeThreshold; ///< Total-free decommit threshold.
  support::ulittle32_t LockPrefixTable; ///< VA of the lock-prefix table.
  support::ulittle32_t MaximumAllocationSize; ///< Maximum allocation size.
  support::ulittle32_t VirtualMemoryThreshold; ///< Virtual memory threshold.
  support::ulittle32_t ProcessAffinityMask; ///< Process affinity mask.
  support::ulittle32_t ProcessHeapFlags; ///< Process heap flags.
  support::ulittle16_t CSDVersion; ///< Service pack version.
  support::ulittle16_t DependentLoadFlags; ///< Dependent load flags.
  support::ulittle32_t EditList; ///< Reserved edit list pointer.
  support::ulittle32_t SecurityCookie; ///< VA of the security cookie.
  support::ulittle32_t SEHandlerTable; ///< VA of the SEH handler table.
  support::ulittle32_t SEHandlerCount; ///< Number of SEH handlers.

  // Added in MSVC 2015 for /guard:cf.
  support::ulittle32_t GuardCFCheckFunction; ///< VA of Guard CF check function pointer.
  support::ulittle32_t GuardCFCheckDispatch; ///< VA of Guard CF check dispatch pointer.
  support::ulittle32_t GuardCFFunctionTable; ///< VA of the Guard CF function table.
  support::ulittle32_t GuardCFFunctionCount; ///< Number of Guard CF function entries.
  support::ulittle32_t GuardFlags; ///< Guard flags (coff_guard_flags).

  // Added in MSVC 2017
  coff_load_config_code_integrity CodeIntegrity; ///< Code integrity configuration.
  support::ulittle32_t GuardAddressTakenIatEntryTable; ///< VA of address-taken IAT entry table.
  support::ulittle32_t GuardAddressTakenIatEntryCount; ///< Number of address-taken IAT entries.
  support::ulittle32_t GuardLongJumpTargetTable; ///< VA of long-jump target table.
  support::ulittle32_t GuardLongJumpTargetCount; ///< Number of long-jump targets.
  support::ulittle32_t DynamicValueRelocTable; ///< VA of the dynamic value relocation table.
  support::ulittle32_t CHPEMetadataPointer; ///< VA of CHPE metadata.
  support::ulittle32_t GuardRFFailureRoutine; ///< VA of Guard RF failure routine.
  support::ulittle32_t GuardRFFailureRoutineFunctionPointer; ///< VA of Guard RF failure routine pointer.
  support::ulittle32_t DynamicValueRelocTableOffset; ///< Section-relative offset of dynamic relocs.
  support::ulittle16_t DynamicValueRelocTableSection; ///< Section index of dynamic value relocs.
  support::ulittle16_t Reserved2; ///< Reserved; must be zero.
  support::ulittle32_t GuardRFVerifyStackPointerFunctionPointer; ///< VA of Guard RF stack-pointer verify pointer.
  support::ulittle32_t HotPatchTableOffset; ///< Offset of the hot-patch table.

  // Added in MSVC 2019
  support::ulittle32_t Reserved3; ///< Reserved; must be zero.
  support::ulittle32_t EnclaveConfigurationPointer; ///< VA of enclave configuration.
  support::ulittle32_t VolatileMetadataPointer; ///< VA of volatile metadata.
  support::ulittle32_t GuardEHContinuationTable; ///< VA of EH continuation table.
  support::ulittle32_t GuardEHContinuationCount; ///< Number of EH continuation entries.
  support::ulittle32_t GuardXFGCheckFunctionPointer; ///< VA of XFG check function pointer.
  support::ulittle32_t GuardXFGDispatchFunctionPointer; ///< VA of XFG dispatch function pointer.
  support::ulittle32_t GuardXFGTableDispatchFunctionPointer; ///< VA of XFG table-dispatch function pointer.
  support::ulittle32_t CastGuardOsDeterminedFailureMode; ///< CastGuard OS-determined failure mode.
};

/// 64-bit load config (IMAGE_LOAD_CONFIG_DIRECTORY64)
struct coff_load_configuration64 {
  support::ulittle32_t Size; ///< Size of this structure.
  support::ulittle32_t TimeDateStamp; ///< Time-date stamp.
  support::ulittle16_t MajorVersion; ///< Major version.
  support::ulittle16_t MinorVersion; ///< Minor version.
  support::ulittle32_t GlobalFlagsClear; ///< Global flags to clear.
  support::ulittle32_t GlobalFlagsSet; ///< Global flags to set.
  support::ulittle32_t CriticalSectionDefaultTimeout; ///< Default critical-section timeout.
  support::ulittle64_t DeCommitFreeBlockThreshold; ///< Free-block decommit threshold.
  support::ulittle64_t DeCommitTotalFreeThreshold; ///< Total-free decommit threshold.
  support::ulittle64_t LockPrefixTable; ///< VA of the lock-prefix table.
  support::ulittle64_t MaximumAllocationSize; ///< Maximum allocation size.
  support::ulittle64_t VirtualMemoryThreshold; ///< Virtual memory threshold.
  support::ulittle64_t ProcessAffinityMask; ///< Process affinity mask.
  support::ulittle32_t ProcessHeapFlags; ///< Process heap flags.
  support::ulittle16_t CSDVersion; ///< Service pack version.
  support::ulittle16_t DependentLoadFlags; ///< Dependent load flags.
  support::ulittle64_t EditList; ///< Reserved edit list pointer.
  support::ulittle64_t SecurityCookie; ///< VA of the security cookie.
  support::ulittle64_t SEHandlerTable; ///< VA of the SEH handler table.
  support::ulittle64_t SEHandlerCount; ///< Number of SEH handlers.

  // Added in MSVC 2015 for /guard:cf.
  support::ulittle64_t GuardCFCheckFunction; ///< VA of Guard CF check function pointer.
  support::ulittle64_t GuardCFCheckDispatch; ///< VA of Guard CF check dispatch pointer.
  support::ulittle64_t GuardCFFunctionTable; ///< VA of the Guard CF function table.
  support::ulittle64_t GuardCFFunctionCount; ///< Number of Guard CF function entries.
  support::ulittle32_t GuardFlags; ///< Guard flags (coff_guard_flags).

  // Added in MSVC 2017
  coff_load_config_code_integrity CodeIntegrity; ///< Code integrity configuration.
  support::ulittle64_t GuardAddressTakenIatEntryTable; ///< VA of address-taken IAT entry table.
  support::ulittle64_t GuardAddressTakenIatEntryCount; ///< Number of address-taken IAT entries.
  support::ulittle64_t GuardLongJumpTargetTable; ///< VA of long-jump target table.
  support::ulittle64_t GuardLongJumpTargetCount; ///< Number of long-jump targets.
  support::ulittle64_t DynamicValueRelocTable; ///< VA of the dynamic value relocation table.
  support::ulittle64_t CHPEMetadataPointer; ///< VA of CHPE metadata.
  support::ulittle64_t GuardRFFailureRoutine; ///< VA of Guard RF failure routine.
  support::ulittle64_t GuardRFFailureRoutineFunctionPointer; ///< VA of Guard RF failure routine pointer.
  support::ulittle32_t DynamicValueRelocTableOffset; ///< Section-relative offset of dynamic relocs.
  support::ulittle16_t DynamicValueRelocTableSection; ///< Section index of dynamic value relocs.
  support::ulittle16_t Reserved2; ///< Reserved; must be zero.
  support::ulittle64_t GuardRFVerifyStackPointerFunctionPointer; ///< VA of Guard RF stack-pointer verify pointer.
  support::ulittle32_t HotPatchTableOffset; ///< Offset of the hot-patch table.

  // Added in MSVC 2019
  support::ulittle32_t Reserved3; ///< Reserved; must be zero.
  support::ulittle64_t EnclaveConfigurationPointer; ///< VA of enclave configuration.
  support::ulittle64_t VolatileMetadataPointer; ///< VA of volatile metadata.
  support::ulittle64_t GuardEHContinuationTable; ///< VA of EH continuation table.
  support::ulittle64_t GuardEHContinuationCount; ///< Number of EH continuation entries.
  support::ulittle64_t GuardXFGCheckFunctionPointer; ///< VA of XFG check function pointer.
  support::ulittle64_t GuardXFGDispatchFunctionPointer; ///< VA of XFG dispatch function pointer.
  support::ulittle64_t GuardXFGTableDispatchFunctionPointer; ///< VA of XFG table-dispatch function pointer.
  support::ulittle64_t CastGuardOsDeterminedFailureMode; ///< CastGuard OS-determined failure mode.
};

/// CHPE / Arm64EC hybrid metadata referenced from the load config.
struct chpe_metadata {
  support::ulittle32_t Version; ///< CHPE metadata version.
  support::ulittle32_t CodeMap; ///< RVA of the code-map range table.
  support::ulittle32_t CodeMapCount; ///< Number of code-map entries.
  support::ulittle32_t CodeRangesToEntryPoints; ///< RVA of code-range-to-entry-point table.
  support::ulittle32_t RedirectionMetadata; ///< RVA of redirection metadata.
  support::ulittle32_t __os_arm64x_dispatch_call_no_redirect; ///< RVA of call dispatch without redirect.
  support::ulittle32_t __os_arm64x_dispatch_ret; ///< RVA of return dispatch helper.
  support::ulittle32_t __os_arm64x_dispatch_call; ///< RVA of call dispatch helper.
  support::ulittle32_t __os_arm64x_dispatch_icall; ///< RVA of indirect-call dispatch helper.
  support::ulittle32_t __os_arm64x_dispatch_icall_cfg; ///< RVA of CFG-aware indirect-call dispatch.
  support::ulittle32_t AlternateEntryPoint; ///< RVA of the alternate entry point.
  support::ulittle32_t AuxiliaryIAT; ///< RVA of the auxiliary import address table.
  support::ulittle32_t CodeRangesToEntryPointsCount; ///< Number of code-range-to-entry-point entries.
  support::ulittle32_t RedirectionMetadataCount; ///< Number of redirection metadata entries.
  support::ulittle32_t GetX64InformationFunctionPointer; ///< RVA of GetX64Information function pointer.
  support::ulittle32_t SetX64InformationFunctionPointer; ///< RVA of SetX64Information function pointer.
  support::ulittle32_t ExtraRFETable; ///< RVA of the extra runtime function entry table.
  support::ulittle32_t ExtraRFETableSize; ///< Size in bytes of the extra RFE table.
  support::ulittle32_t __os_arm64x_dispatch_fptr; ///< RVA of function-pointer dispatch helper.
  support::ulittle32_t AuxiliaryIATCopy; ///< RVA of the auxiliary IAT copy.

  // Added in CHPE metadata v2
  support::ulittle32_t AuxiliaryDelayloadIAT; ///< RVA of the auxiliary delay-load IAT.
  support::ulittle32_t AuxiliaryDelayloadIATCopy; ///< RVA of the auxiliary delay-load IAT copy.
  support::ulittle32_t HybridImageInfoBitfield; ///< Hybrid image info flags bitfield.
};

/// Architecture kind for a CHPE code-map range.
enum chpe_range_type {
  Arm64 = 0, ///< Native AArch64 code.
  Arm64EC = 1, ///< Arm64EC code.
  Amd64 = 2, ///< x64 code.
};

/// CHPE code-map range entry with packed start offset and type.
struct chpe_range_entry {
  support::ulittle32_t StartOffset; ///< Start offset with type in the low bits.
  support::ulittle32_t Length; ///< Length of the range in bytes.

  /// Mask for the range-type bits packed into StartOffset.
  ///
  /// The two low bits of StartOffset contain a range type.
  static constexpr uint32_t TypeMask = 3;

  /// Return the range start offset with the type bits cleared.
  /// @return The range start offset with the type bits cleared.
  uint32_t getStart() const { return StartOffset & ~TypeMask; }
  /// Return the chpe_range_type packed into StartOffset.
  /// @return The chpe_range_type packed into StartOffset.
  uint16_t getType() const { return StartOffset & TypeMask; }
};

/// CHPE mapping from a code range to an entry point.
struct chpe_code_range_entry {
  support::ulittle32_t StartRva; ///< RVA of the start of the code range.
  support::ulittle32_t EndRva; ///< RVA of the end of the code range.
  support::ulittle32_t EntryPoint; ///< RVA of the entry point for the range.
};

/// CHPE redirection from a source RVA to a destination RVA.
struct chpe_redirection_entry {
  support::ulittle32_t Source; ///< Source RVA to redirect from.
  support::ulittle32_t Destination; ///< Destination RVA to redirect to.
};

/// x64 RUNTIME_FUNCTION unwind entry.
struct coff_runtime_function_x64 {
  support::ulittle32_t BeginAddress; ///< RVA of the start of the function.
  support::ulittle32_t EndAddress; ///< RVA of the end of the function.
  support::ulittle32_t UnwindInformation; ///< RVA of the unwind info for the function.
};

/// Header of a PE base relocation block.
struct coff_base_reloc_block_header {
  support::ulittle32_t PageRVA; ///< RVA of the page that contains the fixups.
  support::ulittle32_t BlockSize; ///< Size in bytes of this block, including the header.
};

/// Single PE base relocation entry within a block.
struct coff_base_reloc_block_entry {
  support::ulittle16_t Data; ///< Packed relocation type (high 4) and offset (low 12).

  /// Return the base relocation type.
  /// @return The base relocation type.
  int getType() const { return Data >> 12; }
  /// Return the page-relative offset of the relocation.
  /// @return The page-relative offset of the relocation.
  int getOffset() const { return Data & ((1 << 12) - 1); }
};

/// PE resource directory entry.
struct coff_resource_dir_entry {
  /// Name or integer ID identifying this directory entry.
  union {
    support::ulittle32_t NameOffset; ///< Offset of the name string when named.
    support::ulittle32_t ID; ///< Integer resource ID when not named.
    /// Return the name-string offset with the high bit cleared.
    /// @return The name-string offset with the high bit cleared.
    uint32_t getNameOffset() const {
      return maskTrailingOnes<uint32_t>(31) & NameOffset;
    }
    // Even though the PE/COFF spec doesn't mention this, the high bit of a name
    // offset is set.
    /// Store name-string offset \p Offset with the high bit set.
    /// \param Offset Byte offset of the name string.
    void setNameOffset(uint32_t Offset) { NameOffset = Offset | (1 << 31); }
  } Identifier; ///< Entry name offset or integer ID.
  /// Offset of either a data entry or a subdirectory.
  union {
    support::ulittle32_t DataEntryOffset; ///< Offset of a resource data entry.
    support::ulittle32_t SubdirOffset; ///< Offset of a subdirectory (high bit set).

    /// Return true if this entry references a subdirectory.
    /// @return True if this entry references a subdirectory.
    bool isSubDir() const { return SubdirOffset >> 31; }
    /// Return the offset with the subdirectory high bit cleared.
    /// @return The offset with the subdirectory high bit cleared.
    uint32_t value() const {
      return maskTrailingOnes<uint32_t>(31) & SubdirOffset;
    }

  } Offset; ///< Data-entry or subdirectory offset.
};

/// PE resource data entry describing a leaf resource blob.
struct coff_resource_data_entry {
  support::ulittle32_t DataRVA; ///< RVA of the resource data.
  support::ulittle32_t DataSize; ///< Size in bytes of the resource data.
  support::ulittle32_t Codepage; ///< Code page used for string resources.
  support::ulittle32_t Reserved; ///< Reserved; must be zero.
};

/// PE resource directory table header.
struct coff_resource_dir_table {
  support::ulittle32_t Characteristics; ///< Reserved characteristics flags.
  support::ulittle32_t TimeDateStamp; ///< Time-date stamp of the resource data.
  support::ulittle16_t MajorVersion; ///< Major version number.
  support::ulittle16_t MinorVersion; ///< Minor version number.
  support::ulittle16_t NumberOfNameEntries; ///< Number of named directory entries.
  support::ulittle16_t NumberOfIDEntries; ///< Number of ID directory entries.
};

/// Header for a CodeView `/DEBUG:FASTLINK` `.debug$H` section.
struct debug_h_header {
  support::ulittle32_t Magic; ///< Magic identifying the `.debug$H` format.
  support::ulittle16_t Version; ///< Format version.
  support::ulittle16_t HashAlgorithm; ///< Hash algorithm used for global type hashes.
};

/// Header of the PE dynamic relocation table.
struct coff_dynamic_reloc_table {
  support::ulittle32_t Version; ///< Dynamic relocation table version.
  support::ulittle32_t Size; ///< Size in bytes of the table payload.
};

/// 32-bit dynamic relocation record (version 1).
struct coff_dynamic_relocation32 {
  support::ulittle32_t Symbol; ///< Symbol index or value associated with the fixups.
  support::ulittle32_t BaseRelocSize; ///< Size in bytes of the following base reloc data.
};

/// 64-bit dynamic relocation record (version 1).
struct coff_dynamic_relocation64 {
  support::ulittle64_t Symbol; ///< Symbol index or value associated with the fixups.
  support::ulittle32_t BaseRelocSize; ///< Size in bytes of the following base reloc data.
};

/// 32-bit dynamic relocation record (version 2).
struct coff_dynamic_relocation32_v2 {
  support::ulittle32_t HeaderSize; ///< Size in bytes of this header.
  support::ulittle32_t FixupInfoSize; ///< Size in bytes of the following fixup info.
  support::ulittle32_t Symbol; ///< Symbol index or value associated with the fixups.
  support::ulittle32_t SymbolGroup; ///< Symbol group identifier.
  support::ulittle32_t Flags; ///< Dynamic relocation flags.
};

/// 64-bit dynamic relocation record (version 2).
struct coff_dynamic_relocation64_v2 {
  support::ulittle32_t HeaderSize; ///< Size in bytes of this header.
  support::ulittle32_t FixupInfoSize; ///< Size in bytes of the following fixup info.
  support::ulittle64_t Symbol; ///< Symbol index or value associated with the fixups.
  support::ulittle32_t SymbolGroup; ///< Symbol group identifier.
  support::ulittle32_t Flags; ///< Dynamic relocation flags.
};

static constexpr StringLiteral kArm64ECSectionName = ".obj.arm64ec";

/// ObjectFile implementation for COFF and PE/COFF binaries.
class LLVM_ABI COFFObjectFile : public ObjectFile {
private:
  COFFObjectFile(MemoryBufferRef Object);

  friend class ImportDirectoryEntryRef;
  friend class ExportDirectoryEntryRef;
  const coff_file_header *COFFHeader;
  const coff_bigobj_file_header *COFFBigObjHeader;
  const pe32_header *PE32Header;
  const pe32plus_header *PE32PlusHeader;
  const data_directory *DataDirectory;
  const coff_section *SectionTable;
  const coff_symbol16 *SymbolTable16;
  const coff_symbol32 *SymbolTable32;
  const char *StringTable;
  uint32_t StringTableSize;
  const coff_import_directory_table_entry *ImportDirectory;
  const delay_import_directory_table_entry *DelayImportDirectory;
  uint32_t NumberOfDelayImportDirectory;
  const export_directory_table_entry *ExportDirectory;
  const coff_base_reloc_block_header *BaseRelocHeader;
  const coff_base_reloc_block_header *BaseRelocEnd;
  const debug_directory *DebugDirectoryBegin;
  const debug_directory *DebugDirectoryEnd;
  const coff_tls_directory32 *TLSDirectory32;
  const coff_tls_directory64 *TLSDirectory64;
  // Either coff_load_configuration32 or coff_load_configuration64.
  const void *LoadConfig = nullptr;
  const chpe_metadata *CHPEMetadata = nullptr;
  const coff_dynamic_reloc_table *DynamicRelocTable = nullptr;

  Expected<StringRef> getString(uint32_t offset) const;

  template <typename coff_symbol_type>
  const coff_symbol_type *toSymb(DataRefImpl Symb) const;
  const coff_section *toSec(DataRefImpl Sec) const;
  const coff_relocation *toRel(DataRefImpl Rel) const;

  // Finish initializing the object and return success or an error.
  Error initialize();

  Error initSymbolTablePtr();
  Error initImportTablePtr();
  Error initDelayImportTablePtr();
  Error initExportTablePtr();
  Error initBaseRelocPtr();
  Error initDebugDirectoryPtr();
  Error initTLSDirectoryPtr();
  Error initLoadConfigPtr();
  Error initDynamicRelocPtr(uint32_t SectionIndex, uint32_t SectionOffset);

public:
  /// Create a COFFObjectFile from memory buffer \p Object.
  /// \param Object Memory buffer containing a COFF/PE object or image.
  /// @return The new COFFObjectFile, or an error if parsing fails.
  static Expected<std::unique_ptr<COFFObjectFile>>
  create(MemoryBufferRef Object);

  /// Return a pointer to the start of the COFF symbol table, or 0 if none.
  /// @return A pointer to the start of the COFF symbol table, or 0 if none.
  uintptr_t getSymbolTable() const {
    if (SymbolTable16)
      return reinterpret_cast<uintptr_t>(SymbolTable16);
    if (SymbolTable32)
      return reinterpret_cast<uintptr_t>(SymbolTable32);
    return uintptr_t(0);
  }

  /// Return the COFF string table contents.
  /// @return The COFF string table contents.
  StringRef getStringTable() const {
    return StringRef(StringTable, StringTableSize);
  }

  /// Return the target machine type, adjusted for CHPE/Arm64X images.
  /// @return The target machine type, adjusted for CHPE/Arm64X images.
  uint16_t getMachine() const {
    if (COFFHeader) {
      if (CHPEMetadata) {
        switch (COFFHeader->Machine) {
        case COFF::IMAGE_FILE_MACHINE_AMD64:
          return COFF::IMAGE_FILE_MACHINE_ARM64EC;
        case COFF::IMAGE_FILE_MACHINE_ARM64:
          return COFF::IMAGE_FILE_MACHINE_ARM64X;
        }
      }
      return COFFHeader->Machine;
    }
    if (COFFBigObjHeader)
      return COFFBigObjHeader->Machine;
    llvm_unreachable("no COFF header!");
  }

  /// Return the size of the optional PE header, or 0 if none.
  /// @return The size of the optional PE header, or 0 if none.
  uint16_t getSizeOfOptionalHeader() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0
                                           : COFFHeader->SizeOfOptionalHeader;
    // bigobj doesn't have this field.
    if (COFFBigObjHeader)
      return 0;
    llvm_unreachable("no COFF header!");
  }

  /// Return the COFF characteristics flags, or 0 for bigobj/import libraries.
  /// @return The COFF characteristics flags, or 0 for bigobj/import libraries.
  uint16_t getCharacteristics() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0 : COFFHeader->Characteristics;
    // bigobj doesn't have characteristics to speak of,
    // editbin will silently lie to you if you attempt to set any.
    if (COFFBigObjHeader)
      return 0;
    llvm_unreachable("no COFF header!");
  }

  /// Return the COFF time-date stamp.
  /// @return The COFF time-date stamp.
  uint32_t getTimeDateStamp() const {
    if (COFFHeader)
      return COFFHeader->TimeDateStamp;
    if (COFFBigObjHeader)
      return COFFBigObjHeader->TimeDateStamp;
    llvm_unreachable("no COFF header!");
  }

  /// Return the number of sections in this object.
  /// @return The number of sections in this object.
  uint32_t getNumberOfSections() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0 : COFFHeader->NumberOfSections;
    if (COFFBigObjHeader)
      return COFFBigObjHeader->NumberOfSections;
    llvm_unreachable("no COFF header!");
  }

  /// Return the file offset of the symbol table.
  /// @return The file offset of the symbol table.
  uint32_t getPointerToSymbolTable() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0
                                           : COFFHeader->PointerToSymbolTable;
    if (COFFBigObjHeader)
      return COFFBigObjHeader->PointerToSymbolTable;
    llvm_unreachable("no COFF header!");
  }

  /// Return the symbol count from the COFF header (including aux entries).
  /// @return The symbol count from the COFF header (including aux entries).
  uint32_t getRawNumberOfSymbols() const {
    if (COFFHeader)
      return COFFHeader->isImportLibrary() ? 0 : COFFHeader->NumberOfSymbols;
    if (COFFBigObjHeader)
      return COFFBigObjHeader->NumberOfSymbols;
    llvm_unreachable("no COFF header!");
  }

  /// Return the number of symbols, or 0 if no symbol table is mapped.
  /// @return The number of symbols, or 0 if no symbol table is mapped.
  uint32_t getNumberOfSymbols() const {
    if (!SymbolTable16 && !SymbolTable32)
      return 0;
    return getRawNumberOfSymbols();
  }

  /// Return the size in bytes of the string table.
  /// @return The size in bytes of the string table.
  uint32_t getStringTableSize() const { return StringTableSize; }

  /// Return the export directory table, if present.
  /// @return The export directory table, if present.
  const export_directory_table_entry *getExportTable() const {
    return ExportDirectory;
  }

  /// Return the 32-bit load configuration directory.
  /// @return The 32-bit load configuration directory.
  const coff_load_configuration32 *getLoadConfig32() const {
    assert(!is64());
    return reinterpret_cast<const coff_load_configuration32 *>(LoadConfig);
  }

  /// Return the 64-bit load configuration directory.
  /// @return The 64-bit load configuration directory.
  const coff_load_configuration64 *getLoadConfig64() const {
    assert(is64());
    return reinterpret_cast<const coff_load_configuration64 *>(LoadConfig);
  }

  /// Return CHPE/Arm64EC metadata, if present.
  /// @return CHPE/Arm64EC metadata, if present.
  const chpe_metadata *getCHPEMetadata() const { return CHPEMetadata; }
  /// Return the dynamic relocation table header, if present.
  /// @return The dynamic relocation table header, if present.
  const coff_dynamic_reloc_table *getDynamicRelocTable() const {
    return DynamicRelocTable;
  }

  /// Return a human-readable name for relocation type \p Type.
  /// \param Type COFF relocation type code.
  /// @return A human-readable name for relocation type \p Type.
  StringRef getRelocationTypeName(uint16_t Type) const;

protected:
  /// Advance symbol iterator \p Symb to the next symbol.
  /// \param Symb Opaque symbol iterator state to advance.
  void moveSymbolNext(DataRefImpl &Symb) const override;
  /// Return the name of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The name of symbol \p Symb, or an error on failure.
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  /// Return the virtual address of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The virtual address of symbol \p Symb, or an error on failure.
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  /// Return the alignment of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The alignment of symbol \p Symb.
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  /// Return the raw value field of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The raw value field of symbol \p Symb.
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  /// Return the size of common symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The size of common symbol \p Symb.
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  /// Return SymbolRef flags for symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return SymbolRef flags for symbol \p Symb, or an error on failure.
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  /// Return the SymbolRef type of symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The SymbolRef type of symbol \p Symb, or an error on failure.
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  /// Return the section containing symbol \p Symb.
  /// \param Symb Opaque symbol reference.
  /// @return The section containing symbol \p Symb, or an error on failure.
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;
  /// Advance section iterator \p Sec to the next section.
  /// \param Sec Opaque section iterator state to advance.
  void moveSectionNext(DataRefImpl &Sec) const override;
  /// Return the name of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The name of section \p Sec, or an error on failure.
  Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  /// Return the address of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The address of section \p Sec.
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  /// Return the zero-based index of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The zero-based index of section \p Sec.
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  /// Return the size of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The size of section \p Sec.
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  /// Return the contents of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The contents of section \p Sec, or an error on failure.
  Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;
  /// Return the alignment of section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return The alignment of section \p Sec.
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  /// Return true if section \p Sec is compressed.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec is compressed.
  bool isSectionCompressed(DataRefImpl Sec) const override;
  /// Return true if section \p Sec contains code.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec contains code.
  bool isSectionText(DataRefImpl Sec) const override;
  /// Return true if section \p Sec contains initialized data.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec contains initialized data.
  bool isSectionData(DataRefImpl Sec) const override;
  /// Return true if section \p Sec is BSS/uninitialized.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec is BSS/uninitialized.
  bool isSectionBSS(DataRefImpl Sec) const override;
  /// Return true if section \p Sec has no file contents (virtual).
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec has no file contents (virtual).
  bool isSectionVirtual(DataRefImpl Sec) const override;
  /// Return true if section \p Sec is a debug section.
  /// \param Sec Opaque section reference.
  /// @return True if section \p Sec is a debug section.
  bool isDebugSection(DataRefImpl Sec) const override;
  /// Return an iterator to the first relocation in section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return An iterator to the first relocation in section \p Sec.
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  /// Return an iterator past the last relocation in section \p Sec.
  /// \param Sec Opaque section reference.
  /// @return An iterator past the last relocation in section \p Sec.
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  /// Advance relocation iterator \p Rel to the next relocation.
  /// \param Rel Opaque relocation iterator state to advance.
  void moveRelocationNext(DataRefImpl &Rel) const override;
  /// Return the section-relative offset of relocation \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The section-relative offset of relocation \p Rel.
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  /// Return the symbol referenced by relocation \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The symbol referenced by relocation \p Rel.
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  /// Return the relocation type of \p Rel.
  /// \param Rel Opaque relocation reference.
  /// @return The relocation type of \p Rel.
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  /// Append the relocation type name of \p Rel to \p Result.
  /// \param Rel Opaque relocation reference.
  /// \param Result Output buffer for the type name.
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

public:
  /// Return an iterator to the first symbol.
  /// @return An iterator to the first symbol.
  basic_symbol_iterator symbol_begin() const override;
  /// Return an iterator past the last symbol.
  /// @return An iterator past the last symbol.
  basic_symbol_iterator symbol_end() const override;
  /// Return an iterator to the first section.
  /// @return An iterator to the first section.
  section_iterator section_begin() const override;
  /// Return an iterator past the last section.
  /// @return An iterator past the last section.
  section_iterator section_end() const override;

  /// Return whether this object uses 64-bit addresses (always false for COFF).
  /// @return Whether this object uses 64-bit addresses (always false for COFF).
  bool is64Bit() const override { return false; }

  /// Return the COFF section header for \p Section.
  /// \param Section Generic section reference.
  /// @return The COFF section header for \p Section.
  const coff_section *getCOFFSection(const SectionRef &Section) const;
  /// Return a COFF symbol reference for data-ref \p Ref.
  /// \param Ref Opaque symbol data reference.
  /// @return A COFF symbol reference for data-ref \p Ref.
  COFFSymbolRef getCOFFSymbol(const DataRefImpl &Ref) const;
  /// Return a COFF symbol reference for \p Symbol.
  /// \param Symbol Generic symbol reference.
  /// @return A COFF symbol reference for \p Symbol.
  COFFSymbolRef getCOFFSymbol(const SymbolRef &Symbol) const;
  /// Return the COFF relocation record for \p Reloc.
  /// \param Reloc Generic relocation reference.
  /// @return The COFF relocation record for \p Reloc.
  const coff_relocation *getCOFFRelocation(const RelocationRef &Reloc) const;
  /// Return the one-based COFF section number for \p Sec.
  /// \param Sec Section whose ID is requested.
  /// @return The one-based COFF section number for \p Sec.
  unsigned getSectionID(SectionRef Sec) const;
  /// Return the one-based section number owning \p Sym.
  /// \param Sym Symbol whose section ID is requested.
  /// @return The one-based section number owning \p Sym.
  unsigned getSymbolSectionID(SymbolRef Sym) const;

  /// Return the address size in bytes for this object.
  /// @return The address size in bytes for this object.
  uint8_t getBytesInAddress() const override;
  /// Return a human-readable file format name.
  /// @return A human-readable file format name.
  StringRef getFileFormatName() const override;
  /// Return the architecture of this object.
  /// @return The architecture of this object.
  Triple::ArchType getArch() const override;
  /// Return the image entry-point address, if available.
  /// @return The image entry-point address, if available, or an error on failure.
  Expected<uint64_t> getStartAddress() const override;
  /// Return subtarget features (always empty for COFF).
  /// @return Subtarget features (always empty for COFF), or an error on failure.
  Expected<SubtargetFeatures> getFeatures() const override {
    return SubtargetFeatures();
  }
  /// Return a view of the hybrid (Arm64EC/X) companion object, if any.
  /// @return A view of the hybrid (Arm64EC/X) companion object, if any.
  std::unique_ptr<MemoryBuffer> getHybridObjectView() const;
  /// Return the hybrid object section buffer, if present.
  /// @return The hybrid object section buffer, if present.
  std::optional<MemoryBufferRef> findHybridObjectSection() const;
  /// Return a copy of this object with the hybrid section removed.
  /// @return A copy of this object with the hybrid section removed.
  std::unique_ptr<MemoryBuffer> stripHybridSection() const;

  /// Return an iterator to the first import directory entry.
  /// @return An iterator to the first import directory entry.
  import_directory_iterator import_directory_begin() const;
  /// Return an iterator past the last import directory entry.
  /// @return An iterator past the last import directory entry.
  import_directory_iterator import_directory_end() const;
  /// Return an iterator to the first delay-import directory entry.
  /// @return An iterator to the first delay-import directory entry.
  delay_import_directory_iterator delay_import_directory_begin() const;
  /// Return an iterator past the last delay-import directory entry.
  /// @return An iterator past the last delay-import directory entry.
  delay_import_directory_iterator delay_import_directory_end() const;
  /// Return an iterator to the first export directory entry.
  /// @return An iterator to the first export directory entry.
  export_directory_iterator export_directory_begin() const;
  /// Return an iterator past the last export directory entry.
  /// @return An iterator past the last export directory entry.
  export_directory_iterator export_directory_end() const;
  /// Return an iterator to the first base relocation entry.
  /// @return An iterator to the first base relocation entry.
  base_reloc_iterator base_reloc_begin() const;
  /// Return an iterator past the last base relocation entry.
  /// @return An iterator past the last base relocation entry.
  base_reloc_iterator base_reloc_end() const;
  /// Return an iterator to the first dynamic relocation entry.
  /// @return An iterator to the first dynamic relocation entry.
  dynamic_reloc_iterator dynamic_reloc_begin() const;
  /// Return an iterator past the last dynamic relocation entry.
  /// @return An iterator past the last dynamic relocation entry.
  dynamic_reloc_iterator dynamic_reloc_end() const;
  /// Return a pointer to the first debug directory entry.
  /// @return A pointer to the first debug directory entry.
  const debug_directory *debug_directory_begin() const {
    return DebugDirectoryBegin;
  }
  /// Return a pointer past the last debug directory entry.
  /// @return A pointer past the last debug directory entry.
  const debug_directory *debug_directory_end() const {
    return DebugDirectoryEnd;
  }

  /// Return the range of import directory entries.
  /// @return The range of import directory entries.
  iterator_range<import_directory_iterator> import_directories() const;
  /// Return the range of delay-import directory entries.
  /// @return The range of delay-import directory entries.
  iterator_range<delay_import_directory_iterator>
      delay_import_directories() const;
  /// Return the range of export directory entries.
  /// @return The range of export directory entries.
  iterator_range<export_directory_iterator> export_directories() const;
  /// Return the range of base relocation entries.
  /// @return The range of base relocation entries.
  iterator_range<base_reloc_iterator> base_relocs() const;
  /// Return the range of dynamic relocation entries.
  /// @return The range of dynamic relocation entries.
  iterator_range<dynamic_reloc_iterator> dynamic_relocs() const;
  /// Return the range of debug directory entries.
  /// @return The range of debug directory entries.
  iterator_range<const debug_directory *> debug_directories() const {
    return make_range(debug_directory_begin(), debug_directory_end());
  }

  /// Return the 32-bit TLS directory, if present.
  /// @return The 32-bit TLS directory, if present.
  const coff_tls_directory32 *getTLSDirectory32() const {
    return TLSDirectory32;
  }
  /// Return the 64-bit TLS directory, if present.
  /// @return The 64-bit TLS directory, if present.
  const coff_tls_directory64 *getTLSDirectory64() const {
    return TLSDirectory64;
  }

  /// Return the DOS header for PE images, or nullptr for pure COFF objects.
  /// @return The DOS header for PE images, or nullptr for pure COFF objects.
  const dos_header *getDOSHeader() const {
    if (!PE32Header && !PE32PlusHeader)
      return nullptr;
    return reinterpret_cast<const dos_header *>(base());
  }

  /// Return the standard COFF file header, if present.
  /// @return The standard COFF file header, if present.
  const coff_file_header *getCOFFHeader() const { return COFFHeader; }
  /// Return the bigobj COFF file header, if present.
  /// @return The bigobj COFF file header, if present.
  const coff_bigobj_file_header *getCOFFBigObjHeader() const {
    return COFFBigObjHeader;
  }
  /// Return the PE32 optional header, if present.
  /// @return The PE32 optional header, if present.
  const pe32_header *getPE32Header() const { return PE32Header; }
  /// Return the PE32+ optional header, if present.
  /// @return The PE32+ optional header, if present.
  const pe32plus_header *getPE32PlusHeader() const { return PE32PlusHeader; }

  /// Return data directory entry \p index, or nullptr if absent/out of range.
  /// \param index Zero-based data directory index.
  /// @return Data directory entry \p index, or nullptr if absent/out of range.
  const data_directory *getDataDirectory(uint32_t index) const;
  /// Return the section header for one-based section number \p index.
  /// \param index One-based COFF section number.
  /// @return The section header for one-based section number \p index, or an error on failure.
  Expected<const coff_section *> getSection(int32_t index) const;

  /// Return COFF symbol table entry \p index.
  /// \param index Zero-based symbol table index.
  /// @return COFF symbol table entry \p index, or an error on failure.
  Expected<COFFSymbolRef> getSymbol(uint32_t index) const {
    if (index >= getNumberOfSymbols())
      return errorCodeToError(object_error::parse_failed);
    if (SymbolTable16)
      return COFFSymbolRef(SymbolTable16 + index);
    if (SymbolTable32)
      return COFFSymbolRef(SymbolTable32 + index);
    return errorCodeToError(object_error::parse_failed);
  }

  /// Interpret symbol table entry \p index as auxiliary symbol type \p T.
  /// \param index Zero-based symbol table index.
  /// \param Res Set to the reinterpreted auxiliary symbol pointer.
  /// @return Success, or an error if the symbol index is invalid.
  template <typename T>
  Error getAuxSymbol(uint32_t index, const T *&Res) const {
    Expected<COFFSymbolRef> S = getSymbol(index);
    if (Error E = S.takeError())
      return E;
    Res = reinterpret_cast<const T *>(S->getRawPtr());
    return Error::success();
  }

  /// Return the name of COFF symbol \p Symbol.
  /// \param Symbol Symbol whose name is requested.
  /// @return The name of COFF symbol \p Symbol, or an error on failure.
  Expected<StringRef> getSymbolName(COFFSymbolRef Symbol) const;
  /// Return the name of generic COFF symbol \p Symbol.
  /// \param Symbol Generic symbol record whose name is requested.
  /// @return The name of generic COFF symbol \p Symbol, or an error on failure.
  Expected<StringRef> getSymbolName(const coff_symbol_generic *Symbol) const;

  /// Return the auxiliary symbol data bytes following \p Symbol.
  /// \param Symbol Symbol whose auxiliary records are requested.
  /// @return The auxiliary symbol data bytes following \p Symbol.
  ArrayRef<uint8_t> getSymbolAuxData(COFFSymbolRef Symbol) const;

  /// Return the zero-based index of \p Symbol in the symbol table.
  /// \param Symbol Symbol whose index is requested.
  /// @return The zero-based index of \p Symbol in the symbol table.
  uint32_t getSymbolIndex(COFFSymbolRef Symbol) const;

  /// Return the size in bytes of one symbol table entry.
  /// @return The size in bytes of one symbol table entry.
  size_t getSymbolTableEntrySize() const {
    if (COFFHeader)
      return sizeof(coff_symbol16);
    if (COFFBigObjHeader)
      return sizeof(coff_symbol32);
    llvm_unreachable("null symbol table pointer!");
  }

  /// Return the relocation entries for section \p Sec.
  /// \param Sec Section whose relocations are requested.
  /// @return The relocation entries for section \p Sec.
  ArrayRef<coff_relocation> getRelocations(const coff_section *Sec) const;

  /// Return the name of COFF section \p Sec.
  /// \param Sec Section header to name.
  /// @return The name of COFF section \p Sec, or an error on failure.
  Expected<StringRef> getSectionName(const coff_section *Sec) const;
  /// Return the size in bytes of COFF section \p Sec.
  /// \param Sec Section header whose size is requested.
  /// @return The size in bytes of COFF section \p Sec.
  uint64_t getSectionSize(const coff_section *Sec) const;
  /// Return the contents of COFF section \p Sec.
  /// \param Sec Section header whose contents are requested.
  /// \param Res Set to the section bytes on success.
  /// @return Success, or an error on failure.
  Error getSectionContents(const coff_section *Sec,
                           ArrayRef<uint8_t> &Res) const;

  /// Return the preferred image base address.
  /// @return The preferred image base address.
  uint64_t getImageBase() const;
  /// Translate virtual address \p VA to a pointer into the mapped file.
  /// \param VA Absolute virtual address to resolve.
  /// \param Res Set to the corresponding file pointer on success.
  /// @return Success, or an error if \p VA cannot be resolved.
  Error getVaPtr(uint64_t VA, uintptr_t &Res) const;
  /// Translate relative virtual address \p Rva to a pointer into the mapped file.
  /// \param Rva Relative virtual address to resolve.
  /// \param Res Set to the corresponding file pointer on success.
  /// \param ErrorContext Optional label included in error messages.
  /// @return Success, or an error if \p Rva cannot be resolved.
  Error getRvaPtr(uint32_t Rva, uintptr_t &Res,
                  const char *ErrorContext = nullptr) const;

  /// Return the bytes at RVA \p RVA of length \p Size, or an error if invalid.
  ///
  /// Given an RVA base and size, returns a valid array of bytes or an error
  /// code if the RVA and size is not contained completely within a valid
  /// section.
  /// \param RVA Relative virtual address of the first byte.
  /// \param Size Number of bytes to read.
  /// \param Contents Set to the requested byte range on success.
  /// \param ErrorContext Optional label included in error messages.
  /// @return Success, or an error if the RVA and size are not contained in a
  /// valid section.
  Error getRvaAndSizeAsBytes(uint32_t RVA, uint32_t Size,
                             ArrayRef<uint8_t> &Contents,
                             const char *ErrorContext = nullptr) const;

  /// Resolve the import hint/name pair at RVA \p Rva.
  /// \param Rva RVA of the hint/name table entry.
  /// \param Hint Set to the import hint on success.
  /// \param Name Set to the import name on success.
  /// @return Success, or an error if the hint/name entry is invalid.
  Error getHintName(uint32_t Rva, uint16_t &Hint,
                              StringRef &Name) const;

  /// Get PDB information from a CodeView debug directory entry.
  /// \param DebugDir Debug directory entry describing the CodeView payload.
  /// \param Info Set to the parsed debug info, or nullptr if absent.
  /// \param PDBFileName Set to the PDB path, or empty if absent.
  /// @return Success, or an error if the debug info is corrupt.
  Error getDebugPDBInfo(const debug_directory *DebugDir,
                        const codeview::DebugInfo *&Info,
                        StringRef &PDBFileName) const;

  /// Get PDB information from this executable's CodeView debug directory.
  ///
  /// If the information is not present, Info will be set to nullptr and
  /// PDBFileName will be empty. An error is returned only on corrupt object
  /// files. Convenience accessor that can be used if the debug directory is not
  /// already handy.
  /// \param Info Set to the parsed debug info, or nullptr if absent.
  /// \param PDBFileName Set to the PDB path, or empty if absent.
  /// @return Success, or an error if the debug info is corrupt.
  Error getDebugPDBInfo(const codeview::DebugInfo *&Info,
                        StringRef &PDBFileName) const;

  /// Return true if this is a relocatable COFF object (not an image).
  /// @return True if this is a relocatable COFF object (not an image).
  bool isRelocatableObject() const override;
  /// Return true if this is a PE32+ (64-bit) image.
  /// @return True if this is a PE32+ (64-bit) image.
  bool is64() const { return PE32PlusHeader; }

  /// Map a COFF debug section name to the canonical DWARF-style name.
  /// \param Name Section name from the object file.
  /// @return The canonical debug section name.
  StringRef mapDebugSectionName(StringRef Name) const override;

  /// Return true if \p v is a COFF object file.
  /// \param v Binary to test.
  /// @return True if \p v is a COFF object file.
  static bool classof(const Binary *v) { return v->isCOFF(); }
};

/// Reference to an entry in the COFF import directory.
class ImportDirectoryEntryRef {
public:
  /// Construct an empty import-directory entry reference.
  ImportDirectoryEntryRef() = default;
  /// Construct a reference to import entry \p I in \p Table.
  /// \param Table Import directory table.
  /// \param I Zero-based entry index.
  /// \param Owner COFF object that owns the import directory.
  ImportDirectoryEntryRef(const coff_import_directory_table_entry *Table,
                          uint32_t I, const COFFObjectFile *Owner)
      : ImportTable(Table), Index(I), OwningObject(Owner) {}

  /// Return true if this reference equals \p Other.
  /// \param Other Import entry reference to compare against.
  /// @return True if this reference equals \p Other.
  LLVM_ABI bool operator==(const ImportDirectoryEntryRef &Other) const;
  /// Advance to the next import directory entry.
  LLVM_ABI void moveNext();

  /// Return an iterator to the first imported symbol (via the IAT).
  /// @return An iterator to the first imported symbol (via the IAT).
  LLVM_ABI imported_symbol_iterator imported_symbol_begin() const;
  /// Return an iterator past the last imported symbol (via the IAT).
  /// @return An iterator past the last imported symbol (via the IAT).
  LLVM_ABI imported_symbol_iterator imported_symbol_end() const;
  /// Return the range of symbols imported from this DLL (via the IAT).
  /// @return The range of symbols imported from this DLL (via the IAT).
  LLVM_ABI iterator_range<imported_symbol_iterator> imported_symbols() const;

  /// Return an iterator to the first import lookup table symbol.
  /// @return An iterator to the first import lookup table symbol.
  LLVM_ABI imported_symbol_iterator lookup_table_begin() const;
  /// Return an iterator past the last import lookup table symbol.
  /// @return An iterator past the last import lookup table symbol.
  LLVM_ABI imported_symbol_iterator lookup_table_end() const;
  /// Return the range of symbols in the import lookup table.
  /// @return The range of symbols in the import lookup table.
  LLVM_ABI iterator_range<imported_symbol_iterator>
  lookup_table_symbols() const;

  /// Return the imported DLL name.
  /// \param Result Set to the DLL name on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getName(StringRef &Result) const;
  /// Return the RVA of the import lookup table.
  /// \param Result Set to the ILT RVA on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getImportLookupTableRVA(uint32_t &Result) const;
  /// Return the RVA of the import address table.
  /// \param Result Set to the IAT RVA on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getImportAddressTableRVA(uint32_t &Result) const;

  /// Return the underlying import directory table entry.
  /// \param Result Set to the table entry pointer on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error
  getImportTableEntry(const coff_import_directory_table_entry *&Result) const;

private:
  const coff_import_directory_table_entry *ImportTable;
  uint32_t Index;
  const COFFObjectFile *OwningObject = nullptr;
};

/// Reference to an entry in the delay-load import directory.
class DelayImportDirectoryEntryRef {
public:
  /// Construct an empty delay-import directory entry reference.
  DelayImportDirectoryEntryRef() = default;
  /// Construct a reference to delay-import entry \p I in table \p T.
  /// \param T Delay-import directory table.
  /// \param I Zero-based entry index.
  /// \param Owner COFF object that owns the delay-import directory.
  DelayImportDirectoryEntryRef(const delay_import_directory_table_entry *T,
                               uint32_t I, const COFFObjectFile *Owner)
      : Table(T), Index(I), OwningObject(Owner) {}

  /// Return true if this reference equals \p Other.
  /// \param Other Delay-import entry reference to compare against.
  /// @return True if this reference equals \p Other.
  LLVM_ABI bool operator==(const DelayImportDirectoryEntryRef &Other) const;
  /// Advance to the next delay-import directory entry.
  LLVM_ABI void moveNext();

  /// Return an iterator to the first delay-imported symbol.
  /// @return An iterator to the first delay-imported symbol.
  LLVM_ABI imported_symbol_iterator imported_symbol_begin() const;
  /// Return an iterator past the last delay-imported symbol.
  /// @return An iterator past the last delay-imported symbol.
  LLVM_ABI imported_symbol_iterator imported_symbol_end() const;
  /// Return the range of symbols delay-imported from this DLL.
  /// @return The range of symbols delay-imported from this DLL.
  LLVM_ABI iterator_range<imported_symbol_iterator> imported_symbols() const;

  /// Return the delay-loaded DLL name.
  /// \param Result Set to the DLL name on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getName(StringRef &Result) const;
  /// Return the underlying delay-import directory table entry.
  /// \param Result Set to the table entry pointer on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error
  getDelayImportTable(const delay_import_directory_table_entry *&Result) const;
  /// Return the import address at index \p AddrIndex in the delay IAT.
  /// \param AddrIndex Zero-based index into the delay import address table.
  /// \param Result Set to the import address on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getImportAddress(int AddrIndex, uint64_t &Result) const;

private:
  const delay_import_directory_table_entry *Table;
  uint32_t Index;
  const COFFObjectFile *OwningObject = nullptr;
};

/// Reference to an entry in the COFF export directory.
class ExportDirectoryEntryRef {
public:
  /// Construct an empty export-directory entry reference.
  ExportDirectoryEntryRef() = default;
  /// Construct a reference to export entry \p I in \p Table.
  /// \param Table Export directory table.
  /// \param I Zero-based export name / address index.
  /// \param Owner COFF object that owns the export directory.
  ExportDirectoryEntryRef(const export_directory_table_entry *Table, uint32_t I,
                          const COFFObjectFile *Owner)
      : ExportTable(Table), Index(I), OwningObject(Owner) {}

  /// Return true if this reference equals \p Other.
  /// \param Other Export entry reference to compare against.
  /// @return True if this reference equals \p Other.
  LLVM_ABI bool operator==(const ExportDirectoryEntryRef &Other) const;
  /// Advance to the next export directory entry.
  LLVM_ABI void moveNext();

  /// Return the exporting DLL name.
  /// \param Result Set to the DLL name on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getDllName(StringRef &Result) const;
  /// Return the ordinal base for this export directory.
  /// \param Result Set to the ordinal base on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getOrdinalBase(uint32_t &Result) const;
  /// Return the ordinal of this export.
  /// \param Result Set to the export ordinal on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getOrdinal(uint32_t &Result) const;
  /// Return the RVA of the exported symbol.
  /// \param Result Set to the export RVA on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getExportRVA(uint32_t &Result) const;
  /// Return the exported symbol name.
  /// \param Result Set to the symbol name on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getSymbolName(StringRef &Result) const;

  /// Return whether this export is a forwarder.
  /// \param Result Set to true if the export is forwarded.
  /// @return Success, or an error on failure.
  LLVM_ABI Error isForwarder(bool &Result) const;
  /// Return the forwarder target string.
  /// \param Result Set to the forwarder string on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getForwardTo(StringRef &Result) const;

private:
  const export_directory_table_entry *ExportTable;
  uint32_t Index;
  const COFFObjectFile *OwningObject = nullptr;
};

/// Reference to a symbol entry in an import lookup or address table.
class ImportedSymbolRef {
public:
  /// Construct an empty imported-symbol reference.
  ImportedSymbolRef() = default;
  /// Construct a reference to 32-bit import entry \p Entry at index \p I.
  /// \param Entry Pointer to the 32-bit import lookup table.
  /// \param I Zero-based index within the table.
  /// \param Owner COFF object that owns the import tables.
  ImportedSymbolRef(const import_lookup_table_entry32 *Entry, uint32_t I,
                    const COFFObjectFile *Owner)
      : Entry32(Entry), Entry64(nullptr), Index(I), OwningObject(Owner) {}
  /// Construct a reference to 64-bit import entry \p Entry at index \p I.
  /// \param Entry Pointer to the 64-bit import lookup table.
  /// \param I Zero-based index within the table.
  /// \param Owner COFF object that owns the import tables.
  ImportedSymbolRef(const import_lookup_table_entry64 *Entry, uint32_t I,
                    const COFFObjectFile *Owner)
      : Entry32(nullptr), Entry64(Entry), Index(I), OwningObject(Owner) {}

  /// Return true if this reference equals \p Other.
  /// \param Other Imported-symbol reference to compare against.
  /// @return True if this reference equals \p Other.
  LLVM_ABI bool operator==(const ImportedSymbolRef &Other) const;
  /// Advance to the next imported symbol entry.
  LLVM_ABI void moveNext();

  /// Return the imported symbol name when importing by name.
  /// \param Result Set to the symbol name on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getSymbolName(StringRef &Result) const;
  /// Return whether this entry imports by ordinal.
  /// \param Result Set to true if the entry is an ordinal import.
  /// @return Success, or an error on failure.
  LLVM_ABI Error isOrdinal(bool &Result) const;
  /// Return the import ordinal when importing by ordinal.
  /// \param Result Set to the ordinal on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getOrdinal(uint16_t &Result) const;
  /// Return the hint/name table RVA when importing by name.
  /// \param Result Set to the hint/name RVA on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getHintNameRVA(uint32_t &Result) const;

private:
  const import_lookup_table_entry32 *Entry32;
  const import_lookup_table_entry64 *Entry64;
  uint32_t Index;
  const COFFObjectFile *OwningObject = nullptr;
};

/// Reference to a single PE base relocation entry.
class BaseRelocRef {
public:
  /// Construct an empty base relocation reference.
  BaseRelocRef() = default;
  /// Construct a reference to the first entry in base reloc block \p Header.
  /// \param Header Base relocation block header.
  /// \param Owner COFF object that owns the relocation table.
  BaseRelocRef(const coff_base_reloc_block_header *Header,
               const COFFObjectFile *Owner)
      : Header(Header), Index(0) {}

  /// Return true if this reference equals \p Other.
  /// \param Other Relocation reference to compare against.
  /// @return True if this reference equals \p Other.
  LLVM_ABI bool operator==(const BaseRelocRef &Other) const;
  /// Advance to the next base relocation entry.
  LLVM_ABI void moveNext();

  /// Return the base relocation type.
  /// \param Type Set to the relocation type on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getType(uint8_t &Type) const;
  /// Return the RVA of the location to relocate.
  /// \param Result Set to the relocation RVA on success.
  /// @return Success, or an error on failure.
  LLVM_ABI Error getRVA(uint32_t &Result) const;

private:
  const coff_base_reloc_block_header *Header;
  uint32_t Index;
};

/// Reference to an entry in the PE dynamic relocation table.
class DynamicRelocRef {
public:
  /// Construct an empty dynamic relocation reference.
  DynamicRelocRef() = default;
  /// Construct a reference to the dynamic relocation at \p Header.
  /// \param Header Pointer to the relocation record bytes.
  /// \param Owner COFF object that owns the dynamic relocation table.
  DynamicRelocRef(const void *Header, const COFFObjectFile *Owner)
      : Obj(Owner), Header(reinterpret_cast<const uint8_t *>(Header)) {}

  /// Return true if this reference equals \p Other.
  /// \param Other Relocation reference to compare against.
  /// @return True if this reference equals \p Other.
  LLVM_ABI bool operator==(const DynamicRelocRef &Other) const;
  /// Advance to the next dynamic relocation entry.
  LLVM_ABI void moveNext();
  /// Return the dynamic relocation type.
  /// @return The dynamic relocation type.
  LLVM_ABI uint32_t getType() const;
  /// Return the raw contents of this dynamic relocation record.
  /// \param Ref Set to the record bytes on success.
  LLVM_ABI void getContents(ArrayRef<uint8_t> &Ref) const;

  /// Return an iterator to the first Arm64X fixup in this relocation.
  /// @return An iterator to the first Arm64X fixup in this relocation.
  LLVM_ABI arm64x_reloc_iterator arm64x_reloc_begin() const;
  /// Return an iterator past the last Arm64X fixup in this relocation.
  /// @return An iterator past the last Arm64X fixup in this relocation.
  LLVM_ABI arm64x_reloc_iterator arm64x_reloc_end() const;
  /// Return the range of Arm64X fixups contained in this relocation.
  /// @return The range of Arm64X fixups contained in this relocation.
  LLVM_ABI iterator_range<arm64x_reloc_iterator> arm64x_relocs() const;

private:
  Error validate() const;

  const COFFObjectFile *Obj;
  const uint8_t *Header;

  friend class COFFObjectFile;
};

/// Reference to a single Arm64X dynamic relocation fixup entry.
class Arm64XRelocRef {
public:
  /// Construct an empty Arm64X relocation reference.
  Arm64XRelocRef() = default;
  /// Construct a reference to entry \p Index in relocation block \p Header.
  /// \param Header Base relocation block containing Arm64X fixups.
  /// \param Index Zero-based index of the fixup within the block.
  Arm64XRelocRef(const coff_base_reloc_block_header *Header, uint32_t Index = 0)
      : Header(Header), Index(Index) {}

  /// Return true if this reference equals \p Other.
  /// \param Other Relocation reference to compare against.
  /// @return True if this reference equals \p Other.
  LLVM_ABI bool operator==(const Arm64XRelocRef &Other) const;
  /// Advance to the next Arm64X fixup entry.
  LLVM_ABI void moveNext();

  /// Return the Arm64X fixup type of this entry.
  /// @return The Arm64X fixup type of this entry.
  COFF::Arm64XFixupType getType() const {
    return COFF::Arm64XFixupType((getReloc() >> 12) & 3);
  }
  /// Return the RVA of the location patched by this fixup.
  /// @return The RVA of the location patched by this fixup.
  uint32_t getRVA() const { return Header->PageRVA + (getReloc() & 0xfff); }
  /// Return the size in bytes of the value applied by this fixup.
  /// @return The size in bytes of the value applied by this fixup.
  LLVM_ABI uint8_t getSize() const;
  /// Return the fixup value payload.
  /// @return The fixup value payload.
  LLVM_ABI uint64_t getValue() const;

private:
  const support::ulittle16_t &getReloc(uint32_t Offset = 0) const {
    return reinterpret_cast<const support::ulittle16_t *>(Header +
                                                          1)[Index + Offset];
  }

  uint16_t getArg() const { return getReloc() >> 14; }
  uint8_t getEntrySize() const;
  Error validate(const COFFObjectFile *Obj) const;

  const coff_base_reloc_block_header *Header;
  uint32_t Index;

  friend class DynamicRelocRef;
};

/// Accessor for the `.rsrc` PE/COFF resource section.
class ResourceSectionRef {
public:
  /// Construct an empty resource-section view.
  ResourceSectionRef() = default;
  /// Construct a view over the raw bytes of a resource section.
  /// \param Ref Little-endian resource section contents.
  explicit ResourceSectionRef(StringRef Ref)
      : BBS(Ref, llvm::endianness::little) {}

  /// Load the resource section from \p O, locating `.rsrc` automatically.
  /// \param O COFF object that owns the resource section.
  /// @return Success, or an error if the resource section cannot be loaded.
  LLVM_ABI Error load(const COFFObjectFile *O);
  /// Load resource data from section \p S of object \p O.
  /// \param O COFF object that owns the section.
  /// \param S Resource section to load.
  /// @return Success, or an error if the section cannot be loaded.
  LLVM_ABI Error load(const COFFObjectFile *O, const SectionRef &S);

  /// Return the UTF-16 name string for a named directory entry.
  /// \param Entry Directory entry whose name should be resolved.
  /// @return The UTF-16 name string for a named directory entry, or an error on failure.
  LLVM_ABI Expected<ArrayRef<UTF16>>
  getEntryNameString(const coff_resource_dir_entry &Entry);
  /// Return the subdirectory table pointed to by \p Entry.
  /// \param Entry Directory entry that references a subdirectory.
  /// @return The subdirectory table pointed to by \p Entry, or an error on failure.
  LLVM_ABI Expected<const coff_resource_dir_table &>
  getEntrySubDir(const coff_resource_dir_entry &Entry);
  /// Return the data entry pointed to by \p Entry.
  /// \param Entry Directory entry that references resource data.
  /// @return The data entry pointed to by \p Entry, or an error on failure.
  LLVM_ABI Expected<const coff_resource_data_entry &>
  getEntryData(const coff_resource_dir_entry &Entry);
  /// Return the root resource directory table.
  /// @return The root resource directory table, or an error on failure.
  LLVM_ABI Expected<const coff_resource_dir_table &> getBaseTable();
  /// Return directory entry \p Index from \p Table.
  /// \param Table Directory table to index.
  /// \param Index Zero-based entry index within the table.
  /// @return Directory entry \p Index from \p Table, or an error on failure.
  LLVM_ABI Expected<const coff_resource_dir_entry &>
  getTableEntry(const coff_resource_dir_table &Table, uint32_t Index);

  /// Return the raw bytes described by resource data entry \p Entry.
  /// \param Entry Resource data entry whose payload should be read.
  /// @return The raw bytes described by resource data entry \p Entry, or an error on failure.
  LLVM_ABI Expected<StringRef>
  getContents(const coff_resource_data_entry &Entry);

private:
  BinaryByteStream BBS;

  SectionRef Section;
  const COFFObjectFile *Obj = nullptr;

  std::vector<const coff_relocation *> Relocs;

  Expected<const coff_resource_dir_table &> getTableAtOffset(uint32_t Offset);
  Expected<const coff_resource_dir_entry &>
  getTableEntryAtOffset(uint32_t Offset);
  Expected<const coff_resource_data_entry &>
  getDataEntryAtOffset(uint32_t Offset);
  Expected<ArrayRef<UTF16>> getDirStringAtOffset(uint32_t Offset);
};

/// Frame pointer omission (FPO) data for an x86 function (`_FPO_DATA`).
///
/// Corresponds to the `_FPO_DATA` structure in the PE/COFF spec.
struct FpoData {
  support::ulittle32_t Offset; ///< Offset of the first byte of function code.
  support::ulittle32_t Size; ///< Number of bytes in the function.
  support::ulittle32_t NumLocals; ///< Size of locals in DWORDs.
  support::ulittle16_t NumParams; ///< Size of parameters in DWORDs.
  support::ulittle16_t Attributes; ///< Packed prolog, register, SEH, and frame flags.

  /// Return the size of the function prolog in bytes.
  /// @return The size of the function prolog in bytes.
  int getPrologSize() const { return Attributes & 0xF; }

  /// Return the number of callee-saved registers.
  /// @return The number of callee-saved registers.
  int getNumSavedRegs() const { return (Attributes >> 8) & 0x7; }

  /// Return true if the function uses structured exception handling.
  /// @return True if the function uses structured exception handling.
  bool hasSEH() const { return (Attributes >> 9) & 1; }

  /// Return true if EBP has been allocated as a frame pointer.
  /// @return True if EBP has been allocated as a frame pointer.
  bool useBP() const { return (Attributes >> 10) & 1; }

  /// Return the frame-pointer type encoded in the attributes.
  /// @return The frame-pointer type encoded in the attributes.
  frame_type getFP() const { return static_cast<frame_type>(Attributes >> 14); }
};

/// Error indicating that a requested COFF section has been stripped.
class SectionStrippedError
    : public ErrorInfo<SectionStrippedError, BinaryError> {
public:
  /// Construct an error with \c object_error::section_stripped.
  SectionStrippedError() { setErrorCode(object_error::section_stripped); }
};

} // end namespace object

} // end namespace llvm

#endif // LLVM_OBJECT_COFF_H
