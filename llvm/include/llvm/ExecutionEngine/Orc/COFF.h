//===-------------- COFF.h - COFF format utilities --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Contains utilities for load COFF relocatable object files.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_EXECUTIONENGINE_ORC_COFF_H
#define LLVM_EXECUTIONENGINE_ORC_COFF_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

#include <set>
#include <string>

namespace llvm {

namespace object {
class Archive;
} // namespace object

namespace orc {

/// Scans archive members for COFF import files and records their library names.
///
/// For use with StaticLibraryDefinitionGenerator::VisitMembersFunction.
/// Import-file members are recorded and treated as not loadable; other members
/// are reported as loadable.
class COFFImportFileScanner {
public:
  /// Construct a scanner that records imported dynamic library names into the
  /// given set.
  /// @param ImportedDynamicLibraries Set to insert COFF import library names
  /// into.
  COFFImportFileScanner(std::set<std::string> &ImportedDynamicLibraries)
      : ImportedDynamicLibraries(ImportedDynamicLibraries) {}

  /// Visit an archive member, recording COFF import files and reporting whether
  /// the member is loadable.
  ///
  /// If the member is a COFF import file, its file name is inserted into
  /// ImportedDynamicLibraries and the function returns false (not loadable).
  /// Otherwise returns true (loadable), or false if the member could not be
  /// parsed as a binary.
  /// @param A Archive containing the member.
  /// @param MemberBuf Buffer covering the archive member bytes.
  /// @param Index Index of the member within the archive.
  /// @return True if the member is loadable; false if it is a COFF import file
  /// or could not be parsed as a binary.
  LLVM_ABI Expected<bool>
  operator()(object::Archive &A, MemoryBufferRef MemberBuf, size_t Index) const;

private:
  std::set<std::string> &ImportedDynamicLibraries;
};

} // namespace orc
} // namespace llvm

#endif // LLVM_EXECUTIONENGINE_ORC_MACHO_H
