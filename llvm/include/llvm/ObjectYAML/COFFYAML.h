//===- COFFYAML.h - COFF YAMLIO implementation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation of COFF.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_COFFYAML_H
#define LLVM_OBJECTYAML_COFFYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFF.h"
#include "llvm/ObjectYAML/CodeViewYAMLDebugSections.h"
#include "llvm/ObjectYAML/CodeViewYAMLTypeHashing.h"
#include "llvm/ObjectYAML/CodeViewYAMLTypes.h"
#include "llvm/ObjectYAML/YAML.h"
#include <cstdint>
#include <optional>
#include <vector>

namespace llvm {

namespace yaml {
class ContiguousBlobAccumulator;
}

namespace COFF {

/// Combine two COFF file characteristic bitmasks with a bitwise OR.
/// \param a First characteristic flags.
/// \param b Second characteristic flags.
/// \return The combined characteristic flags.
inline Characteristics operator|(Characteristics a, Characteristics b) {
  uint32_t Ret = static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
  return static_cast<Characteristics>(Ret);
}

/// Combine two COFF section characteristic bitmasks with a bitwise OR.
/// \param a First section characteristic flags.
/// \param b Second section characteristic flags.
/// \return The combined section characteristic flags.
inline SectionCharacteristics operator|(SectionCharacteristics a,
                                        SectionCharacteristics b) {
  uint32_t Ret = static_cast<uint32_t>(a) | static_cast<uint32_t>(b);
  return static_cast<SectionCharacteristics>(Ret);
}

/// Combine two PE DLL characteristic bitmasks with a bitwise OR.
/// \param a First DLL characteristic flags.
/// \param b Second DLL characteristic flags.
/// \return The combined DLL characteristic flags.
inline DLLCharacteristics operator|(DLLCharacteristics a,
                                    DLLCharacteristics b) {
  uint16_t Ret = static_cast<uint16_t>(a) | static_cast<uint16_t>(b);
  return static_cast<DLLCharacteristics>(Ret);
}

} // end namespace COFF

/// YAML representations of COFF and PE object files.
///
/// The structure of the yaml files is not an exact 1:1 match to COFF. In order
/// to use yaml::IO, we use these structures which are closer to the source.
namespace COFFYAML {

/// Strong typedef for a COFF COMDAT selection type value.
struct COMDATType {
  /// Construct a zero-initialized COMDAT type.
  COMDATType() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  COMDATType(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c COMDATType.
  /// \param v Value to copy.
  COMDATType(const COMDATType &v) = default;
  /// Copy-assign from another \c COMDATType.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  COMDATType &operator=(const COMDATType &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  COMDATType &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const COMDATType &rhs) const { return value == rhs.value; }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const COMDATType &rhs) const { return value < rhs.value; }
  /// Stored COMDAT selection type value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

/// Strong typedef for COFF weak-external characteristics.
struct WeakExternalCharacteristics {
  /// Construct a zero-initialized weak-external characteristics value.
  WeakExternalCharacteristics() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  WeakExternalCharacteristics(const uint32_t v) : value(v) {}
  /// Copy-construct from another \c WeakExternalCharacteristics.
  /// \param v Value to copy.
  WeakExternalCharacteristics(const WeakExternalCharacteristics &v) = default;
  /// Copy-assign from another \c WeakExternalCharacteristics.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  WeakExternalCharacteristics &
  operator=(const WeakExternalCharacteristics &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  WeakExternalCharacteristics &operator=(const uint32_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint32_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const WeakExternalCharacteristics &rhs) const {
    return value == rhs.value;
  }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint32_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const WeakExternalCharacteristics &rhs) const {
    return value < rhs.value;
  }
  /// Stored weak-external characteristics value.
  uint32_t value;
  /// Underlying integral base type.
  using BaseType = uint32_t;
};

/// Strong typedef for a COFF auxiliary symbol type tag.
struct AuxSymbolType {
  /// Construct a zero-initialized auxiliary symbol type.
  AuxSymbolType() = default;
  /// Construct from base value \p v.
  /// \param v Value to store.
  AuxSymbolType(const uint8_t v) : value(v) {}
  /// Copy-construct from another \c AuxSymbolType.
  /// \param v Value to copy.
  AuxSymbolType(const AuxSymbolType &v) = default;
  /// Copy-assign from another \c AuxSymbolType.
  /// \param rhs Value to assign.
  /// \return A reference to this object.
  AuxSymbolType &operator=(const AuxSymbolType &rhs) = default;
  /// Assign from base value \p rhs.
  /// \param rhs Base value to assign.
  /// \return A reference to this object.
  AuxSymbolType &operator=(const uint8_t &rhs) {
    value = rhs;
    return *this;
  }
  /// Convert to a const reference to the base value.
  /// \return A const reference to the stored base value.
  operator const uint8_t &() const { return value; }
  /// Return true if this equals \p rhs.
  /// \param rhs Value to compare.
  /// \return True if the values are equal.
  bool operator==(const AuxSymbolType &rhs) const {
    return value == rhs.value;
  }
  /// Return true if this equals base value \p rhs.
  /// \param rhs Base value to compare.
  /// \return True if the values are equal.
  bool operator==(const uint8_t &rhs) const { return value == rhs; }
  /// Return true if this is less than \p rhs.
  /// \param rhs Value to compare.
  /// \return True if this value is less than \p rhs.
  bool operator<(const AuxSymbolType &rhs) const { return value < rhs.value; }
  /// Stored auxiliary symbol type value.
  uint8_t value;
  /// Underlying integral base type.
  using BaseType = uint8_t;
};

/// YAML representation of a COFF relocation entry.
struct Relocation {
  /// Virtual address of the location to relocate.
  uint32_t VirtualAddress;
  /// Relocation type specific to the target architecture.
  uint16_t Type;

  /// Optional name of the symbol this relocation refers to.
  ///
  /// Normally a Relocation can refer to the symbol via its name. It can also
  /// use a direct symbol table index instead (with no name specified), allowing
  /// disambiguating between multiple symbols with the same name or crafting
  /// intentionally broken files for testing.
  StringRef SymbolName;
  /// Optional direct symbol-table index used instead of \c SymbolName.
  std::optional<uint32_t> SymbolTableIndex;
};

/// One piece of structured section contents for YAML.
struct SectionDataEntry {
  /// Optional 32-bit integer payload for this entry.
  std::optional<uint32_t> UInt32;
  /// Optional raw binary payload for this entry.
  yaml::BinaryRef Binary;
  /// Optional 32-bit PE load configuration structure.
  std::optional<object::coff_load_configuration32> LoadConfig32;
  /// Optional 64-bit PE load configuration structure.
  std::optional<object::coff_load_configuration64> LoadConfig64;

  /// Return the serialized size of this entry in bytes.
  /// \return The size of the serialized entry in bytes.
  LLVM_ABI size_t size() const;
  /// Append this entry's binary encoding to \p CBA.
  /// \param CBA Accumulator that receives the encoded bytes.
  LLVM_ABI void writeAsBinary(yaml::ContiguousBlobAccumulator &CBA) const;
};

/// YAML representation of a COFF section.
struct Section {
  /// Native COFF section header fields.
  COFF::section Header;
  /// Optional section alignment override in bytes.
  unsigned Alignment = 0;
  /// Raw section contents when not expressed as structured fields.
  yaml::BinaryRef SectionData;
  /// CodeView \c .debug$S subsections for this section.
  std::vector<CodeViewYAML::YAMLDebugSubsection> DebugS;
  /// CodeView \c .debug$T type records for this section.
  std::vector<CodeViewYAML::LeafRecord> DebugT;
  /// CodeView \c .debug$P type records for this section.
  std::vector<CodeViewYAML::LeafRecord> DebugP;
  /// Optional CodeView \c .debug$H global type hash section.
  std::optional<CodeViewYAML::DebugHSection> DebugH;
  /// Structured section payloads used instead of raw \c SectionData.
  std::vector<SectionDataEntry> StructuredData;
  /// Relocations that apply to this section.
  std::vector<Relocation> Relocations;
  /// Section name as it appears in YAML.
  StringRef Name;

  /// Construct a section with default-initialized fields.
  LLVM_ABI Section();
};

/// YAML representation of a COFF symbol table entry.
struct Symbol {
  /// Native COFF symbol header fields.
  COFF::symbol Header;
  /// Simple (base) type of the symbol.
  COFF::SymbolBaseType SimpleType = COFF::IMAGE_SYM_TYPE_NULL;
  /// Complex (derived) type of the symbol.
  COFF::SymbolComplexType ComplexType = COFF::IMAGE_SYM_DTYPE_NULL;
  /// Optional function-definition auxiliary symbol data.
  std::optional<COFF::AuxiliaryFunctionDefinition> FunctionDefinition;
  /// Optional \c .bf / \c .ef auxiliary symbol data.
  std::optional<COFF::AuxiliarybfAndefSymbol> bfAndefSymbol;
  /// Optional weak-external auxiliary symbol data.
  std::optional<COFF::AuxiliaryWeakExternal> WeakExternal;
  /// Optional source file name associated with a file symbol.
  StringRef File;
  /// Optional section-definition auxiliary symbol data.
  std::optional<COFF::AuxiliarySectionDefinition> SectionDefinition;
  /// Optional CLR token auxiliary symbol data.
  std::optional<COFF::AuxiliaryCLRToken> CLRToken;
  /// Symbol name as it appears in YAML.
  StringRef Name;

  /// Construct a symbol with default-initialized fields.
  LLVM_ABI Symbol();
};

/// YAML representation of an optional PE header.
struct PEHeader {
  /// PE32 optional header fields.
  COFF::PE32Header Header;
  /// Optional PE data directories, indexed by directory type.
  std::optional<COFF::DataDirectory>
      DataDirectories[COFF::NUM_DATA_DIRECTORIES];
};

/// YAML representation of a complete COFF or PE object file.
struct Object {
  /// Optional PE header when describing a PE image.
  std::optional<PEHeader> OptionalHeader;
  /// COFF file header.
  COFF::header Header;
  /// Sections that make up the object.
  std::vector<Section> Sections;
  /// Symbol table entries for the object.
  std::vector<Symbol> Symbols;

  /// Construct an object with default-initialized fields.
  LLVM_ABI Object();
};

} // end namespace COFFYAML

} // end namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of COFFYAML sections use block formatting.
template <> struct SequenceElementTraits<COFFYAML::Section> {
  /// Emit sequences of COFFYAML sections in block style.
  static const bool flow = false;
};

/// Sequences of COFFYAML symbols use block formatting.
template <> struct SequenceElementTraits<COFFYAML::Symbol> {
  /// Emit sequences of COFFYAML symbols in block style.
  static const bool flow = false;
};

/// Sequences of COFFYAML relocations use block formatting.
template <> struct SequenceElementTraits<COFFYAML::Relocation> {
  /// Emit sequences of COFFYAML relocations in block style.
  static const bool flow = false;
};

/// Sequences of COFFYAML section data entries use block formatting.
template <> struct SequenceElementTraits<COFFYAML::SectionDataEntry> {
  /// Emit sequences of COFFYAML section data entries in block style.
  static const bool flow = false;
};

/// YAMLIO scalar enumeration traits for \c COFFYAML::WeakExternalCharacteristics.
template <>
struct ScalarEnumerationTraits<COFFYAML::WeakExternalCharacteristics> {
  /// Map weak-external characteristic enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Weak-external characteristics value being mapped.
  LLVM_ABI static void
  enumeration(IO &IO, COFFYAML::WeakExternalCharacteristics &Value);
};

/// YAMLIO scalar enumeration traits for \c COFFYAML::AuxSymbolType.
template <>
struct ScalarEnumerationTraits<COFFYAML::AuxSymbolType> {
  /// Map auxiliary symbol type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Auxiliary symbol type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFFYAML::AuxSymbolType &Value);
};

/// YAMLIO scalar enumeration traits for \c COFFYAML::COMDATType.
template <>
struct ScalarEnumerationTraits<COFFYAML::COMDATType> {
  /// Map COMDAT type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value COMDAT type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFFYAML::COMDATType &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::MachineTypes.
template <>
struct ScalarEnumerationTraits<COFF::MachineTypes> {
  /// Map COFF machine type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Machine type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::MachineTypes &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::SymbolBaseType.
template <>
struct ScalarEnumerationTraits<COFF::SymbolBaseType> {
  /// Map COFF symbol base type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Symbol base type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::SymbolBaseType &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::SymbolStorageClass.
template <>
struct ScalarEnumerationTraits<COFF::SymbolStorageClass> {
  /// Map COFF symbol storage class enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Symbol storage class being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::SymbolStorageClass &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::SymbolComplexType.
template <>
struct ScalarEnumerationTraits<COFF::SymbolComplexType> {
  /// Map COFF symbol complex type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Symbol complex type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::SymbolComplexType &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::RelocationTypeI386.
template <>
struct ScalarEnumerationTraits<COFF::RelocationTypeI386> {
  /// Map i386 COFF relocation type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Relocation type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::RelocationTypeI386 &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::RelocationTypeAMD64.
template <>
struct ScalarEnumerationTraits<COFF::RelocationTypeAMD64> {
  /// Map AMD64 COFF relocation type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Relocation type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::RelocationTypeAMD64 &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::RelocationTypesMips.
template <> struct ScalarEnumerationTraits<COFF::RelocationTypesMips> {
  /// Map MIPS COFF relocation type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Relocation type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::RelocationTypesMips &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::RelocationTypesARM.
template <>
struct ScalarEnumerationTraits<COFF::RelocationTypesARM> {
  /// Map ARM COFF relocation type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Relocation type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::RelocationTypesARM &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::RelocationTypesARM64.
template <>
struct ScalarEnumerationTraits<COFF::RelocationTypesARM64> {
  /// Map ARM64 COFF relocation type enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Relocation type being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::RelocationTypesARM64 &Value);
};

/// YAMLIO scalar enumeration traits for \c COFF::WindowsSubsystem.
template <>
struct ScalarEnumerationTraits<COFF::WindowsSubsystem> {
  /// Map Windows subsystem enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Windows subsystem being mapped.
  LLVM_ABI static void enumeration(IO &IO, COFF::WindowsSubsystem &Value);
};

/// YAMLIO scalar bitset traits for \c COFF::Characteristics.
template <>
struct ScalarBitSetTraits<COFF::Characteristics> {
  /// Map COFF file characteristic flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Characteristic flags being mapped.
  LLVM_ABI static void bitset(IO &IO, COFF::Characteristics &Value);
};

/// YAMLIO scalar bitset traits for \c COFF::SectionCharacteristics.
template <>
struct ScalarBitSetTraits<COFF::SectionCharacteristics> {
  /// Map COFF section characteristic flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Section characteristic flags being mapped.
  LLVM_ABI static void bitset(IO &IO, COFF::SectionCharacteristics &Value);
};

/// YAMLIO scalar bitset traits for \c COFF::DLLCharacteristics.
template <>
struct ScalarBitSetTraits<COFF::DLLCharacteristics> {
  /// Map PE DLL characteristic flags to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value DLL characteristic flags being mapped.
  LLVM_ABI static void bitset(IO &IO, COFF::DLLCharacteristics &Value);
};

/// YAMLIO mapping traits for \c COFFYAML::Relocation.
template <>
struct MappingTraits<COFFYAML::Relocation> {
  /// Map COFFYAML relocation fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Rel Relocation being mapped.
  LLVM_ABI static void mapping(IO &IO, COFFYAML::Relocation &Rel);
};

/// YAMLIO mapping traits for \c COFFYAML::PEHeader.
template <>
struct MappingTraits<COFFYAML::PEHeader> {
  /// Map PE header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param PH PE header being mapped.
  LLVM_ABI static void mapping(IO &IO, COFFYAML::PEHeader &PH);
};

/// YAMLIO mapping traits for \c COFF::DataDirectory.
template <>
struct MappingTraits<COFF::DataDirectory> {
  /// Map PE data directory fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param DD Data directory being mapped.
  LLVM_ABI static void mapping(IO &IO, COFF::DataDirectory &DD);
};

/// YAMLIO mapping traits for \c COFF::header.
template <>
struct MappingTraits<COFF::header> {
  /// Map COFF file header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param H COFF header being mapped.
  LLVM_ABI static void mapping(IO &IO, COFF::header &H);
};

/// YAMLIO mapping traits for \c COFF::AuxiliaryFunctionDefinition.
template <> struct MappingTraits<COFF::AuxiliaryFunctionDefinition> {
  /// Map auxiliary function-definition fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param AFD Auxiliary function definition being mapped.
  LLVM_ABI static void mapping(IO &IO, COFF::AuxiliaryFunctionDefinition &AFD);
};

/// YAMLIO mapping traits for \c COFF::AuxiliarybfAndefSymbol.
template <> struct MappingTraits<COFF::AuxiliarybfAndefSymbol> {
  /// Map auxiliary \c .bf / \c .ef symbol fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param AAS Auxiliary bf/ef symbol being mapped.
  LLVM_ABI static void mapping(IO &IO, COFF::AuxiliarybfAndefSymbol &AAS);
};

/// YAMLIO mapping traits for \c COFF::AuxiliaryWeakExternal.
template <> struct MappingTraits<COFF::AuxiliaryWeakExternal> {
  /// Map auxiliary weak-external symbol fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param AWE Auxiliary weak external being mapped.
  LLVM_ABI static void mapping(IO &IO, COFF::AuxiliaryWeakExternal &AWE);
};

/// YAMLIO mapping traits for \c COFF::AuxiliarySectionDefinition.
template <> struct MappingTraits<COFF::AuxiliarySectionDefinition> {
  /// Map auxiliary section-definition fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ASD Auxiliary section definition being mapped.
  LLVM_ABI static void mapping(IO &IO, COFF::AuxiliarySectionDefinition &ASD);
};

/// YAMLIO mapping traits for \c COFF::AuxiliaryCLRToken.
template <> struct MappingTraits<COFF::AuxiliaryCLRToken> {
  /// Map auxiliary CLR token fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ACT Auxiliary CLR token being mapped.
  LLVM_ABI static void mapping(IO &IO, COFF::AuxiliaryCLRToken &ACT);
};

/// YAMLIO mapping traits for \c object::coff_load_configuration32.
template <> struct MappingTraits<object::coff_load_configuration32> {
  /// Map 32-bit PE load configuration fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ACT Load configuration being mapped.
  LLVM_ABI static void mapping(IO &IO, object::coff_load_configuration32 &ACT);
};

/// YAMLIO mapping traits for \c object::coff_load_configuration64.
template <> struct MappingTraits<object::coff_load_configuration64> {
  /// Map 64-bit PE load configuration fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ACT Load configuration being mapped.
  LLVM_ABI static void mapping(IO &IO, object::coff_load_configuration64 &ACT);
};

/// YAMLIO mapping traits for \c object::coff_load_config_code_integrity.
template <> struct MappingTraits<object::coff_load_config_code_integrity> {
  /// Map PE load-config code-integrity fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ACT Code-integrity structure being mapped.
  LLVM_ABI static void mapping(IO &IO,
                               object::coff_load_config_code_integrity &ACT);
};

/// YAMLIO mapping traits for \c COFFYAML::Symbol.
template <>
struct MappingTraits<COFFYAML::Symbol> {
  /// Map COFFYAML symbol fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param S Symbol being mapped.
  LLVM_ABI static void mapping(IO &IO, COFFYAML::Symbol &S);
};

/// YAMLIO mapping traits for \c COFFYAML::SectionDataEntry.
template <> struct MappingTraits<COFFYAML::SectionDataEntry> {
  /// Map section data entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Sec Section data entry being mapped.
  LLVM_ABI static void mapping(IO &IO, COFFYAML::SectionDataEntry &Sec);
};

/// YAMLIO mapping traits for \c COFFYAML::Section.
template <>
struct MappingTraits<COFFYAML::Section> {
  /// Map COFFYAML section fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Sec Section being mapped.
  LLVM_ABI static void mapping(IO &IO, COFFYAML::Section &Sec);
};

/// YAMLIO mapping traits for \c COFFYAML::Object.
template <>
struct MappingTraits<COFFYAML::Object> {
  /// Map COFFYAML object fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Object being mapped.
  LLVM_ABI static void mapping(IO &IO, COFFYAML::Object &Obj);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_COFFYAML_H
