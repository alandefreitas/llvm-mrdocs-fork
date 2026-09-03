//===- IFSStub.h ------------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/
///
/// \file
/// This file defines an internal representation of an InterFace Stub.
///
//===-----------------------------------------------------------------------===/

#ifndef LLVM_INTERFACESTUB_IFSSTUB_H
#define LLVM_INTERFACESTUB_IFSSTUB_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/VersionTuple.h"
#include <optional>
#include <vector>

namespace llvm {
namespace ifs {

/// ELF e_machine architecture value used in IFS target descriptions.
typedef uint16_t IFSArch;

/// Symbol type in an InterFace Stub, corresponding to ELF STT_* values.
enum class IFSSymbolType {
  /// Symbol with no type (STT_NOTYPE).
  NoType,
  /// Data object symbol (STT_OBJECT).
  Object,
  /// Function symbol (STT_FUNC).
  Func,
  /// Thread-local storage symbol (STT_TLS).
  TLS,

  // Type information is 4 bits, so 16 is safely out of range.
  /// Unsupported or unrecognized symbol type.
  Unknown = 16,
};

/// Endianness of an IFS target, corresponding to ELF EI_DATA values.
enum class IFSEndiannessType {
  /// Little-endian target (ELFDATA2LSB).
  Little,
  /// Big-endian target (ELFDATA2MSB).
  Big,

  // Endianness info is 1 bytes, 256 is safely out of range.
  /// Unsupported or unrecognized endianness.
  Unknown = 256,
};

/// Address bit width of an IFS target, corresponding to ELF EI_CLASS values.
enum class IFSBitWidthType {
  /// 32-bit target (ELFCLASS32).
  IFS32,
  /// 64-bit target (ELFCLASS64).
  IFS64,

  // Bit width info is 1 bytes, 256 is safely out of range.
  /// Unsupported or unrecognized bit width.
  Unknown = 256,
};

/// A single symbol described by an InterFace Stub.
struct IFSSymbol {
  /// Constructs an empty symbol.
  IFSSymbol() = default;
  /// Constructs a symbol with the given name.
  ///
  /// @param SymbolName Name of the symbol.
  explicit IFSSymbol(std::string SymbolName) : Name(std::move(SymbolName)) {}
  /// Symbol name.
  std::string Name;
  /// Symbol size in bytes, if known.
  std::optional<uint64_t> Size;
  /// Symbol type.
  IFSSymbolType Type = IFSSymbolType::NoType;
  /// Whether the symbol is undefined.
  bool Undefined = false;
  /// Whether the symbol is weak.
  bool Weak = false;
  /// Optional warning message associated with the symbol.
  std::optional<std::string> Warning;
  /// Compares symbols by name for ordered containers.
  ///
  /// @param RHS Symbol to compare against.
  /// @return True if this symbol's name sorts before RHS.
  bool operator<(const IFSSymbol &RHS) const { return Name < RHS.Name; }
};

/// Target platform information for an InterFace Stub.
struct IFSTarget {
  /// Optional LLVM triple string for the target.
  std::optional<std::string> Triple;
  /// Optional object file format name.
  std::optional<std::string> ObjectFormat;
  /// Optional ELF e_machine architecture value.
  std::optional<IFSArch> Arch;
  /// Optional architecture name as a string.
  std::optional<std::string> ArchString;
  /// Optional target endianness.
  std::optional<IFSEndiannessType> Endianness;
  /// Optional target address bit width.
  std::optional<IFSBitWidthType> BitWidth;

  /// Returns true if no target fields are set.
  ///
  /// @return True if every target field is unset.
  LLVM_ABI bool empty();
};

/// Returns true if two IFS targets describe the same platform.
///
/// @param Lhs Left-hand target.
/// @param Rhs Right-hand target.
/// @return True if the targets are equal.
inline bool operator==(const IFSTarget &Lhs, const IFSTarget &Rhs) {
  if (Lhs.Arch != Rhs.Arch || Lhs.BitWidth != Rhs.BitWidth ||
      Lhs.Endianness != Rhs.Endianness ||
      Lhs.ObjectFormat != Rhs.ObjectFormat || Lhs.Triple != Rhs.Triple)
    return false;
  return true;
}

/// Returns true if two IFS targets describe different platforms.
///
/// @param Lhs Left-hand target.
/// @param Rhs Right-hand target.
/// @return True if the targets differ.
inline bool operator!=(const IFSTarget &Lhs, const IFSTarget &Rhs) {
  return !(Lhs == Rhs);
}

/// Cumulative representation of an InterFace Stub.
///
/// Both textual and binary stubs will read into and write from this object.
struct IFSStub {
  // TODO: Add support for symbol versioning.
  /// IFS format version of this stub.
  VersionTuple IfsVersion;
  /// Optional shared-object name (DT_SONAME).
  std::optional<std::string> SoName;
  /// Target platform for this stub.
  IFSTarget Target;
  /// Libraries listed as needed by this stub (DT_NEEDED).
  std::vector<std::string> NeededLibs;
  /// Symbols exported or referenced by this stub.
  std::vector<IFSSymbol> Symbols;

  /// Constructs an empty stub.
  IFSStub() = default;
  /// Copy-constructs a stub.
  ///
  /// @param Stub Stub to copy.
  LLVM_ABI IFSStub(const IFSStub &Stub);
  /// Move-constructs a stub.
  ///
  /// @param Stub Stub to move from.
  LLVM_ABI IFSStub(IFSStub &&Stub);
  /// Destroys the stub.
  virtual ~IFSStub() = default;
};

/// Alias of IFSStub used for an alternate YAML target mapping.
///
/// LLVM's YAML library does not allow mapping a class with 2 traits,
/// which prevents us using 'Target:' field with different definitions.
/// This class makes it possible to map a second traits so the same data
/// structure can be used for 2 different yaml schema.
struct IFSStubTriple : IFSStub {
  /// Constructs an empty stub triple.
  IFSStubTriple() = default;
  /// Constructs a stub triple from an IFSStub.
  ///
  /// @param Stub Stub to copy.
  LLVM_ABI IFSStubTriple(const IFSStub &Stub);
  /// Copy-constructs a stub triple.
  ///
  /// @param Stub Stub triple to copy.
  LLVM_ABI IFSStubTriple(const IFSStubTriple &Stub);
  /// Move-constructs a stub triple.
  ///
  /// @param Stub Stub triple to move from.
  LLVM_ABI IFSStubTriple(IFSStubTriple &&Stub);
};

/// This function convert bit width type from IFS enum to ELF format
/// Currently, ELFCLASS32 and ELFCLASS64 are supported.
///
/// @param BitWidth IFS bit width type.
/// @return The corresponding ELF EI_CLASS bit width value.
LLVM_ABI uint8_t convertIFSBitWidthToELF(IFSBitWidthType BitWidth);

/// This function convert endianness type from IFS enum to ELF format
/// Currently, ELFDATA2LSB and ELFDATA2MSB are supported.
///
/// @param Endianness IFS endianness type.
/// @return The corresponding ELF EI_DATA endianness value.
LLVM_ABI uint8_t convertIFSEndiannessToELF(IFSEndiannessType Endianness);

/// This function convert symbol type from IFS enum to ELF format
/// Currently, STT_NOTYPE, STT_OBJECT, STT_FUNC, and STT_TLS are supported.
///
/// @param SymbolType IFS symbol type.
/// @return The corresponding ELF STT_* symbol type value.
LLVM_ABI uint8_t convertIFSSymbolTypeToELF(IFSSymbolType SymbolType);

/// Maps an ELF EI_CLASS value to an IFSBitWidthType.
///
/// This function extracts ELF bit width from e_ident[EI_CLASS] of an ELF file.
/// Currently, ELFCLASS32 and ELFCLASS64 are supported.
/// Other endianness types are mapped to IFSBitWidthType::Unknown.
///
/// @param BitWidth e_ident[EI_CLASS] value to extract bit width from.
/// @return The corresponding IFSBitWidthType, or IFSBitWidthType::Unknown.
LLVM_ABI IFSBitWidthType convertELFBitWidthToIFS(uint8_t BitWidth);

/// Maps an ELF EI_DATA value to an IFSEndiannessType.
///
/// This function extracts ELF endianness from e_ident[EI_DATA] of an ELF file.
/// Currently, ELFDATA2LSB and ELFDATA2MSB are supported.
/// Other endianness types are mapped to IFSEndiannessType::Unknown.
///
/// @param Endianness e_ident[EI_DATA] value to extract endianness type from.
/// @return The corresponding IFSEndiannessType, or IFSEndiannessType::Unknown.
LLVM_ABI IFSEndiannessType convertELFEndiannessToIFS(uint8_t Endianness);

/// Maps an ELF st_info symbol type to an IFSSymbolType.
///
/// This function extracts symbol type from a symbol's st_info member and
/// maps it to an IFSSymbolType enum.
/// Currently, STT_NOTYPE, STT_OBJECT, STT_FUNC, and STT_TLS are supported.
/// Other symbol types are mapped to IFSSymbolType::Unknown.
///
/// @param SymbolType Binary symbol st_info to extract symbol type from.
/// @return The corresponding IFSSymbolType, or IFSSymbolType::Unknown.
LLVM_ABI IFSSymbolType convertELFSymbolTypeToIFS(uint8_t SymbolType);
} // namespace ifs
} // end namespace llvm

#endif // LLVM_INTERFACESTUB_IFSSTUB_H
