//===- TypeIndexDiscovery.h -------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_TYPEINDEXDISCOVERY_H
#define LLVM_DEBUGINFO_CODEVIEW_TYPEINDEXDISCOVERY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/DebugInfo/CodeView/CVRecord.h"
#include "llvm/Support/Compiler.h"

namespace llvm {
template <typename T> class SmallVectorImpl;
namespace codeview {
class TypeIndex;

/// Kind of TypeIndex reference embedded in a CodeView record.
enum class TiRefKind {
  TypeRef, ///< Reference into the type (TPI) stream.
  IndexRef ///< Reference into the item (IPI) stream.
};

/// Contiguous run of TypeIndex values within a serialized CodeView record.
struct TiReference {
  /// Whether the referenced indices are type or item IDs.
  TiRefKind Kind;
  /// Byte offset of the first TypeIndex within the record content.
  uint32_t Offset;
  /// Number of consecutive TypeIndex values in this run.
  uint32_t Count;
};

/// Discover TypeIndex references in raw type-record bytes.
///
/// \param RecordData Serialized type-record bytes to scan.
/// \param Refs Filled with runs of TypeIndex references found in the record.
LLVM_ABI void discoverTypeIndices(ArrayRef<uint8_t> RecordData,
                                  SmallVectorImpl<TiReference> &Refs);

/// Discover TypeIndex references in a typed CodeView type record.
///
/// \param Type Type record whose content is scanned for TypeIndex values.
/// \param Refs Filled with runs of TypeIndex references found in the record.
LLVM_ABI void discoverTypeIndices(const CVType &Type,
                                  SmallVectorImpl<TiReference> &Refs);

/// Collect every TypeIndex referenced by a typed CodeView type record.
///
/// \param Type Type record whose content is scanned for TypeIndex values.
/// \param Indices Filled with each TypeIndex found in the record, in order.
LLVM_ABI void discoverTypeIndices(const CVType &Type,
                                  SmallVectorImpl<TypeIndex> &Indices);

/// Collect every TypeIndex referenced by raw type-record bytes.
///
/// \param RecordData Serialized type-record bytes to scan.
/// \param Indices Filled with each TypeIndex found in the record, in order.
LLVM_ABI void discoverTypeIndices(ArrayRef<uint8_t> RecordData,
                                  SmallVectorImpl<TypeIndex> &Indices);

/// Discover type indices in symbol records.
///
/// \param Symbol Symbol record whose content is scanned for TypeIndex values.
/// \param Refs Filled with runs of TypeIndex references found in the record.
///
/// \returns False if this is an unknown record; true otherwise.
LLVM_ABI bool discoverTypeIndicesInSymbol(const CVSymbol &Symbol,
                                          SmallVectorImpl<TiReference> &Refs);

/// Discover TypeIndex references in raw symbol-record bytes.
///
/// \param RecordData Serialized symbol-record bytes to scan.
/// \param Refs Filled with runs of TypeIndex references found in the record.
///
/// \returns False if this is an unknown record; true otherwise.
LLVM_ABI bool discoverTypeIndicesInSymbol(ArrayRef<uint8_t> RecordData,
                                          SmallVectorImpl<TiReference> &Refs);

/// Collect every TypeIndex referenced by raw symbol-record bytes.
///
/// \param RecordData Serialized symbol-record bytes to scan.
/// \param Indices Filled with each TypeIndex found in the record, in order.
///
/// \returns False if this is an unknown record; true otherwise.
LLVM_ABI bool discoverTypeIndicesInSymbol(ArrayRef<uint8_t> RecordData,
                                          SmallVectorImpl<TypeIndex> &Indices);
}
}

#endif
