//===- EndianStream.h - Stream ops with endian specific data ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines utilities for operating on streams that have endian
// specific data.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ENDIANSTREAM_H
#define LLVM_SUPPORT_ENDIANSTREAM_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/raw_ostream.h"

namespace llvm {
namespace support {

namespace endian {

/// Write each value in \p values to \p os using the given byte order.
///
/// \param os Stream that receives the endian-converted bytes.
/// \param values Values to write in order.
/// \param endian Byte order to apply before writing each value.
template <typename value_type>
inline void write_array(raw_ostream &os, ArrayRef<value_type> values,
                        endianness endian) {
  for (const auto orig : values) {
    value_type value = byte_swap<value_type>(orig, endian);
    os.write((const char *)&value, sizeof(value_type));
  }
}

/// Write \p value to \p os using the given byte order.
///
/// \param os Stream that receives the endian-converted bytes.
/// \param value Value to write.
/// \param endian Byte order to apply before writing.
template <typename value_type>
inline void write(raw_ostream &os, value_type value, endianness endian) {
  value = byte_swap<value_type>(value, endian);
  os.write((const char *)&value, sizeof(value_type));
}

/// Write a \c float to \p os using the given byte order.
///
/// \param os Stream that receives the endian-converted bytes.
/// \param value Float whose bit pattern is written as a 32-bit integer.
/// \param endian Byte order to apply to the bit pattern.
template <>
inline void write<float>(raw_ostream &os, float value, endianness endian) {
  write(os, llvm::bit_cast<uint32_t>(value), endian);
}

/// Write a \c double to \p os using the given byte order.
///
/// \param os Stream that receives the endian-converted bytes.
/// \param value Double whose bit pattern is written as a 64-bit integer.
/// \param endian Byte order to apply to the bit pattern.
template <>
inline void write<double>(raw_ostream &os, double value,
                          endianness endian) {
  write(os, llvm::bit_cast<uint64_t>(value), endian);
}

/// Write each value in \p vals to \p os using the given byte order.
///
/// \param os Stream that receives the endian-converted bytes.
/// \param vals Values to write in order.
/// \param endian Byte order to apply before writing each value.
template <typename value_type>
inline void write(raw_ostream &os, ArrayRef<value_type> vals,
                  endianness endian) {
  for (value_type v : vals)
    write(os, v, endian);
}

/// Append \p V to \p Out using the given byte order.
///
/// \param Out Buffer that receives the endian-converted bytes.
/// \param V Value to append.
/// \param E Byte order to apply before appending.
template <typename value_type>
inline void write(SmallVectorImpl<char> &Out, value_type V, endianness E) {
  V = byte_swap<value_type>(V, E);
  Out.append((const char *)&V, (const char *)&V + sizeof(value_type));
}

/// Adapter to write values to a stream in a particular byte order.
struct Writer {
  /// Stream that receives endian-converted output.
  raw_ostream &OS;
  /// Byte order applied to values written through this adapter.
  endianness Endian;
  /// Construct a writer that targets \p OS with byte order \p Endian.
  ///
  /// \param OS Stream that receives endian-converted output.
  /// \param Endian Byte order applied to values written through this adapter.
  Writer(raw_ostream &OS, endianness Endian) : OS(OS), Endian(Endian) {}
  /// Write each value in \p Val using this writer's byte order.
  ///
  /// \param Val Values to write in order.
  template <typename value_type> void write(ArrayRef<value_type> Val) {
    endian::write(OS, Val, Endian);
  }
  /// Write \p Val using this writer's byte order.
  ///
  /// \param Val Value to write.
  template <typename value_type> void write(value_type Val) {
    endian::write(OS, Val, Endian);
  }
};

} // end namespace endian

} // end namespace support
} // end namespace llvm

#endif
