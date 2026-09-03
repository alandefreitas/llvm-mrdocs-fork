//===- llvm/ADT/DenseMapInfo.h - Type traits for DenseMap -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines DenseMapInfo traits for DenseMap.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_DENSEMAPINFO_H
#define LLVM_ADT_DENSEMAPINFO_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace llvm {

/// DenseMap-specific hashing helpers.
namespace densemap {
/// Internal bit-mixing utilities for DenseMap hashes.
namespace detail {
// A bit mixer with very low latency using one multiplications and one
// xor-shift. The constant is from splitmix64.
/// Mix bits of \p x for DenseMap hashing.
inline uint64_t mix(uint64_t x) {
  x *= 0xbf58476d1ce4e5b9u;
  x ^= x >> 31;
  return x;
}
} // namespace detail
} // namespace densemap

namespace detail {

/// Simplistic combination of 32-bit hash values into 32-bit hash values.
inline unsigned combineHashValue(unsigned a, unsigned b) {
  uint64_t x = (uint64_t)a << 32 | (uint64_t)b;
  return (unsigned)densemap::detail::mix(x);
}

} // end namespace detail

/// Traits type that supplies DenseMap hashing and equality for value type \p T.
///
/// An information struct used to provide DenseMap with the various necessary
/// components for a given value type `T`. `Enable` is an optional additional
/// parameter that is used to support SFINAE (generally using std::enable_if_t)
/// in derived DenseMapInfo specializations; in non-SFINAE use cases this should
/// just be `void`.
template <typename T, typename Enable = void> struct DenseMapInfo {
  // static unsigned getHashValue(const T &Val);
  // static bool isEqual(const T &LHS, const T &RHS);
};

/// DenseMapInfo specialization for pointer keys.
///
/// Avoid requiring \p T to be complete so clients can instantiate
/// DenseMap<T*, ...> with forward-declared key types.
template <typename T> struct DenseMapInfo<T *> {
  /// Compute a hash value for pointer \p PtrVal.
  /// \param PtrVal Pointer key to hash.
  /// @return Hash of the pointer's address.
  static unsigned getHashValue(const T *PtrVal) {
    return densemap::detail::mix(reinterpret_cast<uintptr_t>(PtrVal));
  }

  /// Return true if \p LHS and \p RHS are the same pointer.
  /// \param LHS Left-hand pointer.
  /// \param RHS Right-hand pointer.
  /// @return True if the pointers are equal.
  static bool isEqual(const T *LHS, const T *RHS) { return LHS == RHS; }
};

/// DenseMapInfo specialization for integral key types.
template <typename T>
struct DenseMapInfo<T, std::enable_if_t<std::is_integral_v<T>>> {
  /// Compute a hash value for integral key \p Val.
  /// \param Val Integral key to hash.
  /// @return Hash of the integral value.
  static unsigned getHashValue(const T &Val) {
    if constexpr (std::is_unsigned_v<T> && sizeof(T) > sizeof(unsigned))
      return densemap::detail::mix(Val);
    else
      return static_cast<unsigned>(Val *
                                   static_cast<std::make_unsigned_t<T>>(37U));
  }

  /// Return true if \p LHS and \p RHS compare equal.
  /// \param LHS Left-hand value.
  /// \param RHS Right-hand value.
  /// @return True if the values are equal.
  static bool isEqual(const T &LHS, const T &RHS) { return LHS == RHS; }
};

/// DenseMapInfo specialization for pairs whose members have DenseMapInfo.
template<typename T, typename U>
struct DenseMapInfo<std::pair<T, U>> {
  /// Pair type this DenseMapInfo specialization describes.
  using Pair = std::pair<T, U>;
  /// DenseMapInfo for the pair's first member type.
  using FirstInfo = DenseMapInfo<T>;
  /// DenseMapInfo for the pair's second member type.
  using SecondInfo = DenseMapInfo<U>;

  /// Compute a hash value for pair \p PairVal.
  /// \param PairVal Pair key to hash.
  /// @return Combined hash of both pair members.
  static unsigned getHashValue(const Pair& PairVal) {
    return detail::combineHashValue(FirstInfo::getHashValue(PairVal.first),
                                    SecondInfo::getHashValue(PairVal.second));
  }

  /// Combine hashes of \p First and \p Second without building a pair.
  ///
  /// Intended for use by other DenseMapInfo specializations that need to
  /// combine member hashes without knowing how to do so manually.
  /// \param First First member value.
  /// \param Second Second member value.
  /// @return Combined hash of the two member values.
  static unsigned getHashValuePiecewise(const T &First, const U &Second) {
    return detail::combineHashValue(FirstInfo::getHashValue(First),
                                    SecondInfo::getHashValue(Second));
  }

  /// Return true if \p LHS and \p RHS compare equal element-wise.
  /// \param LHS Left-hand pair.
  /// \param RHS Right-hand pair.
  /// @return True if both members compare equal.
  static bool isEqual(const Pair &LHS, const Pair &RHS) {
    return FirstInfo::isEqual(LHS.first, RHS.first) &&
           SecondInfo::isEqual(LHS.second, RHS.second);
  }
};

/// DenseMapInfo specialization for tuples whose members have DenseMapInfo.
template <typename... Ts> struct DenseMapInfo<std::tuple<Ts...>> {
  /// Tuple type this DenseMapInfo specialization describes.
  using Tuple = std::tuple<Ts...>;

  /// Recursively combine hash values of tuple elements starting at index \p I.
  /// \param values Tuple whose elements are hashed.
  /// @return Combined hash of elements from index \p I onward.
  template <unsigned I> static unsigned getHashValueImpl(const Tuple &values) {
    if constexpr (I == sizeof...(Ts)) {
      return 0;
    } else {
      using EltType = std::tuple_element_t<I, Tuple>;
      return detail::combineHashValue(
          DenseMapInfo<EltType>::getHashValue(std::get<I>(values)),
          getHashValueImpl<I + 1>(values));
    }
  }

  /// Compute a hash value for tuple \p values.
  /// \param values Tuple key to hash.
  /// @return Combined hash of all tuple elements.
  static unsigned getHashValue(const std::tuple<Ts...> &values) {
    return getHashValueImpl<0>(values);
  }

  /// Compare tuple elements selected by the index sequence for equality.
  /// \param lhs Left-hand tuple.
  /// \param rhs Right-hand tuple.
  /// \param Indices Index sequence selecting which elements to compare.
  /// @return True if all selected elements compare equal.
  template <std::size_t... Is>
  static bool isEqualImpl(const Tuple &lhs, const Tuple &rhs,
                          [[maybe_unused]] std::index_sequence<Is...> Indices) {
    return (DenseMapInfo<std::tuple_element_t<Is, Tuple>>::isEqual(
                std::get<Is>(lhs), std::get<Is>(rhs)) &&
            ...);
  }

  /// Return true if \p lhs and \p rhs compare equal element-wise.
  /// \param lhs Left-hand tuple.
  /// \param rhs Right-hand tuple.
  /// @return True if all elements compare equal.
  static bool isEqual(const Tuple &lhs, const Tuple &rhs) {
    return isEqualImpl(lhs, rhs, std::index_sequence_for<Ts...>{});
  }
};

/// DenseMapInfo specialization for enumeration key types.
template <typename Enum>
struct DenseMapInfo<Enum, std::enable_if_t<std::is_enum_v<Enum>>> {
  /// Underlying integer type of the enumeration.
  using UnderlyingType = std::underlying_type_t<Enum>;
  /// DenseMapInfo for the enum's underlying integer type.
  using Info = DenseMapInfo<UnderlyingType>;

  /// Compute a hash value for enum key \p Val via its underlying type.
  /// \param Val Enumeration key to hash.
  /// @return Hash of the enumerator's underlying integer value.
  static unsigned getHashValue(const Enum &Val) {
    return Info::getHashValue(static_cast<UnderlyingType>(Val));
  }

  /// Return true if \p LHS and \p RHS compare equal.
  /// \param LHS Left-hand enumerator.
  /// \param RHS Right-hand enumerator.
  /// @return True if the enumerators are equal.
  static bool isEqual(const Enum &LHS, const Enum &RHS) { return LHS == RHS; }
};

/// DenseMapInfo specialization for optional keys whose value type has info.
template <typename T> struct DenseMapInfo<std::optional<T>> {
  /// Optional type this DenseMapInfo specialization describes.
  using Optional = std::optional<T>;
  /// DenseMapInfo for the optional's value type.
  using Info = DenseMapInfo<T>;

  /// Compute a hash value for optional key \p OptionalVal.
  /// \param OptionalVal Optional key to hash; empty optionals hash as 0.
  /// @return Hash of the engaged value, or 0 if empty.
  static unsigned getHashValue(const Optional &OptionalVal) {
    if (OptionalVal)
      return detail::combineHashValue(1, Info::getHashValue(*OptionalVal));
    return 0;
  }

  /// Return true if \p LHS and \p RHS are both empty or hold equal values.
  /// \param LHS Left-hand optional.
  /// \param RHS Right-hand optional.
  /// @return True if both are empty or hold equal values.
  static bool isEqual(const Optional &LHS, const Optional &RHS) {
    if (LHS && RHS) {
      return Info::isEqual(LHS.value(), RHS.value());
    }
    return !LHS && !RHS;
  }
};
} // end namespace llvm

#endif // LLVM_ADT_DENSEMAPINFO_H
