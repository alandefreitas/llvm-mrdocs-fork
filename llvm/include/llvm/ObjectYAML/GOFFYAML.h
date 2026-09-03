//===- GOFFYAML.h - GOFF YAMLIO implementation ------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares classes for handling the YAML representation of GOFF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_GOFFYAML_H
#define LLVM_OBJECTYAML_GOFFYAML_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/ObjectYAML/YAML.h"
#include <cstdint>

namespace llvm {

/// YAML representations of GOFF object files.
///
/// The structure of the yaml files is not an exact 1:1 match to GOFF. In order
/// to use yaml::IO, we use these structures which are closer to the source.
namespace GOFFYAML {

/// YAML representation of a GOFF file header.
struct FileHeader {
  /// Target environment identifier from the HDR record.
  uint32_t TargetEnvironment = 0;
  /// Target operating system identifier from the HDR record.
  uint32_t TargetOperatingSystem = 0;
  /// Coded Character Set Identifier (CCSID) for the object.
  uint16_t CCSID = 0;
  /// Character set name associated with the CCSID.
  std::string CharacterSetName;
  /// Language product identifier string from the HDR record.
  std::string LanguageProductIdentifier;
  /// Architecture level of the GOFF object.
  uint32_t ArchitectureLevel = 0;
  /// Optional internal CCSID used by the producing tool.
  std::optional<uint16_t> InternalCCSID;
  /// Optional target software environment identifier.
  std::optional<uint8_t> TargetSoftwareEnvironment;
};

/// YAML representation of a complete GOFF object.
struct Object {
  /// GOFF file header for this object.
  FileHeader Header;
  /// Construct an empty GOFF YAML object.
  LLVM_ABI Object();
};
} // end namespace GOFFYAML
} // end namespace llvm

namespace llvm {
namespace yaml {

/// YAMLIO mapping traits for \c GOFFYAML::FileHeader.
template <> struct MappingTraits<GOFFYAML::FileHeader> {
  /// Map GOFF file-header fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param FileHdr File header being mapped.
  LLVM_ABI static void mapping(IO &IO, GOFFYAML::FileHeader &FileHdr);
};

/// YAMLIO mapping traits for \c GOFFYAML::Object.
template <> struct MappingTraits<GOFFYAML::Object> {
  /// Map GOFF object fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param Obj Object being mapped.
  LLVM_ABI static void mapping(IO &IO, GOFFYAML::Object &Obj);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_GOFFYAML_H
