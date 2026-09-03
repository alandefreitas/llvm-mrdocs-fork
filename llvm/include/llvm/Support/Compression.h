//===-- llvm/Support/Compression.h ---Compression----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains basic functions for compression/decompression.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_COMPRESSION_H
#define LLVM_SUPPORT_COMPRESSION_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
template <typename T> class SmallVectorImpl;
class Error;

/// Compression scheme used for compressed debug sections in object files.
///
/// None indicates no compression. The other members are a subset of
/// compression::Format, which is used for compressed debug sections in some
/// object file formats (e.g. ELF). This is a separate class as we may add new
/// compression::Format members for non-debugging purposes.
enum class DebugCompressionType {
  None, ///< No compression
  Zlib, ///< zlib
  Zstd, ///< Zstandard
};

/// Compression and decompression helpers for zlib, zstd, and related formats.
namespace compression {
/// zlib compression and decompression.
namespace zlib {

/// zlib compression level that stores data without compressing.
constexpr int NoCompression = 0;
/// zlib compression level favoring speed over ratio.
constexpr int BestSpeedCompression = 1;
/// zlib default compression level.
constexpr int DefaultCompression = 6;
/// zlib compression level favoring ratio over speed.
constexpr int BestSizeCompression = 9;

/// Return whether this LLVM build was compiled with zlib support.
///
/// \return True if zlib is available; false otherwise.
LLVM_ABI bool isAvailable();

/// Compress \p Input with zlib into \p CompressedBuffer.
///
/// \param Input Bytes to compress.
/// \param CompressedBuffer Destination; resized to the compressed size.
/// \param Level zlib compression level. Defaults to \c DefaultCompression.
LLVM_ABI void compress(ArrayRef<uint8_t> Input,
                       SmallVectorImpl<uint8_t> &CompressedBuffer,
                       int Level = DefaultCompression);

/// Decompress zlib-compressed \p Input into a caller-provided buffer.
///
/// \param Input Compressed bytes.
/// \param Output Destination buffer with capacity \p UncompressedSize.
/// \param UncompressedSize On input, capacity of \p Output; on output, the
/// number of bytes written.
/// \return Success, or an error describing the zlib failure.
LLVM_ABI Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                          size_t &UncompressedSize);

/// Decompress zlib-compressed \p Input into \p Output.
///
/// \param Input Compressed bytes.
/// \param Output Destination; resized to \p UncompressedSize bytes.
/// \param UncompressedSize Expected uncompressed size in bytes.
/// \return Success, or an error describing the zlib failure.
LLVM_ABI Error decompress(ArrayRef<uint8_t> Input,
                          SmallVectorImpl<uint8_t> &Output,
                          size_t UncompressedSize);

} // End of namespace zlib

/// Zstandard compression and decompression.
namespace zstd {

/// Fastest zstd compression level offered by this API.
constexpr int NoCompression = -5;
/// zstd compression level favoring speed over ratio.
constexpr int BestSpeedCompression = 1;
/// zstd default compression level used by LLVM.
constexpr int DefaultCompression = 5;
/// zstd compression level favoring ratio over speed.
constexpr int BestSizeCompression = 12;

/// Return whether this LLVM build was compiled with zstd support.
///
/// \return True if zstd is available; false otherwise.
LLVM_ABI bool isAvailable();

/// Compress \p Input with zstd into \p CompressedBuffer.
///
/// \param Input Bytes to compress.
/// \param CompressedBuffer Destination; resized to the compressed size.
/// \param Level zstd compression level. Defaults to \c DefaultCompression.
/// \param EnableLdm Whether to enable zstd long-distance matching.
LLVM_ABI void compress(ArrayRef<uint8_t> Input,
                       SmallVectorImpl<uint8_t> &CompressedBuffer,
                       int Level = DefaultCompression, bool EnableLdm = false);

/// Decompress zstd-compressed \p Input into a caller-provided buffer.
///
/// \param Input Compressed bytes.
/// \param Output Destination buffer with capacity \p UncompressedSize.
/// \param UncompressedSize On input, capacity of \p Output; on output, the
/// number of bytes written.
/// \return Success, or an error describing the zstd failure.
LLVM_ABI Error decompress(ArrayRef<uint8_t> Input, uint8_t *Output,
                          size_t &UncompressedSize);

/// Decompress zstd-compressed \p Input into \p Output.
///
/// \param Input Compressed bytes.
/// \param Output Destination; resized to \p UncompressedSize bytes.
/// \param UncompressedSize Expected uncompressed size in bytes.
/// \return Success, or an error describing the zstd failure.
LLVM_ABI Error decompress(ArrayRef<uint8_t> Input,
                          SmallVectorImpl<uint8_t> &Output,
                          size_t UncompressedSize);

} // End of namespace zstd

/// Compression algorithm used by \c compress and \c decompress.
enum class Format {
  Zlib, ///< zlib
  Zstd, ///< Zstandard
};

/// Map a \c DebugCompressionType value to the corresponding \c Format.
///
/// \param Type Debug compression type; must not be
/// \c DebugCompressionType::None.
/// \return The \c Format matching \p Type.
inline Format formatFor(DebugCompressionType Type) {
  switch (Type) {
  case DebugCompressionType::None:
    llvm_unreachable("not a compression type");
  case DebugCompressionType::Zlib:
    return Format::Zlib;
  case DebugCompressionType::Zstd:
    return Format::Zstd;
  }
  llvm_unreachable("");
}

/// Compression format, level, and zstd options for \c compress.
struct Params {
  /// Construct default-level parameters for compression format \p F.
  ///
  /// \param F Compression format whose default level is used.
  constexpr Params(Format F)
      : format(F), level(F == Format::Zlib ? zlib::DefaultCompression
                                           : zstd::DefaultCompression) {}
  /// Construct parameters with an explicit level and optional zstd LDM.
  ///
  /// \param F Compression format.
  /// \param L Compression level for \p F.
  /// \param Ldm Whether to enable zstd long-distance matching (ignored for
  /// zlib).
  constexpr Params(Format F, int L, bool Ldm = false)
      : format(F), level(L), zstdEnableLdm(Ldm) {}
  /// Construct default-level parameters from a debug-section compression type.
  ///
  /// \param Type Debug compression type; must not be
  /// \c DebugCompressionType::None.
  Params(DebugCompressionType Type) : Params(formatFor(Type)) {}

  /// Compression algorithm to use.
  Format format;
  /// Compression level interpreted by \ref format.
  int level;
  /// Enable zstd long-distance matching (ignored for zlib).
  bool zstdEnableLdm = false;
  // This may support multi-threading for zstd in the future. Note that
  // different threads may produce different output, so be careful if certain
  // output determinism is desired.
};

/// Return why format \p F is unavailable, or null if this LLVM build supports
/// it.
///
/// Support is determined by \c LLVM_ENABLE_ZLIB / \c LLVM_ENABLE_ZSTD at build
/// time.
///
/// \param F Compression format to query.
/// \return \c nullptr if \p F is available; otherwise a string literal
/// describing why it is not.
LLVM_ABI const char *getReasonIfUnsupported(Format F);

/// Compress \p Input using the format and options in \p P.
///
/// \param P Format, level, and zstd options.
/// \param Input Bytes to compress.
/// \param Output Destination; resized to the compressed size.
LLVM_ABI void compress(Params P, ArrayRef<uint8_t> Input,
                       SmallVectorImpl<uint8_t> &Output);

/// Decompress \p Input using debug compression type \p T into \p Output.
///
/// The uncompressed size must be known in advance.
///
/// \param T Debug compression type; must not be \c DebugCompressionType::None.
/// \param Input Compressed bytes.
/// \param Output Destination buffer with capacity \p UncompressedSize.
/// \param UncompressedSize Capacity of \p Output in bytes.
/// \return Success, or an error describing the decompression failure.
LLVM_ABI Error decompress(DebugCompressionType T, ArrayRef<uint8_t> Input,
                          uint8_t *Output, size_t UncompressedSize);
/// Decompress \p Input using compression format \p F into \p Output,
/// which is resized to \p UncompressedSize bytes.
///
/// \param F Compression format of \p Input.
/// \param Input Compressed bytes.
/// \param Output Destination; resized to \p UncompressedSize bytes.
/// \param UncompressedSize Uncompressed size in bytes.
/// \return Success, or an error describing the decompression failure.
LLVM_ABI Error decompress(Format F, ArrayRef<uint8_t> Input,
                          SmallVectorImpl<uint8_t> &Output,
                          size_t UncompressedSize);
/// Decompress \p Input using the debug-info compression format \p T into
/// \p Output, which is resized to \p UncompressedSize bytes.
///
/// \param T Debug compression type; must not be \c DebugCompressionType::None.
/// \param Input Compressed bytes.
/// \param Output Destination; resized to \p UncompressedSize bytes.
/// \param UncompressedSize Uncompressed size in bytes.
/// \return Success, or an error describing the decompression failure.
LLVM_ABI Error decompress(DebugCompressionType T, ArrayRef<uint8_t> Input,
                          SmallVectorImpl<uint8_t> &Output,
                          size_t UncompressedSize);

} // End of namespace compression

} // End of namespace llvm

#endif
