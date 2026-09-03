//===--- COFFModuleDefinition.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Windows-specific.
// A parser for the module-definition file (.def file).
// Parsed results are directly written to Config global variable.
//
// The format of module-definition files are described in this document:
// https://msdn.microsoft.com/en-us/library/28d6s79h.aspx
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_COFFMODULEDEFINITION_H
#define LLVM_OBJECT_COFFMODULEDEFINITION_H

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/Object/COFFImportFile.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
namespace object {

/// Parsed contents of a Windows module-definition (.def) file.
struct COFFModuleDefinition {
  /// Exports listed by the EXPORTS directive.
  std::vector<COFFShortExport> Exports;
  /// Default output binary path from NAME or LIBRARY (with .exe/.dll if needed).
  std::string OutputFile;
  /// Import library / DLL name from the NAME or LIBRARY directive.
  std::string ImportName;
  /// Preferred image base from the optional BASE= clause of NAME or LIBRARY.
  uint64_t ImageBase = 0;
  /// Stack reserve size in bytes from the STACKSIZE directive.
  uint64_t StackReserve = 0;
  /// Stack commit size in bytes from the optional STACKSIZE commit value.
  uint64_t StackCommit = 0;
  /// Heap reserve size in bytes from the HEAPSIZE directive.
  uint64_t HeapReserve = 0;
  /// Heap commit size in bytes from the optional HEAPSIZE commit value.
  uint64_t HeapCommit = 0;
  /// Major image version from the VERSION directive.
  uint32_t MajorImageVersion = 0;
  /// Minor image version from the VERSION directive.
  uint32_t MinorImageVersion = 0;
  /// Major preferred operating system version.
  uint32_t MajorOSVersion = 0;
  /// Minor preferred operating system version.
  uint32_t MinorOSVersion = 0;
};

/// Parse a Windows module-definition (.def) file into a COFFModuleDefinition.
///
/// \param MB Buffer containing the .def file text.
/// \param Machine Target COFF machine type used when decorating export names.
/// \param MingwDef True when the .def file follows MinGW naming conventions.
/// \param AddUnderscores True to prefix undecorated i386 export names with '_'.
/// \return The parsed module definition, or an error if parsing fails.
LLVM_ABI Expected<COFFModuleDefinition>
parseCOFFModuleDefinition(MemoryBufferRef MB, COFF::MachineTypes Machine,
                          bool MingwDef = false, bool AddUnderscores = true);

} // End namespace object.
} // End namespace llvm.

#endif
