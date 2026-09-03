//===- XCOFFObjectFile.h - XCOFF object file implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the XCOFFObjectFile class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_XCOFFOBJECTFILE_H
#define LLVM_OBJECT_XCOFFOBJECTFILE_H

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include <limits>

namespace llvm {
namespace object {

class xcoff_symbol_iterator;

/// 32-bit XCOFF file header.
struct XCOFFFileHeader32 {
  support::ubig16_t Magic; ///< Magic number identifying the XCOFF file.
  support::ubig16_t NumberOfSections; ///< Number of sections in the file.

  /// Unix time value; 0 means no timestamp, and negative values are reserved.
  support::big32_t TimeStamp;

  support::ubig32_t SymbolTableOffset; ///< File offset to the symbol table.
  support::big32_t NumberOfSymTableEntries; ///< Number of symbol table entries.
  support::ubig16_t AuxHeaderSize; ///< Size of the optional auxiliary header.
  support::ubig16_t Flags; ///< File header flags.
};

/// 64-bit XCOFF file header.
struct XCOFFFileHeader64 {
  support::ubig16_t Magic; ///< Magic number identifying the XCOFF file.
  support::ubig16_t NumberOfSections; ///< Number of sections in the file.

  /// Unix time value; 0 means no timestamp, and negative values are reserved.
  support::big32_t TimeStamp;

  support::ubig64_t SymbolTableOffset; ///< File offset to the symbol table.
  support::ubig16_t AuxHeaderSize; ///< Size of the optional auxiliary header.
  support::ubig16_t Flags; ///< File header flags.
  support::ubig32_t NumberOfSymTableEntries; ///< Number of symbol table entries.
};

/// CRTP helpers shared by 32- and 64-bit XCOFF auxiliary headers.
template <typename T> struct XCOFFAuxiliaryHeader {
  /// Mask selecting the flag bits in FlagAndTDataAlignment.
  static constexpr uint8_t AuxiHeaderFlagMask = 0xF0;
  /// Mask selecting the TData alignment bits in FlagAndTDataAlignment.
  static constexpr uint8_t AuxiHeaderTDataAlignmentMask = 0x0F;

public:
  /// Return the flag nibble from FlagAndTDataAlignment.
  ///
  /// @return The flag nibble from FlagAndTDataAlignment.
  uint8_t getFlag() const {
    return static_cast<const T *>(this)->FlagAndTDataAlignment &
           AuxiHeaderFlagMask;
  }

  /// Return the TData alignment nibble from FlagAndTDataAlignment.
  ///
  /// @return The TData alignment nibble from FlagAndTDataAlignment.
  uint8_t getTDataAlignment() const {
    return static_cast<const T *>(this)->FlagAndTDataAlignment &
           AuxiHeaderTDataAlignmentMask;
  }

  /// Return the auxiliary header version field.
  ///
  /// @return The auxiliary header version field.
  uint16_t getVersion() const { return static_cast<const T *>(this)->Version; }
  /// Return the program entry-point address.
  ///
  /// @return The program entry-point address.
  uint64_t getEntryPointAddr() const {
    return static_cast<const T *>(this)->EntryPointAddr;
  }
};

/// 32-bit XCOFF optional auxiliary header.
struct XCOFFAuxiliaryHeader32 : XCOFFAuxiliaryHeader<XCOFFAuxiliaryHeader32> {
  support::ubig16_t
      AuxMagic; ///< If the value of the o_vstamp field is greater than 1, the
                ///< o_mflags field is reserved for future use and it should
                ///< contain 0. Otherwise, this field is not used.
  support::ubig16_t
      Version; ///< The valid values are 1 and 2. When the o_vstamp field is 2
               ///< in an XCOFF32 file, the new interpretation of the n_type
               ///< field in the symbol table entry is used.
  support::ubig32_t TextSize; ///< Size of the text section.
  support::ubig32_t InitDataSize; ///< Size of the initialized data section.
  support::ubig32_t BssDataSize; ///< Size of the uninitialized data section.
  support::ubig32_t EntryPointAddr; ///< Virtual address of the entry point.
  support::ubig32_t TextStartAddr; ///< Virtual address of the text section.
  support::ubig32_t DataStartAddr; ///< Virtual address of the data section.
  support::ubig32_t TOCAnchorAddr; ///< Virtual address of the TOC anchor.
  support::ubig16_t SecNumOfEntryPoint; ///< Section number of the entry point.
  support::ubig16_t SecNumOfText; ///< Section number of the text section.
  support::ubig16_t SecNumOfData; ///< Section number of the data section.
  support::ubig16_t SecNumOfTOC; ///< Section number of the TOC section.
  support::ubig16_t SecNumOfLoader; ///< Section number of the loader section.
  support::ubig16_t SecNumOfBSS; ///< Section number of the BSS section.
  support::ubig16_t MaxAlignOfText; ///< Maximum alignment of the text section.
  support::ubig16_t MaxAlignOfData; ///< Maximum alignment of the data section.
  support::ubig16_t ModuleType; ///< Module type field.
  uint8_t CpuFlag; ///< CPU flags for the module.
  uint8_t CpuType; ///< CPU type for the module.
  support::ubig32_t MaxStackSize; ///< If the value is 0, the system default
                                  ///< maximum stack size is used.
  support::ubig32_t MaxDataSize;  ///< If the value is 0, the system default
                                  ///< maximum data size is used.
  support::ubig32_t
      ReservedForDebugger; ///< This field should contain 0. When a loaded
                           ///< program is being debugged, the memory image of
                           ///< this field may be modified by a debugger to
                           ///< insert a trap instruction.
  uint8_t TextPageSize;  ///< Specifies the size of pages for the exec text. The
                         ///< default value is 0 (system-selected page size).
  uint8_t DataPageSize;  ///< Specifies the size of pages for the exec data. The
                         ///< default value is 0 (system-selected page size).
  uint8_t StackPageSize; ///< Specifies the size of pages for the stack. The
                         ///< default value is 0 (system-selected page size).
  uint8_t FlagAndTDataAlignment; ///< Combined flag and TData alignment field.
  support::ubig16_t SecNumOfTData; ///< Section number of the TData section.
  support::ubig16_t SecNumOfTBSS; ///< Section number of the TBSS section.
};

/// 64-bit XCOFF optional auxiliary header.
struct XCOFFAuxiliaryHeader64 : XCOFFAuxiliaryHeader<XCOFFAuxiliaryHeader64> {
  support::ubig16_t AuxMagic; ///< Auxiliary header magic / flags field.
  support::ubig16_t Version; ///< Auxiliary header version.
  support::ubig32_t ReservedForDebugger; ///< Reserved field for debugger use.
  support::ubig64_t TextStartAddr; ///< Virtual address of the text section.
  support::ubig64_t DataStartAddr; ///< Virtual address of the data section.
  support::ubig64_t TOCAnchorAddr; ///< Virtual address of the TOC anchor.
  support::ubig16_t SecNumOfEntryPoint; ///< Section number of the entry point.
  support::ubig16_t SecNumOfText; ///< Section number of the text section.
  support::ubig16_t SecNumOfData; ///< Section number of the data section.
  support::ubig16_t SecNumOfTOC; ///< Section number of the TOC section.
  support::ubig16_t SecNumOfLoader; ///< Section number of the loader section.
  support::ubig16_t SecNumOfBSS; ///< Section number of the BSS section.
  support::ubig16_t MaxAlignOfText; ///< Maximum alignment of the text section.
  support::ubig16_t MaxAlignOfData; ///< Maximum alignment of the data section.
  support::ubig16_t ModuleType; ///< Module type field.
  uint8_t CpuFlag; ///< CPU flags for the module.
  uint8_t CpuType; ///< CPU type for the module.
  uint8_t TextPageSize; ///< Page size for the text segment.
  uint8_t DataPageSize; ///< Page size for the data segment.
  uint8_t StackPageSize; ///< Page size for the stack.
  uint8_t FlagAndTDataAlignment; ///< Combined flag and TData alignment field.
  support::ubig64_t TextSize; ///< Size of the text section.
  support::ubig64_t InitDataSize; ///< Size of the initialized data section.
  support::ubig64_t BssDataSize; ///< Size of the uninitialized data section.
  support::ubig64_t EntryPointAddr; ///< Virtual address of the entry point.
  support::ubig64_t MaxStackSize; ///< Maximum stack size, or 0 for default.
  support::ubig64_t MaxDataSize; ///< Maximum data size, or 0 for default.
  support::ubig16_t SecNumOfTData; ///< Section number of the TData section.
  support::ubig16_t SecNumOfTBSS; ///< Section number of the TBSS section.
  support::ubig16_t XCOFF64Flag; ///< Additional 64-bit XCOFF flags.
};

/// CRTP helpers shared by 32- and 64-bit XCOFF section headers.
template <typename T> struct XCOFFSectionHeader {
  /// Mask for the three least-significant reserved section-flag bits.
  ///
  /// The section flags definitions are the same in both 32- and 64-bit objects.
  static constexpr unsigned SectionFlagsReservedMask = 0x7;

  /// Mask for the low-order 16 bits of section flags (the section type).
  ///
  /// The high-order 16 bits denote the section subtype and are currently used
  /// only for DWARF sections.
  static constexpr unsigned SectionFlagsTypeMask = 0xffffu;

public:
  /// Return the section name as a StringRef.
  ///
  /// @return The section name as a StringRef.
  StringRef getName() const;
  /// Return the section type bits from Flags.
  ///
  /// @return The section type bits from Flags.
  uint16_t getSectionType() const;
  /// Return the section subtype bits from Flags.
  ///
  /// @return The section subtype bits from Flags.
  uint32_t getSectionSubtype() const;
  /// True when the section type uses a reserved flags encoding.
  ///
  /// @return True when the section type uses a reserved flags encoding.
  bool isReservedSectionType() const;
};

// Explicit extern template declarations.
/// 32-bit XCOFF section header (forward declaration).
struct XCOFFSectionHeader32;
/// 64-bit XCOFF section header (forward declaration).
struct XCOFFSectionHeader64;
/// Explicit instantiation of the 32-bit XCOFF section header helpers.
extern template struct LLVM_TEMPLATE_ABI
    XCOFFSectionHeader<XCOFFSectionHeader32>;
/// Explicit instantiation of the 64-bit XCOFF section header helpers.
extern template struct LLVM_TEMPLATE_ABI
    XCOFFSectionHeader<XCOFFSectionHeader64>;

/// 32-bit XCOFF section header.
struct XCOFFSectionHeader32 : XCOFFSectionHeader<XCOFFSectionHeader32> {
  char Name[XCOFF::NameSize]; ///< Section name (not necessarily null-terminated).
  support::ubig32_t PhysicalAddress; ///< Physical address of the section.
  support::ubig32_t VirtualAddress; ///< Virtual address of the section.
  support::ubig32_t SectionSize; ///< Size of the section in bytes.
  support::ubig32_t FileOffsetToRawData; ///< File offset of the section contents.
  support::ubig32_t FileOffsetToRelocationInfo; ///< File offset of relocation info.
  support::ubig32_t FileOffsetToLineNumberInfo; ///< File offset of line-number info.
  support::ubig16_t NumberOfRelocations; ///< Number of relocation entries.
  support::ubig16_t NumberOfLineNumbers; ///< Number of line-number entries.
  support::big32_t Flags; ///< Section flags (type and subtype).
};

/// 64-bit XCOFF section header.
struct XCOFFSectionHeader64 : XCOFFSectionHeader<XCOFFSectionHeader64> {
  char Name[XCOFF::NameSize]; ///< Section name (not necessarily null-terminated).
  support::ubig64_t PhysicalAddress; ///< Physical address of the section.
  support::ubig64_t VirtualAddress; ///< Virtual address of the section.
  support::ubig64_t SectionSize; ///< Size of the section in bytes.
  support::big64_t FileOffsetToRawData; ///< File offset of the section contents.
  support::big64_t FileOffsetToRelocationInfo; ///< File offset of relocation info.
  support::big64_t FileOffsetToLineNumberInfo; ///< File offset of line-number info.
  support::ubig32_t NumberOfRelocations; ///< Number of relocation entries.
  support::ubig32_t NumberOfLineNumbers; ///< Number of line-number entries.
  support::big32_t Flags; ///< Section flags (type and subtype).
  char Padding[4]; ///< Padding to align the 64-bit section header.
};

struct LoaderSectionHeader32;
struct LoaderSectionHeader64;
/// 32-bit loader-section symbol table entry.
struct LoaderSectionSymbolEntry32 {
  /// Name stored as an offset into the loader string table.
  struct NameOffsetInStrTbl {
    support::big32_t IsNameInStrTbl; ///< Zero indicates the name is in the string table.
    support::ubig32_t Offset; ///< Byte offset of the name in the string table.
  };

  char SymbolName[XCOFF::NameSize]; ///< Inline symbol name when not in the string table.
  support::ubig32_t Value; ///< Virtual address of the symbol.
  support::big16_t SectionNumber; ///< Section number containing the symbol.
  uint8_t SymbolType; ///< Symbol type field.
  XCOFF::StorageClass StorageClass; ///< Storage class of the symbol.
  support::ubig32_t ImportFileID; ///< Import file identifier.
  support::ubig32_t ParameterTypeCheck; ///< Parameter type-check hash.

  /// Return the symbol name, resolving string-table names when needed.
  ///
  /// \param LoaderSecHeader Loader section header providing string-table layout.
  /// @return The symbol name, resolving string-table names when needed.
  LLVM_ABI Expected<StringRef>
  getSymbolName(const LoaderSectionHeader32 *LoaderSecHeader) const;
};

/// 64-bit loader-section symbol table entry.
struct LoaderSectionSymbolEntry64 {
  support::ubig64_t Value; ///< Virtual address of the symbol.
  support::ubig32_t Offset; ///< Offset of the symbol name in the string table.
  support::big16_t SectionNumber; ///< Section number containing the symbol.
  uint8_t SymbolType; ///< Symbol type field.
  XCOFF::StorageClass StorageClass; ///< Storage class of the symbol.
  support::ubig32_t ImportFileID; ///< Import file identifier.
  support::ubig32_t ParameterTypeCheck; ///< Parameter type-check hash.

  /// Return the symbol name from the loader string table.
  ///
  /// \param LoaderSecHeader Loader section header providing string-table layout.
  /// @return The symbol name from the loader string table.
  LLVM_ABI Expected<StringRef>
  getSymbolName(const LoaderSectionHeader64 *LoaderSecHeader) const;
};

/// 32-bit loader-section relocation entry.
struct LoaderSectionRelocationEntry32 {
  support::ubig32_t VirtualAddr; ///< Virtual address of the relocation.
  support::big32_t SymbolIndex; ///< Symbol table index for the relocation.
  support::ubig16_t Type; ///< Relocation type.
  support::big16_t SectionNum; ///< Section number of the relocation.
};

/// 64-bit loader-section relocation entry.
struct LoaderSectionRelocationEntry64 {
  support::ubig64_t VirtualAddr; ///< Virtual address of the relocation.
  support::ubig16_t Type; ///< Relocation type.
  support::big16_t SectionNum; ///< Section number of the relocation.
  support::big32_t SymbolIndex; ///< Symbol table index for the relocation.
};

/// 32-bit XCOFF loader section header.
struct LoaderSectionHeader32 {
  support::ubig32_t Version; ///< Loader section version.
  support::ubig32_t NumberOfSymTabEnt; ///< Number of symbol table entries.
  support::ubig32_t NumberOfRelTabEnt; ///< Number of relocation table entries.
  support::ubig32_t LengthOfImpidStrTbl; ///< Length of the import-ID string table.
  support::ubig32_t NumberOfImpid; ///< Number of import identifiers.
  support::big32_t OffsetToImpid; ///< Offset to the import-ID table.
  support::ubig32_t LengthOfStrTbl; ///< Length of the loader string table.
  support::big32_t OffsetToStrTbl; ///< Offset to the loader string table.

  /// Return the file offset of the loader symbol table, or 0 if empty.
  ///
  /// @return The file offset of the loader symbol table, or 0 if empty.
  uint64_t getOffsetToSymTbl() const {
    return NumberOfSymTabEnt == 0 ? 0 : sizeof(LoaderSectionHeader32);
  }

  /// Return the file offset of the loader relocation table, or 0 if empty.
  ///
  /// The relocation table follows the symbol table.
  /// @return The file offset of the loader relocation table, or 0 if empty.
  uint64_t getOffsetToRelEnt() const {
    // Relocation table is after Symbol table.
    return NumberOfRelTabEnt == 0
               ? 0
               : sizeof(LoaderSectionHeader32) +
                     sizeof(LoaderSectionSymbolEntry32) * NumberOfSymTabEnt;
  }
};

/// 64-bit XCOFF loader section header.
struct LoaderSectionHeader64 {
  support::ubig32_t Version; ///< Loader section version.
  support::ubig32_t NumberOfSymTabEnt; ///< Number of symbol table entries.
  support::ubig32_t NumberOfRelTabEnt; ///< Number of relocation table entries.
  support::ubig32_t LengthOfImpidStrTbl; ///< Length of the import-ID string table.
  support::ubig32_t NumberOfImpid; ///< Number of import identifiers.
  support::ubig32_t LengthOfStrTbl; ///< Length of the loader string table.
  support::big64_t OffsetToImpid; ///< Offset to the import-ID table.
  support::big64_t OffsetToStrTbl; ///< Offset to the loader string table.
  support::big64_t OffsetToSymTbl; ///< Offset to the loader symbol table.
  support::big64_t OffsetToRelEnt; ///< Offset to the loader relocation table.

  /// Return the stored offset of the loader symbol table.
  ///
  /// @return The stored offset of the loader symbol table.
  uint64_t getOffsetToSymTbl() const { return OffsetToSymTbl; }
  /// Return the stored offset of the loader relocation table.
  ///
  /// @return The stored offset of the loader relocation table.
  uint64_t getOffsetToRelEnt() const { return OffsetToRelEnt; }
};

/// Exception-section entry for a trap or function association.
template <typename AddressType> struct ExceptionSectionEntry {
  union {
    support::ubig32_t SymbolIdx; ///< Symbol index when Reason is zero.
    AddressType TrapInstAddr; ///< Trap instruction address when Reason is nonzero.
  };
  uint8_t LangId; ///< Language identifier for the exception entry.
  uint8_t Reason; ///< Exception reason code (0 means function association).

  /// Return the symbol-table index of the associated function.
  ///
  /// Valid only when Reason is zero.
  /// @return The symbol-table index of the associated function.
  uint32_t getSymbolIndex() const {
    assert(Reason == 0 && "Get symbol table index of the function only when "
                          "the e_reason field is 0.");
    return SymbolIdx;
  }

  /// Return the address of the trapping instruction.
  ///
  /// Valid only when Reason is nonzero.
  /// @return The address of the trapping instruction.
  uint64_t getTrapInstAddr() const {
    assert(Reason != 0 && "Zero is not a valid trap exception reason code.");
    return TrapInstAddr;
  }
  /// Return the language identifier.
  ///
  /// @return The language identifier.
  uint8_t getLangID() const { return LangId; }
  /// Return the exception reason code.
  ///
  /// @return The exception reason code.
  uint8_t getReason() const { return Reason; }
};

/// 32-bit exception-section entry.
typedef ExceptionSectionEntry<support::ubig32_t> ExceptionSectionEntry32;
/// 64-bit exception-section entry.
typedef ExceptionSectionEntry<support::ubig64_t> ExceptionSectionEntry64;

// Explicit extern template declarations.
/// Explicit instantiation of the 32-bit exception-section entry.
extern template struct LLVM_TEMPLATE_ABI
    ExceptionSectionEntry<support::ubig32_t>;
/// Explicit instantiation of the 64-bit exception-section entry.
extern template struct LLVM_TEMPLATE_ABI
    ExceptionSectionEntry<support::ubig64_t>;

/// View of the XCOFF string table (size word plus character data).
struct XCOFFStringTable {
  uint32_t Size; ///< Size of the string table in bytes, including the size word.
  const char *Data; ///< Pointer to the string-table character data.
};

/// 32-bit csect auxiliary symbol table entry.
struct XCOFFCsectAuxEnt32 {
  support::ubig32_t SectionOrLength; ///< Csect length or containing section/symbol index.
  support::ubig32_t ParameterHashIndex; ///< Parameter type-check hash index.
  support::ubig16_t TypeChkSectNum; ///< Type-check section number.
  uint8_t SymbolAlignmentAndType; ///< Packed symbol alignment and csect type.
  XCOFF::StorageMappingClass StorageMappingClass; ///< Storage-mapping class.
  support::ubig32_t StabInfoIndex; ///< Stab information index.
  support::ubig16_t StabSectNum; ///< Stab section number.
};

/// 64-bit csect auxiliary symbol table entry.
struct XCOFFCsectAuxEnt64 {
  support::ubig32_t SectionOrLengthLowByte; ///< Low 32 bits of section/length field.
  support::ubig32_t ParameterHashIndex; ///< Parameter type-check hash index.
  support::ubig16_t TypeChkSectNum; ///< Type-check section number.
  uint8_t SymbolAlignmentAndType; ///< Packed symbol alignment and csect type.
  XCOFF::StorageMappingClass StorageMappingClass; ///< Storage-mapping class.
  support::ubig32_t SectionOrLengthHighByte; ///< High 32 bits of section/length field.
  uint8_t Pad; ///< Padding byte.
  XCOFF::SymbolAuxType AuxType; ///< Auxiliary entry type (_AUX_CSECT).
};

/// Unified accessor for 32- and 64-bit csect auxiliary entries.
class XCOFFCsectAuxRef {
public:
  /// Mask selecting the csect symbol-type bits.
  static constexpr uint8_t SymbolTypeMask = 0x07;
  /// Mask selecting the symbol-alignment bits.
  static constexpr uint8_t SymbolAlignmentMask = 0xF8;
  /// Bit offset of the alignment field within SymbolAlignmentAndType.
  static constexpr size_t SymbolAlignmentBitOffset = 3;

  /// Construct a reference to a 32-bit csect auxiliary entry.
  ///
  /// \param Entry32 Pointer to the 32-bit csect auxiliary entry.
  XCOFFCsectAuxRef(const XCOFFCsectAuxEnt32 *Entry32) : Entry32(Entry32) {}
  /// Construct a reference to a 64-bit csect auxiliary entry.
  ///
  /// \param Entry64 Pointer to the 64-bit csect auxiliary entry.
  XCOFFCsectAuxRef(const XCOFFCsectAuxEnt64 *Entry64) : Entry64(Entry64) {}

  /// Return the section-or-length field interpreted by csect symbol type.
  ///
  /// For XTY_SD or XTY_CM this is the csect length. For XTY_LD this is the
  /// symbol-table index of the containing csect. For XTY_ER this is 0.
  /// @return The section-or-length field interpreted by csect symbol type.
  uint64_t getSectionOrLength() const {
    return Entry32 ? getSectionOrLength32() : getSectionOrLength64();
  }

  /// Return the 32-bit section-or-length field.
  ///
  /// @return The 32-bit section-or-length field.
  uint32_t getSectionOrLength32() const {
    assert(Entry32 && "32-bit interface called on 64-bit object file.");
    return Entry32->SectionOrLength;
  }

  /// Return the 64-bit section-or-length field assembled from both halves.
  ///
  /// @return The 64-bit section-or-length field assembled from both halves.
  uint64_t getSectionOrLength64() const {
    assert(Entry64 && "64-bit interface called on 32-bit object file.");
    return (static_cast<uint64_t>(Entry64->SectionOrLengthHighByte) << 32) |
           Entry64->SectionOrLengthLowByte;
  }

#define GETVALUE(X) Entry32 ? Entry32->X : Entry64->X

  /// Return the parameter type-check hash index.
  ///
  /// @return The parameter type-check hash index.
  uint32_t getParameterHashIndex() const {
    return GETVALUE(ParameterHashIndex);
  }

  /// Return the type-check section number.
  ///
  /// @return The type-check section number.
  uint16_t getTypeChkSectNum() const { return GETVALUE(TypeChkSectNum); }

  /// Return the storage-mapping class.
  ///
  /// @return The storage-mapping class.
  XCOFF::StorageMappingClass getStorageMappingClass() const {
    return GETVALUE(StorageMappingClass);
  }

  /// Return the address of the underlying auxiliary entry.
  ///
  /// @return The address of the underlying auxiliary entry.
  uintptr_t getEntryAddress() const {
    return Entry32 ? reinterpret_cast<uintptr_t>(Entry32)
                   : reinterpret_cast<uintptr_t>(Entry64);
  }

  /// Return the log2 of the symbol alignment.
  ///
  /// @return The log2 of the symbol alignment.
  uint16_t getAlignmentLog2() const {
    return (getSymbolAlignmentAndType() & SymbolAlignmentMask) >>
           SymbolAlignmentBitOffset;
  }

  /// Return the csect symbol type bits.
  ///
  /// @return The csect symbol type bits.
  uint8_t getSymbolType() const {
    return getSymbolAlignmentAndType() & SymbolTypeMask;
  }

  /// True when this csect auxiliary describes a label (XTY_LD).
  ///
  /// @return True when this csect auxiliary describes a label (XTY_LD).
  bool isLabel() const { return getSymbolType() == XCOFF::XTY_LD; }

  /// Return the 32-bit stab information index.
  ///
  /// @return The 32-bit stab information index.
  uint32_t getStabInfoIndex32() const {
    assert(Entry32 && "32-bit interface called on 64-bit object file.");
    return Entry32->StabInfoIndex;
  }

  /// Return the 32-bit stab section number.
  ///
  /// @return The 32-bit stab section number.
  uint16_t getStabSectNum32() const {
    assert(Entry32 && "32-bit interface called on 64-bit object file.");
    return Entry32->StabSectNum;
  }

  /// Return the 64-bit auxiliary entry type.
  ///
  /// @return The 64-bit auxiliary entry type.
  XCOFF::SymbolAuxType getAuxType64() const {
    assert(Entry64 && "64-bit interface called on 32-bit object file.");
    return Entry64->AuxType;
  }

  /// Return the packed symbol alignment and type byte.
  ///
  /// @return The packed symbol alignment and type byte.
  uint8_t getSymbolAlignmentAndType() const {
    return GETVALUE(SymbolAlignmentAndType);
  }

#undef GETVALUE

private:
  const XCOFFCsectAuxEnt32 *Entry32 = nullptr;
  const XCOFFCsectAuxEnt64 *Entry64 = nullptr;
};

/// File-name auxiliary symbol table entry (C_FILE).
struct XCOFFFileAuxEnt {
  /// Name stored as an offset into the string table.
  typedef struct {
    support::big32_t Magic; ///< Zero indicates the name is in the string table.
    support::ubig32_t Offset; ///< Byte offset of the name in the string table.
    char NamePad[XCOFF::FileNamePadSize]; ///< Padding after the string-table offset.
  } NameInStrTblType;
  union {
    char Name[XCOFF::NameSize + XCOFF::FileNamePadSize]; ///< Inline file name.
    NameInStrTblType NameInStrTbl; ///< String-table form of the file name.
  };
  XCOFF::CFileStringType Type; ///< Kind of string stored in this auxiliary entry.
  uint8_t ReservedZeros[2]; ///< Reserved; must be zero.
  XCOFF::SymbolAuxType AuxType; ///< Auxiliary entry type (64-bit XCOFF only).
};

/// Section auxiliary entry for a C_STAT symbol (32-bit XCOFF only).
struct XCOFFSectAuxEntForStat {
  support::ubig32_t SectionLength; ///< Length of the section.
  support::ubig16_t NumberOfRelocEnt; ///< Number of relocation entries.
  support::ubig16_t NumberOfLineNum; ///< Number of line-number entries.
  uint8_t Pad[10]; ///< Padding to the auxiliary entry size.
};

/// 32-bit function auxiliary symbol table entry.
struct XCOFFFunctionAuxEnt32 {
  support::ubig32_t OffsetToExceptionTbl; ///< Offset to the exception table.
  support::ubig32_t SizeOfFunction; ///< Size of the function in bytes.
  support::ubig32_t PtrToLineNum; ///< Pointer/offset to line-number information.
  support::big32_t SymIdxOfNextBeyond; ///< Symbol index of the next function or beyond.
  uint8_t Pad[2]; ///< Padding bytes.
};

/// 64-bit function auxiliary symbol table entry.
struct XCOFFFunctionAuxEnt64 {
  support::ubig64_t PtrToLineNum; ///< Pointer/offset to line-number information.
  support::ubig32_t SizeOfFunction; ///< Size of the function in bytes.
  support::big32_t SymIdxOfNextBeyond; ///< Symbol index of the next function or beyond.
  uint8_t Pad; ///< Padding byte.
  XCOFF::SymbolAuxType AuxType; ///< Auxiliary entry type (_AUX_FCN).
};

/// Exception auxiliary symbol table entry.
struct XCOFFExceptionAuxEnt {
  support::ubig64_t OffsetToExceptionTbl; ///< Offset to the exception table.
  support::ubig32_t SizeOfFunction; ///< Size of the function in bytes.
  support::big32_t SymIdxOfNextBeyond; ///< Symbol index of the next function or beyond.
  uint8_t Pad; ///< Padding byte.
  XCOFF::SymbolAuxType AuxType; ///< Auxiliary entry type (_AUX_EXCEPT).
};

/// 32-bit block auxiliary symbol table entry.
struct XCOFFBlockAuxEnt32 {
  uint8_t ReservedZeros1[2]; ///< Reserved; must be zero.
  support::ubig16_t LineNumHi; ///< High half of the source line number.
  support::ubig16_t LineNumLo; ///< Low half of the source line number.
  uint8_t ReservedZeros2[12]; ///< Reserved; must be zero.
};

/// 64-bit block auxiliary symbol table entry.
struct XCOFFBlockAuxEnt64 {
  support::ubig32_t LineNum; ///< Source line number.
  uint8_t Pad[13]; ///< Padding bytes.
  XCOFF::SymbolAuxType AuxType; ///< Auxiliary entry type (_AUX_SYM).
};

/// 32-bit DWARF section auxiliary symbol table entry.
struct XCOFFSectAuxEntForDWARF32 {
  support::ubig32_t LengthOfSectionPortion; ///< Length of this DWARF section portion.
  uint8_t Pad1[4]; ///< Padding bytes.
  support::ubig32_t NumberOfRelocEnt; ///< Number of relocation entries.
  uint8_t Pad2[6]; ///< Padding bytes.
};

/// 64-bit DWARF section auxiliary symbol table entry.
struct XCOFFSectAuxEntForDWARF64 {
  support::ubig64_t LengthOfSectionPortion; ///< Length of this DWARF section portion.
  support::ubig64_t NumberOfRelocEnt; ///< Number of relocation entries.
  uint8_t Pad; ///< Padding byte.
  XCOFF::SymbolAuxType AuxType; ///< Auxiliary entry type (_AUX_SECT).
};

/// XCOFF relocation entry parameterized by address width.
template <typename AddressType> struct XCOFFRelocation {
public:
  AddressType VirtualAddress; ///< Virtual address of the relocatable field.
  support::ubig32_t SymbolIndex; ///< Symbol table index used by the relocation.

  /// Packed relocation info field (see XR_* masks).
  uint8_t Info;

  XCOFF::RelocationType Type; ///< Relocation type.

public:
  /// True when the relocation value is treated as signed.
  ///
  /// @return True when the relocation value is treated as signed.
  bool isRelocationSigned() const;
  /// True when the fixup-indicated bit is set in Info.
  ///
  /// @return True when the fixup-indicated bit is set in Info.
  bool isFixupIndicated() const;

  /// Return the number of bits being relocated.
  ///
  /// @return The number of bits being relocated.
  uint8_t getRelocatedLength() const;
};

/// Explicit instantiation of the 32-bit XCOFF relocation entry.
extern template struct LLVM_TEMPLATE_ABI
    XCOFFRelocation<llvm::support::ubig32_t>;
/// Explicit instantiation of the 64-bit XCOFF relocation entry.
extern template struct LLVM_TEMPLATE_ABI
    XCOFFRelocation<llvm::support::ubig64_t>;

/// 32-bit XCOFF relocation entry.
struct XCOFFRelocation32 : XCOFFRelocation<llvm::support::ubig32_t> {};
/// 64-bit XCOFF relocation entry.
struct XCOFFRelocation64 : XCOFFRelocation<llvm::support::ubig64_t> {};

class XCOFFSymbolRef;

/// ObjectFile implementation for 32- and 64-bit XCOFF binaries.
class LLVM_ABI XCOFFObjectFile : public ObjectFile {
private:
  const void *FileHeader = nullptr;
  const void *AuxiliaryHeader = nullptr;
  const void *SectionHeaderTable = nullptr;

  const void *SymbolTblPtr = nullptr;
  XCOFFStringTable StringTable = {0, nullptr};

  const XCOFFSectionHeader32 *sectionHeaderTable32() const;
  const XCOFFSectionHeader64 *sectionHeaderTable64() const;
  template <typename T> const T *sectionHeaderTable() const;

  size_t getFileHeaderSize() const;
  size_t getSectionHeaderSize() const;

  const XCOFFSectionHeader32 *toSection32(DataRefImpl Ref) const;
  const XCOFFSectionHeader64 *toSection64(DataRefImpl Ref) const;
  uintptr_t getSectionHeaderTableAddress() const;
  uintptr_t getEndOfSymbolTableAddress() const;

  DataRefImpl getSectionByType(XCOFF::SectionTypeFlags SectType) const;
  uint64_t getSectionFileOffsetToRawData(DataRefImpl Sec) const;

  // This returns a pointer to the start of the storage for the name field of
  // the 32-bit or 64-bit SectionHeader struct. This string is *not* necessarily
  // null-terminated.
  const char *getSectionNameInternal(DataRefImpl Sec) const;

  static bool isReservedSectionNumber(int16_t SectionNumber);

  // Constructor and "create" factory function. The constructor is only a thin
  // wrapper around the base constructor. The "create" function fills out the
  // XCOFF-specific information and performs the error checking along the way.
  XCOFFObjectFile(unsigned Type, MemoryBufferRef Object);
  static Expected<std::unique_ptr<XCOFFObjectFile>> create(unsigned Type,
                                                           MemoryBufferRef MBR);

  // Helper for parsing the StringTable. Returns an 'Error' if parsing failed
  // and an XCOFFStringTable if parsing succeeded.
  static Expected<XCOFFStringTable> parseStringTable(const XCOFFObjectFile *Obj,
                                                     uint64_t Offset);

  // Make a friend so it can call the private 'create' function.
  friend Expected<std::unique_ptr<ObjectFile>>
  ObjectFile::createXCOFFObjectFile(MemoryBufferRef Object, unsigned FileType);

  void checkSectionAddress(uintptr_t Addr, uintptr_t TableAddr) const;

public:
  /// Sentinel returned by getRelocationOffset when the offset is invalid.
  static constexpr uint64_t InvalidRelocOffset =
      std::numeric_limits<uint64_t>::max();

  // Interface inherited from base classes.
  /// Advance \p Symb to the next symbol table entry.
  ///
  /// \param Symb Symbol data reference to advance.
  void moveSymbolNext(DataRefImpl &Symb) const override;
  /// Return ObjectFile symbol flags for \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return ObjectFile symbol flags for \p Symb.
  Expected<uint32_t> getSymbolFlags(DataRefImpl Symb) const override;
  /// Return an iterator to the first symbol.
  ///
  /// @return An iterator to the first symbol.
  basic_symbol_iterator symbol_begin() const override;
  /// Return an iterator past the last symbol.
  ///
  /// @return An iterator past the last symbol.
  basic_symbol_iterator symbol_end() const override;

  /// Iterator range over XCOFFSymbolRef symbols.
  using xcoff_symbol_iterator_range = iterator_range<xcoff_symbol_iterator>;
  /// Return a range of XCOFF-typed symbol iterators.
  ///
  /// @return A range of XCOFF-typed symbol iterators.
  xcoff_symbol_iterator_range symbols() const;

  /// True when this object is a 64-bit XCOFF file.
  ///
  /// @return True when this object is a 64-bit XCOFF file.
  bool is64Bit() const override;
  /// Return the name of symbol \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return The name of symbol \p Symb.
  Expected<StringRef> getSymbolName(DataRefImpl Symb) const override;
  /// Return the virtual address of symbol \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return The virtual address of symbol \p Symb.
  Expected<uint64_t> getSymbolAddress(DataRefImpl Symb) const override;
  /// Return the raw symbol value for \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return The raw symbol value for \p Symb.
  uint64_t getSymbolValueImpl(DataRefImpl Symb) const override;
  /// Return the alignment of symbol \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return The alignment of symbol \p Symb.
  uint32_t getSymbolAlignment(DataRefImpl Symb) const override;
  /// Return the size of common symbol \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return The size of common symbol \p Symb.
  uint64_t getCommonSymbolSizeImpl(DataRefImpl Symb) const override;
  /// Return the ObjectFile symbol type for \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return The ObjectFile symbol type for \p Symb.
  Expected<SymbolRef::Type> getSymbolType(DataRefImpl Symb) const override;
  /// Return the section containing symbol \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return The section containing symbol \p Symb.
  Expected<section_iterator> getSymbolSection(DataRefImpl Symb) const override;

  /// Advance \p Sec to the next section.
  ///
  /// \param Sec Section data reference to advance.
  void moveSectionNext(DataRefImpl &Sec) const override;
  /// Return the name of section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return The name of section \p Sec.
  Expected<StringRef> getSectionName(DataRefImpl Sec) const override;
  /// Return the virtual address of section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return The virtual address of section \p Sec.
  uint64_t getSectionAddress(DataRefImpl Sec) const override;
  /// Return the zero-based index of section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return The zero-based index of section \p Sec.
  uint64_t getSectionIndex(DataRefImpl Sec) const override;
  /// Return the size in bytes of section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return The size in bytes of section \p Sec.
  uint64_t getSectionSize(DataRefImpl Sec) const override;
  /// Return the raw contents of section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return The raw contents of section \p Sec.
  Expected<ArrayRef<uint8_t>>
  getSectionContents(DataRefImpl Sec) const override;
  /// Return the alignment of section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return The alignment of section \p Sec.
  uint64_t getSectionAlignment(DataRefImpl Sec) const override;
  /// True when section \p Sec is compressed.
  ///
  /// \param Sec Section data reference.
  /// @return True when section \p Sec is compressed.
  bool isSectionCompressed(DataRefImpl Sec) const override;
  /// True when section \p Sec contains executable text.
  ///
  /// \param Sec Section data reference.
  /// @return True when section \p Sec contains executable text.
  bool isSectionText(DataRefImpl Sec) const override;
  /// True when section \p Sec contains initialized data.
  ///
  /// \param Sec Section data reference.
  /// @return True when section \p Sec contains initialized data.
  bool isSectionData(DataRefImpl Sec) const override;
  /// True when section \p Sec is BSS (uninitialized data).
  ///
  /// \param Sec Section data reference.
  /// @return True when section \p Sec is BSS (uninitialized data).
  bool isSectionBSS(DataRefImpl Sec) const override;
  /// True when section \p Sec is a debug section.
  ///
  /// \param Sec Section data reference.
  /// @return True when section \p Sec is a debug section.
  bool isDebugSection(DataRefImpl Sec) const override;

  /// True when section \p Sec has no file contents (virtual).
  ///
  /// \param Sec Section data reference.
  /// @return True when section \p Sec has no file contents (virtual).
  bool isSectionVirtual(DataRefImpl Sec) const override;
  /// Return an iterator to the first relocation in section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return An iterator to the first relocation in section \p Sec.
  relocation_iterator section_rel_begin(DataRefImpl Sec) const override;
  /// Return an iterator past the last relocation in section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return An iterator past the last relocation in section \p Sec.
  relocation_iterator section_rel_end(DataRefImpl Sec) const override;

  /// Advance \p Rel to the next relocation.
  ///
  /// \param Rel Relocation data reference to advance.
  void moveRelocationNext(DataRefImpl &Rel) const override;

  /// Return the section-relative offset of relocation \p Rel.
  ///
  /// Returns the relocation offset with the base address of the containing
  /// section as zero, or InvalidRelocOffset on errors (such as a relocation
  /// that does not refer to an address in any section).
  ///
  /// \param Rel Relocation data reference.
  /// @return The section-relative offset of relocation \p Rel.
  uint64_t getRelocationOffset(DataRefImpl Rel) const override;
  /// Return the symbol referenced by relocation \p Rel.
  ///
  /// \param Rel Relocation data reference.
  /// @return The symbol referenced by relocation \p Rel.
  symbol_iterator getRelocationSymbol(DataRefImpl Rel) const override;
  /// Return the relocation type of \p Rel.
  ///
  /// \param Rel Relocation data reference.
  /// @return The relocation type of \p Rel.
  uint64_t getRelocationType(DataRefImpl Rel) const override;
  /// Append a human-readable relocation type name for \p Rel to \p Result.
  ///
  /// \param Rel Relocation data reference.
  /// \param Result Buffer that receives the type name.
  void getRelocationTypeName(DataRefImpl Rel,
                             SmallVectorImpl<char> &Result) const override;

  /// Return an iterator to the first section.
  ///
  /// @return An iterator to the first section.
  section_iterator section_begin() const override;
  /// Return an iterator past the last section.
  ///
  /// @return An iterator past the last section.
  section_iterator section_end() const override;
  /// Return the number of bytes in an address for this object.
  ///
  /// @return The number of bytes in an address for this object.
  uint8_t getBytesInAddress() const override;
  /// Return a short string naming the file format.
  ///
  /// @return A short string naming the file format.
  StringRef getFileFormatName() const override;
  /// Return the architecture of this object file.
  ///
  /// @return The architecture of this object file.
  Triple::ArchType getArch() const override;
  /// Return subtarget features encoded in this object, if any.
  ///
  /// @return Subtarget features encoded in this object, if any.
  Expected<SubtargetFeatures> getFeatures() const override;
  /// Return the start/entry address of the object, if available.
  ///
  /// @return The start/entry address of the object, if available.
  Expected<uint64_t> getStartAddress() const override;
  /// Map an XCOFF debug section name to a canonical DWARF-style name.
  ///
  /// \param Name Section name to map.
  /// @return The canonical DWARF-style section name for \p Name.
  StringRef mapDebugSectionName(StringRef Name) const override;
  /// True when this object is a relocatable object file.
  ///
  /// @return True when this object is a relocatable object file.
  bool isRelocatableObject() const override;

  // Below here is the non-inherited interface.

  /// Return \p Size bytes of raw data starting at \p Start, naming the region.
  ///
  /// \param Start Pointer into the object file buffer.
  /// \param Size Number of bytes to return.
  /// \param Name Name used in diagnostics if the range is invalid.
  /// @return \p Size bytes of raw data starting at \p Start, or an error if the
  /// range is invalid.
  Expected<StringRef> getRawData(const char *Start, uint64_t Size,
                                 StringRef Name) const;

  /// Return the 32-bit auxiliary header, or nullptr if absent/wrong mode.
  ///
  /// @return The 32-bit auxiliary header, or nullptr if absent/wrong mode.
  const XCOFFAuxiliaryHeader32 *auxiliaryHeader32() const;
  /// Return the 64-bit auxiliary header, or nullptr if absent/wrong mode.
  ///
  /// @return The 64-bit auxiliary header, or nullptr if absent/wrong mode.
  const XCOFFAuxiliaryHeader64 *auxiliaryHeader64() const;

  /// Return a pointer to the start of the symbol table.
  ///
  /// @return A pointer to the start of the symbol table.
  const void *getPointerToSymbolTable() const { return SymbolTblPtr; }

  /// Return the section name for XCOFF symbol \p Ref.
  ///
  /// \param Ref XCOFF symbol reference.
  /// @return The section name for XCOFF symbol \p Ref.
  Expected<StringRef> getSymbolSectionName(XCOFFSymbolRef Ref) const;
  /// Return the section number/ID for symbol \p Sym.
  ///
  /// \param Sym Symbol reference.
  /// @return The section number/ID for symbol \p Sym.
  unsigned getSymbolSectionID(SymbolRef Sym) const;
  /// Convert a DataRefImpl into an XCOFFSymbolRef.
  ///
  /// \param Ref Symbol data reference.
  /// @return An XCOFFSymbolRef for \p Ref.
  XCOFFSymbolRef toSymbolRef(DataRefImpl Ref) const;

  // File header related interfaces.
  /// Return the 32-bit file header.
  ///
  /// @return The 32-bit file header.
  const XCOFFFileHeader32 *fileHeader32() const;
  /// Return the 64-bit file header.
  ///
  /// @return The 64-bit file header.
  const XCOFFFileHeader64 *fileHeader64() const;
  /// Return the file-header magic number.
  ///
  /// @return The file-header magic number.
  uint16_t getMagic() const;
  /// Return the number of sections from the file header.
  ///
  /// @return The number of sections from the file header.
  uint16_t getNumberOfSections() const;
  /// Return the file-header timestamp.
  ///
  /// @return The file-header timestamp.
  int32_t getTimeStamp() const;

  // Symbol table offset and entry count are handled differently between
  // XCOFF32 and XCOFF64.
  /// Return the 32-bit symbol-table file offset.
  ///
  /// @return The 32-bit symbol-table file offset.
  uint32_t getSymbolTableOffset32() const;
  /// Return the 64-bit symbol-table file offset.
  ///
  /// @return The 64-bit symbol-table file offset.
  uint64_t getSymbolTableOffset64() const;

  /// Return the raw (possibly negative) 32-bit symbol-table entry count.
  ///
  /// Negative values are reserved for future use.
  /// @return The raw (possibly negative) 32-bit symbol-table entry count.
  int32_t getRawNumberOfSymbolTableEntries32() const;

  /// Return the sanitized 32-bit symbol-table entry count usable as an index.
  ///
  /// @return The sanitized 32-bit symbol-table entry count usable as an index.
  uint32_t getLogicalNumberOfSymbolTableEntries32() const;

  /// Return the 64-bit symbol-table entry count.
  ///
  /// @return The 64-bit symbol-table entry count.
  uint32_t getNumberOfSymbolTableEntries64() const;

  /// Return the symbol-table entry count for the current object mode.
  ///
  /// Uses getLogicalNumberOfSymbolTableEntries32 or
  /// getNumberOfSymbolTableEntries64 depending on the object mode.
  /// @return The symbol-table entry count for the current object mode.
  uint32_t getNumberOfSymbolTableEntries() const;

  /// Return the symbol-table index of the entry at \p SymEntPtr.
  ///
  /// \param SymEntPtr Address of a symbol table entry in this object.
  /// @return The symbol-table index of the entry at \p SymEntPtr.
  uint32_t getSymbolIndex(uintptr_t SymEntPtr) const;
  /// Return the size associated with symbol \p Symb.
  ///
  /// \param Symb Symbol data reference.
  /// @return The size associated with symbol \p Symb.
  uint64_t getSymbolSize(DataRefImpl Symb) const;
  /// Return the address of the symbol table entry at index \p Idx.
  ///
  /// \param Idx Zero-based symbol table index.
  /// @return The address of the symbol table entry at index \p Idx.
  uintptr_t getSymbolByIndex(uint32_t Idx) const {
    return reinterpret_cast<uintptr_t>(SymbolTblPtr) +
           XCOFF::SymbolTableEntrySize * Idx;
  }
  /// Return the address of the symbol table entry at \p SymbolTableIndex.
  ///
  /// \param SymbolTableIndex Zero-based symbol table index.
  /// @return The address of the symbol table entry at \p SymbolTableIndex.
  uintptr_t getSymbolEntryAddressByIndex(uint32_t SymbolTableIndex) const;
  /// Return the name of the symbol at \p SymbolTableIndex.
  ///
  /// \param SymbolTableIndex Zero-based symbol table index.
  /// @return The name of the symbol at \p SymbolTableIndex.
  Expected<StringRef> getSymbolNameByIndex(uint32_t SymbolTableIndex) const;

  /// Return the C_FILE name from auxiliary entry \p CFileEntPtr.
  ///
  /// \param CFileEntPtr Pointer to a file auxiliary entry.
  /// @return The C_FILE name from auxiliary entry \p CFileEntPtr.
  Expected<StringRef> getCFileName(const XCOFFFileAuxEnt *CFileEntPtr) const;
  /// Return the size of the optional auxiliary header.
  ///
  /// @return The size of the optional auxiliary header.
  uint16_t getOptionalHeaderSize() const;
  /// Return the file-header flags.
  ///
  /// @return The file-header flags.
  uint16_t getFlags() const;

  // Section header table related interfaces.
  /// Return the 32-bit section header table.
  ///
  /// @return The 32-bit section header table.
  ArrayRef<XCOFFSectionHeader32> sections32() const;
  /// Return the 64-bit section header table.
  ///
  /// @return The 64-bit section header table.
  ArrayRef<XCOFFSectionHeader64> sections64() const;

  /// Return the flags field of section \p Sec.
  ///
  /// \param Sec Section data reference.
  /// @return The flags field of section \p Sec.
  int32_t getSectionFlags(DataRefImpl Sec) const;
  /// Return a DataRefImpl for the section with 1-based number \p Num.
  ///
  /// \param Num One-based XCOFF section number.
  /// @return A DataRefImpl for the section with 1-based number \p Num.
  Expected<DataRefImpl> getSectionByNum(int16_t Num) const;

  /// Return the file offset of raw data for the first section of type \p SectType.
  ///
  /// \param SectType Section type flag to search for.
  /// @return The file offset of raw data for the first section of type \p SectType.
  Expected<uintptr_t>
  getSectionFileOffsetToRawData(XCOFF::SectionTypeFlags SectType) const;

  /// Assert that \p SymbolEntPtr points into this object's symbol table.
  ///
  /// \param SymbolEntPtr Address expected to be a symbol table entry.
  void checkSymbolEntryPointer(uintptr_t SymbolEntPtr) const;

  // Relocation-related interfaces.
  /// Return the number of relocation entries for section \p Sec.
  ///
  /// \param Sec Section header whose relocation count is requested.
  /// @return The number of relocation entries for section \p Sec.
  template <typename T>
  Expected<uint32_t>
  getNumberOfRelocationEntries(const XCOFFSectionHeader<T> &Sec) const;

  /// Return the relocation entries for section \p Sec.
  ///
  /// \param Sec Section header whose relocations are requested.
  /// @return The relocation entries for section \p Sec.
  template <typename Shdr, typename Reloc>
  Expected<ArrayRef<Reloc>> relocations(const Shdr &Sec) const;

  // Loader section related interfaces.
  /// Return the contents of the import file table from the loader section.
  ///
  /// @return The contents of the import file table from the loader section.
  Expected<StringRef> getImportFileTable() const;

  // Exception-related interface.
  /// Return the exception-section entries of type \c ExceptEnt.
  ///
  /// @return The exception-section entries of type \c ExceptEnt.
  template <typename ExceptEnt>
  Expected<ArrayRef<ExceptEnt>> getExceptionEntries() const;

  /// Return the string-table entry at byte offset \p Offset.
  ///
  /// \param Offset Byte offset into the string table.
  /// @return The string-table entry at byte offset \p Offset.
  Expected<StringRef> getStringTableEntry(uint32_t Offset) const;

  /// Return the entire string table as a StringRef.
  ///
  /// @return The entire string table as a StringRef.
  StringRef getStringTable() const;

  /// Return the auxiliary type stored at \p AuxEntryAddress (64-bit only).
  ///
  /// \param AuxEntryAddress Address of a 64-bit auxiliary entry.
  /// @return The auxiliary type stored at \p AuxEntryAddress (64-bit only).
  const XCOFF::SymbolAuxType *getSymbolAuxType(uintptr_t AuxEntryAddress) const;

  /// Advance \p CurrentAddress by \p Distance symbol table entries.
  ///
  /// \param CurrentAddress Address of a symbol table entry.
  /// \param Distance Number of entries to advance.
  /// @return The address \p Distance entries after \p CurrentAddress.
  static uintptr_t getAdvancedSymbolEntryAddress(uintptr_t CurrentAddress,
                                                 uint32_t Distance);

  /// True when \p B is an XCOFF binary.
  ///
  /// \param B Binary to test.
  /// @return True when \p B is an XCOFF binary.
  static bool classof(const Binary *B) { return B->isXCOFF(); }

  /// Return the CPU name from the auxiliary header, if available.
  ///
  /// @return The CPU name from the auxiliary header, if available.
  std::optional<StringRef> tryGetCPUName() const override;
}; // XCOFFObjectFile

/// Language and CPU type IDs packed into a C_FILE symbol type field.
typedef struct {
  uint8_t LanguageId; ///< Source language identifier for a C_FILE symbol.
  uint8_t CpuTypeId; ///< CPU type identifier for a C_FILE symbol.
} CFileLanguageIdAndTypeIdType;

/// 32-bit XCOFF symbol table entry.
struct XCOFFSymbolEntry32 {
  /// Name stored as an offset into the string table.
  typedef struct {
    support::big32_t Magic; ///< Zero indicates the name is in the string table.
    support::ubig32_t Offset; ///< Byte offset of the name in the string table.
  } NameInStrTblType;

  union {
    char SymbolName[XCOFF::NameSize]; ///< Inline symbol name.
    NameInStrTblType NameInStrTbl; ///< String-table form of the symbol name.
  };

  support::ubig32_t Value; ///< Symbol value; storage class-dependent.
  support::big16_t SectionNumber; ///< Section number for the symbol.

  union {
    support::ubig16_t SymbolType; ///< Symbol type field.
    CFileLanguageIdAndTypeIdType CFileLanguageIdAndTypeId; ///< C_FILE language/CPU IDs.
  };

  XCOFF::StorageClass StorageClass; ///< Storage class of the symbol.
  uint8_t NumberOfAuxEntries; ///< Number of auxiliary entries that follow.
};

/// 64-bit XCOFF symbol table entry.
struct XCOFFSymbolEntry64 {
  support::ubig64_t Value; ///< Symbol value; storage class-dependent.
  support::ubig32_t Offset; ///< Offset of the symbol name in the string table.
  support::big16_t SectionNumber; ///< Section number for the symbol.

  union {
    support::ubig16_t SymbolType; ///< Symbol type field.
    CFileLanguageIdAndTypeIdType CFileLanguageIdAndTypeId; ///< C_FILE language/CPU IDs.
  };

  XCOFF::StorageClass StorageClass; ///< Storage class of the symbol.
  uint8_t NumberOfAuxEntries; ///< Number of auxiliary entries that follow.
};

extern template LLVM_TEMPLATE_ABI Expected<ArrayRef<XCOFFRelocation32>>
XCOFFObjectFile::relocations<XCOFFSectionHeader32, XCOFFRelocation32>(
    const XCOFFSectionHeader32 &Sec) const;
extern template LLVM_TEMPLATE_ABI Expected<ArrayRef<XCOFFRelocation64>>
XCOFFObjectFile::relocations<XCOFFSectionHeader64, XCOFFRelocation64>(
    const XCOFFSectionHeader64 &Sec) const;

/// SymbolRef specialized for XCOFF symbol table entries.
class XCOFFSymbolRef : public SymbolRef {
public:
  /// Magic values for XCOFF symbol name encoding.
  enum {
    NAME_IN_STR_TBL_MAGIC = 0x0 ///< Name is an offset into the string table.
  };

  /// Construct an XCOFF symbol reference owned by \p OwningObjectPtr.
  ///
  /// \param SymEntDataRef Data reference pointing at the symbol table entry.
  /// \param OwningObjectPtr Owning XCOFF object file (must be non-null).
  XCOFFSymbolRef(DataRefImpl SymEntDataRef,
                 const XCOFFObjectFile *OwningObjectPtr)
      : SymbolRef(SymEntDataRef, OwningObjectPtr) {
    assert(OwningObjectPtr && "OwningObjectPtr cannot be nullptr!");
    assert(SymEntDataRef.p != 0 &&
           "Symbol table entry pointer cannot be nullptr!");
  }

  /// Return the underlying 32-bit symbol table entry.
  ///
  /// @return The underlying 32-bit symbol table entry.
  const XCOFFSymbolEntry32 *getSymbol32() const {
    return reinterpret_cast<const XCOFFSymbolEntry32 *>(getRawDataRefImpl().p);
  }

  /// Return the underlying 64-bit symbol table entry.
  ///
  /// @return The underlying 64-bit symbol table entry.
  const XCOFFSymbolEntry64 *getSymbol64() const {
    return reinterpret_cast<const XCOFFSymbolEntry64 *>(getRawDataRefImpl().p);
  }

  /// Return the symbol value for the object's bit width.
  ///
  /// @return The symbol value for the object's bit width.
  uint64_t getValue() const {
    return getObject()->is64Bit() ? getValue64() : getValue32();
  }

  /// Return the 32-bit symbol value.
  ///
  /// @return The 32-bit symbol value.
  uint32_t getValue32() const {
    return reinterpret_cast<const XCOFFSymbolEntry32 *>(getRawDataRefImpl().p)
        ->Value;
  }

  /// Return the 64-bit symbol value.
  ///
  /// @return The 64-bit symbol value.
  uint64_t getValue64() const {
    return reinterpret_cast<const XCOFFSymbolEntry64 *>(getRawDataRefImpl().p)
        ->Value;
  }

  /// Return the size associated with this symbol.
  ///
  /// @return The size associated with this symbol.
  uint64_t getSize() const {
    return getObject()->getSymbolSize(getRawDataRefImpl());
  }

#define GETVALUE(X)                                                            \
  getObject()->is64Bit()                                                       \
      ? reinterpret_cast<const XCOFFSymbolEntry64 *>(getRawDataRefImpl().p)->X \
      : reinterpret_cast<const XCOFFSymbolEntry32 *>(getRawDataRefImpl().p)->X

  /// Return the section number for this symbol.
  ///
  /// @return The section number for this symbol.
  int16_t getSectionNumber() const { return GETVALUE(SectionNumber); }

  /// Return the symbol type field.
  ///
  /// @return The symbol type field.
  uint16_t getSymbolType() const { return GETVALUE(SymbolType); }

  /// Return the language ID for a C_FILE symbol.
  ///
  /// @return The language ID for a C_FILE symbol.
  uint8_t getLanguageIdForCFile() const {
    assert(getStorageClass() == XCOFF::C_FILE &&
           "This interface is for C_FILE only.");
    return GETVALUE(CFileLanguageIdAndTypeId.LanguageId);
  }

  /// Return the CPU type ID for a C_FILE symbol.
  ///
  /// @return The CPU type ID for a C_FILE symbol.
  uint8_t getCPUTypeIddForCFile() const {
    assert(getStorageClass() == XCOFF::C_FILE &&
           "This interface is for C_FILE only.");
    return GETVALUE(CFileLanguageIdAndTypeId.CpuTypeId);
  }

  /// Return the storage class of this symbol.
  ///
  /// @return The storage class of this symbol.
  XCOFF::StorageClass getStorageClass() const { return GETVALUE(StorageClass); }

  /// Return the number of auxiliary entries that follow this symbol.
  ///
  /// @return The number of auxiliary entries that follow this symbol.
  uint8_t getNumberOfAuxEntries() const { return GETVALUE(NumberOfAuxEntries); }

#undef GETVALUE

  /// Return the address of the underlying symbol table entry.
  ///
  /// @return The address of the underlying symbol table entry.
  uintptr_t getEntryAddress() const {
    return getRawDataRefImpl().p;
  }

  /// Return the symbol name.
  ///
  /// @return The symbol name.
  LLVM_ABI Expected<StringRef> getName() const;
  /// True when this symbol represents a function.
  ///
  /// @return True when this symbol represents a function.
  LLVM_ABI Expected<bool> isFunction() const;
  /// True when this symbol has a csect auxiliary entry.
  ///
  /// @return True when this symbol has a csect auxiliary entry.
  LLVM_ABI bool isCsectSymbol() const;
  /// Return the csect auxiliary entry for this symbol.
  ///
  /// @return The csect auxiliary entry for this symbol.
  LLVM_ABI Expected<XCOFFCsectAuxRef> getXCOFFCsectAuxRef() const;

private:
  const XCOFFObjectFile *getObject() const {
    return cast<XCOFFObjectFile>(BasicSymbolRef::getObject());
  }
};

/// Symbol iterator that yields XCOFFSymbolRef values.
class xcoff_symbol_iterator : public symbol_iterator {
public:
  /// Construct from a generic basic_symbol_iterator.
  ///
  /// \param B Basic symbol iterator to wrap.
  xcoff_symbol_iterator(const basic_symbol_iterator &B)
      : symbol_iterator(B) {}

  /// Construct from an XCOFFSymbolRef.
  ///
  /// \param Symbol Symbol to position the iterator at.
  xcoff_symbol_iterator(const XCOFFSymbolRef *Symbol)
      : symbol_iterator(*Symbol) {}

  /// Return a pointer to the current XCOFFSymbolRef.
  ///
  /// @return A pointer to the current XCOFFSymbolRef.
  const XCOFFSymbolRef *operator->() const {
    return static_cast<const XCOFFSymbolRef *>(symbol_iterator::operator->());
  }

  /// Return a reference to the current XCOFFSymbolRef.
  ///
  /// @return A reference to the current XCOFFSymbolRef.
  const XCOFFSymbolRef &operator*() const {
    return static_cast<const XCOFFSymbolRef &>(symbol_iterator::operator*());
  }
};

/// Parsed vector-extension field from an XCOFF traceback table.
class TBVectorExt {
  uint16_t Data;
  SmallString<32> VecParmsInfo;

  TBVectorExt(StringRef TBvectorStrRef, Error &Err);

public:
  /// Parse a vector-extension field from \p TBvectorStrRef.
  ///
  /// \param TBvectorStrRef Bytes encoding the traceback vector extension.
  /// @return The parsed vector extension, or an error on failure.
  LLVM_ABI static Expected<TBVectorExt> create(StringRef TBvectorStrRef);
  /// Return the number of vector registers saved.
  ///
  /// @return The number of vector registers saved.
  LLVM_ABI uint8_t getNumberOfVRSaved() const;
  /// True when vector registers are saved on the stack.
  ///
  /// @return True when vector registers are saved on the stack.
  LLVM_ABI bool isVRSavedOnStack() const;
  /// True when the function has a variable argument list.
  ///
  /// @return True when the function has a variable argument list.
  LLVM_ABI bool hasVarArgs() const;
  /// Return the number of vector parameters.
  ///
  /// @return The number of vector parameters.
  LLVM_ABI uint8_t getNumberOfVectorParms() const;
  /// True when the function uses VMX instructions.
  ///
  /// @return True when the function uses VMX instructions.
  LLVM_ABI bool hasVMXInstruction() const;
  /// Return the encoded vector parameter-type information.
  ///
  /// @return The encoded vector parameter-type information.
  SmallString<32> getVectorParmsInfo() const { return VecParmsInfo; };
};

/// This class provides methods to extract traceback table data from a buffer.
/// The various accessors may reference the buffer provided via the constructor.

class XCOFFTracebackTable {
  const uint8_t *const TBPtr;
  bool Is64BitObj;
  std::optional<SmallString<32>> ParmsType;
  std::optional<uint32_t> TraceBackTableOffset;
  std::optional<uint32_t> HandlerMask;
  std::optional<uint32_t> NumOfCtlAnchors;
  std::optional<SmallVector<uint32_t, 8>> ControlledStorageInfoDisp;
  std::optional<StringRef> FunctionName;
  std::optional<uint8_t> AllocaRegister;
  std::optional<TBVectorExt> VecExt;
  std::optional<uint8_t> ExtensionTable;
  std::optional<uint64_t> EhInfoDisp;

  XCOFFTracebackTable(const uint8_t *Ptr, uint64_t &Size, Error &Err,
                      bool Is64Bit = false);

public:
  /// Parse an XCOFF Traceback Table from \a Ptr with \a Size bytes.
  /// Returns an XCOFFTracebackTable upon successful parsing, otherwise an
  /// Error is returned.
  ///
  /// \param[in] Ptr
  ///   A pointer that points just past the initial 4 bytes of zeros at the
  ///   beginning of an XCOFF Traceback Table.
  ///
  /// \param[in, out] Size
  ///    A pointer that points to the length of the XCOFF Traceback Table.
  ///    If the XCOFF Traceback Table is not parsed successfully or there are
  ///    extra bytes that are not recognized, \a Size will be updated to be the
  ///    size up to the end of the last successfully parsed field of the table.
  ///
  /// \param Is64Bits True when parsing a traceback table from a 64-bit object.
  /// @return The parsed traceback table, or an error on failure.
  LLVM_ABI static Expected<XCOFFTracebackTable>
  create(const uint8_t *Ptr, uint64_t &Size, bool Is64Bits = false);
  /// Return the traceback table version.
  ///
  /// @return The traceback table version.
  LLVM_ABI uint8_t getVersion() const;
  /// Return the language ID encoded in the traceback table.
  ///
  /// @return The language ID encoded in the traceback table.
  LLVM_ABI uint8_t getLanguageID() const;

  /// True when the procedure has global linkage.
  ///
  /// @return True when the procedure has global linkage.
  LLVM_ABI bool isGlobalLinkage() const;
  /// True when an out-of-line epilog or prologue is used.
  ///
  /// @return True when an out-of-line epilog or prologue is used.
  LLVM_ABI bool isOutOfLineEpilogOrPrologue() const;
  /// True when a traceback-table offset field is present.
  ///
  /// @return True when a traceback-table offset field is present.
  LLVM_ABI bool hasTraceBackTableOffset() const;
  /// True when this is an internal procedure.
  ///
  /// @return True when this is an internal procedure.
  LLVM_ABI bool isInternalProcedure() const;
  /// True when controlled storage information is present.
  ///
  /// @return True when controlled storage information is present.
  LLVM_ABI bool hasControlledStorage() const;
  /// True when the procedure is TOC-less.
  ///
  /// @return True when the procedure is TOC-less.
  LLVM_ABI bool isTOCless() const;
  /// True when floating-point operations are present.
  ///
  /// @return True when floating-point operations are present.
  LLVM_ABI bool isFloatingPointPresent() const;
  /// True when FP operation logging or abort is enabled.
  ///
  /// @return True when FP operation logging or abort is enabled.
  LLVM_ABI bool isFloatingPointOperationLogOrAbortEnabled() const;

  /// True when the procedure is an interrupt handler.
  ///
  /// @return True when the procedure is an interrupt handler.
  LLVM_ABI bool isInterruptHandler() const;
  /// True when the function name is present in the traceback table.
  ///
  /// @return True when the function name is present in the traceback table.
  LLVM_ABI bool isFuncNamePresent() const;
  /// True when alloca is used by the procedure.
  ///
  /// @return True when alloca is used by the procedure.
  LLVM_ABI bool isAllocaUsed() const;
  /// Return the on-condition directive field.
  ///
  /// @return The on-condition directive field.
  LLVM_ABI uint8_t getOnConditionDirective() const;
  /// True when the CR register is saved.
  ///
  /// @return True when the CR register is saved.
  LLVM_ABI bool isCRSaved() const;
  /// True when the LR register is saved.
  ///
  /// @return True when the LR register is saved.
  LLVM_ABI bool isLRSaved() const;

  /// True when the back chain is stored.
  ///
  /// @return True when the back chain is stored.
  LLVM_ABI bool isBackChainStored() const;
  /// True when the fixup bit is set.
  ///
  /// @return True when the fixup bit is set.
  LLVM_ABI bool isFixup() const;
  /// Return the number of floating-point registers saved.
  ///
  /// @return The number of floating-point registers saved.
  LLVM_ABI uint8_t getNumOfFPRsSaved() const;

  /// True when vector information is present.
  ///
  /// @return True when vector information is present.
  LLVM_ABI bool hasVectorInfo() const;
  /// True when an extension table is present.
  ///
  /// @return True when an extension table is present.
  LLVM_ABI bool hasExtensionTable() const;
  /// Return the number of general-purpose registers saved.
  ///
  /// @return The number of general-purpose registers saved.
  LLVM_ABI uint8_t getNumOfGPRsSaved() const;

  /// Return the number of fixed-point parameters.
  ///
  /// @return The number of fixed-point parameters.
  LLVM_ABI uint8_t getNumberOfFixedParms() const;

  /// Return the number of floating-point parameters.
  ///
  /// @return The number of floating-point parameters.
  LLVM_ABI uint8_t getNumberOfFPParms() const;
  /// True when parameters are passed on the stack.
  ///
  /// @return True when parameters are passed on the stack.
  LLVM_ABI bool hasParmsOnStack() const;

  /// Return the optional encoded parameter-type string.
  ///
  /// @return The optional encoded parameter-type string.
  const std::optional<SmallString<32>> &getParmsType() const {
    return ParmsType;
  }
  /// Return the optional traceback-table offset field.
  ///
  /// @return The optional traceback-table offset field.
  const std::optional<uint32_t> &getTraceBackTableOffset() const {
    return TraceBackTableOffset;
  }
  /// Return the optional exception handler mask.
  ///
  /// @return The optional exception handler mask.
  const std::optional<uint32_t> &getHandlerMask() const { return HandlerMask; }
  /// Return the optional number of controlled-storage anchors.
  ///
  /// @return The optional number of controlled-storage anchors.
  const std::optional<uint32_t> &getNumOfCtlAnchors() {
    return NumOfCtlAnchors;
  }
  /// Return optional controlled-storage info displacements.
  ///
  /// @return The optional controlled-storage info displacements.
  const std::optional<SmallVector<uint32_t, 8>> &
  getControlledStorageInfoDisp() {
    return ControlledStorageInfoDisp;
  }
  /// Return the optional function name from the traceback table.
  ///
  /// @return The optional function name from the traceback table.
  const std::optional<StringRef> &getFunctionName() const {
    return FunctionName;
  }
  /// Return the optional register used for alloca.
  ///
  /// @return The optional register used for alloca.
  const std::optional<uint8_t> &getAllocaRegister() const {
    return AllocaRegister;
  }
  /// Return the optional vector-extension information.
  ///
  /// @return The optional vector-extension information.
  const std::optional<TBVectorExt> &getVectorExt() const { return VecExt; }
  /// Return the optional extension-table byte.
  ///
  /// @return The optional extension-table byte.
  const std::optional<uint8_t> &getExtensionTable() const {
    return ExtensionTable;
  }
  /// Return the optional exception-handling info displacement.
  ///
  /// @return The optional exception-handling info displacement.
  const std::optional<uint64_t> &getEhInfoDisp() const { return EhInfoDisp; }
};

/// True when \p Bytes begin with the XCOFF traceback-table initial zeros.
///
/// \param Bytes Candidate bytes at a potential traceback-table start.
/// @return True when \p Bytes begin with the XCOFF traceback-table initial zeros.
LLVM_ABI bool doesXCOFFTracebackTableBegin(ArrayRef<uint8_t> Bytes);
} // namespace object
} // namespace llvm

#endif // LLVM_OBJECT_XCOFFOBJECTFILE_H
