//===----- XCOFFYAML.h - XCOFF YAMLIO implementation ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation of XCOFF.
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_OBJECTYAML_XCOFFYAML_H
#define LLVM_OBJECTYAML_XCOFFYAML_H

#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/ObjectYAML/YAML.h"
#include <optional>
#include <vector>

namespace llvm {
/// YAML representations of XCOFF object files.
namespace XCOFFYAML {

/// YAML representation of an XCOFF file header.
struct FileHeader {
  /// Magic number identifying the XCOFF format (32-bit or 64-bit).
  llvm::yaml::Hex16 Magic;
  /// Number of sections in the object file.
  uint16_t NumberOfSections;
  /// Time stamp of the object file.
  int32_t TimeStamp;
  /// File offset of the symbol table.
  llvm::yaml::Hex64 SymbolTableOffset;
  /// Number of entries in the symbol table.
  int32_t NumberOfSymTableEntries;
  /// Size in bytes of the optional auxiliary header.
  uint16_t AuxHeaderSize;
  /// File header flags.
  llvm::yaml::Hex16 Flags;
};

/// YAML representation of an XCOFF optional auxiliary header.
struct AuxiliaryHeader {
  /// Auxiliary header magic number.
  std::optional<llvm::yaml::Hex16> Magic;
  /// Auxiliary header version.
  std::optional<llvm::yaml::Hex16> Version;
  /// Virtual address of the text section.
  std::optional<llvm::yaml::Hex64> TextStartAddr;
  /// Virtual address of the data section.
  std::optional<llvm::yaml::Hex64> DataStartAddr;
  /// Virtual address of the TOC anchor.
  std::optional<llvm::yaml::Hex64> TOCAnchorAddr;
  /// Section number of the entry point.
  std::optional<uint16_t> SecNumOfEntryPoint;
  /// Section number of the text section.
  std::optional<uint16_t> SecNumOfText;
  /// Section number of the data section.
  std::optional<uint16_t> SecNumOfData;
  /// Section number of the TOC section.
  std::optional<uint16_t> SecNumOfTOC;
  /// Section number of the loader section.
  std::optional<uint16_t> SecNumOfLoader;
  /// Section number of the BSS section.
  std::optional<uint16_t> SecNumOfBSS;
  /// Maximum alignment of the text section.
  std::optional<llvm::yaml::Hex16> MaxAlignOfText;
  /// Maximum alignment of the data section.
  std::optional<llvm::yaml::Hex16> MaxAlignOfData;
  /// Module type identifier.
  std::optional<llvm::yaml::Hex16> ModuleType;
  /// CPU flag field.
  std::optional<llvm::yaml::Hex8> CpuFlag;
  /// CPU type field.
  std::optional<llvm::yaml::Hex8> CpuType;
  /// Text page size.
  std::optional<llvm::yaml::Hex8> TextPageSize;
  /// Data page size.
  std::optional<llvm::yaml::Hex8> DataPageSize;
  /// Stack page size.
  std::optional<llvm::yaml::Hex8> StackPageSize;
  /// Combined flag and thread-local data alignment field.
  std::optional<llvm::yaml::Hex8> FlagAndTDataAlignment;
  /// Size of the text section in bytes.
  std::optional<llvm::yaml::Hex64> TextSize;
  /// Size of initialized data in bytes.
  std::optional<llvm::yaml::Hex64> InitDataSize;
  /// Size of BSS data in bytes.
  std::optional<llvm::yaml::Hex64> BssDataSize;
  /// Virtual address of the entry point.
  std::optional<llvm::yaml::Hex64> EntryPointAddr;
  /// Maximum stack size in bytes.
  std::optional<llvm::yaml::Hex64> MaxStackSize;
  /// Maximum data size in bytes.
  std::optional<llvm::yaml::Hex64> MaxDataSize;
  /// Section number of the thread-local data section.
  std::optional<uint16_t> SecNumOfTData;
  /// Section number of the thread-local BSS section.
  std::optional<uint16_t> SecNumOfTBSS;
  /// Auxiliary header flags.
  std::optional<llvm::yaml::Hex16> Flag;
};

/// YAML representation of an XCOFF relocation entry.
struct Relocation {
  /// Virtual address of the location being relocated.
  llvm::yaml::Hex64 VirtualAddress;
  /// Symbol table index of the symbol referenced by this relocation.
  llvm::yaml::Hex64 SymbolIndex;
  /// Relocation info byte.
  llvm::yaml::Hex8 Info;
  /// Relocation type.
  llvm::yaml::Hex8 Type;
};

/// YAML representation of an XCOFF section.
struct Section {
  /// Section name.
  StringRef SectionName;
  /// Virtual address of the section.
  llvm::yaml::Hex64 Address;
  /// Size of the section in bytes.
  llvm::yaml::Hex64 Size;
  /// File offset of the section data.
  llvm::yaml::Hex64 FileOffsetToData;
  /// File offset of the section's relocation entries.
  llvm::yaml::Hex64 FileOffsetToRelocations;
  /// File offset of the section's line number entries.
  ///
  /// Line number pointer. Not supported yet.
  llvm::yaml::Hex64 FileOffsetToLineNumbers;
  /// Number of relocation entries in this section.
  llvm::yaml::Hex16 NumberOfRelocations;
  /// Number of line number entries in this section.
  ///
  /// Line number counts. Not supported yet.
  llvm::yaml::Hex16 NumberOfLineNumbers;
  /// Section type and attribute flags.
  uint32_t Flags;
  /// Optional DWARF section subtype when this is a DWARF section.
  std::optional<XCOFF::DwarfSectionSubtypeFlags> SectionSubtype;
  /// Raw section contents.
  yaml::BinaryRef SectionData;
  /// Relocations that apply to this section.
  std::vector<Relocation> Relocations;
};

/// Discriminator for the concrete kind of an XCOFF auxiliary symbol entry.
enum AuxSymbolType : uint8_t {
  /// Exception auxiliary entry.
  AUX_EXCEPT = 255,
  /// Function auxiliary entry.
  AUX_FCN = 254,
  /// Block / symbol auxiliary entry.
  AUX_SYM = 253,
  /// File auxiliary entry.
  AUX_FILE = 252,
  /// Csect auxiliary entry.
  AUX_CSECT = 251,
  /// Section (DWARF) auxiliary entry.
  AUX_SECT = 250,
  /// Stat section auxiliary entry (XCOFF32 only).
  AUX_STAT = 249
};

/// Base YAML representation of an XCOFF auxiliary symbol table entry.
struct LLVM_ABI AuxSymbolEnt {
  /// Kind of this auxiliary symbol entry.
  AuxSymbolType Type;

  /// Construct an auxiliary symbol entry of the given kind.
  /// \param T Auxiliary symbol type discriminator.
  explicit AuxSymbolEnt(AuxSymbolType T) : Type(T) {}
  /// Destroy an auxiliary symbol entry.
  virtual ~AuxSymbolEnt();
};

/// YAML representation of an XCOFF file auxiliary symbol entry.
struct FileAuxEnt : AuxSymbolEnt {
  /// File name or other string stored in this auxiliary entry.
  std::optional<StringRef> FileNameOrString;
  /// Interpretation of \c FileNameOrString.
  std::optional<XCOFF::CFileStringType> FileStringType;

  /// Construct a file auxiliary symbol entry.
  FileAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_FILE) {}
  /// Return true if \p S is a file auxiliary symbol entry.
  /// \param S Auxiliary symbol entry to test.
  /// \return True when \p S has type \c AUX_FILE.
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_FILE;
  }
};

/// YAML representation of an XCOFF csect auxiliary symbol entry.
struct CsectAuxEnt : AuxSymbolEnt {
  // Only for XCOFF32.
  /// Section number or length (XCOFF32 only).
  std::optional<uint32_t> SectionOrLength;
  /// Stab info index (XCOFF32 only).
  std::optional<uint32_t> StabInfoIndex;
  /// Stab section number (XCOFF32 only).
  std::optional<uint16_t> StabSectNum;
  // Only for XCOFF64.
  /// Low 32 bits of the section number or length (XCOFF64 only).
  std::optional<uint32_t> SectionOrLengthLo;
  /// High 32 bits of the section number or length (XCOFF64 only).
  std::optional<uint32_t> SectionOrLengthHi;
  // Common fields for both XCOFF32 and XCOFF64.
  /// Parameter type-check hash index.
  std::optional<uint32_t> ParameterHashIndex;
  /// Type-check section number.
  std::optional<uint16_t> TypeChkSectNum;
  /// Symbol type within the csect.
  std::optional<XCOFF::SymbolType> SymbolType;
  /// Symbol alignment as a power of two.
  std::optional<uint8_t> SymbolAlignment;
  /// Combined symbol alignment and type encoding.
  ///
  /// The two previous values can be encoded as a single value.
  std::optional<uint8_t> SymbolAlignmentAndType;
  /// Storage mapping class of the csect.
  std::optional<XCOFF::StorageMappingClass> StorageMappingClass;

  /// Construct a csect auxiliary symbol entry.
  CsectAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_CSECT) {}
  /// Return true if \p S is a csect auxiliary symbol entry.
  /// \param S Auxiliary symbol entry to test.
  /// \return True when \p S has type \c AUX_CSECT.
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_CSECT;
  }
};

/// YAML representation of an XCOFF function auxiliary symbol entry.
struct FunctionAuxEnt : AuxSymbolEnt {
  /// Offset to the exception table (XCOFF32 only).
  std::optional<uint32_t> OffsetToExceptionTbl;
  /// Pointer to the line number table for this function.
  std::optional<uint64_t> PtrToLineNum;
  /// Size of the function in bytes.
  std::optional<uint32_t> SizeOfFunction;
  /// Symbol table index of the next symbol beyond this function.
  std::optional<int32_t> SymIdxOfNextBeyond;

  /// Construct a function auxiliary symbol entry.
  FunctionAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_FCN) {}
  /// Return true if \p S is a function auxiliary symbol entry.
  /// \param S Auxiliary symbol entry to test.
  /// \return True when \p S has type \c AUX_FCN.
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_FCN;
  }
};

/// YAML representation of an XCOFF exception auxiliary symbol entry.
///
/// Only for XCOFF64.
struct ExcpetionAuxEnt : AuxSymbolEnt {
  /// Offset to the exception table.
  std::optional<uint64_t> OffsetToExceptionTbl;
  /// Size of the function in bytes.
  std::optional<uint32_t> SizeOfFunction;
  /// Symbol table index of the next symbol beyond this function.
  std::optional<int32_t> SymIdxOfNextBeyond;

  /// Construct an exception auxiliary symbol entry.
  ExcpetionAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_EXCEPT) {}
  /// Return true if \p S is an exception auxiliary symbol entry.
  /// \param S Auxiliary symbol entry to test.
  /// \return True when \p S has type \c AUX_EXCEPT.
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_EXCEPT;
  }
};

/// YAML representation of an XCOFF block auxiliary symbol entry.
struct BlockAuxEnt : AuxSymbolEnt {
  // Only for XCOFF32.
  /// High half of the source line number (XCOFF32 only).
  std::optional<uint16_t> LineNumHi;
  /// Low half of the source line number (XCOFF32 only).
  std::optional<uint16_t> LineNumLo;
  // Only for XCOFF64.
  /// Source line number (XCOFF64 only).
  std::optional<uint32_t> LineNum;

  /// Construct a block auxiliary symbol entry.
  BlockAuxEnt() : AuxSymbolEnt(AuxSymbolType::AUX_SYM) {}
  /// Return true if \p S is a block auxiliary symbol entry.
  /// \param S Auxiliary symbol entry to test.
  /// \return True when \p S has type \c AUX_SYM.
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_SYM;
  }
};

/// YAML representation of an XCOFF DWARF section auxiliary symbol entry.
struct SectAuxEntForDWARF : AuxSymbolEnt {
  /// Length of the portion of the section described by this entry.
  std::optional<uint32_t> LengthOfSectionPortion;
  /// Number of relocation entries for this section portion.
  std::optional<uint32_t> NumberOfRelocEnt;

  /// Construct a DWARF section auxiliary symbol entry.
  SectAuxEntForDWARF() : AuxSymbolEnt(AuxSymbolType::AUX_SECT) {}
  /// Return true if \p S is a DWARF section auxiliary symbol entry.
  /// \param S Auxiliary symbol entry to test.
  /// \return True when \p S has type \c AUX_SECT.
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_SECT;
  }
};

/// YAML representation of an XCOFF stat section auxiliary symbol entry.
///
/// Only for XCOFF32.
struct SectAuxEntForStat : AuxSymbolEnt {
  /// Length of the section in bytes.
  std::optional<uint32_t> SectionLength;
  /// Number of relocation entries for the section.
  std::optional<uint16_t> NumberOfRelocEnt;
  /// Number of line number entries for the section.
  std::optional<uint16_t> NumberOfLineNum;

  /// Construct a stat section auxiliary symbol entry.
  SectAuxEntForStat() : AuxSymbolEnt(AuxSymbolType::AUX_STAT) {}
  /// Return true if \p S is a stat section auxiliary symbol entry.
  /// \param S Auxiliary symbol entry to test.
  /// \return True when \p S has type \c AUX_STAT.
  static bool classof(const AuxSymbolEnt *S) {
    return S->Type == AuxSymbolType::AUX_STAT;
  }
};

/// YAML representation of an XCOFF symbol table entry.
struct Symbol {
  /// Symbol name.
  StringRef SymbolName;
  /// Symbol value; storage class-dependent.
  llvm::yaml::Hex64 Value;
  /// Optional name of the section containing this symbol.
  std::optional<StringRef> SectionName;
  /// Optional one-based section index containing this symbol.
  std::optional<uint16_t> SectionIndex;
  /// Symbol type encoding.
  llvm::yaml::Hex16 Type;
  /// Symbol storage class.
  XCOFF::StorageClass StorageClass;
  /// Optional number of auxiliary entries that follow this symbol.
  std::optional<uint8_t> NumberOfAuxEntries;
  /// Auxiliary symbol entries associated with this symbol.
  std::vector<std::unique_ptr<AuxSymbolEnt>> AuxEntries;
};

/// YAML representation of an XCOFF string table.
struct StringTable {
  /// The total size of the string table.
  std::optional<uint32_t> ContentSize;
  /// The value of the length field for the first 4 bytes of the table.
  std::optional<uint32_t> Length;
  /// Individual strings stored in the table.
  std::optional<std::vector<StringRef>> Strings;
  /// Raw string table bytes when not represented as individual strings.
  std::optional<yaml::BinaryRef> RawContent;
};

/// YAML representation of a complete XCOFF object file.
struct Object {
  /// XCOFF file header.
  FileHeader Header;
  /// Optional auxiliary header.
  std::optional<AuxiliaryHeader> AuxHeader;
  /// Sections in the object.
  std::vector<Section> Sections;
  /// Symbol table entries.
  std::vector<Symbol> Symbols;
  /// String table.
  StringTable StrTbl;
  /// Construct an empty XCOFF YAML object.
  LLVM_ABI Object();
};
} // namespace XCOFFYAML
} // namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of XCOFF symbols use block formatting.
template <> struct SequenceElementTraits<XCOFFYAML::Symbol> {
  /// Emit sequences of XCOFF symbols in block style.
  static const bool flow = false;
};

/// Sequences of XCOFF relocations use block formatting.
template <> struct SequenceElementTraits<XCOFFYAML::Relocation> {
  /// Emit sequences of XCOFF relocations in block style.
  static const bool flow = false;
};

/// Sequences of XCOFF sections use block formatting.
template <> struct SequenceElementTraits<XCOFFYAML::Section> {
  /// Emit sequences of XCOFF sections in block style.
  static const bool flow = false;
};

/// Sequences of owned XCOFF auxiliary symbol entries use block formatting.
template <>
struct SequenceElementTraits<std::unique_ptr<llvm::XCOFFYAML::AuxSymbolEnt>> {
  /// Emit sequences of owned XCOFF auxiliary symbol entries in block style.
  static const bool flow = false;
};

/// YAMLIO scalar bitset traits for \c XCOFF::SectionTypeFlags.
template <> struct ScalarBitSetTraits<XCOFF::SectionTypeFlags> {
  /// Map XCOFF section type flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Section type flags being mapped.
  LLVM_ABI static void bitset(IO &IO, XCOFF::SectionTypeFlags &Value);
};

/// YAMLIO scalar enumeration traits for \c XCOFF::DwarfSectionSubtypeFlags.
template <> struct ScalarEnumerationTraits<XCOFF::DwarfSectionSubtypeFlags> {
  /// Map DWARF section subtype enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value DWARF section subtype being mapped.
  LLVM_ABI static void enumeration(IO &IO,
                                   XCOFF::DwarfSectionSubtypeFlags &Value);
};

/// YAMLIO scalar enumeration traits for \c XCOFF::StorageClass.
template <> struct ScalarEnumerationTraits<XCOFF::StorageClass> {
  /// Map XCOFF storage class enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Storage class being mapped.
  LLVM_ABI static void enumeration(IO &IO, XCOFF::StorageClass &Value);
};

/// YAMLIO scalar enumeration traits for \c XCOFF::StorageMappingClass.
template <> struct ScalarEnumerationTraits<XCOFF::StorageMappingClass> {
  /// Map XCOFF storage mapping class enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Storage mapping class being mapped.
  LLVM_ABI static void enumeration(IO &IO, XCOFF::StorageMappingClass &Value);
};

/// YAMLIO scalar enumeration traits for \c XCOFF::SymbolType.
template <> struct ScalarEnumerationTraits<XCOFF::SymbolType> {
  /// Map XCOFF symbol type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Symbol type being mapped.
  LLVM_ABI static void enumeration(IO &IO, XCOFF::SymbolType &Value);
};

/// YAMLIO scalar enumeration traits for \c XCOFF::CFileStringType.
template <> struct ScalarEnumerationTraits<XCOFF::CFileStringType> {
  /// Map XCOFF file-string type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Type File-string type being mapped.
  LLVM_ABI static void enumeration(IO &IO, XCOFF::CFileStringType &Type);
};

/// YAMLIO scalar enumeration traits for \c XCOFFYAML::AuxSymbolType.
template <> struct ScalarEnumerationTraits<XCOFFYAML::AuxSymbolType> {
  /// Map XCOFF auxiliary symbol type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Type Auxiliary symbol type being mapped.
  LLVM_ABI static void enumeration(IO &IO, XCOFFYAML::AuxSymbolType &Type);
};

/// YAMLIO mapping traits for \c XCOFFYAML::FileHeader.
template <> struct MappingTraits<XCOFFYAML::FileHeader> {
  /// Map XCOFF file header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param H File header being mapped.
  LLVM_ABI static void mapping(IO &IO, XCOFFYAML::FileHeader &H);
};

/// YAMLIO mapping traits for \c XCOFFYAML::AuxiliaryHeader.
template <> struct MappingTraits<XCOFFYAML::AuxiliaryHeader> {
  /// Map XCOFF auxiliary header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param AuxHdr Auxiliary header being mapped.
  LLVM_ABI static void mapping(IO &IO, XCOFFYAML::AuxiliaryHeader &AuxHdr);
};

/// YAMLIO mapping traits for owned XCOFF auxiliary symbol entries.
template <> struct MappingTraits<std::unique_ptr<XCOFFYAML::AuxSymbolEnt>> {
  /// Map an owned XCOFF auxiliary symbol entry to and from YAML.
  /// \param IO YAML input/output state.
  /// \param AuxSym Auxiliary symbol entry being mapped.
  LLVM_ABI static void
  mapping(IO &IO, std::unique_ptr<XCOFFYAML::AuxSymbolEnt> &AuxSym);
};

/// YAMLIO mapping traits for \c XCOFFYAML::Symbol.
template <> struct MappingTraits<XCOFFYAML::Symbol> {
  /// Map XCOFF symbol fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param S Symbol being mapped.
  LLVM_ABI static void mapping(IO &IO, XCOFFYAML::Symbol &S);
};

/// YAMLIO mapping traits for \c XCOFFYAML::Relocation.
template <> struct MappingTraits<XCOFFYAML::Relocation> {
  /// Map XCOFF relocation fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param R Relocation being mapped.
  LLVM_ABI static void mapping(IO &IO, XCOFFYAML::Relocation &R);
};

/// YAMLIO mapping traits for \c XCOFFYAML::Section.
template <> struct MappingTraits<XCOFFYAML::Section> {
  /// Map XCOFF section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Sec Section being mapped.
  LLVM_ABI static void mapping(IO &IO, XCOFFYAML::Section &Sec);
};

/// YAMLIO mapping traits for \c XCOFFYAML::StringTable.
template <> struct MappingTraits<XCOFFYAML::StringTable> {
  /// Map XCOFF string table fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Str String table being mapped.
  LLVM_ABI static void mapping(IO &IO, XCOFFYAML::StringTable &Str);
};

/// YAMLIO mapping traits for \c XCOFFYAML::Object.
template <> struct MappingTraits<XCOFFYAML::Object> {
  /// Map XCOFF object fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Object being mapped.
  LLVM_ABI static void mapping(IO &IO, XCOFFYAML::Object &Obj);
};

} // namespace yaml
} // namespace llvm

#endif // LLVM_OBJECTYAML_XCOFFYAML_H
