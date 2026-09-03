//===- ArchiveWriter.h - ar archive file format writer ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Declares the writeArchive function for writing an archive file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_ARCHIVEWRITER_H
#define LLVM_OBJECT_ARCHIVEWRITER_H

#include "llvm/Object/Archive.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// A member to include when writing a new archive.
struct NewArchiveMember {
  /// Contents of the member.
  std::unique_ptr<MemoryBuffer> Buf;
  /// Name of the member as it will appear in the archive.
  StringRef MemberName;
  /// Modification time stored in the member header.
  sys::TimePoint<std::chrono::seconds> ModTime;
  /// User ID stored in the member header.
  unsigned UID = 0;
  /// Group ID stored in the member header.
  unsigned GID = 0;
  /// Unix permission bits stored in the member header.
  unsigned Perms = 0644;

  /// Construct an empty archive member with default metadata.
  NewArchiveMember() = default;
  /// Construct a member whose contents and name come from \p BufRef.
  ///
  /// \param BufRef Buffer holding the member data; its identifier is used as
  ///        the member name.
  LLVM_ABI NewArchiveMember(MemoryBufferRef BufRef);

  /// Detect the archive format from the object or bitcode file.
  ///
  /// This helps assume the archive format when creating or editing archives in
  /// the case one isn't explicitly set.
  ///
  /// \return The archive kind inferred from the member contents.
  LLVM_ABI object::Archive::Kind detectKindFromObject() const;

  /// Create a member from an existing archive child.
  ///
  /// \param OldMember Existing archive child to copy.
  /// \param Deterministic If true, omit non-deterministic metadata (mtime, uid,
  ///        gid, and permissions).
  /// \return The new member, or an error if the child cannot be read.
  LLVM_ABI static Expected<NewArchiveMember>
  getOldMember(const object::Archive::Child &OldMember, bool Deterministic);

  /// Create a member by reading \p FileName from disk.
  ///
  /// \param FileName Path of the file to include.
  /// \param Deterministic If true, omit non-deterministic metadata (mtime, uid,
  ///        gid, and permissions).
  /// \return The new member, or an error if the file cannot be read.
  LLVM_ABI static Expected<NewArchiveMember> getFile(StringRef FileName,
                                                     bool Deterministic);
};

/// Compute a relative path from the directory of \p From to \p To.
///
/// \param From Path whose parent directory is the relative-path base.
/// \param To Destination path to express relative to \p From.
/// \return The relative path string, or an error if it cannot be computed.
LLVM_ABI Expected<std::string> computeArchiveRelativePath(StringRef From,
                                                          StringRef To);

/// Controls which symbol tables are written into the archive.
enum class SymtabWritingMode {
  NoSymtab,     ///< Do not write a symbol table.
  NormalSymtab, ///< Write the normal symbol table; for Big Archive, write both
                ///< 32-bit and 64-bit symbol tables.
  BigArchive32, ///< Only write the 32-bit Big Archive symbol table.
  BigArchive64  ///< Only write the 64-bit Big Archive symbol table.
};

/// Log \p Err to stderr as a warning.
///
/// \param Err Error to report.
LLVM_ABI void warnToStderr(Error Err);

/// Write an archive directly to an output stream.
///
/// \param Out Stream to receive the archive bytes.
/// \param NewMembers Members to include in the archive.
/// \param WriteSymtab Which symbol table(s) to emit.
/// \param Kind Archive format to write.
/// \param Deterministic If true, use deterministic member metadata.
/// \param Thin If true, write a thin archive (GNU format only).
/// \param IsEC Optional override for COFF ARM64EC archive detection.
/// \param Warn Callback invoked for non-fatal warnings while writing.
/// \return Error::success() on success, or an error if writing fails.
LLVM_ABI Error writeArchiveToStream(
    raw_ostream &Out, ArrayRef<NewArchiveMember> NewMembers,
    SymtabWritingMode WriteSymtab, object::Archive::Kind Kind,
    bool Deterministic, bool Thin, std::optional<bool> IsEC = std::nullopt,
    function_ref<void(Error)> Warn = warnToStderr);

/// Write an archive to the file named \p ArcName.
///
/// \param ArcName Output path for the archive file.
/// \param NewMembers Members to include in the archive.
/// \param WriteSymtab Which symbol table(s) to emit.
/// \param Kind Archive format to write.
/// \param Deterministic If true, use deterministic member metadata.
/// \param Thin If true, write a thin archive (GNU format only).
/// \param OldArchiveBuf Optional buffer backing an existing archive being
///        updated; released before the final rename so Windows can replace the
///        file.
/// \param IsEC Optional override for COFF ARM64EC archive detection.
/// \param Warn Callback invoked for non-fatal warnings while writing.
/// \return Error::success() on success, or an error if writing fails.
LLVM_ABI Error
writeArchive(StringRef ArcName, ArrayRef<NewArchiveMember> NewMembers,
             SymtabWritingMode WriteSymtab, object::Archive::Kind Kind,
             bool Deterministic, bool Thin,
             std::unique_ptr<MemoryBuffer> OldArchiveBuf = nullptr,
             std::optional<bool> IsEC = std::nullopt,
             function_ref<void(Error)> Warn = warnToStderr);

/// Write an archive into a memory buffer.
///
/// writeArchiveToBuffer is similar to writeArchive but returns the Archive in a
/// buffer instead of writing it out to a file.
///
/// \param NewMembers Members to include in the archive.
/// \param WriteSymtab Which symbol table(s) to emit.
/// \param Kind Archive format to write.
/// \param Deterministic If true, use deterministic member metadata.
/// \param Thin If true, write a thin archive (GNU format only).
/// \param Warn Callback invoked for non-fatal warnings while writing.
/// \return A buffer containing the archive, or an error if writing fails.
LLVM_ABI Expected<std::unique_ptr<MemoryBuffer>>
writeArchiveToBuffer(ArrayRef<NewArchiveMember> NewMembers,
                     SymtabWritingMode WriteSymtab, object::Archive::Kind Kind,
                     bool Deterministic, bool Thin,
                     function_ref<void(Error)> Warn = warnToStderr);
}

#endif
