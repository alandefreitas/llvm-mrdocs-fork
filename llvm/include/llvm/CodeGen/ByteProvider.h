//===-- include/llvm/CodeGen/ByteProvider.h - Map bytes ---------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// \file
// This file implements ByteProvider. The purpose of ByteProvider is to provide
// a map between a target node's byte (byte position is DestOffset) and the
// source (and byte position) that provides it (in Src and SrcOffset
// respectively) See CodeGen/SelectionDAG/DAGCombiner.cpp MatchLoadCombine
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_BYTEPROVIDER_H
#define LLVM_CODEGEN_BYTEPROVIDER_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/DataTypes.h"
#include <optional>
#include <type_traits>

namespace llvm {

/// Represents the known origin of an individual byte in a combine pattern.
///
/// The value of the byte is either constant zero, or comes from memory /
/// some other productive instruction (e.g. arithmetic instructions).
/// Bit manipulation instructions like shifts are not ByteProviders, rather
/// are used to extract Bytes.
template <typename ISelOp> class ByteProvider {
private:
  ByteProvider(std::optional<ISelOp> Src, int64_t DestOffset, int64_t SrcOffset)
      : Src(Src), DestOffset(DestOffset), SrcOffset(SrcOffset) {}

  // TODO -- use constraint in c++20
  // Does this type correspond with an operation in selection DAG
  // Only allow classes with member function getOpcode
  template <typename U>
  using check_has_getOpcode =
      decltype(std::declval<std::remove_pointer_t<U> &>().getOpcode());

  template <typename U>
  static constexpr bool has_getOpcode =
      is_detected<check_has_getOpcode, U>::value;

public:
  /// Optional source operation that originally produced the relevant bits.
  ///
  /// For constant zero providers \c Src is \c std::nullopt. For actual
  /// providers it is the node which originally produced the relevant bits.
  std::optional<ISelOp> Src = std::nullopt;
  /// Offset of the byte in the destination being mapped.
  int64_t DestOffset = 0;
  /// Offset in the ultimate source node that maps to \c DestOffset.
  int64_t SrcOffset = 0;

  /// Construct an empty ByteProvider with no source.
  ByteProvider() = default;

  /// Create a ByteProvider that maps a destination byte to a source operation.
  ///
  /// \param Val Source operation that produced the byte, or nullopt.
  /// \param ByteOffset Offset of the byte in the destination being mapped.
  /// \param VectorOffset Offset in the source operation that supplies the byte.
  /// \return A ByteProvider that maps the destination byte to \p Val.
  static ByteProvider getSrc(std::optional<ISelOp> Val, int64_t ByteOffset,
                             int64_t VectorOffset) {
    static_assert(has_getOpcode<ISelOp>,
                  "ByteProviders must contain an operation in selection DAG.");
    return ByteProvider(Val, ByteOffset, VectorOffset);
  }

  /// Create a ByteProvider that represents a constant zero byte.
  ///
  /// \return A ByteProvider with no source, representing constant zero.
  static ByteProvider getConstantZero() {
    return ByteProvider<ISelOp>(std::nullopt, 0, 0);
  }
  /// Return true if this provider represents a constant zero byte.
  ///
  /// \return True if this provider has no source operation.
  bool isConstantZero() const { return !Src; }

  /// Return true if this provider has a non-empty source operation.
  ///
  /// \return True if \c Src holds a source operation.
  bool hasSrc() const { return Src.has_value(); }

  /// Return true if this provider and \p Other share the same source.
  ///
  /// \param Other Other ByteProvider to compare sources with.
  /// \return True if both providers have the same source operation.
  bool hasSameSrc(const ByteProvider &Other) const { return Other.Src == Src; }

  /// Return true if this provider equals \p Other in source and offsets.
  ///
  /// \param Other Other ByteProvider to compare against.
  /// \return True if source and both offsets match \p Other.
  bool operator==(const ByteProvider &Other) const {
    return Other.Src == Src && Other.DestOffset == DestOffset &&
           Other.SrcOffset == SrcOffset;
  }
};
} // end namespace llvm

#endif // LLVM_CODEGEN_BYTEPROVIDER_H
