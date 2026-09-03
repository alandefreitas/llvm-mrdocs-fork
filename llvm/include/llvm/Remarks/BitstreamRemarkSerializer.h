//===-- BitstreamRemarkSerializer.h - Bitstream serializer ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides an implementation of the serializer using the LLVM
// Bitstream format.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_REMARKS_BITSTREAMREMARKSERIALIZER_H
#define LLVM_REMARKS_BITSTREAMREMARKSERIALIZER_H

#include "llvm/Bitstream/BitstreamWriter.h"
#include "llvm/Remarks/BitstreamRemarkContainer.h"
#include "llvm/Remarks/RemarkSerializer.h"
#include <optional>

namespace llvm {
namespace remarks {

/// Forward declaration of the Remarks record.
struct Remarks;

/// Serialize the remarks to LLVM bitstream.
/// This class provides ways to emit remarks in the LLVM bitstream format and
/// its associated metadata.
struct BitstreamRemarkSerializerHelper {
  /// Buffer used to construct records and pass to the bitstream writer.
  SmallVector<uint64_t, 64> R;
  /// The Bitstream writer.
  BitstreamWriter Bitstream;
  /// The type of the container we are serializing.
  BitstreamRemarkContainerType ContainerType;

  /// Abbreviation ID for the container-info metadata record.
  ///
  /// Abbrev IDs are initialized in the block info block. Note: depending on the
  /// container type, some IDs might be uninitialized. Warning: When adding more
  /// abbrev IDs, make sure to update the BlockCodeSize (in the call to
  /// EnterSubblock).
  uint64_t RecordMetaContainerInfoAbbrevID = 0;
  /// Abbreviation ID for the remark-version metadata record.
  uint64_t RecordMetaRemarkVersionAbbrevID = 0;
  /// Abbreviation ID for the string-table metadata record.
  uint64_t RecordMetaStrTabAbbrevID = 0;
  /// Abbreviation ID for the external-file metadata record.
  uint64_t RecordMetaExternalFileAbbrevID = 0;
  /// Abbreviation ID for the remark header record.
  uint64_t RecordRemarkHeaderAbbrevID = 0;
  /// Abbreviation ID for the remark debug-location record.
  uint64_t RecordRemarkDebugLocAbbrevID = 0;
  /// Abbreviation ID for the remark hotness record.
  uint64_t RecordRemarkHotnessAbbrevID = 0;
  /// Abbreviation ID for a remark argument that includes a debug location.
  uint64_t RecordRemarkArgWithDebugLocAbbrevID = 0;
  /// Abbreviation ID for a remark argument without a debug location.
  uint64_t RecordRemarkArgWithoutDebugLocAbbrevID = 0;

  /// Construct a helper that writes a \p ContainerType bitstream to \p OS.
  /// \param ContainerType Kind of remark container to serialize.
  /// \param OS Output stream that receives the bitstream.
  LLVM_ABI
  BitstreamRemarkSerializerHelper(BitstreamRemarkContainerType ContainerType,
                                  raw_ostream &OS);

  /// Copy construction is deleted; Bitstream points into owned buffers.
  /// @param Other Unused; this constructor is deleted.
  BitstreamRemarkSerializerHelper(const BitstreamRemarkSerializerHelper &Other) =
      delete;
  /// Copy assignment is deleted; Bitstream points into owned buffers.
  /// @param Other Unused; this assignment is deleted.
  BitstreamRemarkSerializerHelper &
  operator=(const BitstreamRemarkSerializerHelper &Other) = delete;
  /// Move construction is deleted; Bitstream points into owned buffers.
  /// @param Other Unused; this constructor is deleted.
  BitstreamRemarkSerializerHelper(BitstreamRemarkSerializerHelper &&Other) =
      delete;
  /// Move assignment is deleted; Bitstream points into owned buffers.
  /// @param Other Unused; this assignment is deleted.
  BitstreamRemarkSerializerHelper &
  operator=(BitstreamRemarkSerializerHelper &&Other) = delete;

  /// Set up the necessary block info entries according to the container type.
  LLVM_ABI void setupBlockInfo();

  /// Set up the block info for the metadata block.
  LLVM_ABI void setupMetaBlockInfo();
  /// The remark version in the metadata block.
  LLVM_ABI void setupMetaRemarkVersion();
  /// Emit the remark version record into the metadata block.
  /// \param RemarkVersion Version number of the remark entry format.
  LLVM_ABI void emitMetaRemarkVersion(uint64_t RemarkVersion);
  /// The strtab in the metadata block.
  LLVM_ABI void setupMetaStrTab();
  /// Emit the string table record into the metadata block.
  /// \param StrTab String table to serialize.
  LLVM_ABI void emitMetaStrTab(const StringTable &StrTab);
  /// The external file in the metadata block.
  LLVM_ABI void setupMetaExternalFile();
  /// Emit the external file path record into the metadata block.
  /// \param Filename Path of the external remarks file.
  LLVM_ABI void emitMetaExternalFile(StringRef Filename);

  /// The block info for the remarks block.
  LLVM_ABI void setupRemarkBlockInfo();

  /// Emit the main metadata at the beginning of the file.
  /// \param Filename Optional external remarks filename to record.
  LLVM_ABI void emitMetaBlock(std::optional<StringRef> Filename = std::nullopt);

  /// Emit the remaining metadata at the end of the file. Here we emit metadata
  /// that is only known once all remarks were emitted.
  /// \param StrTab String table finalized after all remarks were emitted.
  LLVM_ABI void emitLateMetaBlock(const StringTable &StrTab);

  /// Emit a remark block. The string table is required.
  /// \param Remark Remark to serialize into a remark block.
  /// \param StrTab String table used to intern remark strings.
  LLVM_ABI void emitRemark(const Remark &Remark, StringTable &StrTab);
};

/// Implementation of the remark serializer using LLVM bitstream.
struct LLVM_ABI BitstreamRemarkSerializer : public RemarkSerializer {
  /// The file should contain:
  /// 1) The block info block that describes how to read the blocks.
  /// 2) The metadata block that contains various information about the remarks
  ///    in the file.
  /// 3) A number of remark blocks.
  /// 4) Another metadata block for metadata that is only finalized once all
  ///    remarks were emitted (e.g. StrTab)

  /// The helper to emit bitstream. This is nullopt when the Serializer has not
  /// been setup yet.
  std::optional<BitstreamRemarkSerializerHelper> Helper;

  /// Construct a serializer that will create its own string table.
  /// \param OS Output stream that receives the serialized remarks.
  BitstreamRemarkSerializer(raw_ostream &OS);
  /// Construct a serializer with a pre-filled string table.
  /// \param OS Output stream that receives the serialized remarks.
  /// \param StrTab Pre-filled string table to use while serializing.
  BitstreamRemarkSerializer(raw_ostream &OS, StringTable StrTab);

  /// Destroy the serializer, finalizing emission if needed.
  ~BitstreamRemarkSerializer() override;

  /// Emit a remark to the stream. This also emits the metadata associated to
  /// the remarks. This writes the serialized output to the provided stream.
  /// \param Remark Remark to emit.
  void emit(const Remark &Remark) override;

  /// Finalize emission of remarks.
  ///
  /// This emits the late metadata block and flushes internal buffers. It is
  /// safe to call this function multiple times, and it is automatically
  /// executed on destruction of the Serializer.
  void finalize() override;

  /// Return the metadata serializer associated with this remark serializer.
  ///
  /// Based on the container type of the current serializer, the container type
  /// of the metadata serializer will change.
  /// \param OS Output stream for the metadata.
  /// \param ExternalFilename Path to an external remarks file, if any.
  /// @return Metadata serializer matching this remark serializer's container.
  std::unique_ptr<MetaSerializer>
  metaSerializer(raw_ostream &OS, StringRef ExternalFilename) override;

  /// Return true if \p S is a bitstream remark serializer.
  /// \param S Serializer to test.
  /// @return True if \p S is a BitstreamRemarkSerializer.
  static bool classof(const RemarkSerializer *S) {
    return S->SerializerFormat == Format::Bitstream;
  }

private:
  void setup();
};

/// Serializer of metadata for bitstream remarks.
struct LLVM_ABI BitstreamMetaSerializer : public MetaSerializer {
  /// Optional helper used to emit the metadata bitstream.
  std::optional<BitstreamRemarkSerializerHelper> Helper;

  /// Path of the external remarks file recorded in the metadata.
  StringRef ExternalFilename;

  /// Create a new meta serializer based on \p ContainerType.
  /// \param OS Output stream that receives the metadata bitstream.
  /// \param ContainerType Kind of remark container whose metadata is emitted.
  /// \param ExternalFilename Path of the external remarks file to record.
  BitstreamMetaSerializer(raw_ostream &OS,
                          BitstreamRemarkContainerType ContainerType,
                          StringRef ExternalFilename)
      : MetaSerializer(OS), ExternalFilename(ExternalFilename) {
    Helper.emplace(ContainerType, OS);
  }

  /// Emit the metadata block to the configured output stream.
  void emit() override;
};

} // end namespace remarks
} // end namespace llvm

#endif // LLVM_REMARKS_BITSTREAMREMARKSERIALIZER_H
