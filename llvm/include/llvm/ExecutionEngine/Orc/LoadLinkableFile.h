//===--- LoadLinkableFile.h -- Load relocatables and archives ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A wrapper for `MemoryBuffer::getFile` / `MemoryBuffer::getFileSlice` that:
//
//   1. Handles relocatable object files, archives, and macho universal
//      binaries.
//   2. Adds file paths to errors by default.
//   3. Checks architecture compatibility up-front.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_LOADLINKABLEFILE_H
#define LLVM_EXECUTIONENGINE_ORC_LOADLINKABLEFILE_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TargetParser/Triple.h"

namespace llvm {
namespace orc {

/// Kind of linkable file loaded from a path.
enum class LinkableFileKind {
  /// Static archive containing object files.
  Archive,
  /// Relocatable object file.
  RelocatableObject
};

/// Whether loadLinkableFile may or must load a static archive.
enum class LoadArchives {
  /// Linkable file must not be an archive.
  Never,
  /// Linkable file is allowed to be an archive.
  Allowed,
  /// Linkable file is required to be an archive.
  Required
};

/// Create a MemoryBuffer covering the "linkable" part of the given path.
///
/// The path must contain a relocatable object file or universal binary, or
/// (if \p LA is Allowed or Required) an archive.
///
/// If the path is a universal binary then it must contain a slice whose
/// architecture matches the architecture in the triple (an error will be
/// returned if there is no such slice, or if the triple does not specify an
/// architectur).
///
/// If the path (or universal binary slice) is a relocatable object file then
/// its architecture must match the architecture in the triple (if given).
///
/// If the path (or universal binary slice) is a relocatable object file then
/// its format must match the format in the triple (if given).
///
/// No verification (e.g. architecture or format) is performed on the contents
/// of archives.
///
/// If IdentifierOverride is provided then it will be used as the name of the
/// resulting buffer, rather than Path.
/// \param Path Path to the relocatable object, archive, or universal binary.
/// \param TT Triple used to select and validate architecture and format.
/// \param LA Whether archives are forbidden, allowed, or required.
/// \param IdentifierOverride Optional name for the resulting buffer; defaults
///        to Path when omitted.
/// \return A MemoryBuffer covering the linkable content and its kind, or an
///         error.
LLVM_ABI Expected<std::pair<std::unique_ptr<MemoryBuffer>, LinkableFileKind>>
loadLinkableFile(StringRef Path, const Triple &TT, LoadArchives LA,
                 std::optional<StringRef> IdentifierOverride = std::nullopt);

} // End namespace orc
} // End namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_LOADLINKABLEFILE_H
