//===- SwapByteOrder.h - Generic and optimized byte swaps -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares generic and optimized functions to swap the byte order of
// an integral type.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_SWAPBYTEORDER_H
#define LLVM_SUPPORT_SWAPBYTEORDER_H

#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/bit.h"
#include <cstdint>
#include <type_traits>

namespace llvm {

namespace sys {

/// True if this host stores multi-byte values most-significant byte first.
constexpr bool IsBigEndianHost =
    llvm::endianness::native == llvm::endianness::big;

/// True if this host stores multi-byte values least-significant byte first.
constexpr bool IsLittleEndianHost = !IsBigEndianHost;

/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline unsigned char      getSwappedBytes(unsigned char      C) { return llvm::byteswap(C); }
/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline   signed char      getSwappedBytes( signed  char      C) { return llvm::byteswap(C); }
/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline          char      getSwappedBytes(         char      C) { return llvm::byteswap(C); }

/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline unsigned short     getSwappedBytes(unsigned short     C) { return llvm::byteswap(C); }
/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline   signed short     getSwappedBytes(  signed short     C) { return llvm::byteswap(C); }

/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline unsigned int       getSwappedBytes(unsigned int       C) { return llvm::byteswap(C); }
/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline   signed int       getSwappedBytes(  signed int       C) { return llvm::byteswap(C); }

/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline unsigned long      getSwappedBytes(unsigned long      C) { return llvm::byteswap(C); }
/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline   signed long      getSwappedBytes(  signed long      C) { return llvm::byteswap(C); }

/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline unsigned long long getSwappedBytes(unsigned long long C) { return llvm::byteswap(C); }
/// Return \p C with its bytes reversed.
/// \param C Value whose bytes are reversed.
/// \return Value of the same type with bytes in reverse order.
inline   signed long long getSwappedBytes(  signed long long C) { return llvm::byteswap(C); }

/// Return float \p C with its underlying bytes reversed.
/// \param C Float value whose underlying bytes are reversed.
/// \return Float with its underlying bytes in reverse order.
inline float getSwappedBytes(float C) {
  return llvm::bit_cast<float>(llvm::byteswap(llvm::bit_cast<uint32_t>(C)));
}

/// Return double \p C with its underlying bytes reversed.
/// \param C Double value whose underlying bytes are reversed.
/// \return Double with its underlying bytes in reverse order.
inline double getSwappedBytes(double C) {
  return llvm::bit_cast<double>(llvm::byteswap(llvm::bit_cast<uint64_t>(C)));
}

/// Return enum value \p C with the bytes of its underlying integer reversed.
/// \param C Enum value whose underlying integer bytes are reversed.
/// \return Enum value with the bytes of its underlying integer reversed.
template <typename T>
inline std::enable_if_t<std::is_enum_v<T>, T> getSwappedBytes(T C) {
  return static_cast<T>(llvm::byteswap(llvm::to_underlying(C)));
}

/// Reverse the byte order of \p Value in place.
/// \param Value Value to byte-swap in place.
template<typename T>
inline void swapByteOrder(T &Value) {
  Value = getSwappedBytes(Value);
}

} // end namespace sys
} // end namespace llvm

#endif
