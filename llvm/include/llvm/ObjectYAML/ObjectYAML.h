//===- ObjectYAML.h ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_OBJECTYAML_H
#define LLVM_OBJECTYAML_OBJECTYAML_H

#include "llvm/ObjectYAML/ArchiveYAML.h"
#include "llvm/ObjectYAML/COFFYAML.h"
#include "llvm/ObjectYAML/DXContainerYAML.h"
#include "llvm/ObjectYAML/ELFYAML.h"
#include "llvm/ObjectYAML/GOFFYAML.h"
#include "llvm/ObjectYAML/MachOYAML.h"
#include "llvm/ObjectYAML/MinidumpYAML.h"
#include "llvm/ObjectYAML/OffloadYAML.h"
#include "llvm/ObjectYAML/WasmYAML.h"
#include "llvm/ObjectYAML/XCOFFYAML.h"
#include "llvm/Support/YAMLTraits.h"
#include <memory>

namespace llvm {
namespace yaml {

class IO;

/// YAML representation of an object file in one of several formats.
struct YamlObjectFile {
  /// YAML representation of a Unix archive, if present.
  std::unique_ptr<ArchYAML::Archive> Arch;
  /// YAML representation of an ELF object, if present.
  std::unique_ptr<ELFYAML::Object> Elf;
  /// YAML representation of a COFF object, if present.
  std::unique_ptr<COFFYAML::Object> Coff;
  /// YAML representation of a GOFF object, if present.
  std::unique_ptr<GOFFYAML::Object> Goff;
  /// YAML representation of a Mach-O object, if present.
  std::unique_ptr<MachOYAML::Object> MachO;
  /// YAML representation of a fat Mach-O binary, if present.
  std::unique_ptr<MachOYAML::UniversalBinary> FatMachO;
  /// YAML representation of a minidump, if present.
  std::unique_ptr<MinidumpYAML::Object> Minidump;
  /// YAML representation of an offload binary, if present.
  std::unique_ptr<OffloadYAML::Binary> Offload;
  /// YAML representation of a Wasm object, if present.
  std::unique_ptr<WasmYAML::Object> Wasm;
  /// YAML representation of an XCOFF object, if present.
  std::unique_ptr<XCOFFYAML::Object> Xcoff;
  /// YAML representation of a DXContainer object, if present.
  std::unique_ptr<DXContainerYAML::Object> DXContainer;
};

/// YAMLIO mapping traits for \c YamlObjectFile.
template <> struct MappingTraits<YamlObjectFile> {
  /// Map object-file fields to and from YAML.
  /// \param IO YAML input/output state.
  /// \param ObjectFile Object file being mapped.
  LLVM_ABI static void mapping(IO &IO, YamlObjectFile &ObjectFile);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_OBJECTYAML_H
