//===- DebugChecksumsSubsection.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGCHECKSUMSSUBSECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGCHECKSUMSSUBSECTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/DebugSubsection.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <vector>

namespace llvm {

class BinaryStreamReader;
class BinaryStreamWriter;

namespace codeview {

class DebugStringTableSubsection;

/// One source-file checksum recorded in a CodeView FileChecksums subsection.
struct FileChecksumEntry {
  /// Byte offset of the filename in the global string table.
  uint32_t FileNameOffset;
  /// The type of checksum algorithm used.
  FileChecksumKind Kind;
  /// The raw checksum bytes.
  ArrayRef<uint8_t> Checksum;
};

} // end namespace codeview

/// Extracts \c FileChecksumEntry records from a CodeView checksums stream.
template <> struct VarStreamArrayExtractor<codeview::FileChecksumEntry> {
public:
  /// No per-extractor context is required for file checksum entries.
  using ContextType = void;

  /// Extract one \c FileChecksumEntry from \p Stream into \p Item.
  ///
  /// \param Stream Stream positioned at the start of the next checksum entry.
  /// \param Len Set to the number of bytes occupied by the extracted entry.
  /// \param Item Set to the extracted file checksum entry.
  ///
  /// \returns An Error on failure, or success if an entry was extracted.
  LLVM_ABI Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                            codeview::FileChecksumEntry &Item);
};

namespace codeview {

/// Read-only view of a CodeView FileChecksums debug subsection.
class DebugChecksumsSubsectionRef final : public DebugSubsectionRef {
  using FileChecksumArray = VarStreamArray<codeview::FileChecksumEntry>;
  using Iterator = FileChecksumArray::Iterator;

public:
  /// Construct an empty, uninitialized file-checksums subsection reference.
  DebugChecksumsSubsectionRef()
      : DebugSubsectionRef(DebugSubsectionKind::FileChecksums) {}

  /// Return true if \p S is a FileChecksums subsection reference.
  ///
  /// \param S Subsection reference to test.
  ///
  /// \returns True if \p S is a FileChecksums subsection reference.
  static bool classof(const DebugSubsectionRef *S) {
    return S->kind() == DebugSubsectionKind::FileChecksums;
  }

  /// Return true if the underlying checksum array has been initialized.
  ///
  /// \returns True if the underlying checksum array has been initialized.
  bool valid() const { return Checksums.valid(); }

  /// Initialize this view from checksum entries read via \p Reader.
  ///
  /// \param Reader Reader positioned at the start of the checksums data.
  ///
  /// \returns An Error on failure, or success if initialization succeeded.
  LLVM_ABI Error initialize(BinaryStreamReader Reader);
  /// Initialize this view from the checksum entries in \p Stream.
  ///
  /// \param Stream Stream containing the serialized checksums subsection.
  ///
  /// \returns An Error on failure, or success if initialization succeeded.
  LLVM_ABI Error initialize(BinaryStreamRef Stream);

  /// Return an iterator to the first file checksum entry.
  ///
  /// \returns An iterator to the first file checksum entry.
  Iterator begin() const { return Checksums.begin(); }
  /// Return an iterator past the last file checksum entry.
  ///
  /// \returns An iterator past the last file checksum entry.
  Iterator end() const { return Checksums.end(); }

  /// Return the underlying array of file checksum entries.
  ///
  /// \returns The underlying array of file checksum entries.
  const FileChecksumArray &getArray() const { return Checksums; }

private:
  FileChecksumArray Checksums;
};

/// Writable CodeView FileChecksums debug subsection.
class LLVM_ABI DebugChecksumsSubsection final : public DebugSubsection {
public:
  /// Construct a checksums subsection that stores filenames in \p Strings.
  ///
  /// \param Strings String table subsection used to intern source file names.
  explicit DebugChecksumsSubsection(DebugStringTableSubsection &Strings);

  /// Return true if \p S is a FileChecksums subsection.
  ///
  /// \param S Subsection to test.
  ///
  /// \returns True if \p S is a FileChecksums subsection.
  static bool classof(const DebugSubsection *S) {
    return S->kind() == DebugSubsectionKind::FileChecksums;
  }

  /// Append a checksum for \p FileName using algorithm \p Kind and \p Bytes.
  ///
  /// \param FileName Source file name to associate with the checksum.
  /// \param Kind Checksum algorithm kind.
  /// \param Bytes Raw checksum bytes for the file.
  void addChecksum(StringRef FileName, FileChecksumKind Kind,
                   ArrayRef<uint8_t> Bytes);

  /// Return the serialized size of this subsection in bytes.
  ///
  /// \returns The serialized size of this subsection in bytes.
  uint32_t calculateSerializedSize() const override;
  /// Write this subsection's serialized form to \p Writer.
  ///
  /// \param Writer Destination stream writer.
  ///
  /// \returns An Error on failure, or success if the write completed.
  Error commit(BinaryStreamWriter &Writer) const override;
  /// Return the byte offset of the checksum entry for \p FileName.
  ///
  /// \param FileName Source file name whose checksum offset is requested.
  ///
  /// \returns Byte offset of the matching entry within this subsection.
  uint32_t mapChecksumOffset(StringRef FileName) const;

private:
  DebugStringTableSubsection &Strings;

  DenseMap<uint32_t, uint32_t> OffsetMap;
  uint32_t SerializedSize = 0;
  BumpPtrAllocator Storage;
  std::vector<FileChecksumEntry> Checksums;
};

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGCHECKSUMSSUBSECTION_H
