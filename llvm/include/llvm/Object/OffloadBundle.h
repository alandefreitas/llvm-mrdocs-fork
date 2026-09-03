//===- OffloadBundle.h - Utilities for offload bundles---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-------------------------------------------------------------------------===//
//
// This file contains the binary format used for budingling device metadata with
// an associated device image. The data can then be stored inside a host object
// file to create a fat binary and read by the linker. This is intended to be a
// thin wrapper around the image itself. If this format becomes sufficiently
// complex it should be moved to a standard binary format like msgpack or ELF.
//
//===-------------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_OFFLOADBUNDLE_H
#define LLVM_OBJECT_OFFLOADBUNDLE_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Binary.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Compression.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace llvm {

namespace object {

/// Utilities for compressing and decompressing offload bundles.
///
/// The format is as follows:
/// - Magic Number (4 bytes) - A constant "CCOB".
/// - Version (2 bytes)
/// - Compression Method (2 bytes) - Uses the values from
/// llvm::compression::Format.
/// - Total file size (4 bytes in V2, 8 bytes in V3).
/// - Uncompressed Size (4 bytes in V1/V2, 8 bytes in V3).
/// - Truncated MD5 Hash (8 bytes).
/// - Compressed Data (variable length).
class CompressedOffloadBundle {
private:
  static inline const llvm::StringRef MagicNumber = "CCOB";

public:
  /// Header fields describing a compressed offload bundle.
  struct CompressedBundleHeader {
    /// Format version of the compressed bundle header.
    unsigned Version;
    /// Compression method used for the payload.
    llvm::compression::Format CompressionFormat;
    /// Total file size, present in header versions that store it.
    std::optional<size_t> FileSize;
    /// Size of the uncompressed payload in bytes.
    size_t UncompressedFileSize;
    /// Truncated MD5 hash of the uncompressed data.
    uint64_t Hash;

    /// Parse a compressed bundle header from \p Buffer.
    /// \param Buffer The raw bytes beginning at a compressed bundle.
    /// \returns The parsed header, or an error on failure.
    LLVM_ABI static llvm::Expected<CompressedBundleHeader>
        tryParse(llvm::StringRef Buffer);
  };

  /// Default compressed-bundle format version used when compressing.
  static inline const uint16_t DefaultVersion = 3;

  /// Compress \p Input into a compressed offload bundle buffer.
  /// \param P Compression parameters to apply.
  /// \param Input Uncompressed input buffer to compress.
  /// \param Version Compressed-bundle format version to write.
  /// \param VerboseStream Optional stream for verbose diagnostic output.
  /// \returns A memory buffer holding the compressed bundle, or an error.
  LLVM_ABI static llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
  compress(llvm::compression::Params P, const llvm::MemoryBuffer &Input,
           uint16_t Version, raw_ostream *VerboseStream = nullptr);
  /// Decompress a compressed offload bundle in \p Input.
  /// \param Input Buffer containing a compressed offload bundle.
  /// \param VerboseStream Optional stream for verbose diagnostic output.
  /// \returns A memory buffer holding the decompressed data, or an error.
  LLVM_ABI static llvm::Expected<std::unique_ptr<llvm::MemoryBuffer>>
  decompress(const llvm::MemoryBuffer &Input,
             raw_ostream *VerboseStream = nullptr);
};

/// Bundle entry in binary clang-offload-bundler format.
struct OffloadBundleEntry {
  /// Byte offset of this entry within the fat binary.
  uint64_t Offset = 0u;
  /// Size in bytes of this entry's code object.
  uint64_t Size = 0u;
  /// Length in bytes of the entry identifier string.
  uint64_t IDLength = 0u;
  /// Target triple / identifier string for this entry.
  std::string ID;
  /// Construct a bundle entry from offset, size, ID length, and ID.
  /// \param O Byte offset of the entry within the fat binary.
  /// \param S Size in bytes of the entry's code object.
  /// \param I Length in bytes of the identifier string.
  /// \param T Identifier string for this entry.
  OffloadBundleEntry(uint64_t O, uint64_t S, uint64_t I, StringRef T)
      : Offset(O), Size(S), IDLength(I), ID(T.str()) {}
  /// Print a human-readable description of this entry to \p OS.
  /// \param OS Output stream to write the entry description to.
  void dumpInfo(raw_ostream &OS) {
    OS << "Offset = " << Offset << ", Size = " << Size
       << ", ID Length = " << IDLength << ", ID = " << ID << "\n";
  }
  /// Print this entry as a file URI referencing \p FilePath to \p OS.
  /// \param OS Output stream to write the URI to.
  /// \param FilePath Path of the file containing this entry.
  void dumpURI(raw_ostream &OS, StringRef FilePath) {
    OS << ID.data() << "\tfile://" << FilePath << "#offset=" << Offset
       << "&size=" << Size << "\n";
  }
};

/// Fat binary embedded in object files in clang-offload-bundler format
class OffloadBundleFatBin {

  uint64_t Size = 0u;
  StringRef FileName;
  uint64_t NumberOfEntries;
  bool Decompressed;
  SmallVector<OffloadBundleEntry> Entries;

public:
  /// Owning buffer holding decompressed fat-binary data when requested.
  std::unique_ptr<MemoryBuffer> DecompressedBuffer;

  /// Return the list of bundle entries in this fat binary.
  /// \returns The list of bundle entries in this fat binary.
  SmallVector<OffloadBundleEntry> getEntries() { return Entries; }
  /// Return the size in bytes of the fat binary.
  /// \returns The size in bytes of the fat binary.
  uint64_t getSize() const { return Size; }
  /// Return the source file name associated with this fat binary.
  /// \returns The source file name associated with this fat binary.
  StringRef getFileName() const { return FileName; }
  /// Return the number of bundle entries.
  /// \returns The number of bundle entries.
  uint64_t getNumEntries() const { return NumberOfEntries; }
  /// Return whether this fat binary was decompressed on creation.
  /// \returns Whether this fat binary was decompressed on creation.
  bool isDecompressed() const { return Decompressed; }

  /// Create an OffloadBundleFatBin from the section in \p Source.
  /// \param Source Memory buffer containing the fat binary section.
  /// \param SectionOffset Offset of the section within the file.
  /// \param FileName Name of the source file for URI reporting.
  /// \param Decompress Whether to decompress the bundle on creation.
  /// \returns A unique pointer to the created fat binary, or an error.
  LLVM_ABI static Expected<std::unique_ptr<OffloadBundleFatBin>>
  create(MemoryBufferRef Source, uint64_t SectionOffset, StringRef FileName,
         bool Decompress = false);
  /// Extract this fat binary's bundle data from \p Source.
  /// \param Source Object file containing the embedded fat binary.
  /// \returns Success or an error describing the failure.
  LLVM_ABI Error extractBundle(const ObjectFile &Source);

  /// Write each bundle entry out as a standalone code-object file.
  /// \returns Success or an error describing the failure.
  LLVM_ABI Error dumpEntryToCodeObject();

  /// Read bundle entries from \p Section starting at \p SectionOffset.
  /// \param Section Raw bytes of the fat-binary section.
  /// \param SectionOffset Offset of the section within the containing file.
  /// \returns Success or an error describing the failure.
  LLVM_ABI Error readEntries(StringRef Section, uint64_t SectionOffset);
  /// Print a human-readable description of each entry to standard output.
  void dumpEntries() {
    for (OffloadBundleEntry &Entry : Entries)
      Entry.dumpInfo(outs());
  }

  /// Print each entry as a file URI to standard output.
  void printEntriesAsURI() {
    for (OffloadBundleEntry &Entry : Entries)
      Entry.dumpURI(outs(), FileName);
  }

  /// Construct a fat binary wrapper around \p Source from \p File.
  /// \param Source Memory buffer containing the fat binary section.
  /// \param File Name of the source file for URI reporting.
  /// \param Decompress Whether to copy and own a decompressed buffer.
  OffloadBundleFatBin(MemoryBufferRef Source, StringRef File,
                      bool Decompress = false)
      : FileName(File), NumberOfEntries(0), Decompressed(Decompress),
        Entries(SmallVector<OffloadBundleEntry>()) {
    if (Decompress)
      DecompressedBuffer =
          MemoryBuffer::getMemBufferCopy(Source.getBuffer(), File);
  }
};

/// Kind of URI used to locate an offload bundle entry.
enum UriTypeT {
  /// URI referring to a file on disk.
  FILE_URI,
  /// URI referring to an in-memory buffer.
  MEMORY_URI
};

/// Parsed URI locating an offload bundle entry within a file or buffer.
struct OffloadBundleURI {
  /// Byte offset of the entry within the referenced resource.
  int64_t Offset = 0;
  /// Size in bytes of the entry within the referenced resource.
  int64_t Size = 0;
  /// Process ID associated with a memory URI, if applicable.
  uint64_t ProcessID = 0;
  /// File path component of a file URI.
  StringRef FileName;
  /// Whether this URI refers to a file or to memory.
  UriTypeT URIType;

  // Constructors
  // TODO: add a Copy ctor ?
  /// Construct a file URI for \p File at \p Off with length \p Size.
  /// \param File Path of the file containing the entry.
  /// \param Off Byte offset of the entry within the file.
  /// \param Size Size in bytes of the entry.
  OffloadBundleURI(StringRef File, int64_t Off, int64_t Size)
      : Offset(Off), Size(Size), ProcessID(0), FileName(File),
        URIType(FILE_URI) {}

public:
  /// Create an OffloadBundleURI by parsing \p Str as a URI of \p Type.
  /// \param Str URI string to parse.
  /// \param Type Expected URI kind (\c FILE_URI or \c MEMORY_URI).
  /// \returns A unique pointer to the parsed URI, or an error.
  static Expected<std::unique_ptr<OffloadBundleURI>>
  createOffloadBundleURI(StringRef Str, UriTypeT Type) {
    switch (Type) {
    case FILE_URI:
      return createFileURI(Str);
      break;
    case MEMORY_URI:
      return createMemoryURI(Str);
      break;
    }
    llvm_unreachable("Unknown UriTypeT enum");
  }

  /// Parse \p Str as a \c file:// URI with offset and size query parts.
  /// \param Str URI string beginning with \c file://.
  /// \returns A unique pointer to the parsed URI, or an error.
  static Expected<std::unique_ptr<OffloadBundleURI>>
  createFileURI(StringRef Str) {
    int64_t O = 0;
    int64_t S = 0;

    if (!Str.consume_front("file://"))
      return createStringError(object_error::parse_failed,
                               "Reading type of URI");

    StringRef FilePathname =
        Str.take_until([](char C) { return (C == '#') || (C == '?'); });
    Str = Str.drop_front(FilePathname.size());

    if (!Str.consume_front("#offset="))
      return createStringError(object_error::parse_failed,
                               "Reading 'offset' in URI");

    StringRef OffsetStr = Str.take_until([](char C) { return C == '&'; });
    OffsetStr.getAsInteger(10, O);
    Str = Str.drop_front(OffsetStr.size());

    if (!Str.consume_front("&size="))
      return createStringError(object_error::parse_failed,
                               "Reading 'size' in URI");

    Str.getAsInteger(10, S);
    std::unique_ptr<OffloadBundleURI> OffloadingURI(
        new OffloadBundleURI(FilePathname, O, S));
    return std::move(OffloadingURI);
  }

  /// Parse \p Str as a memory URI (currently unsupported).
  /// \param Str URI string identifying an in-memory resource.
  /// \returns Always an error; memory URIs are not yet implemented.
  static Expected<std::unique_ptr<OffloadBundleURI>>
  createMemoryURI(StringRef Str) {
    // TODO: add parseMemoryURI type
    return createStringError(object_error::parse_failed,
                             "Memory Type URI is not currently supported.");
  }

  /// Return the file path component of this URI.
  /// \returns The file path component of this URI.
  StringRef getFileName() const { return FileName; }
};

/// Extracts fat binary in binary clang-offload-bundler format from object \p
/// Obj and return it in \p Bundles.
/// \param Obj Object file that may contain embedded offload fat binaries.
/// \param Bundles Output list populated with extracted fat binaries.
/// \returns Success or an error describing the failure.
LLVM_ABI Error extractOffloadBundleFatBinary(
    const ObjectFile &Obj, SmallVectorImpl<OffloadBundleFatBin> &Bundles);

/// Extract code object memory from the given \p Source object file at \p Offset
/// and of \p Size, and copy into \p OutputFileName.
/// \param Source Object file containing the code object.
/// \param Offset Byte offset of the code object within \p Source.
/// \param Size Size in bytes of the code object.
/// \param OutputFileName Path of the file to write the extracted object to.
/// \returns Success or an error describing the failure.
LLVM_ABI Error extractCodeObject(const ObjectFile &Source, size_t Offset,
                                 size_t Size, StringRef OutputFileName);

/// Extract code object memory from the given \p Buffer at \p Offset and of \p
/// Size, and copy into \p OutputFileName.
/// \param Buffer Memory buffer containing the code object.
/// \param Offset Byte offset of the code object within \p Buffer.
/// \param Size Size in bytes of the code object.
/// \param OutputFileName Path of the file to write the extracted object to.
/// \returns Success or an error describing the failure.
LLVM_ABI Error extractCodeObject(MemoryBufferRef Buffer, int64_t Offset,
                                 int64_t Size, StringRef OutputFileName);
/// Extracts an Offload Bundle Entry given by URI.
/// \param URIstr URI identifying the offload bundle entry to extract.
/// \returns Success or an error describing the failure.
LLVM_ABI Error extractOffloadBundleByURI(StringRef URIstr);

} // namespace object

} // namespace llvm
#endif
