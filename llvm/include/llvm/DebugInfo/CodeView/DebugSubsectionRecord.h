//===- DebugSubsectionRecord.h ----------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONRECORD_H
#define LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONRECORD_H

#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>
#include <memory>

namespace llvm {

class BinaryStreamWriter;

namespace codeview {

class DebugSubsection;

/// On-disk header for a CodeView debug subsection.
///
/// Corresponds to the `CV_DebugSSubsectionHeader_t` structure.
struct DebugSubsectionHeader {
  /// Subsection kind; a \c codeview::DebugSubsectionKind value.
  support::ulittle32_t Kind;
  /// Number of bytes occupied by this record's payload.
  support::ulittle32_t Length;
};

/// A parsed CodeView debug subsection record (kind plus payload bytes).
class DebugSubsectionRecord {
public:
  /// Construct an empty subsection record with no kind or data.
  LLVM_ABI DebugSubsectionRecord();
  /// Construct a subsection record with the given kind and payload.
  ///
  /// \param Kind Subsection kind for this record.
  /// \param Data Payload bytes following the subsection header.
  LLVM_ABI DebugSubsectionRecord(DebugSubsectionKind Kind,
                                 BinaryStreamRef Data);

  /// Parse a subsection record from the start of \p Stream into \p Info.
  ///
  /// \param Stream Stream positioned at a subsection header.
  /// \param Info Record filled with the parsed kind and payload.
  ///
  /// \returns Success, or an Error if the stream is too short or corrupt.
  LLVM_ABI static Error initialize(BinaryStreamRef Stream,
                                   DebugSubsectionRecord &Info);

  /// Return the total serialized size of this record, including alignment.
  ///
  /// \returns The total serialized size of this record, including alignment.
  LLVM_ABI uint32_t getRecordLength() const;
  /// Return the subsection kind of this record.
  ///
  /// \returns The subsection kind of this record.
  LLVM_ABI DebugSubsectionKind kind() const;
  /// Return the payload bytes of this record, excluding the header.
  ///
  /// \returns The payload bytes of this record, excluding the header.
  LLVM_ABI BinaryStreamRef getRecordData() const;

private:
  DebugSubsectionKind Kind = DebugSubsectionKind::None;
  BinaryStreamRef Data;
};

/// Builds a serialized CodeView debug subsection record for writing.
class DebugSubsectionRecordBuilder {
public:
  /// Construct a builder that serializes the given subsection.
  ///
  /// \param Subsection Subsection whose contents will be written.
  LLVM_ABI
  DebugSubsectionRecordBuilder(std::shared_ptr<DebugSubsection> Subsection);

  /// Construct a builder that copies an existing subsection record.
  ///
  /// Use this to copy existing subsections directly from source to destination.
  /// For example, line table subsections in an object file only need to be
  /// relocated before being copied into the PDB.
  ///
  /// \param Contents Existing subsection record to copy.
  LLVM_ABI DebugSubsectionRecordBuilder(const DebugSubsectionRecord &Contents);

  /// Return the number of bytes this subsection will occupy when serialized.
  ///
  /// \returns The number of bytes this subsection will occupy when serialized.
  LLVM_ABI uint32_t calculateSerializedLength() const;
  /// Serialize this subsection into \p Writer for the given container.
  ///
  /// \param Writer Destination stream writer for the subsection bytes.
  /// \param Container Whether the destination is an object file or a PDB.
  ///
  /// \returns Success, or an Error if writing fails.
  LLVM_ABI Error commit(BinaryStreamWriter &Writer,
                        CodeViewContainer Container) const;

private:
  /// The subsection to build. Will be null if Contents is non-empty.
  std::shared_ptr<DebugSubsection> Subsection;

  /// The bytes of the subsection. Only non-empty if Subsection is null.
  /// FIXME: Reduce the size of this.
  DebugSubsectionRecord Contents;
};

} // end namespace codeview

/// Extracts \c DebugSubsectionRecord values from a variable-length stream array.
template <> struct VarStreamArrayExtractor<codeview::DebugSubsectionRecord> {
  /// Extract one debug subsection record from \p Stream into \p Info.
  ///
  /// \param Stream Stream positioned at the start of the next subsection.
  /// \param Length Set to the aligned byte length occupied by the record.
  /// \param Info Set to the extracted subsection record.
  ///
  /// \returns An Error on failure, or success if a record was extracted.
  Error operator()(BinaryStreamRef Stream, uint32_t &Length,
                   codeview::DebugSubsectionRecord &Info) {
    // FIXME: We need to pass the container type through to this function.  In
    // practice this isn't super important since the subsection header describes
    // its length and we can just skip it.  It's more important when writing.
    if (auto EC = codeview::DebugSubsectionRecord::initialize(Stream, Info))
      return EC;
    Length = alignTo(Info.getRecordLength(), 4);
    return Error::success();
  }
};

namespace codeview {

/// Array of variable-length \c DebugSubsectionRecord values in a binary stream.
using DebugSubsectionArray = VarStreamArray<DebugSubsectionRecord>;

} // end namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_DEBUGSUBSECTIONRECORD_H
