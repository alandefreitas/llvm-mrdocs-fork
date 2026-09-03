//===- OffloadYAML.h - Offload Binary YAMLIO implementation -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation of
/// offloading binaries.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_OFFLOADYAML_H
#define LLVM_OBJECTYAML_OFFLOADYAML_H

#include "llvm/Object/OffloadBinary.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/Support/YAMLTraits.h"
#include <optional>

namespace llvm {
/// YAML representations of offloading binaries.
namespace OffloadYAML {

/// YAML representation of an offloading binary.
struct Binary {
  /// YAML representation of a string-map entry in an offload member.
  struct StringEntry {
    /// Key name for this string-map entry.
    StringRef Key;
    /// Value associated with \c Key.
    StringRef Value;
  };

  /// YAML representation of a single offload binary member (image entry).
  struct Member {
    /// Kind of embedded image stored in this member.
    std::optional<object::ImageKind> ImageKind;
    /// Offloading producer or runtime associated with this member.
    std::optional<object::OffloadKind> OffloadKind;
    /// Additional flags associated with this member.
    std::optional<uint32_t> Flags;
    /// Optional string-map metadata for this member.
    std::optional<std::vector<StringEntry>> StringEntries;
    /// Raw image payload for this member.
    std::optional<yaml::BinaryRef> Content;
  };

  /// Offloading binary format version identifier.
  std::optional<uint32_t> Version;
  /// Size in bytes of the entire offloading binary.
  std::optional<uint64_t> Size;
  /// Offset in bytes to the start of the entries block.
  std::optional<uint64_t> EntriesOffset;
  /// Number of metadata entries in the binary.
  std::optional<uint64_t> EntriesCount;
  /// Offload members (image entries) contained in the binary.
  std::vector<Member> Members;
};

} // end namespace OffloadYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of offload binary members use block formatting.
template <> struct SequenceElementTraits<llvm::OffloadYAML::Binary::Member> {
  /// Emit sequences of offload binary members in block style.
  static const bool flow = false;
};

/// Sequences of offload string entries use block formatting.
template <>
struct SequenceElementTraits<llvm::OffloadYAML::Binary::StringEntry> {
  /// Emit sequences of offload string entries in block style.
  static const bool flow = false;
};

/// YAMLIO scalar enumeration traits for \c object::ImageKind.
template <> struct ScalarEnumerationTraits<object::ImageKind> {
  /// Map image kind enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Image kind being mapped.
  LLVM_ABI static void enumeration(IO &IO, object::ImageKind &Value);
};

/// YAMLIO scalar enumeration traits for \c object::OffloadKind.
template <> struct ScalarEnumerationTraits<object::OffloadKind> {
  /// Map offload kind enumerators to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Value Offload kind being mapped.
  LLVM_ABI static void enumeration(IO &IO, object::OffloadKind &Value);
};

/// YAMLIO mapping traits for \c OffloadYAML::Binary.
template <> struct MappingTraits<OffloadYAML::Binary> {
  /// Map offload binary fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param O Offload binary being mapped.
  LLVM_ABI static void mapping(IO &IO, OffloadYAML::Binary &O);
};

/// YAMLIO mapping traits for \c OffloadYAML::Binary::StringEntry.
template <> struct MappingTraits<OffloadYAML::Binary::StringEntry> {
  /// Map offload string-entry fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param M String entry being mapped.
  LLVM_ABI static void mapping(IO &IO, OffloadYAML::Binary::StringEntry &M);
};

/// YAMLIO mapping traits for \c OffloadYAML::Binary::Member.
template <> struct MappingTraits<OffloadYAML::Binary::Member> {
  /// Map offload member fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param M Offload member being mapped.
  LLVM_ABI static void mapping(IO &IO, OffloadYAML::Binary::Member &M);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_OFFLOADYAML_H
