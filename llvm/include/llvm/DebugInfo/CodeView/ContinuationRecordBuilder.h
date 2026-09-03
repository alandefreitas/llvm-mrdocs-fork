//===- ContinuationRecordBuilder.h ------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_CONTINUATIONRECORDBUILDER_H
#define LLVM_DEBUGINFO_CODEVIEW_CONTINUATIONRECORDBUILDER_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/DebugInfo/CodeView/TypeRecordMapping.h"
#include "llvm/Support/BinaryByteStream.h"
#include "llvm/Support/BinaryStreamWriter.h"
#include "llvm/Support/Compiler.h"
#include <cstdint>
#include <vector>

namespace llvm {
namespace codeview {
class TypeIndex;

/// Kind of large CodeView record that may need LF_INDEX continuation segments.
enum class ContinuationRecordKind {
  FieldList,          ///< Class/struct/union field list (LF_FIELDLIST).
  MethodOverloadList, ///< Overloaded method list (LF_METHODLIST).
};

/// Serializes field or method lists that may span multiple continuation records.
class ContinuationRecordBuilder {
  SmallVector<uint32_t, 4> SegmentOffsets;
  std::optional<ContinuationRecordKind> Kind;
  AppendingBinaryByteStream Buffer;
  BinaryStreamWriter SegmentWriter;
  TypeRecordMapping Mapping;
  ArrayRef<uint8_t> InjectedSegmentBytes;

  uint32_t getCurrentSegmentLength() const;

  void insertSegmentEnd(uint32_t Offset);
  CVType createSegmentRecord(uint32_t OffBegin, uint32_t OffEnd,
                             std::optional<TypeIndex> RefersTo);

public:
  /// Construct an empty continuation record builder.
  LLVM_ABI ContinuationRecordBuilder();
  /// Destroy the continuation record builder.
  LLVM_ABI ~ContinuationRecordBuilder();

  /// Begin serializing a new field or method-overload list of \p RecordKind.
  ///
  /// \param RecordKind Whether to emit an LF_FIELDLIST or LF_METHODLIST.
  LLVM_ABI void begin(ContinuationRecordKind RecordKind);

  /// Serialize member \p Record into the current list, splitting if needed.
  ///
  /// This template is explicitly instantiated in the implementation file for
  /// all supported types. The method itself is ugly, so inlining it into the
  /// header file clutters an otherwise straightforward interface.
  ///
  /// \param Record Member type record to append to the current segment.
  template <typename RecordType> void writeMemberType(RecordType &Record);

  /// Finish the list and return its continuation segments for type index \p Index.
  ///
  /// \param Index Type index that will be assigned to the first segment; later
  ///        segments use consecutive indices.
  /// \returns Serialized CVType segments in reverse index order (last fragment
  ///          first), ready to insert into a type table.
  LLVM_ABI std::vector<CVType> end(TypeIndex Index);
};
} // namespace codeview
} // namespace llvm

#endif
