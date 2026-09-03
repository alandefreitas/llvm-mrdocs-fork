//===- BinaryStreamArray.h - Array backed by an arbitrary stream *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// Lightweight arrays that are backed by an arbitrary BinaryStream.  This file
/// provides two different array implementations.
///
///     VarStreamArray - Arrays of variable length records.  The user specifies
///       an Extractor type that can extract a record from a given offset and
///       return the number of bytes consumed by the record.
///
///     FixedStreamArray - Arrays of fixed length records.  This is similar in
///       spirit to ArrayRef<T>, but since it is backed by a BinaryStream, the
///       elements of the array need not be laid out in contiguous memory.
///

#ifndef LLVM_SUPPORT_BINARYSTREAMARRAY_H
#define LLVM_SUPPORT_BINARYSTREAMARRAY_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Alignment.h"
#include "llvm/Support/BinaryStreamRef.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <cstdint>

namespace llvm {

/// Trait specialized to extract one variable-length record from a stream.
///
/// VarStreamArrayExtractor is intended to be specialized to provide customized
/// extraction logic.  On input it receives a BinaryStreamRef pointing to the
/// beginning of the next record, but where the length of the record is not yet
/// known.  Upon completion, it should return an appropriate Error instance if
/// a record could not be extracted, or if one could be extracted it should
/// return success and set Len to the number of bytes this record occupied in
/// the underlying stream, and it should fill out the fields of the value type
/// Item appropriately to represent the current record.
///
/// You can specialize this template for your own custom value types to avoid
/// having to specify a second template argument to VarStreamArray (documented
/// below).
template <typename T> struct VarStreamArrayExtractor {
  // Method intentionally deleted.  You must provide an explicit specialization
  // with the following method implemented.

  /// Extract one record from \p Stream into \p Item.
  ///
  /// \param Stream Stream positioned at the start of the next record.
  /// \param Len Set to the number of bytes occupied by the extracted record.
  /// \param Item Set to the extracted record value.
  ///
  /// \returns An Error on failure, or success if a record was extracted.
  Error operator()(BinaryStreamRef Stream, uint32_t &Len,
                   T &Item) const = delete;
};

template <typename ValueType, typename Extractor> class VarStreamArrayIterator;

/// Array of variable-length records backed by a BinaryStream.
///
/// VarStreamArray represents an array of variable length records backed by a
/// stream.  This could be a contiguous sequence of bytes in memory, it could
/// be a file on disk, or it could be a PDB stream where bytes are stored as
/// discontiguous blocks in a file.  Usually it is desirable to treat arrays
/// as contiguous blocks of memory, but doing so with large PDB files, for
/// example, could mean allocating huge amounts of memory just to allow
/// re-ordering of stream data to be contiguous before iterating over it.  By
/// abstracting this out, we need not duplicate this memory, and we can
/// iterate over arrays in arbitrarily formatted streams.  Elements are parsed
/// lazily on iteration, so there is no upfront cost associated with building
/// or copying a VarStreamArray, no matter how large it may be.
///
/// You create a VarStreamArray by specifying a ValueType and an Extractor type.
/// If you do not specify an Extractor type, you are expected to specialize
/// VarStreamArrayExtractor<T> for your ValueType.
///
/// By default an Extractor is default constructed in the class, but in some
/// cases you might find it useful for an Extractor to maintain state across
/// extractions.  In this case you can provide your own Extractor through a
/// secondary constructor.  The following examples show various ways of
/// creating a VarStreamArray.
///
///       // Will use VarStreamArrayExtractor<MyType> as the extractor.
///       VarStreamArray<MyType> MyTypeArray;
///
///       // Will use a default-constructed MyExtractor as the extractor.
///       VarStreamArray<MyType, MyExtractor> MyTypeArray2;
///
///       // Will use the specific instance of MyExtractor provided.
///       // MyExtractor need not be default-constructible in this case.
///       MyExtractor E(SomeContext);
///       VarStreamArray<MyType, MyExtractor> MyTypeArray3(E);
///
template <typename ValueType,
          typename Extractor = VarStreamArrayExtractor<ValueType>>
class VarStreamArray {
  friend class VarStreamArrayIterator<ValueType, Extractor>;

public:
  /// Iterator over the records in this array.
  using Iterator = VarStreamArrayIterator<ValueType, Extractor>;

  /// Construct an empty VarStreamArray.
  VarStreamArray() = default;

  /// Construct a VarStreamArray that uses the given extractor.
  ///
  /// \param E Extractor used to parse records from the stream.
  explicit VarStreamArray(const Extractor &E) : E(E) {}

  /// Construct a VarStreamArray over the given stream.
  ///
  /// \param Stream Underlying stream containing the records.
  /// \param Skew Byte offset at which the first record begins.
  explicit VarStreamArray(BinaryStreamRef Stream, uint32_t Skew = 0)
      : Stream(Stream), Skew(Skew) {}

  /// Construct a VarStreamArray over a stream with a custom extractor.
  ///
  /// \param Stream Underlying stream containing the records.
  /// \param E Extractor used to parse records from the stream.
  /// \param Skew Byte offset at which the first record begins.
  VarStreamArray(BinaryStreamRef Stream, const Extractor &E, uint32_t Skew = 0)
      : Stream(Stream), E(E), Skew(Skew) {}

  /// Return an iterator to the first record in the array.
  ///
  /// \param HadError Optional out-parameter set to true if extraction fails.
  /// \returns An iterator to the first record in the array.
  Iterator begin(bool *HadError = nullptr) const {
    return Iterator(*this, E, Skew, nullptr);
  }

  /// Return true if this array has a valid underlying stream.
  ///
  /// \returns true if this array has a valid underlying stream.
  bool valid() const { return Stream.valid(); }

  /// Return true if \p Offset refers to a valid record boundary.
  ///
  /// \param Offset Byte offset into the underlying stream.
  /// \returns true if \p Offset refers to a valid record boundary.
  bool isOffsetValid(uint32_t Offset) const { return at(Offset) != end(); }

  /// Return the byte skew applied to the start of the array.
  ///
  /// \returns The byte skew applied to the start of the array.
  uint32_t skew() const { return Skew; }

  /// Return an iterator past the last record in the array.
  ///
  /// \returns An iterator past the last record in the array.
  Iterator end() const { return Iterator(E); }

  /// Return true if the underlying stream has no bytes.
  ///
  /// \returns true if the underlying stream has no bytes.
  bool empty() const { return Stream.getLength() == 0; }

  /// Return a view of the records between the given byte offsets.
  ///
  /// \param Begin Inclusive byte offset of the first record (at or after skew).
  /// \param End Exclusive byte offset past the last byte of the substream.
  /// \returns A VarStreamArray covering the requested byte range.
  VarStreamArray<ValueType, Extractor> substream(uint32_t Begin,
                                                 uint32_t End) const {
    assert(Begin >= Skew);
    // We should never cut off the beginning of the stream since it might be
    // skewed, meaning the initial bytes are important.
    BinaryStreamRef NewStream = Stream.slice(0, End);
    return {NewStream, E, Begin};
  }

  /// Return an iterator to the record at the given stream offset.
  ///
  /// This is considered unsafe since the behavior is undefined if \p Offset
  /// does not refer to the beginning of a valid record.
  ///
  /// \param Offset Byte offset into the array's underlying stream.
  /// \returns An iterator positioned at \p Offset.
  Iterator at(uint32_t Offset) const {
    return Iterator(*this, E, Offset, nullptr);
  }

  /// Return a const reference to the record extractor.
  ///
  /// \returns A const reference to the record extractor.
  const Extractor &getExtractor() const { return E; }

  /// Return a mutable reference to the record extractor.
  ///
  /// \returns A mutable reference to the record extractor.
  Extractor &getExtractor() { return E; }

  /// Return the BinaryStreamRef backing this array.
  ///
  /// \returns The BinaryStreamRef backing this array.
  BinaryStreamRef getUnderlyingStream() const { return Stream; }

  /// Replace the underlying stream and optional skew.
  ///
  /// \param NewStream Stream that becomes the new backing store.
  /// \param NewSkew Byte offset at which the first record begins.
  void setUnderlyingStream(BinaryStreamRef NewStream, uint32_t NewSkew = 0) {
    Stream = NewStream;
    Skew = NewSkew;
  }

  /// Advance past the first record by increasing the skew.
  void drop_front() { Skew += begin()->length(); }

private:
  BinaryStreamRef Stream;
  Extractor E;
  uint32_t Skew = 0;
};

/// Forward iterator over the records of a VarStreamArray.
template <typename ValueType, typename Extractor>
class VarStreamArrayIterator
    : public iterator_facade_base<VarStreamArrayIterator<ValueType, Extractor>,
                                  std::forward_iterator_tag, const ValueType> {
  using IterType = VarStreamArrayIterator<ValueType, Extractor>;
  using ArrayType = VarStreamArray<ValueType, Extractor>;

public:
  /// Construct an iterator positioned at a given offset in the array.
  ///
  /// \param Array Array being iterated.
  /// \param E Extractor used to parse the current record.
  /// \param Offset Byte offset of the current record in the stream.
  /// \param HadError Optional out-parameter set to true if extraction fails.
  VarStreamArrayIterator(const ArrayType &Array, const Extractor &E,
                         uint32_t Offset, bool *HadError)
      : IterRef(Array.Stream.drop_front(Offset)), Extract(E),
        Array(&Array), AbsOffset(Offset), HadError(HadError) {
    if (IterRef.getLength() == 0)
      moveToEnd();
    else {
      auto EC = Extract(IterRef, ThisLen, ThisValue);
      if (EC) {
        consumeError(std::move(EC));
        markError();
      }
    }
  }

  /// Construct a default (empty) end iterator.
  VarStreamArrayIterator() = default;

  /// Construct an end iterator that holds a copy of the extractor.
  ///
  /// \param E Extractor stored in the end iterator.
  explicit VarStreamArrayIterator(const Extractor &E) : Extract(E) {}

  /// Destroy this iterator.
  ~VarStreamArrayIterator() = default;

  /// Return true if this iterator equals \p R.
  ///
  /// \param R Iterator to compare against.
  /// \returns true if both iterators are at the same position or both are end.
  bool operator==(const IterType &R) const {
    if (Array && R.Array) {
      // Both have a valid array, make sure they're same.
      assert(Array == R.Array);
      return IterRef == R.IterRef;
    }

    // Both iterators are at the end.
    if (!Array && !R.Array)
      return true;

    // One is not at the end and one is.
    return false;
  }

  /// Return a const reference to the current record.
  ///
  /// \returns A const reference to the current record.
  const ValueType &operator*() const {
    assert(Array && !HasError);
    return ThisValue;
  }

  /// Advance this iterator by \p N records.
  ///
  /// \param N Number of records to skip.
  /// \returns A reference to this iterator.
  IterType &operator+=(unsigned N) {
    for (unsigned I = 0; I < N; ++I) {
      // We are done with the current record, discard it so that we are
      // positioned at the next record.
      AbsOffset += ThisLen;
      IterRef = IterRef.drop_front(ThisLen);
      if (IterRef.getLength() == 0) {
        // There is nothing after the current record, we must make this an end
        // iterator.
        moveToEnd();
      } else {
        // There is some data after the current record.
        auto EC = Extract(IterRef, ThisLen, ThisValue);
        if (EC) {
          consumeError(std::move(EC));
          markError();
        } else if (ThisLen == 0) {
          // An empty record? Make this an end iterator.
          moveToEnd();
        }
      }
    }
    return *this;
  }

  /// Return the absolute byte offset of the current record.
  ///
  /// \returns The absolute byte offset of the current record.
  uint32_t offset() const { return AbsOffset; }

  /// Return the length in bytes of the current record.
  ///
  /// \returns The length in bytes of the current record.
  uint32_t getRecordLength() const { return ThisLen; }

private:
  void moveToEnd() {
    Array = nullptr;
    ThisLen = 0;
  }
  void markError() {
    moveToEnd();
    HasError = true;
    if (HadError != nullptr)
      *HadError = true;
  }

  ValueType ThisValue;
  BinaryStreamRef IterRef;
  Extractor Extract;
  const ArrayType *Array{nullptr};
  uint32_t ThisLen{0};
  uint32_t AbsOffset{0};
  bool HasError{false};
  bool *HadError{nullptr};
};

template <typename T> class FixedStreamArrayIterator;

/// Array of fixed-size records backed by a BinaryStream.
///
/// FixedStreamArray is similar to VarStreamArray, except with each record
/// having a fixed-length.  As with VarStreamArray, there is no upfront
/// cost associated with building or copying a FixedStreamArray, as the
/// memory for each element is not read from the backing stream until that
/// element is iterated.
template <typename T> class FixedStreamArray {
  friend class FixedStreamArrayIterator<T>;

public:
  /// Iterator over the elements in this array.
  using Iterator = FixedStreamArrayIterator<T>;

  /// Construct an empty FixedStreamArray.
  FixedStreamArray() = default;

  /// Construct a FixedStreamArray over the given stream.
  ///
  /// \param Stream Underlying stream whose length must be a multiple of
  ///        sizeof(T).
  explicit FixedStreamArray(BinaryStreamRef Stream) : Stream(Stream) {
    assert(Stream.getLength() % sizeof(T) == 0);
  }

  /// Return true if this array refers to the same stream as \p Other.
  ///
  /// \param Other Array to compare against.
  /// \returns true if both arrays refer to the same stream.
  bool operator==(const FixedStreamArray<T> &Other) const {
    return Stream == Other.Stream;
  }

  /// Return true if this array does not refer to the same stream as \p Other.
  ///
  /// \param Other Array to compare against.
  /// \returns true if the arrays refer to different streams.
  bool operator!=(const FixedStreamArray<T> &Other) const {
    return !(*this == Other);
  }

  /// Copy-construct a FixedStreamArray.
  ///
  /// \param Other Array to copy.
  FixedStreamArray(const FixedStreamArray &Other) = default;

  /// Copy-assign a FixedStreamArray.
  ///
  /// \param Other Array to copy from.
  /// \returns A reference to this array.
  FixedStreamArray &operator=(const FixedStreamArray &Other) = default;

  /// Return a const reference to the element at \p Index.
  ///
  /// \param Index Zero-based index of the element to access.
  /// \returns A const reference to the element at \p Index.
  const T &operator[](uint32_t Index) const {
    assert(Index < size());
    uint32_t Off = Index * sizeof(T);
    ArrayRef<uint8_t> Data;
    if (auto EC = Stream.readBytes(Off, sizeof(T), Data)) {
      assert(false && "Unexpected failure reading from stream");
      // This should never happen since we asserted that the stream length was
      // an exact multiple of the element size.
      consumeError(std::move(EC));
    }
    assert(isAddrAligned(Align::Of<T>(), Data.data()));
    return *reinterpret_cast<const T *>(Data.data());
  }

  /// Return the number of elements in the array.
  ///
  /// \returns The number of elements in the array.
  uint32_t size() const { return Stream.getLength() / sizeof(T); }

  /// Return true if the array contains no elements.
  ///
  /// \returns true if the array contains no elements.
  bool empty() const { return size() == 0; }

  /// Return an iterator to the first element.
  ///
  /// \returns An iterator to the first element.
  FixedStreamArrayIterator<T> begin() const {
    return FixedStreamArrayIterator<T>(*this, 0);
  }

  /// Return an iterator past the last element.
  ///
  /// \returns An iterator past the last element.
  FixedStreamArrayIterator<T> end() const {
    return FixedStreamArrayIterator<T>(*this, size());
  }

  /// Return a const reference to the first element.
  ///
  /// \returns A const reference to the first element.
  const T &front() const { return *begin(); }

  /// Return a const reference to the last element.
  ///
  /// \returns A const reference to the last element.
  const T &back() const {
    FixedStreamArrayIterator<T> I = end();
    return *(--I);
  }

  /// Return the BinaryStreamRef backing this array.
  ///
  /// \returns The BinaryStreamRef backing this array.
  BinaryStreamRef getUnderlyingStream() const { return Stream; }

private:
  BinaryStreamRef Stream;
};

/// Random-access iterator over the elements of a FixedStreamArray.
template <typename T>
class FixedStreamArrayIterator
    : public iterator_facade_base<FixedStreamArrayIterator<T>,
                                  std::random_access_iterator_tag, const T> {

public:
  /// Construct an iterator at the given index in \p Array.
  ///
  /// \param Array Array being iterated.
  /// \param Index Zero-based element index.
  FixedStreamArrayIterator(const FixedStreamArray<T> &Array, uint32_t Index)
      : Array(Array), Index(Index) {}

  /// Copy-construct an iterator.
  ///
  /// \param Other Iterator to copy.
  FixedStreamArrayIterator(const FixedStreamArrayIterator<T> &Other)
      : Array(Other.Array), Index(Other.Index) {}

  /// Copy-assign from another iterator.
  ///
  /// \param Other Iterator to copy from.
  /// \returns A reference to this iterator.
  FixedStreamArrayIterator<T> &
  operator=(const FixedStreamArrayIterator<T> &Other) {
    Array = Other.Array;
    Index = Other.Index;
    return *this;
  }

  /// Return a const reference to the current element.
  ///
  /// \returns A const reference to the element at the current index.
  const T &operator*() const { return Array[Index]; }

  /// Return a const reference to the current element.
  ///
  /// \returns A const reference to the element at the current index.
  const T &operator*() { return Array[Index]; }

  /// Return true if this iterator equals \p R.
  ///
  /// \param R Iterator to compare against.
  /// \returns true if both iterators refer to the same array element.
  bool operator==(const FixedStreamArrayIterator<T> &R) const {
    assert(Array == R.Array);
    return (Index == R.Index) && (Array == R.Array);
  }

  /// Advance this iterator by \p N elements.
  ///
  /// \param N Number of elements to move forward.
  /// \returns A reference to this iterator.
  FixedStreamArrayIterator<T> &operator+=(std::ptrdiff_t N) {
    Index += N;
    return *this;
  }

  /// Move this iterator backward by \p N elements.
  ///
  /// \param N Number of elements to move backward.
  /// \returns A reference to this iterator.
  FixedStreamArrayIterator<T> &operator-=(std::ptrdiff_t N) {
    assert(std::ptrdiff_t(Index) >= N);
    Index -= N;
    return *this;
  }

  /// Return the distance from \p R to this iterator.
  ///
  /// \param R Iterator to subtract.
  /// \returns The number of elements between \p R and this iterator.
  std::ptrdiff_t operator-(const FixedStreamArrayIterator<T> &R) const {
    assert(Array == R.Array);
    assert(Index >= R.Index);
    return Index - R.Index;
  }

  /// Return true if this iterator precedes \p RHS.
  ///
  /// \param RHS Iterator to compare against.
  /// \returns true if this iterator's index is less than \p RHS's.
  bool operator<(const FixedStreamArrayIterator<T> &RHS) const {
    assert(Array == RHS.Array);
    return Index < RHS.Index;
  }

private:
  FixedStreamArray<T> Array;
  uint32_t Index;
};

} // namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMARRAY_H
