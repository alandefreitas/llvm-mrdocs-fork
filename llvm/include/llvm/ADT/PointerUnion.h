//===- llvm/ADT/PointerUnion.h - Pointer Type Union -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the PointerUnion class, which is a discriminated union of
/// pointer types.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POINTERUNION_H
#define LLVM_ADT_POINTERUNION_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace llvm {

/// Implementation details for PointerUnion tag packing.
namespace pointer_union_detail {

/// Determine the number of bits required to store values in [0, NumValues).
///
/// This is ceil(log2(NumValues)).
/// \param NumValues Exclusive upper bound of the value range.
/// \return Number of bits needed to represent values in [0, NumValues).
constexpr int bitsRequired(unsigned NumValues) {
  return NumValues == 0 ? 0 : llvm::bit_width_constexpr(NumValues - 1);
}

/// Return the minimum number of low bits available across pointer types \p Ts.
/// \return Smallest NumLowBitsAvailable among \p Ts.
template <typename... Ts> constexpr int lowBitsAvailable() {
  return std::min(
      {static_cast<int>(PointerLikeTypeTraits<Ts>::NumLowBitsAvailable)...});
}

/// True if all types have enough low bits for a fixed-width tag.
/// \return True if lowBitsAvailable is at least bitsRequired for \p PTs.
template <typename... PTs> constexpr bool useFixedWidthTags() {
  return lowBitsAvailable<PTs...>() >= bitsRequired(sizeof...(PTs));
}

/// True if types are in non-decreasing NumLowBitsAvailable order.
/// \return True when each type has at least as many low bits as the previous.
// TODO: Switch to llvm::is_sorted when it becomes constexpr.
template <typename... PTs> constexpr bool typesInNonDecreasingBitOrder() {
  int Bits[] = {PointerLikeTypeTraits<PTs>::NumLowBitsAvailable...};
  for (size_t I = 1; I < sizeof...(PTs); ++I)
    if (Bits[I] < Bits[I - 1])
      return false;
  return true;
}

/// Tag descriptor for one type in the union.
struct TagEntry {
  /// Bit pattern stored in the low bits identifying this type.
  uintptr_t Value;
  /// Mask covering all tag bits for this entry.
  uintptr_t Mask;
};

/// Compute a fixed-width tag table for the pointer types.
///
/// All types have enough bits for the tag. For example, with 4 types and 3
/// available bits, the tag is 2 bits wide (values 0-3) and each entry has the
/// same mask of 0x3.
/// \return Array of TagEntry values and masks, one per type in \p PTs.
template <typename... PTs>
constexpr std::array<TagEntry, sizeof...(PTs)> computeFixedTags() {
  constexpr size_t N = sizeof...(PTs);
  constexpr uintptr_t TagMask = (uintptr_t(1) << bitsRequired(N)) - 1;
  std::array<TagEntry, N> Result = {};
  for (size_t I = 0; I < N; ++I) {
    Result[I].Value = uintptr_t(I);
    Result[I].Mask = TagMask;
  }
  return Result;
}

/// Compute a variable-width tag table, or nullopt if types do not fit.
///
/// Types must be in non-decreasing NumLowBitsAvailable order. Groups types by
/// available bits into tiers; each non-final tier reserves its highest code as
/// an escape prefix.
///
/// Example with 3 tiers (2-bit, 3-bit, 5-bit types):
///   Tier 0 (2 bits): codes 0b00, 0b01, 0b10; escape = 0b11
///   Tier 1 (3 bits): codes 0b011, escape = 0b111
///   Tier 2 (5 bits): codes 0b00111, 0b01111, 0b10111, 0b11111
/// \return Tag table for \p PTs, or nullopt if encoding does not fit.
template <typename... PTs>
constexpr std::optional<std::array<TagEntry, sizeof...(PTs)>>
computeExtendedTags() {
  constexpr size_t N = sizeof...(PTs);
  std::array<TagEntry, N> Result = {};
  int Bits[] = {PointerLikeTypeTraits<PTs>::NumLowBitsAvailable...};
  uintptr_t EscapePrefix = 0;
  int PrevBits = 0;
  size_t I = 0;
  // Walk tiers (groups of types with the same NumLowBitsAvailable). For each
  // tier, assign tag values using the new bits introduced by this tier,
  // prefixed by the accumulated escape codes from previous tiers. Non-final
  // tiers reserve their highest code as an escape to the next tier.
  while (I < N) {
    int TierBits = Bits[I];
    if (TierBits < PrevBits)
      return std::nullopt;
    int NewBits = TierBits - PrevBits;
    size_t TierEnd = I;
    while (TierEnd < N && Bits[TierEnd] == TierBits)
      ++TierEnd;
    bool IsLastTier = (TierEnd == N);
    size_t TypesInTier = TierEnd - I;
    size_t Capacity =
        IsLastTier ? (size_t(1) << NewBits) : ((size_t(1) << NewBits) - 1);
    if (TypesInTier > Capacity)
      return std::nullopt;
    for (size_t J = 0; J < TypesInTier; ++J) {
      Result[I + J].Value = EscapePrefix | (uintptr_t(J) << PrevBits);
      Result[I + J].Mask = (uintptr_t(1) << TierBits) - 1;
    }
    uintptr_t EscapeCode = (uintptr_t(1) << NewBits) - 1;
    EscapePrefix |= EscapeCode << PrevBits;
    PrevBits = TierBits;
    I = TierEnd;
  }
  return Result;
}

/// CRTP base generating per-type constructors and assignment operators.
///
/// Non-template constructors allow implicit conversions (derived-to-base,
/// non-const-to-const).
template <typename Derived, int Idx, typename... Types>
class PointerUnionMembers;

/// Terminal CRTP specialization when no alternative types remain.
template <typename Derived, int Idx> class PointerUnionMembers<Derived, Idx> {
protected:
  /// Packed pointer bits and discriminator tag.
  detail::PunnedPointer<void *> Val;
  /// Construct a null union with tag value zero.
  PointerUnionMembers() : Val(uintptr_t(0)) {}

  template <typename To, typename From, typename Enable>
  friend struct ::llvm::CastInfo;
  template <typename> friend struct ::llvm::PointerLikeTypeTraits;
};

/// Recursive CRTP specialization for alternative type \c Type at index \c Idx.
template <typename Derived, int Idx, typename Type, typename... Types>
class PointerUnionMembers<Derived, Idx, Type, Types...>
    : public PointerUnionMembers<Derived, Idx + 1, Types...> {
  using Base = PointerUnionMembers<Derived, Idx + 1, Types...>;

public:
  /// Inherit constructors for the remaining alternative types.
  using Base::Base;
  /// Construct a null union with tag value zero.
  PointerUnionMembers() = default;

  /// Construct holding pointer \p V of this alternative type.
  /// \param V Pointer value to store for this alternative.
  PointerUnionMembers(Type V) { this->Val = Derived::encode(V); }

  /// Inherit assignment operators for the remaining alternative types.
  using Base::operator=;
  /// Assign pointer \p V as this alternative type.
  /// \param V Pointer value to store for this alternative.
  /// \return Reference to the derived PointerUnion.
  Derived &operator=(Type V) {
    this->Val = Derived::encode(V);
    return static_cast<Derived &>(*this);
  }
};

} // end namespace pointer_union_detail

/// A discriminated union of two or more pointer types, with the discriminator
/// in the low bits of the pointer.
///
/// This implementation is extremely efficient in space due to leveraging the
/// low bits of the pointer, while exposing a natural and type-safe API.
///
/// When all types have enough alignment for a fixed-width tag,
/// the tag is placed in the high end of the available low bits, leaving spare
/// low bits for nesting in PointerIntPair or SmallPtrSet. When types have
/// heterogeneous alignment, a variable-length escape-encoded tag
/// is used; in that case, types must be listed in non-decreasing
/// NumLowBitsAvailable order.
///
/// Common use patterns would be something like this:
///    PointerUnion<int*, float*> P;
///    P = (int*)0;
///    printf("%d %d", P.is<int*>(), P.is<float*>());  // prints "1 0"
///    X = P.get<int*>();     // ok.
///    Y = P.get<float*>();   // runtime assertion failure.
///    Z = P.get<double*>();  // compile time failure.
///    P = (float*)0;
///    Y = P.get<float*>();   // ok.
///    X = P.get<int*>();     // runtime assertion failure.
///    PointerUnion<int*, int*> Q; // compile time failure.
template <typename... PTs>
class PointerUnion
    : public pointer_union_detail::PointerUnionMembers<PointerUnion<PTs...>, 0,
                                                       PTs...> {
  static_assert(sizeof...(PTs) > 0, "PointerUnion must have at least one type");
  static_assert(TypesAreDistinct<PTs...>::value,
                "PointerUnion alternative types cannot be repeated");

  using Base = typename PointerUnion::PointerUnionMembers;
  using First = TypeAtIndex<0, PTs...>;

  template <typename, int, typename...>
  friend class pointer_union_detail::PointerUnionMembers;
  template <typename To, typename From, typename Enable> friend struct CastInfo;
  template <typename> friend struct PointerLikeTypeTraits;

  // These are constexpr functions rather than static constexpr data members
  // so that alignof() on potentially incomplete types is not evaluated at
  // class-definition time.

  static constexpr bool useFixedWidthTags() {
    return pointer_union_detail::useFixedWidthTags<PTs...>();
  }

  static constexpr int minLowBitsAvailable() {
    return pointer_union_detail::lowBitsAvailable<PTs...>();
  }

  static constexpr int tagBits() {
    return pointer_union_detail::bitsRequired(sizeof...(PTs));
  }

  /// When using fixed-width tags, the tag is shifted to the high end of the
  /// available low bits so that the lowest bits remain free for nesting. With
  /// variable-width encoding mode, the tag starts at bit 0.
  static constexpr int tagShift() {
    return useFixedWidthTags() ? (minLowBitsAvailable() - tagBits()) : 0;
  }

  using TagTable = std::array<pointer_union_detail::TagEntry, sizeof...(PTs)>;

  /// Returns the tag lookup table for this union's encoding scheme.
  static constexpr TagTable getTagTable() {
    if constexpr (useFixedWidthTags()) {
      return pointer_union_detail::computeFixedTags<PTs...>();
    } else {
      static_assert(
          pointer_union_detail::typesInNonDecreasingBitOrder<PTs...>(),
          "Variable-width PointerUnion types must be in non-decreasing "
          "NumLowBitsAvailable order");
      constexpr auto Table =
          pointer_union_detail::computeExtendedTags<PTs...>();
      static_assert(Table.has_value(),
                    "Too many types for the available low bits");
      return *Table;
    }
  }

  // Variable-width isNull: check membership in the sparse set of tag values.
  // A single threshold comparison does not work here because lower-tier
  // non-null pointers can encode to values below higher-tier thresholds.
  template <size_t... Is>
  static constexpr bool isNullVariableImpl(uintptr_t V,
                                           std::index_sequence<Is...>) {
    constexpr TagTable Table = getTagTable();
    static_assert(tagShift() == 0,
                  "isNullVariableImpl assumes tag starts at bit 0");
    return ((V == Table[Is].Value) || ...);
  }

  template <typename T> static uintptr_t encode(T V) {
    constexpr TagTable Table = getTagTable();
    constexpr int Shift = tagShift();
    constexpr size_t Idx = FirstIndexOfType<T, PTs...>::value;
    static_assert(Table[0].Value == 0,
                  "First type must have tag value 0 for getAddrOfPtr1");
    uintptr_t PtrInt = reinterpret_cast<uintptr_t>(
        PointerLikeTypeTraits<T>::getAsVoidPointer(V));
    assert((PtrInt & (Table[Idx].Mask << Shift)) == 0 &&
           "Pointer low bits collide with tag");
    return PtrInt | (Table[Idx].Value << Shift);
  }

public:
  /// Construct a null PointerUnion holding the first alternative type.
  PointerUnion() = default;
  /// Construct a null PointerUnion from nullptr.
  /// \param Null Unused nullptr literal used to select this overload.
  PointerUnion(std::nullptr_t Null) : PointerUnion() {}
  /// Inherit constructors for each alternative pointer type.
  using Base::Base;
  /// Inherit assignment operators for each alternative pointer type.
  using Base::operator=;

  /// Assignment from nullptr clears the union, resetting to the first type.
  /// \param Null Unused nullptr literal used to select this overload.
  /// \return Const reference to this PointerUnion.
  const PointerUnion &operator=(std::nullptr_t Null) {
    this->Val = uintptr_t(0);
    return *this;
  }

  /// Test if the pointer held in the union is null, regardless of
  /// which type it is.
  /// \return True if the held pointer is null.
  bool isNull() const {
    if constexpr (useFixedWidthTags()) {
      return (static_cast<uintptr_t>(this->Val.asInt()) >>
              minLowBitsAvailable()) == 0;
    } else {
      return isNullVariableImpl(static_cast<uintptr_t>(this->Val.asInt()),
                                std::index_sequence_for<PTs...>{});
    }
  }

  /// Return true if the held pointer is non-null.
  /// \return True if the held pointer is non-null.
  explicit operator bool() const { return !isNull(); }

  /// Returns the current pointer if it is of the specified pointer type,
  /// otherwise returns null.
  /// \return The held pointer as \p T, or null if the type does not match.
  template <typename T> inline T dyn_cast() const {
    return llvm::dyn_cast_if_present<T>(*this);
  }

  /// If the union is set to the first pointer type get an address pointing to
  /// it.
  /// \return Const pointer to the first alternative when that type is active.
  First const *getAddrOfPtr1() const {
    return const_cast<PointerUnion *>(this)->getAddrOfPtr1();
  }

  /// If the union is set to the first pointer type get an address pointing to
  /// it.
  /// \return Pointer to the first alternative when that type is active.
  First *getAddrOfPtr1() {
    static_assert(FirstIndexOfType<First, PTs...>::value == 0,
                  "First type must have tag value 0 for getAddrOfPtr1");
    assert(isa<First>(*this) && "Val is not the first pointer");
    // tag == 0 for first type, so asInt() is the raw pointer value.
    assert(
        PointerLikeTypeTraits<First>::getAsVoidPointer(cast<First>(*this)) ==
            reinterpret_cast<void *>(this->Val.asInt()) &&
        "Can't get the address because PointerLikeTypeTraits changes the ptr");
    return const_cast<First *>(
        reinterpret_cast<const First *>(this->Val.getPointerAddress()));
  }

  /// Return the packed pointer and tag bits as an opaque void pointer.
  /// \return Opaque void pointer encoding the packed bits.
  void *getOpaqueValue() const {
    return reinterpret_cast<void *>(this->Val.asInt());
  }

  /// Reconstruct a PointerUnion from opaque void pointer \p VP.
  /// \param VP Opaque value previously returned by getOpaqueValue.
  /// \return PointerUnion whose packed bits match \p VP.
  static inline PointerUnion getFromOpaqueValue(void *VP) {
    PointerUnion V;
    V.Val = reinterpret_cast<intptr_t>(VP);
    return V;
  }

  /// Compare two unions for equality of their opaque bit patterns.
  /// \param lhs Left-hand PointerUnion.
  /// \param rhs Right-hand PointerUnion.
  /// \return True if \p lhs and \p rhs have the same opaque bits.
  friend bool operator==(PointerUnion lhs, PointerUnion rhs) {
    return lhs.getOpaqueValue() == rhs.getOpaqueValue();
  }

  /// Compare two unions for inequality of their opaque bit patterns.
  /// \param lhs Left-hand PointerUnion.
  /// \param rhs Right-hand PointerUnion.
  /// \return True if \p lhs and \p rhs differ in opaque bits.
  friend bool operator!=(PointerUnion lhs, PointerUnion rhs) {
    return lhs.getOpaqueValue() != rhs.getOpaqueValue();
  }

  /// Order unions by their opaque bit patterns.
  /// \param lhs Left-hand PointerUnion.
  /// \param rhs Right-hand PointerUnion.
  /// \return True if \p lhs's opaque bits are less than \p rhs's.
  friend bool operator<(PointerUnion lhs, PointerUnion rhs) {
    return lhs.getOpaqueValue() < rhs.getOpaqueValue();
  }
};

/// CastInfo specialization for non-const PointerUnion sources.
template <typename To, typename... PTs>
struct CastInfo<To, PointerUnion<PTs...>>
    : public DefaultDoCastIfPossible<To, PointerUnion<PTs...>,
                                     CastInfo<To, PointerUnion<PTs...>>> {
  /// Source PointerUnion type for this cast specialization.
  using From = PointerUnion<PTs...>;

  /// Return true if \p F currently holds a pointer of type \c To.
  /// \param F PointerUnion to test.
  /// \return True if \p F's tag matches type \c To.
  static inline bool isPossible(From &F) {
    constexpr std::array<pointer_union_detail::TagEntry, sizeof...(PTs)> Table =
        From::getTagTable();
    constexpr int Shift = From::tagShift();
    constexpr size_t Idx = FirstIndexOfType<To, PTs...>::value;
    auto V = reinterpret_cast<uintptr_t>(F.getOpaqueValue());
    constexpr uintptr_t TagMask = Table[Idx].Mask << Shift;
    constexpr uintptr_t TagValue = Table[Idx].Value << Shift;
    return (V & TagMask) == TagValue;
  }

  /// Extract the \c To pointer from \p F; asserts the tag matches.
  /// \param F PointerUnion known to hold type \c To.
  /// \return The held pointer converted to type \c To.
  static To doCast(From &F) {
    assert(isPossible(F) && "cast to an incompatible type!");
    constexpr std::array<pointer_union_detail::TagEntry, sizeof...(PTs)> Table =
        From::getTagTable();
    constexpr int Shift = From::tagShift();
    constexpr size_t Idx = FirstIndexOfType<To, PTs...>::value;
    constexpr uintptr_t PtrMask = ~(uintptr_t(Table[Idx].Mask) << Shift);
    void *Ptr = reinterpret_cast<void *>(
        reinterpret_cast<uintptr_t>(F.getOpaqueValue()) & PtrMask);
    return PointerLikeTypeTraits<To>::getFromVoidPointer(Ptr);
  }

  /// Return a null \c To value when a cast fails.
  /// \return A default-constructed null \c To.
  static inline To castFailed() { return To(); }
};

/// CastInfo specialization for const PointerUnion sources.
template <typename To, typename... PTs>
struct CastInfo<To, const PointerUnion<PTs...>>
    : public ConstStrippingForwardingCast<To, const PointerUnion<PTs...>,
                                          CastInfo<To, PointerUnion<PTs...>>> {
};

/// PointerLikeTypeTraits specialization treating PointerUnion as a pointer.
///
/// Spare low bits below the tag are available for nesting. This specialization
/// is only instantiated when used (lazy), so PointerLikeTypeTraits<PTs> /
/// alignof() are not evaluated for incomplete types.
template <typename... PTs> struct PointerLikeTypeTraits<PointerUnion<PTs...>> {
  /// PointerUnion type this traits specialization describes.
  using Union = PointerUnion<PTs...>;

  /// Return the opaque packed bits of \p P as a void pointer.
  /// \param P PointerUnion to convert.
  /// \return Opaque void pointer encoding \p P's packed bits.
  static inline void *getAsVoidPointer(const Union &P) {
    return P.getOpaqueValue();
  }

  /// Reconstruct a PointerUnion from opaque void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return PointerUnion whose packed bits match \p P.
  static inline Union getFromVoidPointer(void *P) {
    return Union::getFromOpaqueValue(P);
  }

  /// Number of spare low bits below the tag available for nesting.
  static constexpr int NumLowBitsAvailable = Union::tagShift();
};

/// DenseMapInfo specialization so PointerUnion can be used as a DenseMap key.
template <typename... PTs> struct DenseMapInfo<PointerUnion<PTs...>> {
  /// PointerUnion type this DenseMapInfo specialization describes.
  using Union = PointerUnion<PTs...>;

  /// Hash \p UnionVal by its opaque packed bit pattern.
  /// \param UnionVal Key value to hash.
  /// \return Hash of \p UnionVal's opaque packed bits.
  static unsigned getHashValue(const Union &UnionVal) {
    auto Key = reinterpret_cast<uintptr_t>(UnionVal.getOpaqueValue());
    return DenseMapInfo<uintptr_t>::getHashValue(Key);
  }

  /// Return true if \p LHS and \p RHS have equal opaque bit patterns.
  /// \param LHS Left-hand key.
  /// \param RHS Right-hand key.
  /// \return True if \p LHS and \p RHS compare equal.
  static bool isEqual(const Union &LHS, const Union &RHS) {
    return LHS == RHS;
  }
};

} // end namespace llvm

#endif // LLVM_ADT_POINTERUNION_H
