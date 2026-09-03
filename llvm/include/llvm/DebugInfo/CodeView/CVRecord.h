//===- CVRecord.h -----------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CVRECORD_H
#define LLVM_DEBUGINFO_CODEVIEW_CVRECORD_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CodeView.h"
#include "llvm/DebugInfo/CodeView/CodeViewError.h"
#include "llvm/DebugInfo/CodeView/RecordSerialization.h"
#include "llvm/Support/BinaryStreamReader.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/Error.h"
#include <cstdint>

namespace llvm {

namespace codeview {

/// Fat pointer (base + size) to a CodeView symbol or type record.
///
/// Carrying the size separately instead of trusting the size stored in the
/// record prefix provides some extra safety and flexibility.
template <typename Kind> class CVRecord {
public:
  /// Construct an empty, invalid record.
  CVRecord() = default;

  /// Construct a record that refers to the given byte range.
  ///
  /// \param Data Bytes of a complete CodeView record including its prefix.
  CVRecord(ArrayRef<uint8_t> Data) : RecordData(Data) {}

  /// Construct a record from a prefix pointer and an explicit size.
  ///
  /// \param P Pointer to the record prefix at the start of the record.
  /// \param Size Total number of bytes in the record, including the prefix.
  CVRecord(const RecordPrefix *P, size_t Size)
      : RecordData(reinterpret_cast<const uint8_t *>(P), Size) {}

  /// Return true if this record has a non-zero kind.
  ///
  /// \returns True if the record kind is non-zero.
  bool valid() const { return kind() != Kind(0); }

  /// Return the total size of the record in bytes.
  ///
  /// \returns The number of bytes in the record, including the prefix.
  uint32_t length() const { return RecordData.size(); }

  /// Return the record kind from the prefix, or zero if the data is too short.
  ///
  /// \returns The kind stored in the record prefix, or zero if too short.
  Kind kind() const {
    if (RecordData.size() < sizeof(RecordPrefix))
      return Kind(0);
    return static_cast<Kind>(static_cast<uint16_t>(
        reinterpret_cast<const RecordPrefix *>(RecordData.data())->RecordKind));
  }

  /// Return the full record bytes, including the prefix.
  ///
  /// \returns The complete record byte range, including the prefix.
  ArrayRef<uint8_t> data() const { return RecordData; }

  /// Return the full record bytes as a string reference.
  ///
  /// \returns The full record bytes viewed as a \c StringRef.
  StringRef str_data() const {
    return StringRef(reinterpret_cast<const char *>(RecordData.data()),
                     RecordData.size());
  }

  /// Return the record payload after the prefix.
  ///
  /// \returns The record bytes following the prefix.
  ArrayRef<uint8_t> content() const {
    return RecordData.drop_front(sizeof(RecordPrefix));
  }

  /// Bytes of the complete record, including the prefix.
  ArrayRef<uint8_t> RecordData;
};

/// CodeView type record view.
using CVType = CVRecord<TypeLeafKind>;
/// CodeView symbol record view.
using CVSymbol = CVRecord<SymbolKind>;

/// Invoke \p F for each contiguous CodeView record in \p StreamBuffer.
///
/// \param StreamBuffer Contiguous byte buffer of serialized CodeView records.
/// \param F Callable invoked with each parsed \c Record; may return an Error.
///
/// \returns The first error from \p F, a corrupt-record error if a prefix or
/// length is invalid, or success when the buffer is fully consumed.
template <typename Record, typename Func>
Error forEachCodeViewRecord(ArrayRef<uint8_t> StreamBuffer, Func F) {
  while (!StreamBuffer.empty()) {
    if (StreamBuffer.size() < sizeof(RecordPrefix))
      return make_error<CodeViewError>(cv_error_code::corrupt_record);

    const RecordPrefix *Prefix =
        reinterpret_cast<const RecordPrefix *>(StreamBuffer.data());

    size_t RealLen = Prefix->RecordLen + 2;
    if (StreamBuffer.size() < RealLen)
      return make_error<CodeViewError>(cv_error_code::corrupt_record);

    ArrayRef<uint8_t> Data = StreamBuffer.take_front(RealLen);
    StreamBuffer = StreamBuffer.drop_front(RealLen);

    Record R(Data);
    if (auto EC = F(R))
      return EC;
  }
  return Error::success();
}

  /// Read a complete record from a stream at a random offset.
///
/// \param Stream Binary stream containing CodeView records.
/// \param Offset Byte offset of the record prefix within \p Stream.
///
/// \returns The parsed record, or an error if the prefix or length is invalid.
template <typename Kind>
inline Expected<CVRecord<Kind>> readCVRecordFromStream(BinaryStreamRef Stream,
                                                       uint32_t Offset) {
  const RecordPrefix *Prefix = nullptr;
  BinaryStreamReader Reader(Stream);
  Reader.setOffset(Offset);

  if (auto EC = Reader.readObject(Prefix))
    return std::move(EC);
  if (Prefix->RecordLen < 2)
    return make_error<CodeViewError>(cv_error_code::corrupt_record);

  Reader.setOffset(Offset);
  ArrayRef<uint8_t> RawData;
  if (auto EC = Reader.readBytes(RawData, Prefix->RecordLen + sizeof(uint16_t)))
    return std::move(EC);
  return codeview::CVRecord<Kind>(RawData);
}

} // end namespace codeview

/// Extractor that parses one CodeView record from a variable-length stream.
template <typename Kind>
struct VarStreamArrayExtractor<codeview::CVRecord<Kind>> {
  /// Extract one CodeView record from \p Stream into \p Item.
  ///
  /// \param Stream Stream positioned at the start of the next record.
  /// \param Len Set to the number of bytes occupied by the extracted record.
  /// \param Item Set to the extracted CodeView record.
  ///
  /// \returns An Error on failure, or success if a record was extracted.
  Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                   codeview::CVRecord<Kind> &Item) {
    auto ExpectedRec = codeview::readCVRecordFromStream<Kind>(Stream, 0);
    if (!ExpectedRec)
      return ExpectedRec.takeError();
    Item = *ExpectedRec;
    Len = ExpectedRec->length();
    return Error::success();
  }
};

namespace codeview {
/// Variable-length array of CodeView symbol records.
using CVSymbolArray = VarStreamArray<CVSymbol>;
/// Variable-length array of CodeView type records.
using CVTypeArray = VarStreamArray<CVType>;
/// Iterator range over a \c CVTypeArray.
using CVTypeRange = iterator_range<CVTypeArray::Iterator>;
} // namespace codeview

} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_CVRECORD_H
