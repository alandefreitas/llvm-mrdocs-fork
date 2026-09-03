//===- BinaryStreamRef.h - A copyable reference to a stream -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_BINARYSTREAMREF_H
#define LLVM_SUPPORT_BINARYSTREAMREF_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/BinaryStream.h"
#include "llvm/Support/BinaryStreamError.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cstdint>
#include <memory>
#include <optional>

namespace llvm {

/// Common stuff for mutable and immutable StreamRefs.
template <class RefType, class StreamType> class BinaryStreamRefBase {
protected:
  /// Construct an empty stream reference.
  BinaryStreamRefBase() = default;

  /// Construct a reference that borrows an existing stream.
  ///
  /// \param BorrowedImpl Stream to borrow; length tracks the stream unless
  ///        the stream has the BSF_Append flag.
  explicit BinaryStreamRefBase(StreamType &BorrowedImpl)
      : BorrowedImpl(&BorrowedImpl), ViewOffset(0) {
    if (!(BorrowedImpl.getFlags() & BSF_Append))
      Length = BorrowedImpl.getLength();
  }

  /// Construct a reference that shares ownership of a stream.
  ///
  /// \param SharedImpl Shared pointer to the underlying stream.
  /// \param Offset Byte offset into the stream where the view begins.
  /// \param Length Optional length of the view; if unset, the reference
  ///        length-tracks the underlying stream.
  BinaryStreamRefBase(std::shared_ptr<StreamType> SharedImpl, uint64_t Offset,
                      std::optional<uint64_t> Length)
      : SharedImpl(SharedImpl), BorrowedImpl(SharedImpl.get()),
        ViewOffset(Offset), Length(Length) {}

  /// Construct a reference that borrows a stream with an explicit view.
  ///
  /// \param BorrowedImpl Stream to borrow.
  /// \param Offset Byte offset into the stream where the view begins.
  /// \param Length Optional length of the view; if unset, the reference
  ///        length-tracks the underlying stream.
  BinaryStreamRefBase(StreamType &BorrowedImpl, uint64_t Offset,
                      std::optional<uint64_t> Length)
      : BorrowedImpl(&BorrowedImpl), ViewOffset(Offset), Length(Length) {}

  /// Copy-construct a stream reference.
  ///
  /// \param Other Reference to copy.
  BinaryStreamRefBase(const BinaryStreamRefBase &Other) = default;

  /// Copy-assign a stream reference.
  ///
  /// \param Other Reference to copy from.
  /// \returns A reference to this stream reference.
  BinaryStreamRefBase &operator=(const BinaryStreamRefBase &Other) = default;

  /// Move-assign a stream reference.
  ///
  /// \param Other Reference to move from.
  /// \returns A reference to this stream reference.
  BinaryStreamRefBase &operator=(BinaryStreamRefBase &&Other) = default;

  /// Move-construct a stream reference.
  ///
  /// \param Other Reference to move from.
  BinaryStreamRefBase(BinaryStreamRefBase &&Other) = default;

public:
  /// Return the endianness of multi-byte values in the underlying stream.
  ///
  /// \returns The endianness of multi-byte values in the underlying stream.
  llvm::endianness getEndian() const { return BorrowedImpl->getEndian(); }

  /// Return the length in bytes of this stream reference's view.
  ///
  /// \returns The length in bytes of this stream reference's view.
  uint64_t getLength() const {
    if (Length)
      return *Length;

    return BorrowedImpl ? (BorrowedImpl->getLength() - ViewOffset) : 0;
  }

  /// Return a new BinaryStreamRef with the first \p N elements removed.  If
  /// this BinaryStreamRef is length-tracking, then the resulting one will be
  /// too.
  ///
  /// \param N Number of elements to remove from the front.
  /// \returns A new BinaryStreamRef with the first \p N elements removed.
  RefType drop_front(uint64_t N) const {
    if (!BorrowedImpl)
      return RefType();

    N = std::min(N, getLength());
    RefType Result(static_cast<const RefType &>(*this));
    if (N == 0)
      return Result;

    Result.ViewOffset += N;
    if (Result.Length)
      *Result.Length -= N;
    return Result;
  }

  /// Return a new BinaryStreamRef with the last \p N elements removed.
  ///
  /// If this BinaryStreamRef is length-tracking and \p N is greater than 0,
  /// then this BinaryStreamRef will no longer length-track.
  ///
  /// \param N Number of elements to remove from the end.
  /// \returns A new BinaryStreamRef with the last \p N elements removed.
  RefType drop_back(uint64_t N) const {
    if (!BorrowedImpl)
      return RefType();

    RefType Result(static_cast<const RefType &>(*this));
    N = std::min(N, getLength());

    if (N == 0)
      return Result;

    // Since we're dropping non-zero bytes from the end, stop length-tracking
    // by setting the length of the resulting StreamRef to an explicit value.
    if (!Result.Length)
      Result.Length = getLength();

    *Result.Length -= N;
    return Result;
  }

  /// Return a new BinaryStreamRef with only the first \p N elements remaining.
  ///
  /// \param N Number of elements to keep from the front.
  /// \returns A new BinaryStreamRef with only the first \p N elements remaining.
  RefType keep_front(uint64_t N) const {
    assert(N <= getLength());
    return drop_back(getLength() - N);
  }

  /// Return a new BinaryStreamRef with only the last \p N elements remaining.
  ///
  /// \param N Number of elements to keep from the back.
  /// \returns A new BinaryStreamRef with only the last \p N elements remaining.
  RefType keep_back(uint64_t N) const {
    assert(N <= getLength());
    return drop_front(getLength() - N);
  }

  /// Return a new BinaryStreamRef with the first and last \p N elements
  /// removed.
  ///
  /// \param N Number of elements to remove from each end.
  /// \returns A new BinaryStreamRef with the first and last \p N elements
  /// removed.
  RefType drop_symmetric(uint64_t N) const {
    return drop_front(N).drop_back(N);
  }

  /// Return a new BinaryStreamRef with the first \p Offset elements removed,
  /// and retaining exactly \p Len elements.
  ///
  /// \param Offset Number of elements to drop from the front.
  /// \param Len Number of elements to keep.
  /// \returns A new BinaryStreamRef covering \p Len elements starting at
  /// \p Offset.
  RefType slice(uint64_t Offset, uint64_t Len) const {
    return drop_front(Offset).keep_front(Len);
  }

  /// Return true if this reference refers to a valid underlying stream.
  ///
  /// \returns true if this reference refers to a valid underlying stream.
  bool valid() const { return BorrowedImpl != nullptr; }

  /// Compare two stream references for equality of impl, offset, and length.
  ///
  /// \param LHS Left-hand stream reference.
  /// \param RHS Right-hand stream reference.
  /// \returns true if both references have the same impl, offset, and length.
  friend bool operator==(const RefType &LHS, const RefType &RHS) {
    if (LHS.BorrowedImpl != RHS.BorrowedImpl)
      return false;
    if (LHS.ViewOffset != RHS.ViewOffset)
      return false;
    if (LHS.Length != RHS.Length)
      return false;
    return true;
  }

protected:
  /// Validate that \p Offset and \p DataSize fall within this view for reading.
  ///
  /// \param Offset Byte offset relative to this view at which to begin.
  /// \param DataSize Number of bytes to be read.
  /// \returns Error::success() if the range is valid, otherwise an appropriate
  /// error.
  Error checkOffsetForRead(uint64_t Offset, uint64_t DataSize) const {
    if (Offset > getLength())
      return make_error<BinaryStreamError>(stream_error_code::invalid_offset);
    if (getLength() < DataSize + Offset)
      return make_error<BinaryStreamError>(stream_error_code::stream_too_short);
    return Error::success();
  }

  /// Shared ownership of the underlying stream, if any.
  std::shared_ptr<StreamType> SharedImpl;
  /// Non-owning pointer to the underlying stream implementation.
  StreamType *BorrowedImpl = nullptr;
  /// Byte offset into the underlying stream where this view begins.
  uint64_t ViewOffset = 0;
  /// Explicit length of this view, or unset when length-tracking.
  std::optional<uint64_t> Length;
};

/// A copyable, read-only window into a BinaryStream.
///
/// BinaryStreamRef is to BinaryStream what ArrayRef is to an Array.  It
/// provides copy-semantics and read only access to a "window" of the underlying
/// BinaryStream. Note that BinaryStreamRef is *not* a BinaryStream.  That is to
/// say, it does not inherit and override the methods of BinaryStream.  In
/// general, you should not pass around pointers or references to BinaryStreams
/// and use inheritance to achieve polymorphism.  Instead, you should pass
/// around BinaryStreamRefs by value and achieve polymorphism that way.
class BinaryStreamRef
    : public BinaryStreamRefBase<BinaryStreamRef, BinaryStream> {
  friend BinaryStreamRefBase<BinaryStreamRef, BinaryStream>;
  friend class WritableBinaryStreamRef;

  /// Construct a BinaryStreamRef that shares ownership of \p Impl.
  ///
  /// \param Impl Shared pointer to the underlying stream.
  /// \param ViewOffset Byte offset into the stream where the view begins.
  /// \param Length Optional length of the view.
  BinaryStreamRef(std::shared_ptr<BinaryStream> Impl, uint64_t ViewOffset,
                  std::optional<uint64_t> Length)
      : BinaryStreamRefBase(Impl, ViewOffset, Length) {}

public:
  /// Construct an empty BinaryStreamRef.
  BinaryStreamRef() = default;

  /// Construct a BinaryStreamRef that borrows \p Stream.
  ///
  /// \param Stream Stream to borrow.
  LLVM_ABI BinaryStreamRef(BinaryStream &Stream);

  /// Construct a BinaryStreamRef that borrows a view of \p Stream.
  ///
  /// \param Stream Stream to borrow.
  /// \param Offset Byte offset into \p Stream where the view begins.
  /// \param Length Optional length of the view.
  LLVM_ABI BinaryStreamRef(BinaryStream &Stream, uint64_t Offset,
                           std::optional<uint64_t> Length);

  /// Construct a BinaryStreamRef over a contiguous byte array.
  ///
  /// \param Data Bytes to expose as a stream.
  /// \param Endian Endianness of multi-byte values in the data.
  LLVM_ABI explicit BinaryStreamRef(ArrayRef<uint8_t> Data,
                                    llvm::endianness Endian);

  /// Construct a BinaryStreamRef over a string's bytes.
  ///
  /// \param Data String whose bytes are exposed as a stream.
  /// \param Endian Endianness of multi-byte values in the data.
  LLVM_ABI explicit BinaryStreamRef(StringRef Data, llvm::endianness Endian);

  /// Copy-construct a BinaryStreamRef.
  ///
  /// \param Other Reference to copy.
  BinaryStreamRef(const BinaryStreamRef &Other) = default;

  /// Copy-assign a BinaryStreamRef.
  ///
  /// \param Other Reference to copy from.
  /// \returns A reference to this stream reference.
  BinaryStreamRef &operator=(const BinaryStreamRef &Other) = default;

  /// Move-construct a BinaryStreamRef.
  ///
  /// \param Other Reference to move from.
  BinaryStreamRef(BinaryStreamRef &&Other) = default;

  /// Move-assign a BinaryStreamRef.
  ///
  /// \param Other Reference to move from.
  /// \returns A reference to this stream reference.
  BinaryStreamRef &operator=(BinaryStreamRef &&Other) = default;

  /// Deleted; use BinaryStreamRef::slice() instead.
  ///
  /// \param S Source stream reference.
  /// \param Offset Byte offset into \p S.
  /// \param Length Number of bytes in the sub-view.
  BinaryStreamRef(BinaryStreamRef &S, uint64_t Offset,
                  uint64_t Length) = delete;

  /// Given an Offset into this StreamRef and a Size, return a reference to a
  /// buffer owned by the stream.
  ///
  /// \param Offset Byte offset into this view at which to begin reading.
  /// \param Size Number of bytes to read.
  /// \param Buffer Set to the resulting data slice on success.
  ///
  /// \returns a success error code if the entire range of data is within the
  /// bounds of this BinaryStreamRef's view and the implementation could read
  /// the data, and an appropriate error code otherwise.
  LLVM_ABI Error readBytes(uint64_t Offset, uint64_t Size,
                           ArrayRef<uint8_t> &Buffer) const;

  /// Given an Offset into this BinaryStreamRef, return a reference to the
  /// largest buffer the stream could support without necessitating a copy.
  ///
  /// \param Offset Byte offset into this view at which to begin reading.
  /// \param Buffer Set to the longest contiguous chunk on success.
  ///
  /// \returns a success error code if implementation could read the data,
  /// and an appropriate error code otherwise.
  LLVM_ABI Error readLongestContiguousChunk(uint64_t Offset,
                                            ArrayRef<uint8_t> &Buffer) const;
};

/// A substream identified by an absolute offset and a BinaryStreamRef.
struct BinarySubstreamRef {
  /// Absolute offset of this substream in the parent stream.
  uint64_t Offset = 0;
  /// Stream data that makes up this substream.
  BinaryStreamRef StreamData;

  /// Return a substream of this substream.
  ///
  /// \param Off Offset relative to this substream.
  /// \param Size Number of bytes to include.
  /// \returns A substream covering \p Size bytes starting at \p Off.
  BinarySubstreamRef slice(uint64_t Off, uint64_t Size) const {
    BinaryStreamRef SubSub = StreamData.slice(Off, Size);
    return {Off + Offset, SubSub};
  }

  /// Return a substream with the first \p N bytes removed.
  ///
  /// \param N Number of bytes to drop from the front.
  /// \returns A substream with the first \p N bytes removed.
  BinarySubstreamRef drop_front(uint64_t N) const {
    return slice(N, size() - N);
  }

  /// Return a substream containing only the first \p N bytes.
  ///
  /// \param N Number of bytes to keep.
  /// \returns A substream containing only the first \p N bytes.
  BinarySubstreamRef keep_front(uint64_t N) const { return slice(0, N); }

  /// Split this substream at \p Off into a front and back pair.
  ///
  /// \param Off Byte offset at which to split.
  /// \returns A pair of the front and back substreams.
  std::pair<BinarySubstreamRef, BinarySubstreamRef> split(uint64_t Off) const {
    return {keep_front(Off), drop_front(Off)};
  }

  /// Return the length in bytes of this substream.
  ///
  /// \returns The length in bytes of this substream.
  uint64_t size() const { return StreamData.getLength(); }

  /// Return true if this substream has zero length.
  ///
  /// \returns true if the substream has zero length.
  bool empty() const { return size() == 0; }
};

/// A copyable, writable window into a WritableBinaryStream.
class WritableBinaryStreamRef
    : public BinaryStreamRefBase<WritableBinaryStreamRef,
                                 WritableBinaryStream> {
  friend BinaryStreamRefBase<WritableBinaryStreamRef, WritableBinaryStream>;

  /// Construct a WritableBinaryStreamRef that shares ownership of \p Impl.
  ///
  /// \param Impl Shared pointer to the underlying writable stream.
  /// \param ViewOffset Byte offset into the stream where the view begins.
  /// \param Length Optional length of the view.
  WritableBinaryStreamRef(std::shared_ptr<WritableBinaryStream> Impl,
                          uint64_t ViewOffset, std::optional<uint64_t> Length)
      : BinaryStreamRefBase(Impl, ViewOffset, Length) {}

  Error checkOffsetForWrite(uint64_t Offset, uint64_t DataSize) const {
    if (!(BorrowedImpl->getFlags() & BSF_Append))
      return checkOffsetForRead(Offset, DataSize);

    if (Offset > getLength())
      return make_error<BinaryStreamError>(stream_error_code::invalid_offset);
    return Error::success();
  }

public:
  /// Construct an empty WritableBinaryStreamRef.
  WritableBinaryStreamRef() = default;

  /// Construct a WritableBinaryStreamRef that borrows \p Stream.
  ///
  /// \param Stream Writable stream to borrow.
  LLVM_ABI WritableBinaryStreamRef(WritableBinaryStream &Stream);

  /// Construct a WritableBinaryStreamRef that borrows a view of \p Stream.
  ///
  /// \param Stream Writable stream to borrow.
  /// \param Offset Byte offset into \p Stream where the view begins.
  /// \param Length Optional length of the view.
  LLVM_ABI WritableBinaryStreamRef(WritableBinaryStream &Stream,
                                   uint64_t Offset,
                                   std::optional<uint64_t> Length);

  /// Construct a WritableBinaryStreamRef over a contiguous mutable byte array.
  ///
  /// \param Data Mutable bytes to expose as a stream.
  /// \param Endian Endianness of multi-byte values in the data.
  LLVM_ABI explicit WritableBinaryStreamRef(MutableArrayRef<uint8_t> Data,
                                            llvm::endianness Endian);

  /// Copy-construct a WritableBinaryStreamRef.
  ///
  /// \param Other Reference to copy.
  WritableBinaryStreamRef(const WritableBinaryStreamRef &Other) = default;

  /// Copy-assign a WritableBinaryStreamRef.
  ///
  /// \param Other Reference to copy from.
  /// \returns A reference to this stream reference.
  WritableBinaryStreamRef &
  operator=(const WritableBinaryStreamRef &Other) = default;

  /// Move-construct a WritableBinaryStreamRef.
  ///
  /// \param Other Reference to move from.
  WritableBinaryStreamRef(WritableBinaryStreamRef &&Other) = default;

  /// Move-assign a WritableBinaryStreamRef.
  ///
  /// \param Other Reference to move from.
  /// \returns A reference to this stream reference.
  WritableBinaryStreamRef &operator=(WritableBinaryStreamRef &&Other) = default;

  /// Deleted; use WritableBinaryStreamRef::slice() instead.
  ///
  /// \param S Source stream reference.
  /// \param Offset Byte offset into \p S.
  /// \param Length Number of bytes in the sub-view.
  WritableBinaryStreamRef(WritableBinaryStreamRef &S, uint64_t Offset,
                          uint64_t Length) = delete;

  /// Given an Offset into this WritableBinaryStreamRef and some input data,
  /// writes the data to the underlying stream.
  ///
  /// \param Offset Byte offset into this view at which to write.
  /// \param Data Bytes to write into the stream.
  ///
  /// \returns a success error code if the data could fit within the underlying
  /// stream at the specified location and the implementation could write the
  /// data, and an appropriate error code otherwise.
  LLVM_ABI Error writeBytes(uint64_t Offset, ArrayRef<uint8_t> Data) const;

  /// Conver this WritableBinaryStreamRef to a read-only BinaryStreamRef.
  ///
  /// \returns A read-only BinaryStreamRef over the same view.
  LLVM_ABI operator BinaryStreamRef() const;

  /// For buffered streams, commits changes to the backing store.
  ///
  /// \returns a success error code if the commit succeeded, otherwise an
  /// appropriate error code.
  LLVM_ABI Error commit();
};

} // end namespace llvm

#endif // LLVM_SUPPORT_BINARYSTREAMREF_H
