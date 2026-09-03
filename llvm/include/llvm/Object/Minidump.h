//===- Minidump.h - Minidump object file implementation ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECT_MINIDUMP_H
#define LLVM_OBJECT_MINIDUMP_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/fallible_iterator.h"
#include "llvm/ADT/iterator.h"
#include "llvm/BinaryFormat/Minidump.h"
#include "llvm/Object/Binary.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace object {

/// A class providing access to the contents of a minidump file.
class MinidumpFile : public Binary {
public:
  /// Construct a MinidumpFile from a memory buffer.
  ///
  /// Returns an error if this file cannot be identified as a minidump file, or
  /// if its contents are badly corrupted (i.e. we cannot read the stream
  /// directory).
  ///
  /// \param Source Memory buffer holding the putative minidump contents.
  /// \return A MinidumpFile, or an error if the buffer is not a valid minidump.
  LLVM_ABI static Expected<std::unique_ptr<MinidumpFile>>
  create(MemoryBufferRef Source);

  /// True if \p B is a MinidumpFile.
  ///
  /// \param B Binary to test.
  /// \return True if \p B is a MinidumpFile.
  static bool classof(const Binary *B) { return B->isMinidump(); }

  /// Returns the contents of the minidump header.
  ///
  /// \return A const reference to the minidump header.
  const minidump::Header &header() const { return Header; }

  /// Returns the list of streams (stream directory entries) in this file.
  ///
  /// \return The stream directory entries in this minidump.
  ArrayRef<minidump::Directory> streams() const { return Streams; }

  /// Returns the raw contents of the stream given by the directory entry.
  ///
  /// \param Stream Directory entry identifying the stream to slice.
  /// \return The raw bytes of the stream described by \p Stream.
  ArrayRef<uint8_t> getRawStream(const minidump::Directory &Stream) const {
    return getData().slice(Stream.Location.RVA, Stream.Location.DataSize);
  }

  /// Returns the raw contents of the stream of the given type, or std::nullopt
  /// if the file does not contain a stream of this type.
  ///
  /// \param Type Stream type to look up in the stream directory.
  /// \return The stream bytes, or std::nullopt if no stream of that type exists.
  LLVM_ABI std::optional<ArrayRef<uint8_t>>
  getRawStream(minidump::StreamType Type) const;

  /// Returns the raw contents of an object given by the LocationDescriptor. An
  /// error is returned if the descriptor points outside of the minidump file.
  ///
  /// \param Desc Location within the minidump that describes the object bytes.
  /// \return The object bytes, or an error if \p Desc is out of bounds.
  Expected<ArrayRef<uint8_t>>
  getRawData(minidump::LocationDescriptor Desc) const {
    return getDataSlice(getData(), Desc.RVA, Desc.DataSize);
  }

  /// Returns the minidump string at the given offset. An error is returned if
  /// we fail to parse the string, or the string is invalid UTF16.
  ///
  /// \param Offset File offset of the minidump string to decode.
  /// \return The decoded UTF-8 string, or an error on parse failure.
  LLVM_ABI Expected<std::string> getString(size_t Offset) const;

  /// Returns the SystemInfo stream cast to its structure type.
  ///
  /// An error is returned if the file does not contain this stream, or the
  /// stream is smaller than the size of the SystemInfo structure. The internal
  /// consistency of the stream is not checked in any way.
  ///
  /// \return The SystemInfo stream, or an error if it is missing or truncated.
  Expected<const minidump::SystemInfo &> getSystemInfo() const {
    return getStream<minidump::SystemInfo>(minidump::StreamType::SystemInfo);
  }

  /// Returns the module list from the ModuleList stream.
  ///
  /// An error is returned if the file does not contain this stream, or if the
  /// stream is not large enough to contain the number of modules declared in
  /// the stream header. The consistency of the Module entries themselves is
  /// not checked in any way.
  ///
  /// \return The ModuleList entries, or an error if the stream is missing or
  ///         truncated.
  Expected<ArrayRef<minidump::Module>> getModuleList() const {
    return getListStream<minidump::Module>(minidump::StreamType::ModuleList);
  }

  /// Returns the thread list from the ThreadList stream.
  ///
  /// An error is returned if the file does not contain this stream, or if the
  /// stream is not large enough to contain the number of threads declared in
  /// the stream header. The consistency of the Thread entries themselves is
  /// not checked in any way.
  ///
  /// \return The ThreadList entries, or an error if the stream is missing or
  ///         truncated.
  Expected<ArrayRef<minidump::Thread>> getThreadList() const {
    return getListStream<minidump::Thread>(minidump::StreamType::ThreadList);
  }

  /// Returns the Exception stream for the given directory entry.
  ///
  /// An error is returned if the associated stream is smaller than the size of
  /// the ExceptionStream structure, or the directory supplied is not of kind
  /// exception stream.
  ///
  /// \param Directory Stream directory entry expected to describe an exception
  ///        stream.
  /// \return The exception stream for \p Directory, or an error on failure.
  Expected<const minidump::ExceptionStream &>
  getExceptionStream(minidump::Directory Directory) const {
    if (Directory.Type != minidump::StreamType::Exception) {
      return createError("Not an exception stream");
    }

    return getStreamFromDirectory<minidump::ExceptionStream>(Directory);
  }

  /// Returns the first exception stream in the file.
  ///
  /// An error is returned if the associated stream is smaller than the size of
  /// the ExceptionStream structure, or the directory supplied is not of kind
  /// exception stream.
  ///
  /// \return The first exception stream, or an error if none is present.
  Expected<const minidump::ExceptionStream &> getExceptionStream() const {
    auto it = getExceptionStreams();
    if (it.begin() == it.end())
      return createError("No exception streams");
    return *it.begin();
  }

  /// Returns memory descriptors from the MemoryList stream.
  ///
  /// The descriptors provide the content of interesting regions of memory at
  /// the time the minidump was taken. An error is returned if the file does not
  /// contain this stream, or if the stream is not large enough to contain the
  /// number of memory descriptors declared in the stream header. The
  /// consistency of the MemoryDescriptor entries themselves is not checked in
  /// any way.
  ///
  /// \return The MemoryList descriptors, or an error if the stream is missing or
  ///         truncated.
  Expected<ArrayRef<minidump::MemoryDescriptor>> getMemoryList() const {
    return getListStream<minidump::MemoryDescriptor>(
        minidump::StreamType::MemoryList);
  }

  /// Returns the header to the memory 64 list stream. An error is returned if
  /// the file does not contain this stream.
  ///
  /// \return The Memory64List stream header, or an error if the stream is missing.
  Expected<minidump::Memory64ListHeader> getMemoryList64Header() const {
    return getStream<minidump::Memory64ListHeader>(
        minidump::StreamType::Memory64List);
  }

  /// Forward iterator over MemoryInfo entries in a MemoryInfoList stream.
  class MemoryInfoIterator
      : public iterator_facade_base<MemoryInfoIterator,
                                    std::forward_iterator_tag,
                                    minidump::MemoryInfo> {
  public:
    /// Construct an iterator over \p Storage using \p Stride between entries.
    ///
    /// \param Storage Raw bytes of the MemoryInfoList payload.
    /// \param Stride Byte stride between consecutive MemoryInfo records.
    MemoryInfoIterator(ArrayRef<uint8_t> Storage, size_t Stride)
        : Storage(Storage), Stride(Stride) {
      assert(Storage.size() % Stride == 0);
    }

    /// Return true if this iterator and \p R refer to the same position.
    ///
    /// \param R Iterator to compare against.
    /// \return True if both iterators refer to the same position.
    bool operator==(const MemoryInfoIterator &R) const {
      return Storage.size() == R.Storage.size();
    }

    /// Return a reference to the MemoryInfo at the current position.
    ///
    /// \return Reference to the MemoryInfo at the current position.
    const minidump::MemoryInfo &operator*() const {
      assert(Storage.size() >= sizeof(minidump::MemoryInfo));
      return *reinterpret_cast<const minidump::MemoryInfo *>(Storage.data());
    }

    /// Advance to the next MemoryInfo entry and return this iterator.
    ///
    /// \return Reference to this iterator after advancing.
    MemoryInfoIterator &operator++() {
      Storage = Storage.drop_front(Stride);
      return *this;
    }

  private:
    ArrayRef<uint8_t> Storage;
    size_t Stride;
  };

  /// Class the provides an iterator over the memory64 memory ranges. Only the
  /// the first descriptor is validated as readable beforehand.
  class Memory64Iterator {
  public:
    /// Construct a begin iterator over \p Descriptors backed by \p Storage.
    ///
    /// \param Storage Raw bytes of the Memory64List payload after the header.
    /// \param Descriptors Memory64 descriptors to walk.
    /// \return A begin iterator over the given Memory64 descriptors.
    static Memory64Iterator
    begin(ArrayRef<uint8_t> Storage,
          ArrayRef<minidump::MemoryDescriptor_64> Descriptors) {
      return Memory64Iterator(Storage, Descriptors);
    }

    /// Construct an end iterator past the last memory64 range.
    ///
    /// \return An end iterator past the last memory64 range.
    static Memory64Iterator end() { return Memory64Iterator(); }

    /// Return true if this iterator and \p R are both at end or both not.
    ///
    /// \param R Iterator to compare against.
    /// \return True if both iterators are at end or both are not.
    bool operator==(const Memory64Iterator &R) const {
      return IsEnd == R.IsEnd;
    }

    /// Return true if this iterator and \p R are not equal.
    ///
    /// \param R Iterator to compare against.
    /// \return True if the iterators are not equal.
    bool operator!=(const Memory64Iterator &R) const { return !(*this == R); }

    /// Return the current descriptor paired with its memory contents.
    ///
    /// \return The current descriptor paired with its memory contents.
    const std::pair<minidump::MemoryDescriptor_64, ArrayRef<uint8_t>> &
    operator*() {
      return Current;
    }

    /// Return a pointer to the current descriptor and content pair.
    ///
    /// \return Pointer to the current descriptor and content pair.
    const std::pair<minidump::MemoryDescriptor_64, ArrayRef<uint8_t>> *
    operator->() {
      return &Current;
    }

    /// Advance to the next descriptor, or report an error if it is unreadable.
    ///
    /// \return Success, or an error if the next descriptor is unreadable.
    Error inc() {
      if (Descriptors.empty()) {
        IsEnd = true;
        return Error::success();
      }

      // Drop front gives us an array ref, so we need to call .front() as well.
      const minidump::MemoryDescriptor_64 &Descriptor = Descriptors.front();
      if (Descriptor.DataSize > Storage.size()) {
        IsEnd = true;
        return make_error<GenericBinaryError>(
            "Memory64 Descriptor exceeds end of file.",
            object_error::unexpected_eof);
      }

      ArrayRef<uint8_t> Content = Storage.take_front(Descriptor.DataSize);
      Current = std::make_pair(Descriptor, Content);

      Storage = Storage.drop_front(Descriptor.DataSize);
      Descriptors = Descriptors.drop_front();

      return Error::success();
    }

  private:
    // This constructor expects that the first descriptor is readable.
    Memory64Iterator(ArrayRef<uint8_t> Storage,
                     ArrayRef<minidump::MemoryDescriptor_64> Descriptors)
        : Storage(Storage), Descriptors(Descriptors), IsEnd(false) {
      assert(!Descriptors.empty() &&
             Storage.size() >= Descriptors.front().DataSize);
      minidump::MemoryDescriptor_64 Descriptor = Descriptors.front();
      ArrayRef<uint8_t> Content = Storage.take_front(Descriptor.DataSize);
      Current = std::make_pair(Descriptor, Content);
      this->Descriptors = Descriptors.drop_front();
      this->Storage = Storage.drop_front(Descriptor.DataSize);
    }

    Memory64Iterator()
        : Storage(ArrayRef<uint8_t>()),
          Descriptors(ArrayRef<minidump::MemoryDescriptor_64>()), IsEnd(true) {}

    std::pair<minidump::MemoryDescriptor_64, ArrayRef<uint8_t>> Current;
    ArrayRef<uint8_t> Storage;
    ArrayRef<minidump::MemoryDescriptor_64> Descriptors;
    bool IsEnd;
  };

  /// Iterator over exception streams listed in the minidump directory.
  class ExceptionStreamsIterator {
  public:
    /// Construct an iterator over \p Streams using \p File to parse each one.
    ///
    /// \param Streams Exception stream directory entries to walk.
    /// \param File Minidump that owns the stream payloads.
    ExceptionStreamsIterator(ArrayRef<minidump::Directory> Streams,
                             const MinidumpFile *File)
        : Streams(Streams), File(File) {}

    /// Return true if this iterator and \p R refer to the same position.
    ///
    /// \param R Iterator to compare against.
    /// \return True if both iterators refer to the same position.
    bool operator==(const ExceptionStreamsIterator &R) const {
      return Streams.size() == R.Streams.size();
    }

    /// Return true if this iterator and \p R are not equal.
    ///
    /// \param R Iterator to compare against.
    /// \return True if the iterators are not equal.
    bool operator!=(const ExceptionStreamsIterator &R) const {
      return !(*this == R);
    }

    /// Return the exception stream at the current directory entry.
    ///
    /// \return The exception stream at the current position, or an error.
    Expected<const minidump::ExceptionStream &> operator*() {
      return File->getExceptionStream(Streams.front());
    }

    /// Advance to the next exception stream directory entry.
    ///
    /// \return Reference to this iterator after advancing.
    ExceptionStreamsIterator &operator++() {
      if (!Streams.empty())
        Streams = Streams.drop_front();

      return *this;
    }

  private:
    ArrayRef<minidump::Directory> Streams;
    const MinidumpFile *File;
  };

  /// Fallible iterator over Memory64List descriptor and content pairs.
  using FallibleMemory64Iterator = llvm::fallible_iterator<Memory64Iterator>;

  /// Returns an iterator range over each exception stream in the file.
  ///
  /// The contents of the exception strema are not validated before being read;
  /// an error will be returned if the stream is not large enough to contain an
  /// exception stream, or if the stream points beyond the end of the file.
  ///
  /// \return An iterator range over exception streams in this minidump.
  LLVM_ABI iterator_range<ExceptionStreamsIterator> getExceptionStreams() const;

  /// Returns an iterator pairing each Memory64List descriptor with its content.
  ///
  /// An error is returned if the file does not contain a Memory64List stream,
  /// or if the descriptor data is unreadable.
  ///
  /// \param Err Set on iteration failure for the fallible iterator.
  /// \return A fallible iterator range over Memory64 descriptor and content pairs.
  LLVM_ABI iterator_range<FallibleMemory64Iterator>
  getMemory64List(Error &Err) const;

  /// Returns memory info descriptors from the MemoryInfoList stream.
  ///
  /// The descriptors provide properties (e.g. permissions) of interesting
  /// regions of memory at the time the minidump was taken. An error is returned
  /// if the file does not contain this stream, or if the stream is not large
  /// enough to contain the number of memory descriptors declared in the stream
  /// header. The consistency of the MemoryInfoList entries themselves is not
  /// checked in any way.
  ///
  /// \return An iterator range over MemoryInfo entries, or an error on failure.
  LLVM_ABI Expected<iterator_range<MemoryInfoIterator>>
  getMemoryInfoList() const;

private:
  static Error createError(StringRef Str) {
    return make_error<GenericBinaryError>(Str, object_error::parse_failed);
  }

  static Error createEOFError() {
    return make_error<GenericBinaryError>("Unexpected EOF",
                                          object_error::unexpected_eof);
  }

  /// Return a slice of the given data array, with bounds checking.
  LLVM_ABI static Expected<ArrayRef<uint8_t>>
  getDataSlice(ArrayRef<uint8_t> Data, uint64_t Offset, uint64_t Size);

  /// Return the slice of the given data array as an array of objects of the
  /// given type. The function checks that the input array is large enough to
  /// contain the correct number of objects of the given type.
  template <typename T>
  static Expected<ArrayRef<T>> getDataSliceAs(ArrayRef<uint8_t> Data,
                                              uint64_t Offset, uint64_t Count);

  MinidumpFile(MemoryBufferRef Source, const minidump::Header &Header,
               ArrayRef<minidump::Directory> Streams,
               DenseMap<minidump::StreamType, std::size_t> StreamMap,
               std::vector<minidump::Directory> ExceptionStreams)
      : Binary(ID_Minidump, Source), Header(Header), Streams(Streams),
        StreamMap(std::move(StreamMap)),
        ExceptionStreams(std::move(ExceptionStreams)) {}

  ArrayRef<uint8_t> getData() const {
    return arrayRefFromStringRef(Data.getBuffer());
  }

  /// Return the stream of the given type, cast to the appropriate type. Checks
  /// that the stream is large enough to hold an object of this type.
  template <typename T>
  Expected<const T &>
  getStreamFromDirectory(minidump::Directory Directory) const;

  /// Return the stream of the given type, cast to the appropriate type. Checks
  /// that the stream is large enough to hold an object of this type.
  template <typename T>
  Expected<const T &> getStream(minidump::StreamType Stream) const;

  /// Return the contents of a stream which contains a list of fixed-size items,
  /// prefixed by the list size.
  template <typename T>
  Expected<ArrayRef<T>> getListStream(minidump::StreamType Stream) const;

  const minidump::Header &Header;
  ArrayRef<minidump::Directory> Streams;
  DenseMap<minidump::StreamType, std::size_t> StreamMap;
  std::vector<minidump::Directory> ExceptionStreams;
};

template <typename T>
Expected<const T &>
MinidumpFile::getStreamFromDirectory(minidump::Directory Directory) const {
  ArrayRef<uint8_t> Stream = getRawStream(Directory);
  if (Stream.size() >= sizeof(T))
    return *reinterpret_cast<const T *>(Stream.data());
  return createEOFError();
}

template <typename T>
Expected<const T &> MinidumpFile::getStream(minidump::StreamType Type) const {
  if (std::optional<ArrayRef<uint8_t>> Stream = getRawStream(Type)) {
    if (Stream->size() >= sizeof(T))
      return *reinterpret_cast<const T *>(Stream->data());
    return createEOFError();
  }
  return createError("No such stream");
}

template <typename T>
Expected<ArrayRef<T>> MinidumpFile::getDataSliceAs(ArrayRef<uint8_t> Data,
                                                   uint64_t Offset,
                                                   uint64_t Count) {
  // Check for overflow.
  if (Count > std::numeric_limits<uint64_t>::max() / sizeof(T))
    return createEOFError();
  Expected<ArrayRef<uint8_t>> Slice =
      getDataSlice(Data, Offset, sizeof(T) * Count);
  if (!Slice)
    return Slice.takeError();

  return ArrayRef<T>(reinterpret_cast<const T *>(Slice->data()), Count);
}

template <typename T>
Expected<ArrayRef<T>>
MinidumpFile::getListStream(minidump::StreamType Type) const {
  std::optional<ArrayRef<uint8_t>> Stream = getRawStream(Type);
  if (!Stream)
    return createError("No such stream");
  auto ExpectedSize = getDataSliceAs<support::ulittle32_t>(*Stream, 0, 1);
  if (!ExpectedSize)
    return ExpectedSize.takeError();

  size_t ListSize = ExpectedSize.get()[0];

  size_t ListOffset = 4;
  // Some producers insert additional padding bytes to align the list to an
  // 8-byte boundary. Check for that by comparing the list size with the overall
  // stream size.
  if (ListOffset + sizeof(T) * ListSize < Stream->size())
    ListOffset = 8;

  return getDataSliceAs<T>(*Stream, ListOffset, ListSize);
}

} // end namespace object
} // end namespace llvm

#endif // LLVM_OBJECT_MINIDUMP_H
