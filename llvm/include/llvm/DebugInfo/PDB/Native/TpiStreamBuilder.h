//===- TpiStreamBuilder.h - PDB Tpi Stream Creation -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_PDB_NATIVE_TPISTREAMBUILDER_H
#define LLVM_DEBUGINFO_PDB_NATIVE_TPISTREAMBUILDER_H

#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/DebugInfo/PDB/Native/RawConstants.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

#include <vector>

namespace llvm {
class BinaryByteStream;
template <typename T> struct BinaryItemTraits;

template <> struct BinaryItemTraits<llvm::codeview::CVType> {
  static size_t length(const codeview::CVType &Item) { return Item.length(); }
  static ArrayRef<uint8_t> bytes(const codeview::CVType &Item) {
    return Item.data();
  }
};

namespace msf {
class MSFBuilder;
struct MSFLayout;
}
namespace pdb {
struct TpiStreamHeader;

/// Builds the PDB Type Information (TPI) or ID Information (IPI) stream.
class TpiStreamBuilder {
public:
  /// Construct a TPI/IPI stream builder that allocates in \p Msf.
  ///
  /// \param Msf The MSF builder that owns the PDB streams.
  /// \param StreamIdx The MSF stream index for the TPI or IPI stream.
  LLVM_ABI explicit TpiStreamBuilder(msf::MSFBuilder &Msf, uint32_t StreamIdx);
  /// Destroy the TPI stream builder.
  LLVM_ABI ~TpiStreamBuilder();

  /// Deleted copy constructor.
  ///
  /// \param Other Unused; copy construction is not supported.
  TpiStreamBuilder(const TpiStreamBuilder &Other) = delete;
  /// Deleted copy assignment operator.
  ///
  /// \param Other Unused; copy assignment is not supported.
  TpiStreamBuilder &operator=(const TpiStreamBuilder &Other) = delete;

  /// Set the TPI stream version header field.
  ///
  /// \param Version The PDB TPI version to store in the stream header.
  LLVM_ABI void setVersionHeader(PdbRaw_TpiVer Version);
  /// Append a single CodeView type record and its hash.
  ///
  /// \param Type The serialized CodeView type record bytes to append.
  /// \param Hash The hash value associated with \p Type.
  LLVM_ABI void addTypeRecord(ArrayRef<uint8_t> Type, uint32_t Hash);
  /// Append a contiguous buffer of CodeView type records and their hashes.
  ///
  /// \param Types Concatenated serialized type record bytes.
  /// \param Sizes Per-record byte sizes that partition \p Types.
  /// \param Hashes Per-record hash values corresponding to each size in
  ///     \p Sizes.
  LLVM_ABI void addTypeRecords(ArrayRef<uint8_t> Types,
                               ArrayRef<uint16_t> Sizes,
                               ArrayRef<uint32_t> Hashes);

  /// Finalize MSF stream layout for the TPI stream and its hash stream.
  ///
  /// \return Success, or an error if stream allocation fails.
  LLVM_ABI Error finalizeMsfLayout();

  /// Return the number of type records added to this builder.
  ///
  /// \return The number of type records added to this builder.
  uint32_t getRecordCount() const { return TypeRecordCount; }

  /// Commit the TPI stream and hash stream into \p Buffer.
  ///
  /// \param Layout The finalized MSF layout describing stream positions.
  /// \param Buffer Writable view of the MSF file into which data is written.
  /// \return Success, or an error if writing any stream fails.
  LLVM_ABI Error commit(const msf::MSFLayout &Layout,
                        WritableBinaryStreamRef Buffer);

  /// Return the number of bytes required to serialize the TPI stream.
  ///
  /// \return The number of bytes required to serialize the TPI stream.
  LLVM_ABI uint32_t calculateSerializedLength();

private:
  void updateTypeIndexOffsets(ArrayRef<uint16_t> Sizes);

  uint32_t calculateHashBufferSize() const;
  uint32_t calculateIndexOffsetSize() const;
  Error finalize();

  msf::MSFBuilder &Msf;
  BumpPtrAllocator &Allocator;

  uint32_t TypeRecordCount = 0;
  size_t TypeRecordBytes = 0;

  PdbRaw_TpiVer VerHeader = PdbRaw_TpiVer::PdbTpiV80;
  std::vector<ArrayRef<uint8_t>> TypeRecBuffers;
  std::vector<uint32_t> TypeHashes;
  std::vector<codeview::TypeIndexOffset> TypeIndexOffsets;
  uint32_t HashStreamIndex = kInvalidStreamIndex;
  std::unique_ptr<BinaryByteStream> HashValueStream;

  const TpiStreamHeader *Header;
  uint32_t Idx;
};
} // namespace pdb
}

#endif
