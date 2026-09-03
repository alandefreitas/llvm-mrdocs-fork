//===- DbiModuleDescriptor.h - PDB module information -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTOR_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTOR_H

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {
template <typename T> struct VarStreamArrayExtractor;

namespace pdb {
struct ModuleInfoHeader;
struct SectionContrib;
/// Descriptor for one module entry in the DBI Module Info substream.
class DbiModuleDescriptor {
  friend class DbiStreamBuilder;

public:
  /// Construct an empty module descriptor.
  DbiModuleDescriptor() = default;
  /// Copy-construct a module descriptor from \p Info.
  ///
  /// \param Info The module descriptor to copy.
  DbiModuleDescriptor(const DbiModuleDescriptor &Info) = default;
  /// Copy-assign a module descriptor from \p Info.
  ///
  /// \param Info The module descriptor to copy.
  ///
  /// \returns A reference to this module descriptor.
  DbiModuleDescriptor &operator=(const DbiModuleDescriptor &Info) = default;

  /// Parse a module descriptor from \p Stream into \p Info.
  ///
  /// \param Stream Stream positioned at the start of a ModuleInfoHeader.
  /// \param Info Set to the parsed module descriptor on success.
  ///
  /// \returns An Error on failure, or success if the descriptor was parsed.
  LLVM_ABI static Error initialize(BinaryStreamRef Stream,
                                   DbiModuleDescriptor &Info);

  /// Return true if this module has EC (edit-and-continue) symbolic info.
  ///
  /// \returns True if this module has EC symbolic info.
  LLVM_ABI bool hasECInfo() const;
  /// Return the type server index encoded in the module flags.
  ///
  /// \returns The type server index from the module flags.
  LLVM_ABI uint16_t getTypeServerIndex() const;
  /// Return the MSF stream index of this module's debug info stream.
  ///
  /// \returns The MSF stream index of this module's debug info stream.
  LLVM_ABI uint16_t getModuleStreamIndex() const;
  /// Return the size in bytes of local symbol debug info in the module stream.
  ///
  /// \returns The size in bytes of local symbol debug info.
  LLVM_ABI uint32_t getSymbolDebugInfoByteSize() const;
  /// Return the size in bytes of C11 line number info in the module stream.
  ///
  /// \returns The size in bytes of C11 line number info.
  LLVM_ABI uint32_t getC11LineInfoByteSize() const;
  /// Return the size in bytes of C13 line number info in the module stream.
  ///
  /// \returns The size in bytes of C13 line number info.
  LLVM_ABI uint32_t getC13LineInfoByteSize() const;
  /// Return the number of source files contributing to this module.
  ///
  /// \returns The number of contributing source files.
  LLVM_ABI uint32_t getNumberOfFiles() const;
  /// Return the name-index of the primary source file for this module.
  ///
  /// \returns The name-index of the primary source file.
  LLVM_ABI uint32_t getSourceFileNameIndex() const;
  /// Return the name-index of the path to the compiler PDB for this module.
  ///
  /// \returns The name-index of the compiler PDB path.
  LLVM_ABI uint32_t getPdbFilePathNameIndex() const;

  /// Return the module name string following the ModuleInfoHeader.
  ///
  /// \returns The module name string.
  LLVM_ABI StringRef getModuleName() const;
  /// Return the object file name string following the module name.
  ///
  /// \returns The object file name string.
  LLVM_ABI StringRef getObjFileName() const;

  /// Return the total on-disk size of this module descriptor record.
  ///
  /// \returns The total on-disk size of this descriptor record.
  LLVM_ABI uint32_t getRecordLength() const;

  /// Return the first section contribution recorded for this module.
  ///
  /// \returns A const reference to the first section contribution.
  LLVM_ABI const SectionContrib &getSectionContrib() const;

private:
  StringRef ModuleName;
  StringRef ObjFileName;
  const ModuleInfoHeader *Layout = nullptr;
};

} // end namespace pdb

/// VarStreamArray extractor for DBI module descriptors.
template <> struct VarStreamArrayExtractor<pdb::DbiModuleDescriptor> {
  /// Extract one module descriptor from \p Stream into \p Info.
  ///
  /// \param Stream Stream positioned at the start of the next descriptor.
  /// \param Length Set to the number of bytes occupied by the descriptor.
  /// \param Info Set to the extracted module descriptor.
  ///
  /// \returns An Error on failure, or success if a descriptor was extracted.
  Error operator()(BinaryStreamRef Stream, uint32_t &Length,
                   pdb::DbiModuleDescriptor &Info) {
    if (auto EC = pdb::DbiModuleDescriptor::initialize(Stream, Info))
      return EC;
    Length = Info.getRecordLength();
    return Error::success();
  }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULEDESCRIPTOR_H
