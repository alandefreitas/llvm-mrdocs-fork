//===- ArchiveYAML.h - Archive YAMLIO implementation ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares classes for handling the YAML representation of archives.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_ARCHIVEYAML_H
#define LLVM_OBJECTYAML_ARCHIVEYAML_H

#include "llvm/Support/YAMLTraits.h"
#include "llvm/ObjectYAML/YAML.h"
#include "llvm/ADT/MapVector.h"
#include <optional>

namespace llvm {
/// YAML representations of Unix archive (ar) files.
namespace ArchYAML {

/// YAML representation of a Unix archive.
struct Archive {
  /// YAML representation of a single archive member (child).
  struct Child {
    /// A fixed-width archive header field and its YAML value.
    struct Field {
      /// Construct a field with an empty default and zero max length.
      Field() = default;
      /// Construct a field with the given default text and maximum length.
      /// \param Default Default value used when the field is omitted in YAML.
      /// \param Length Maximum number of characters allowed in the field.
      Field(StringRef Default, unsigned Length)
          : DefaultValue(Default), MaxLength(Length) {}
      /// Current field value as read from or written to YAML.
      StringRef Value;
      /// Default value applied when the field is omitted.
      StringRef DefaultValue;
      /// Maximum number of characters allowed for this field.
      unsigned MaxLength;
    };

    /// Initialize header fields with archive-format defaults and lengths.
    Child() {
      Fields["Name"] = {"", 16};
      Fields["LastModified"] = {"0", 12};
      Fields["UID"] = {"0", 6};
      Fields["GID"] = {"0", 6};
      Fields["AccessMode"] = {"0", 8};
      Fields["Size"] = {"0", 10};
      Fields["Terminator"] = {"`\n", 2};
    }

    /// Archive member header fields keyed by their YAML names.
    MapVector<StringRef, Field> Fields;

    /// Raw member payload when represented as a binary blob.
    std::optional<yaml::BinaryRef> Content;
    /// Optional byte used to pad the member to an even boundary.
    std::optional<llvm::yaml::Hex8> PaddingByte;
  };

  /// Archive magic string (defaults to "!<arch>\\n").
  StringRef Magic;
  /// Optional list of archive members.
  std::optional<std::vector<Child>> Members;
  /// Raw archive contents when members are not listed individually.
  std::optional<yaml::BinaryRef> Content;
};

} // end namespace ArchYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// Sequences of archive members use block formatting.
template <> struct SequenceElementTraits<llvm::ArchYAML::Archive::Child> {
  /// Emit sequences of archive members in block style.
  static const bool flow = false;
};

/// YAMLIO mapping traits for \c ArchYAML::Archive.
template <> struct MappingTraits<ArchYAML::Archive> {
  /// Map archive fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param A Archive object being mapped.
  LLVM_ABI static void mapping(IO &IO, ArchYAML::Archive &A);
  /// Validate that archive YAML fields are consistent.
  /// \param IO YAML input/output state.
  /// \param A Archive object to validate.
  /// \return An error message, or an empty string on success.
  LLVM_ABI static std::string validate(IO &IO, ArchYAML::Archive &A);
};

/// YAMLIO mapping traits for \c ArchYAML::Archive::Child.
template <> struct MappingTraits<ArchYAML::Archive::Child> {
  /// Map archive member fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param C Archive member being mapped.
  LLVM_ABI static void mapping(IO &IO, ArchYAML::Archive::Child &C);
  /// Validate that archive member field lengths are within limits.
  /// \param IO YAML input/output state.
  /// \param C Archive member to validate.
  /// \return An error message, or an empty string on success.
  LLVM_ABI static std::string validate(IO &IO, ArchYAML::Archive::Child &C);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_ARCHIVEYAML_H
