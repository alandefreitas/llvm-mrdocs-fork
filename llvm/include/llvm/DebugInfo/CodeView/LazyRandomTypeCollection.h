//===- LazyRandomTypeCollection.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_CODEVIEW_LAZYRANDOMTYPECOLLECTION_H
#define LLVM_DEBUGINFO_CODEVIEW_LAZYRANDOMTYPECOLLECTION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/CodeView/TypeCollection.h"
#include "llvm/DebugInfo/CodeView/TypeIndex.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/BinaryStreamArray.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/StringSaver.h"
#include <cstdint>
#include <vector>

namespace llvm {
namespace codeview {

/// Provides amortized O(1) random access to a CodeView type stream.
///
/// Normally to access a type from a type stream, you must know its byte
/// offset into the type stream, because type records are variable-lengthed.
/// However, this is not the way we prefer to access them.  For example, given
/// a symbol record one of the fields may be the TypeIndex of the symbol's
/// type record.  Or given a type record such as an array type, there might
/// be a TypeIndex for the element type.  Sequential access is perfect when
/// we're just dumping every entry, but it's very poor for real world usage.
///
/// Type streams in PDBs contain an additional field which is a list of pairs
/// containing indices and their corresponding offsets, roughly every ~8KB of
/// record data.  This general idea need not be confined to PDBs though.  By
/// supplying such an array, the producer of a type stream can allow the
/// consumer much better access time, because the consumer can find the nearest
/// index in this array, and do a linear scan forward only from there.
///
/// LazyRandomTypeCollection implements this algorithm, but additionally goes
/// one step further by caching offsets of every record that has been visited at
/// least once.  This way, even repeated visits of the same record will never
/// require more than one linear scan.  For a type stream of N elements divided
/// into M chunks of roughly equal size, this yields a worst case lookup time
/// of O(N/M) and an amortized time of O(1).
class LLVM_ABI LazyRandomTypeCollection : public TypeCollection {
  using PartialOffsetArray = FixedStreamArray<TypeIndexOffset>;

  struct CacheEntry {
    CVType Type;
    uint32_t Offset;
    StringRef Name;
  };

public:
  /// Construct an empty collection sized for about \p RecordCountHint records.
  ///
  /// \param RecordCountHint Initial capacity hint for the type-record cache.
  explicit LazyRandomTypeCollection(uint32_t RecordCountHint);
  /// Construct a collection over the type stream bytes in \p Data.
  ///
  /// \param Data Serialized CodeView type stream bytes.
  /// \param RecordCountHint Initial capacity hint for the type-record cache.
  LazyRandomTypeCollection(StringRef Data, uint32_t RecordCountHint);
  /// Construct a collection over the type stream bytes in \p Data.
  ///
  /// \param Data Serialized CodeView type stream bytes.
  /// \param RecordCountHint Initial capacity hint for the type-record cache.
  LazyRandomTypeCollection(ArrayRef<uint8_t> Data, uint32_t RecordCountHint);
  /// Construct a collection over \p Types using \p PartialOffsets for lookup.
  ///
  /// \param Types Array of CodeView type records to index.
  /// \param RecordCountHint Initial capacity hint for the type-record cache.
  /// \param PartialOffsets Sparse type-index to byte-offset pairs for seeking.
  LazyRandomTypeCollection(const CVTypeArray &Types, uint32_t RecordCountHint,
                           PartialOffsetArray PartialOffsets);
  /// Construct a collection over \p Types without partial offset information.
  ///
  /// \param Types Array of CodeView type records to index.
  /// \param RecordCountHint Initial capacity hint for the type-record cache.
  LazyRandomTypeCollection(const CVTypeArray &Types, uint32_t RecordCountHint);

  /// Replace the underlying type stream with the bytes in \p Data.
  ///
  /// \param Data Serialized CodeView type stream bytes.
  /// \param RecordCountHint Initial capacity hint for the type-record cache.
  void reset(ArrayRef<uint8_t> Data, uint32_t RecordCountHint);
  /// Replace the underlying type stream with the bytes in \p Data.
  ///
  /// \param Data Serialized CodeView type stream bytes.
  /// \param RecordCountHint Initial capacity hint for the type-record cache.
  void reset(StringRef Data, uint32_t RecordCountHint);
  /// Replace the underlying type stream by reading remaining bytes from \p Reader.
  ///
  /// \param Reader Binary stream positioned at the start of a type stream.
  /// \param RecordCountHint Initial capacity hint for the type-record cache.
  void reset(BinaryStreamReader &Reader, uint32_t RecordCountHint);

  /// Return the byte offset of the type record at \p Index in the type stream.
  ///
  /// \param Index Type index of the record whose stream offset is requested.
  /// \returns The byte offset of the type record within the type stream.
  uint32_t getOffsetOfType(TypeIndex Index);

  /// Try to return the type record at \p Index, or \c std::nullopt on failure.
  ///
  /// \param Index Type index of the record to retrieve.
  /// \returns The type record at \p Index, or \c std::nullopt if unavailable.
  std::optional<CVType> tryGetType(TypeIndex Index);
  /// Return the type record at \p Index, or an Error if the index is invalid.
  ///
  /// \param Index Type index of the record to retrieve.
  /// \returns The type record at \p Index, or an Error if the index is invalid.
  llvm::Expected<CVType> getTypeOrError(TypeIndex Index);
  /// Return the CodeView type record at \p Index.
  ///
  /// \param Index Type index of the record to retrieve.
  /// \returns The CodeView type record at \p Index.
  CVType getType(TypeIndex Index) override;

  /// Return the name of the type at \p Index.
  ///
  /// \param Index Type index whose name is requested.
  /// \returns The name of the type at \p Index.
  StringRef getTypeName(TypeIndex Index) override;
  /// Return true if \p Index refers to a type record already resolved in the cache.
  ///
  /// \param Index Type index to test for membership.
  /// \returns True if \p Index refers to a type record already resolved in the cache.
  bool contains(TypeIndex Index) override;
  /// Return the number of type records discovered so far.
  ///
  /// \returns The number of type records discovered so far.
  uint32_t size() override;
  /// Return the number of type records the cache can hold without growing.
  ///
  /// \returns The number of type records the cache can hold without growing.
  uint32_t capacity() override;
  /// Return the first non-simple type index, or \c std::nullopt if none exist.
  ///
  /// \returns The first non-simple type index, or \c std::nullopt if none exist.
  std::optional<TypeIndex> getFirst() override;
  /// Return the type index after \p Prev, or \c std::nullopt at the end.
  ///
  /// \param Prev The preceding type index in iteration order.
  /// \returns The type index after \p Prev, or \c std::nullopt at the end.
  std::optional<TypeIndex> getNext(TypeIndex Prev) override;
  /// Replace the record at \p Index with \p Data.
  ///
  /// \param Index Type index of the record to replace; must already exist.
  /// \param Data New record bytes to store at \p Index.
  /// \param Stabilize If true, copy \p Data into stable storage.
  /// \returns Whether the replacement succeeded.
  bool replaceType(TypeIndex &Index, CVType Data, bool Stabilize) override;

private:
  Error ensureTypeExists(TypeIndex Index);
  void ensureCapacityFor(TypeIndex Index);

  Error visitRangeForType(TypeIndex TI);
  Error fullScanForType(TypeIndex TI);
  void visitRange(TypeIndex Begin, uint32_t BeginOffset, TypeIndex End);

  /// Number of actual records.
  uint32_t Count = 0;

  /// The largest type index which we've visited.
  TypeIndex LargestTypeIndex = TypeIndex::None();

  BumpPtrAllocator Allocator;
  StringSaver NameStorage;

  /// The type array to allow random access visitation of.
  CVTypeArray Types;

  std::vector<CacheEntry> Records;

  /// An array of index offsets for the given type stream, allowing log(N)
  /// lookups of a type record by index.  Similar to KnownOffsets but only
  /// contains offsets for some type indices, some of which may not have
  /// ever been visited.
  PartialOffsetArray PartialOffsets;
};

} // end namespace codeview
} // end namespace llvm

#endif // LLVM_DEBUGINFO_CODEVIEW_LAZYRANDOMTYPECOLLECTION_H
