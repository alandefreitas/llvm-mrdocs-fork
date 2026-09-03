//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines ContiguousBlobAccumulator, the size-limited output buffer
/// shared by the yaml2obj emitters.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_OBJECTYAML_CONTIGUOUSBLOBACCUMULATOR_H
#define LLVM_OBJECTYAML_CONTIGUOUSBLOBACCUMULATOR_H

#include "llvm/Support/EndianStream.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace yaml {

class BinaryRef;

/// Builds a contiguous binary blob while tracking an absolute output offset.
///
/// The offset notionally begins at \c InitialOffset. The blob may be limited
/// to an arbitrary size; once that limit is reached, further writes are
/// ignored and the error condition is remembered so reporting can be deferred
/// until a convenient time.
class ContiguousBlobAccumulator {
  const uint64_t InitialOffset;
  const uint64_t MaxSize;

  SmallVector<char, 128> Buf;
  raw_svector_ostream OS;
  Error ReachedLimitErr = Error::success();

  LLVM_ABI bool checkLimit(uint64_t Size);

public:
  /// Construct an accumulator whose absolute offset starts at \p BaseOffset.
  /// \param BaseOffset Absolute offset of the first byte written.
  /// \param SizeLimit Maximum number of bytes that may be written.
  ContiguousBlobAccumulator(uint64_t BaseOffset, uint64_t SizeLimit)
      : InitialOffset(BaseOffset), MaxSize(SizeLimit), OS(Buf) {}

  /// Return the number of bytes written into the accumulator so far.
  /// \returns Number of bytes currently stored in the accumulator.
  uint64_t tell() const { return OS.tell(); }
  /// Return the absolute output offset of the next byte to be written.
  /// \returns Absolute offset of the next byte to be written.
  uint64_t getOffset() const { return InitialOffset + OS.tell(); }
  /// Write the accumulated blob contents to \p Out.
  /// \param Out Destination stream that receives the blob bytes.
  void writeBlobToStream(raw_ostream &Out) const { Out << OS.str(); }

  /// Take ownership of any deferred size-limit error.
  ///
  /// Probes the limit with a zero-byte write first so a prior exact-limit
  /// write is still reported.
  /// \returns The deferred size-limit error, or success if none was set.
  Error takeLimitError() {
    // Request to write 0 bytes to check we did not reach the limit.
    checkLimit(0);
    return std::move(ReachedLimitErr);
  }

  /// Pad the blob with zeros until the absolute offset is aligned.
  /// \param Align Alignment to pad up to.
  /// \returns The new absolute offset.
  LLVM_ABI uint64_t padToAlignment(unsigned Align);

  /// Return the underlying stream if \p Size more bytes fit under the limit.
  /// \param Size Number of bytes the caller intends to write.
  /// \returns Pointer to the raw stream, or \c nullptr if the limit would be
  /// exceeded.
  raw_ostream *getRawOS(uint64_t Size) {
    if (checkLimit(Size))
      return &OS;
    return nullptr;
  }

  /// Append up to \p N bytes from \p Bin as raw binary data.
  /// \param Bin Binary content to append.
  /// \param N Maximum number of bytes to write; defaults to all of \p Bin.
  LLVM_ABI void writeAsBinary(const BinaryRef &Bin, uint64_t N = UINT64_MAX);

  /// Append \p Num zero bytes to the blob.
  /// \param Num Number of zero bytes to write.
  void writeZeros(uint64_t Num) {
    if (checkLimit(Num))
      OS.write_zeros(Num);
  }

  /// Append \p Size bytes from \p Ptr to the blob.
  /// \param Ptr Source bytes to copy.
  /// \param Size Number of bytes to write.
  void write(const char *Ptr, size_t Size) {
    if (checkLimit(Size))
      OS.write(Ptr, Size);
  }

  /// Append a single byte \p C to the blob.
  /// \param C Byte value to write.
  void write(unsigned char C) {
    if (checkLimit(1))
      OS.write(C);
  }

  /// Append \p Val encoded as an unsigned LEB128 integer.
  /// \param Val Value to encode.
  /// \returns Number of bytes written.
  LLVM_ABI unsigned writeULEB128(uint64_t Val);

  /// Append \p Val encoded as a signed LEB128 integer.
  /// \param Val Value to encode.
  /// \returns Number of bytes written.
  LLVM_ABI unsigned writeSLEB128(int64_t Val);

  /// Append \p Val encoded in endianness \p E.
  /// \param Val Value to write.
  /// \param E Endianness used to serialize \p Val.
  template <typename T> void write(T Val, llvm::endianness E) {
    if (checkLimit(sizeof(T)))
      support::endian::write<T>(OS, Val, E);
  }

  /// Overwrite bytes at relative offset \p Pos with \p Val in endianness \p E.
  /// \param Pos Offset within the accumulator at which to write.
  /// \param Val Value to store.
  /// \param E Endianness used to serialize \p Val.
  template <typename T>
  void updateDataAt(uint64_t Pos, T Val, llvm::endianness E) {
    char Data[sizeof(T)];
    support::endian::write<T>(Data, Val, E);
    updateDataAt(Pos, Data, sizeof(Data));
  }

  /// Overwrite \p Size bytes at relative offset \p Pos with \p Data.
  /// \param Pos Offset within the accumulator at which to write.
  /// \param Data Source bytes to copy.
  /// \param Size Number of bytes to overwrite.
  LLVM_ABI void updateDataAt(uint64_t Pos, const void *Data, size_t Size);
};

} // end namespace yaml
} // end namespace llvm

#endif // LLVM_OBJECTYAML_CONTIGUOUSBLOBACCUMULATOR_H
