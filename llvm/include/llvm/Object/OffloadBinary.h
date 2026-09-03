//===- OffloadBinary.h - Utilities for handling offloading code -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the binary format used for bundling device metadata with
// an associated device image. The data can then be stored inside a host object
// file to create a fat binary and read by the linker. This is intended to be a
// thin wrapper around the image itself. If this format becomes sufficiently
// complex it should be moved to a standard binary format like msgpack or ELF.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_OFFLOADBINARY_H
#define LLVM_OBJECT_OFFLOADBINARY_H

#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"
#include <memory>

namespace llvm {

namespace object {

/// The producer of the associated offloading image.
enum OffloadKind : uint16_t {
  OFK_None = 0,          ///< No offload kind.
  OFK_OpenMP = (1 << 0), ///< OpenMP offloading.
  OFK_Cuda = (1 << 1),   ///< CUDA offloading.
  OFK_HIP = (1 << 2),    ///< HIP offloading.
  OFK_SYCL = (1 << 3),   ///< SYCL offloading.
  OFK_LAST = (1 << 4),   ///< Sentinel one past the last valid offload kind.
};

/// The type of contents the offloading image contains.
enum ImageKind : uint16_t {
  IMG_None = 0,  ///< No image kind.
  IMG_Object,    ///< Native object file image.
  IMG_Bitcode,   ///< LLVM bitcode image.
  IMG_Cubin,     ///< CUDA cubin image.
  IMG_Fatbinary, ///< CUDA fatbinary image.
  IMG_PTX,       ///< NVIDIA PTX image.
  IMG_SPIRV,     ///< SPIR-V image.
  IMG_LAST,      ///< Sentinel one past the last valid image kind.
};

/// Flags associated with the Entry.
enum OffloadEntryFlags : uint32_t {
  OIF_None = 0,            ///< No flags set.
  OIF_Metadata = (1 << 0), ///< Metadata-only entry with no image.
};

/// Binary serialization of an offloading image and its metadata.
///
/// A simple binary serialization of an offloading file. We use this format to
/// embed the offloading image into the host executable so it can be extracted
/// and used by the linker.
///
/// Many of these could be stored in the same section by the time the linker
/// sees it so we mark this information with a header. The version is used to
/// detect ABI stability and the size is used to find other offloading entries
/// that may exist in the same section. All offsets are given as absolute byte
/// offsets from the beginning of the file.
class OffloadBinary : public Binary {
public:
  /// Const iterator over key/value string pairs in the binary.
  using string_iterator = MapVector<StringRef, StringRef>::const_iterator;
  /// Range of string key/value pair iterators.
  using string_iterator_range = iterator_range<string_iterator>;

  /// The current version of the binary used for backwards compatibility.
  static const uint32_t Version = 2;

  /// The offloading metadata that will be serialized to a memory buffer.
  struct OffloadingImage {
    ImageKind TheImageKind = ImageKind::IMG_None; ///< Kind of contents in Image.
    /// Producer of this offloading image.
    OffloadKind TheOffloadKind = OffloadKind::OFK_None;
    uint32_t Flags = 0; ///< Flags associated with this image entry.
    MapVector<StringRef, StringRef> StringData; ///< Key/value metadata strings.
    std::unique_ptr<MemoryBuffer> Image; ///< Raw offloading image contents.
  };

  /// File header for a serialized offload binary.
  struct Header {
    uint8_t Magic[4] = {0x10, 0xFF, 0x10, 0xAD}; ///< Magic bytes (0x10FF10AD).
    uint32_t Version = OffloadBinary::Version; ///< Format version identifier.
    uint64_t Size;          ///< Size in bytes of this entire binary.
    uint64_t EntriesOffset; ///< Offset in bytes to the start of entries block.
    uint64_t EntriesCount;  ///< Number of metadata entries in the binary.
  };

  /// Metadata for a single offloading image entry.
  struct Entry {
    ImageKind TheImageKind;     ///< Kind of the image stored.
    OffloadKind TheOffloadKind; ///< Producer of this image.
    uint32_t Flags;             ///< Additional flags associated with the entry.
    uint64_t StringOffset;      ///< Offset in bytes to the string map.
    uint64_t NumStrings;        ///< Number of entries in the string map.
    uint64_t ImageOffset;       ///< Offset in bytes of the actual binary image.
    uint64_t ImageSize;         ///< Size in bytes of the binary image.
  };

  /// String map entry with an explicit value size (version 2+).
  struct StringEntry {
    uint64_t KeyOffset;   ///< Offset to the null-terminated key string.
    uint64_t ValueOffset; ///< Offset to the value bytes.
    uint64_t ValueSize;   ///< Size of the value in bytes.
  };

  /// String map entry for version 1 binaries (null-terminated value).
  struct StringEntryV1 {
    uint64_t KeyOffset;   ///< Offset to the null-terminated key string.
    uint64_t ValueOffset; ///< Offset to the null-terminated value string.
  };

  /// Attempt to extract and validate the header from the offloading binary in
  /// \p Buf.
  ///
  /// \param Buf The memory buffer containing the offload binary.
  /// \returns A pointer to the validated header, or an error.
  LLVM_ABI
  static Expected<const Header *> extractHeader(MemoryBufferRef Buf);

  /// Parse offloading binaries from a memory buffer.
  ///
  /// Attempt to parse the offloading binary stored in \p Buf.
  /// For version 1 binaries, always returns a single OffloadBinary.
  /// For version 2+ binaries:
  ///   - If \p Index is provided, returns the OffloadBinary at that index.
  ///   - If \p Index is std::nullopt, returns all OffloadBinary entries.
  /// \param Buf The memory buffer containing the offload binary.
  /// \param Index Optional index to select a specific entry. If not provided,
  ///              all entries are returned (version 2+ only).
  /// \returns An array of unique pointers to OffloadBinary objects, or an
  /// error.
  LLVM_ABI static Expected<SmallVector<std::unique_ptr<OffloadBinary>>>
  create(MemoryBufferRef Buf, std::optional<uint64_t> Index = std::nullopt);

  /// Serialize the contents of \p OffloadingData to a binary buffer to be read
  /// later.
  ///
  /// \param OffloadingData Offloading images to serialize.
  /// \returns A small string containing the serialized binary.
  LLVM_ABI static SmallString<0>
  write(ArrayRef<OffloadingImage> OffloadingData);

  /// Return the required alignment for offload binary data.
  /// \returns The required alignment in bytes.
  static uint64_t getAlignment() { return 8; }

  /// Return the image kind of this entry.
  /// \returns The image kind of this entry.
  ImageKind getImageKind() const { return TheEntry->TheImageKind; }
  /// Return the offload kind of this entry.
  /// \returns The offload kind of this entry.
  OffloadKind getOffloadKind() const { return TheEntry->TheOffloadKind; }
  /// Return the format version from the header.
  /// \returns The format version from the header.
  uint32_t getVersion() const { return TheHeader->Version; }
  /// Return the flags associated with this entry.
  /// \returns The flags associated with this entry.
  uint32_t getFlags() const { return TheEntry->Flags; }
  /// Return the total size of the binary from the header.
  /// \returns The total size of the binary from the header.
  uint64_t getSize() const { return TheHeader->Size; }
  /// Return the index of this entry within the serialized binary.
  /// \returns The index of this entry within the serialized binary.
  uint64_t getIndex() const { return Index; }

  /// Return the target triple string from the string map.
  /// \returns The target triple string from the string map.
  StringRef getTriple() const { return getString("triple"); }
  /// Return the architecture string from the string map.
  /// \returns The architecture string from the string map.
  StringRef getArch() const { return getString("arch"); }
  /// Return the raw image bytes for this entry.
  /// \returns The raw image bytes for this entry.
  StringRef getImage() const {
    return StringRef(&Buffer[TheEntry->ImageOffset], TheEntry->ImageSize);
  }

  /// Iterate over all key and value pairs in the binary.
  /// \returns A range over all key and value pairs in the binary.
  string_iterator_range strings() const { return StringData; }

  /// Look up a string value by key in the string map.
  ///
  /// \param Key The string map key to look up.
  /// \returns The value associated with \p Key, or an empty string if absent.
  StringRef getString(StringRef Key) const { return StringData.lookup(Key); }

  /// Return true if \p V is an OffloadBinary.
  ///
  /// \param V Binary to test.
  /// \returns True if \p V is an OffloadBinary.
  static bool classof(const Binary *V) { return V->isOffloadFile(); }

private:
  OffloadBinary(MemoryBufferRef Source, const Header *TheHeader,
                const Entry *TheEntry, const uint64_t Index = 0)
      : Binary(Binary::ID_Offload, Source), Buffer(Source.getBufferStart()),
        TheHeader(TheHeader), TheEntry(TheEntry), Index(Index) {
    // StringEntryV1 and StringEntry have ABI compatible Key/ValueOffset fields,
    // but different sizes, so we need to manually calculate offset.
    const char *StringMapBegin = &Buffer[TheEntry->StringOffset];
    const size_t StringEntrySize =
        TheHeader->Version == 1 ? sizeof(StringEntryV1) : sizeof(StringEntry);
    for (uint64_t I = 0, E = TheEntry->NumStrings; I != E; ++I) {
      const char *StringEntryPtr = StringMapBegin + I * StringEntrySize;
      const StringEntryV1 *EntryV1 =
          reinterpret_cast<const StringEntryV1 *>(StringEntryPtr);
      StringRef Key = &Buffer[EntryV1->KeyOffset];
      if (TheHeader->Version == 1) {
        StringData[Key] = &Buffer[EntryV1->ValueOffset];
      } else {
        const StringEntry *Entry =
            reinterpret_cast<const StringEntry *>(StringEntryPtr);
        StringData[Key] =
            StringRef(&Buffer[Entry->ValueOffset], Entry->ValueSize);
      }
    }
  }

  OffloadBinary(const OffloadBinary &Other) = delete;

  /// Map from keys to offsets in the binary.
  MapVector<StringRef, StringRef> StringData;
  /// Raw pointer to the MemoryBufferRef for convenience.
  const char *Buffer;
  /// Location of the header within the binary.
  const Header *TheHeader;
  /// Location of the metadata entries within the binary.
  const Entry *TheEntry;
  /// Index of the entry in the list of entries serialized in the Buffer.
  const uint64_t Index;
};

/// Owning container for a single OffloadBinary and its memory buffer.
///
/// Memory is shared between multiple OffloadBinary instances read from
/// the single serialized offload binary.
class OffloadFile : public OwningBinary<OffloadBinary> {
public:
  /// Triple and architecture pair identifying an offload target.
  using TargetID = std::pair<StringRef, StringRef>;

  /// Construct an OffloadFile owning \p Binary and its backing \p Buffer.
  ///
  /// \param Binary Parsed offload binary.
  /// \param Buffer Memory buffer backing Binary.
  OffloadFile(std::unique_ptr<OffloadBinary> Binary,
              std::unique_ptr<MemoryBuffer> Buffer)
      : OwningBinary<OffloadBinary>(std::move(Binary), std::move(Buffer)) {}

  /// Make a deep copy of this offloading file.
  /// \returns A deep copy of this offloading file.
  OffloadFile copy() const {
    std::unique_ptr<MemoryBuffer> Buffer = MemoryBuffer::getMemBufferCopy(
        getBinary()->getMemoryBufferRef().getBuffer(),
        getBinary()->getMemoryBufferRef().getBufferIdentifier());

    // This parsing should never fail because it has already been parsed.
    auto NewBinaryOrErr =
        OffloadBinary::create(*Buffer, getBinary()->getIndex());
    assert(NewBinaryOrErr && "Failed to parse a copy of the binary?");
    if (!NewBinaryOrErr)
      llvm::consumeError(NewBinaryOrErr.takeError());
    return OffloadFile(std::move((*NewBinaryOrErr)[0]), std::move(Buffer));
  }

  /// We use the Triple and Architecture pair to group linker inputs together.
  /// This conversion function lets us use these inputs in a hash-map.
  /// \returns The triple and architecture pair for this file.
  operator TargetID() const {
    return std::make_pair(getBinary()->getTriple(), getBinary()->getArch());
  }
};

/// Extracts embedded device offloading code from a memory \p Buffer to a list
/// of \p Binaries.
///
/// \param Buffer Memory buffer that may contain embedded offload binaries.
/// \param Binaries Destination list that receives extracted OffloadFile
/// objects.
/// \returns Success or an error describing the failure.
LLVM_ABI Error extractOffloadBinaries(MemoryBufferRef Buffer,
                                      SmallVectorImpl<OffloadFile> &Binaries);

/// Convert a string \p Name to an image kind.
///
/// \param Name Image kind name to convert.
/// \returns The image kind corresponding to \p Name.
LLVM_ABI ImageKind getImageKind(StringRef Name);

/// Convert an image kind to its string representation.
///
/// \param Name Image kind to convert.
/// \returns The string representation of \p Name.
LLVM_ABI StringRef getImageKindName(ImageKind Name);

/// Convert a string \p Name to an offload kind.
///
/// \param Name Offload kind name to convert.
/// \returns The offload kind corresponding to \p Name.
LLVM_ABI OffloadKind getOffloadKind(StringRef Name);

/// Convert an offload kind to its string representation.
///
/// \param Name Offload kind to convert.
/// \returns The string representation of \p Name.
LLVM_ABI StringRef getOffloadKindName(OffloadKind Name);

/// Return whether a provided target can satisfy a requested target.
///
/// An image built for target \p Provided can provide the device code for a
/// request for target \p Requested. This is directional: a feature-unspecified
/// or generic image serves a more specific request (e.g. a generic
/// static-archive member pulled into a specific device-image group), but not
/// vice versa. For AMDGPU a target id is a string conforming to the following
/// BNF syntax:
///
///  target-id ::= '<arch> ( : <feature> ( '+' | '-' ) )*'
///
/// The features 'xnack' and 'sramecc' are currently supported. These can be in
/// the state of on, off, and any when unspecified. A provided target marked as
/// any can bind with either on or off.
///
/// \param Provided Target identity of an available image.
/// \param Requested Target identity being requested.
/// \returns True if \p Provided can satisfy \p Requested.
LLVM_ABI bool areTargetsCompatible(const OffloadFile::TargetID &Provided,
                                   const OffloadFile::TargetID &Requested);

/// Returns true if \p LHS and \p RHS denote the same logical target, i.e. the
/// same processor and features.
///
/// \param LHS First target identity to compare.
/// \param RHS Second target identity to compare.
/// \returns True if \p LHS and \p RHS denote the same logical target.
LLVM_ABI bool areTargetsEquivalent(const OffloadFile::TargetID &LHS,
                                   const OffloadFile::TargetID &RHS);

} // namespace object

} // namespace llvm
#endif
