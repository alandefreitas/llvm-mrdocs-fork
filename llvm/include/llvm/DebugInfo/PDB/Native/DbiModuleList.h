//===- DbiModuleList.h - PDB module information list ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULELIST_H
#define LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULELIST_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/DebugInfo/PDB/Native/DbiModuleDescriptor.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <vector>

namespace llvm {
namespace pdb {

class DbiModuleList;
struct FileInfoSubstreamHeader;

/// Random-access iterator over the source file names of one DBI module.
class DbiModuleSourceFilesIterator
    : public iterator_facade_base<DbiModuleSourceFilesIterator,
                                  std::random_access_iterator_tag, StringRef> {
  using BaseType = DbiModuleSourceFilesIterator::iterator_facade_base;

public:
  /// Construct an iterator at source file \p Filei of module \p Modi.
  ///
  /// \param Modules Module list that owns the file-name table.
  /// \param Modi Zero-based module index to iterate.
  /// \param Filei Zero-based source-file index within that module.
  LLVM_ABI DbiModuleSourceFilesIterator(const DbiModuleList &Modules,
                                        uint32_t Modi, uint16_t Filei);
  /// Construct a default (singular / end) source-files iterator.
  DbiModuleSourceFilesIterator() = default;
  /// Copy-construct a source-files iterator from \p R.
  ///
  /// \param R The iterator to copy.
  DbiModuleSourceFilesIterator(const DbiModuleSourceFilesIterator &R) = default;
  /// Copy-assign a source-files iterator from \p R.
  ///
  /// \param R The iterator to copy.
  ///
  /// \returns A reference to this iterator after assignment.
  DbiModuleSourceFilesIterator &
  operator=(const DbiModuleSourceFilesIterator &R) = default;

  /// Return true if this iterator equals \p R.
  ///
  /// \param R The other iterator to compare.
  ///
  /// \returns True if both iterators refer to the same module and file index.
  LLVM_ABI bool operator==(const DbiModuleSourceFilesIterator &R) const;

  /// Return the current source file name.
  ///
  /// \returns A const reference to the file name at this iterator position.
  const StringRef &operator*() const { return ThisValue; }
  /// Return the current source file name.
  ///
  /// \returns A mutable reference to the file name at this iterator position.
  StringRef &operator*() { return ThisValue; }

  /// Return true if this iterator precedes \p RHS.
  ///
  /// \param RHS The other iterator to compare.
  ///
  /// \returns True if this iterator's file index is less than \p RHS.
  LLVM_ABI bool operator<(const DbiModuleSourceFilesIterator &RHS) const;
  /// Return the distance from \p R to this iterator.
  ///
  /// \param R The iterator to subtract from this one.
  ///
  /// \returns The signed difference in source-file indices.
  LLVM_ABI std::ptrdiff_t
  operator-(const DbiModuleSourceFilesIterator &R) const;
  /// Advance this iterator by \p N positions.
  ///
  /// \param N Number of source-file entries to move forward (or back if
  ///     negative).
  ///
  /// \returns A reference to this iterator after advancing.
  LLVM_ABI DbiModuleSourceFilesIterator &operator+=(std::ptrdiff_t N);
  /// Move this iterator backward by \p N positions.
  ///
  /// \param N Number of source-file entries to move backward (or forward if
  ///     negative).
  ///
  /// \returns A reference to this iterator after retreating.
  LLVM_ABI DbiModuleSourceFilesIterator &operator-=(std::ptrdiff_t N);

private:
  void setValue();

  bool isEnd() const;
  bool isCompatible(const DbiModuleSourceFilesIterator &R) const;
  bool isUniversalEnd() const;

  StringRef ThisValue;
  const DbiModuleList *Modules{nullptr};
  uint32_t Modi{0};
  uint16_t Filei{0};
};

/// Parsed view of the DBI Module Info and File Info substreams.
class DbiModuleList {
  friend DbiModuleSourceFilesIterator;

public:
  /// Initialize this list from the Module Info and File Info substreams.
  ///
  /// \param ModInfo Binary stream for the Module Info (Modi) substream.
  /// \param FileInfo Binary stream for the File Info substream.
  ///
  /// \returns An Error on failure, or success if both substreams were parsed.
  LLVM_ABI Error initialize(BinaryStreamRef ModInfo, BinaryStreamRef FileInfo);

  /// Return the source file name at global file-name index \p Index.
  ///
  /// \param Index Index into the File Info name offset table.
  ///
  /// \returns The file name string, or an Error if \p Index is out of range.
  LLVM_ABI Expected<StringRef> getFileName(uint32_t Index) const;
  /// Return the number of modules in the Module Info substream.
  ///
  /// \returns The module count.
  LLVM_ABI uint32_t getModuleCount() const;
  /// Return the total number of source file contributions across all modules.
  ///
  /// \returns The total source-file contribution count.
  LLVM_ABI uint32_t getSourceFileCount() const;
  /// Return the number of source files contributing to module \p Modi.
  ///
  /// \param Modi Zero-based module index.
  ///
  /// \returns The number of source files for that module.
  LLVM_ABI uint16_t getSourceFileCount(uint32_t Modi) const;

  /// Return an iterator range over the source file names of module \p Modi.
  ///
  /// \param Modi Zero-based module index.
  ///
  /// \returns A range of \c DbiModuleSourceFilesIterator values.
  LLVM_ABI iterator_range<DbiModuleSourceFilesIterator>
  source_files(uint32_t Modi) const;

  /// Return the module descriptor for module \p Modi.
  ///
  /// \param Modi Zero-based module index.
  ///
  /// \returns The \c DbiModuleDescriptor for that module.
  LLVM_ABI DbiModuleDescriptor getModuleDescriptor(uint32_t Modi) const;

private:
  Error initializeModInfo(BinaryStreamRef ModInfo);
  Error initializeFileInfo(BinaryStreamRef FileInfo);

  VarStreamArray<DbiModuleDescriptor> Descriptors;

  FixedStreamArray<support::little32_t> FileNameOffsets;
  FixedStreamArray<support::ulittle16_t> ModFileCountArray;

  // For each module, there are multiple filenames, which can be obtained by
  // knowing the index of the file.  Given the index of the file, one can use
  // that as an offset into the FileNameOffsets array, which contains the
  // absolute offset of the file name in NamesBuffer.  Thus, for each module
  // we store the first index in the FileNameOffsets array for this module.
  // The number of files for the corresponding module is stored in
  // ModFileCountArray.
  std::vector<uint32_t> ModuleInitialFileIndex;

  // In order to provide random access into the Descriptors array, we iterate it
  // once up front to find the offsets of the individual items and store them in
  // this array.
  std::vector<uint32_t> ModuleDescriptorOffsets;

  const FileInfoSubstreamHeader *FileInfoHeader = nullptr;

  BinaryStreamRef ModInfoSubstream;
  BinaryStreamRef FileInfoSubstream;
  BinaryStreamRef NamesBuffer;
};

} // end namespace pdb
} // end namespace llvm

#endif // LLVM_DEBUGINFO_PDB_NATIVE_DBIMODULELIST_H
