//===- llvm/ADT/PointerIntPair.h - Pair for pointer and int -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the PointerIntPair class.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_POINTERINTPAIR_H
#define LLVM_ADT_POINTERINTPAIR_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/PointerLikeTypeTraits.h"
#include "llvm/Support/type_traits.h"
#include <cassert>
#include <cstdint>
#include <cstring>
#include <limits>

namespace llvm {

namespace detail {
template <typename Ptr> struct PunnedPointer {
  static_assert(sizeof(Ptr) == sizeof(intptr_t), "");

  // Asserts that allow us to let the compiler implement the destructor and
  // copy/move constructors
  static_assert(std::is_trivially_destructible<Ptr>::value, "");
  static_assert(std::is_trivially_copy_constructible<Ptr>::value, "");
  static_assert(std::is_trivially_move_constructible<Ptr>::value, "");

  explicit constexpr PunnedPointer(intptr_t i = 0) { *this = i; }

  constexpr intptr_t asInt() const {
    intptr_t R = 0;
    std::memcpy(&R, Data, sizeof(R));
    return R;
  }

  constexpr operator intptr_t() const { return asInt(); }

  constexpr PunnedPointer &operator=(intptr_t V) {
    std::memcpy(Data, &V, sizeof(Data));
    return *this;
  }

  Ptr *getPointerAddress() { return reinterpret_cast<Ptr *>(Data); }
  const Ptr *getPointerAddress() const { return reinterpret_cast<Ptr *>(Data); }

private:
  alignas(Ptr) unsigned char Data[sizeof(Ptr)];
};
} // namespace detail

template <typename T, typename Enable> struct DenseMapInfo;
/// Helpers that pack and unpack pointer/integer bits for PointerIntPair. @seebelow
template <typename PointerT, unsigned IntBits, typename PtrTraits>
struct PointerIntPairInfo;

/// A pointer and a small integer packed into the space of one pointer.
///
/// This class implements a pair of a pointer and small integer.  It is designed
/// to represent this in the space required by one pointer by bitmangling the
/// integer into the low part of the pointer.  This can only be done for small
/// integers: typically up to 3 bits, but it depends on the number of bits
/// available according to PointerLikeTypeTraits for the type.
///
/// Note that PointerIntPair always puts the IntVal part in the highest bits
/// possible.  For example, PointerIntPair<void*, 1, bool> will put the bit for
/// the bool into bit #2, not bit #0, which allows the low two bits to be used
/// for something else.  For example, this allows:
///   PointerIntPair<PointerIntPair<void*, 1, bool>, 1, bool>
/// ... and the two bools will land in different bits.
template <typename PointerTy, unsigned IntBits, typename IntType = unsigned,
          typename PtrTraits = PointerLikeTypeTraits<PointerTy>,
          typename Info = PointerIntPairInfo<PointerTy, IntBits, PtrTraits>>
class PointerIntPair {
  // Used by MSVC visualizer and generally helpful for debugging/visualizing.
  using InfoTy = Info;
  detail::PunnedPointer<PointerTy> Value;

public:
  /// Construct a null pointer with integer zero.
  constexpr PointerIntPair() = default;

  /// Construct from a pointer and an integer packed into one word.
  /// \param PtrVal Pointer to store.
  /// \param IntVal Integer to pack into the spare bits.
  PointerIntPair(PointerTy PtrVal, IntType IntVal) {
    setPointerAndInt(PtrVal, IntVal);
  }

  /// Construct from a pointer with the integer field cleared.
  /// \param PtrVal Pointer to store.
  explicit PointerIntPair(PointerTy PtrVal) { initWithPointer(PtrVal); }

  /// Return the pointer portion of the pair.
  /// \return The stored pointer value.
  PointerTy getPointer() const { return Info::getPointer(Value); }

  /// Return the integer portion of the pair.
  /// \return The stored integer value.
  IntType getInt() const { return (IntType)Info::getInt(Value); }

  /// Replace the pointer portion, preserving the integer bits.
  /// \param PtrVal New pointer value.
  void setPointer(PointerTy PtrVal) & {
    Value = Info::updatePointer(Value, PtrVal);
  }

  /// Replace the integer portion, preserving the pointer bits.
  /// \param IntVal New integer value.
  void setInt(IntType IntVal) & {
    Value = Info::updateInt(Value, static_cast<intptr_t>(IntVal));
  }

  /// Initialize with \p PtrVal and clear the integer field.
  /// \param PtrVal Pointer to store.
  void initWithPointer(PointerTy PtrVal) & {
    Value = Info::updatePointer(0, PtrVal);
  }

  /// Pack both \p PtrVal and \p IntVal into the stored bit pattern.
  /// \param PtrVal Pointer to store.
  /// \param IntVal Integer to pack into the spare bits.
  void setPointerAndInt(PointerTy PtrVal, IntType IntVal) & {
    Value = Info::updateInt(Info::updatePointer(0, PtrVal),
                            static_cast<intptr_t>(IntVal));
  }

  /// Return the address of the stored pointer when the integer bits are clear.
  /// \return Address of the stored pointer value.
  PointerTy const *getAddrOfPointer() const {
    return const_cast<PointerIntPair *>(this)->getAddrOfPointer();
  }

  /// Return the address of the stored pointer when the integer bits are clear.
  /// \return Address of the stored pointer value.
  PointerTy *getAddrOfPointer() {
    assert(Value == reinterpret_cast<intptr_t>(getPointer()) &&
           "Can only return the address if IntBits is cleared and "
           "PtrTraits doesn't change the pointer");
    return Value.getPointerAddress();
  }

  /// Return the packed bits as an opaque void pointer.
  /// \return Opaque void pointer representing the packed bits.
  void *getOpaqueValue() const {
    return reinterpret_cast<void *>(Value.asInt());
  }

  /// Replace the packed bits with those from opaque pointer \p Val.
  /// \param Val Opaque value previously returned by getOpaqueValue.
  void setFromOpaqueValue(void *Val) & {
    Value = reinterpret_cast<intptr_t>(Val);
  }

  /// Reconstruct a pair from an opaque void pointer value.
  /// \param V Opaque value previously returned by getOpaqueValue.
  /// \return PointerIntPair reconstructed from \p V.
  static PointerIntPair getFromOpaqueValue(void *V) {
    PointerIntPair P;
    P.setFromOpaqueValue(V);
    return P;
  }

  // Allow PointerIntPairs to be created from const void * if and only if the
  // pointer type could be created from a const void *.
  /// Reconstruct a pair from a const opaque void pointer value.
  /// \param V Opaque value previously returned by getOpaqueValue.
  /// \return PointerIntPair reconstructed from \p V.
  static PointerIntPair getFromOpaqueValue(const void *V) {
    (void)PtrTraits::getFromVoidPointer(V);
    return getFromOpaqueValue(const_cast<void *>(V));
  }

  /// Compare two pairs for equality of their packed bits.
  /// \param RHS Right-hand pair.
  /// \return True if both pairs have identical packed bits.
  bool operator==(const PointerIntPair &RHS) const {
    return Value == RHS.Value;
  }

  /// Compare two pairs for inequality of their packed bits.
  /// \param RHS Right-hand pair.
  /// \return True if the packed bits differ.
  bool operator!=(const PointerIntPair &RHS) const {
    return Value != RHS.Value;
  }

  /// Order pairs by their packed bit pattern.
  /// \param RHS Right-hand pair.
  /// \return True if this pair's packed bits are less than \p RHS.
  bool operator<(const PointerIntPair &RHS) const { return Value < RHS.Value; }
  /// Return true if this pair's bits are greater than \p RHS.
  /// \param RHS Right-hand pair.
  /// \return True if this pair's packed bits are greater than \p RHS.
  bool operator>(const PointerIntPair &RHS) const { return Value > RHS.Value; }

  /// Return true if this pair's bits are less than or equal to \p RHS.
  /// \param RHS Right-hand pair.
  /// \return True if this pair's packed bits are <= \p RHS.
  bool operator<=(const PointerIntPair &RHS) const {
    return Value <= RHS.Value;
  }

  /// Return true if this pair's bits are greater than or equal to \p RHS.
  /// \param RHS Right-hand pair.
  /// \return True if this pair's packed bits are >= \p RHS.
  bool operator>=(const PointerIntPair &RHS) const {
    return Value >= RHS.Value;
  }
};

/// Helpers that pack and unpack pointer/integer bits for PointerIntPair.
template <typename PointerT, unsigned IntBits, typename PtrTraits>
struct PointerIntPairInfo {
  static_assert(PtrTraits::NumLowBitsAvailable <
                    std::numeric_limits<uintptr_t>::digits,
                "cannot use a pointer type that has all bits free");
  static_assert(IntBits <= PtrTraits::NumLowBitsAvailable,
                "PointerIntPair with integer size too large for pointer");
  /// Bit masks and shifts used to pack pointer and integer fields.
  enum MaskAndShiftConstants : uintptr_t {
    /// PointerBitMask - The bits that come from the pointer.
    PointerBitMask = (~(uintptr_t)0) << PtrTraits::NumLowBitsAvailable,

    /// IntShift - The number of low bits that we reserve for other uses, and
    /// keep zero.
    IntShift = (uintptr_t)PtrTraits::NumLowBitsAvailable - IntBits,

    /// IntMask - This is the unshifted mask for valid bits of the int type.
    IntMask = ((uintptr_t)1 << IntBits) - 1,

    // ShiftedIntMask - This is the bits for the integer shifted in place.
    ShiftedIntMask = (uintptr_t)(IntMask << IntShift)
  };

  static PointerT getPointer(intptr_t Value) {
    return PtrTraits::getFromVoidPointer(
        reinterpret_cast<void *>(Value & PointerBitMask));
  }

  static intptr_t getInt(intptr_t Value) {
    return (Value >> IntShift) & IntMask;
  }

  /// Replace the pointer bits of \p OrigValue with those of \p Ptr.
  static intptr_t updatePointer(intptr_t OrigValue, PointerT Ptr) {
    intptr_t PtrWord =
        reinterpret_cast<intptr_t>(PtrTraits::getAsVoidPointer(Ptr));
    assert((PtrWord & ~PointerBitMask) == 0 &&
           "Pointer is not sufficiently aligned");
    // Preserve all low bits, just update the pointer.
    return PtrWord | (OrigValue & ~PointerBitMask);
  }

  /// Replace the integer bits of \p OrigValue with \p Int.
  static intptr_t updateInt(intptr_t OrigValue, intptr_t Int) {
    assert((Int & ~IntMask) == 0 && "Integer too large for field");

    // Preserve all bits other than the ones we are updating.
    return (OrigValue & ~ShiftedIntMask) | Int << IntShift;
  }
};

/// DenseMapInfo specialization so PointerIntPair can be a DenseMap key.
template <typename PointerTy, unsigned IntBits, typename IntType>
struct DenseMapInfo<PointerIntPair<PointerTy, IntBits, IntType>, void> {
  /// PointerIntPair type this DenseMapInfo specialization describes.
  using Ty = PointerIntPair<PointerTy, IntBits, IntType>;

  /// Hash \p V by mixing its opaque packed bit pattern.
  /// \param V Key value to hash.
  /// \return Hash value derived from the opaque packed bits.
  static unsigned getHashValue(Ty V) {
    uintptr_t IV = reinterpret_cast<uintptr_t>(V.getOpaqueValue());
    return unsigned(IV) ^ unsigned(IV >> 9);
  }

  /// Return true if \p LHS and \p RHS have equal packed bits.
  /// \param LHS Left-hand key.
  /// \param RHS Right-hand key.
  /// \return True if \p LHS and \p RHS have equal packed bits.
  static bool isEqual(const Ty &LHS, const Ty &RHS) { return LHS == RHS; }
};

/// PointerLikeTypeTraits specialization treating PointerIntPair as a pointer.
///
/// Enables use with SmallPtrSet and nested pointer-like containers. Spare low
/// bits remaining after the packed integer are available for nesting.
template <typename PointerTy, unsigned IntBits, typename IntType,
          typename PtrTraits>
struct PointerLikeTypeTraits<
    PointerIntPair<PointerTy, IntBits, IntType, PtrTraits>> {
  /// Return the packed bits of \p P as a void pointer.
  /// \param P Pair to convert.
  /// \return Opaque void pointer representing the packed bits.
  static inline void *
  getAsVoidPointer(const PointerIntPair<PointerTy, IntBits, IntType> &P) {
    return P.getOpaqueValue();
  }

  /// Reconstruct a PointerIntPair from opaque void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return PointerIntPair reconstructed from \p P.
  static inline PointerIntPair<PointerTy, IntBits, IntType>
  getFromVoidPointer(void *P) {
    return PointerIntPair<PointerTy, IntBits, IntType>::getFromOpaqueValue(P);
  }

  /// Reconstruct a PointerIntPair from opaque const void pointer \p P.
  /// \param P Opaque value previously returned by getAsVoidPointer.
  /// \return PointerIntPair reconstructed from \p P.
  static inline PointerIntPair<PointerTy, IntBits, IntType>
  getFromVoidPointer(const void *P) {
    return PointerIntPair<PointerTy, IntBits, IntType>::getFromOpaqueValue(P);
  }

  /// Number of spare low bits remaining after packing the integer field.
  static constexpr int NumLowBitsAvailable =
      PtrTraits::NumLowBitsAvailable - IntBits;
};

// Allow structured bindings on PointerIntPair.
/// Structured-binding accessor: index 0 is the pointer, index 1 is the int.
/// \param Pair Pair to unpack.
/// \return The pointer when I is 0, otherwise the integer.
template <std::size_t I, typename PointerTy, unsigned IntBits, typename IntType,
          typename PtrTraits, typename Info>
decltype(auto)
get(const PointerIntPair<PointerTy, IntBits, IntType, PtrTraits, Info> &Pair) {
  static_assert(I < 2);
  if constexpr (I == 0)
    return Pair.getPointer();
  else
    return Pair.getInt();
}

} // end namespace llvm

namespace std {
template <typename PointerTy, unsigned IntBits, typename IntType,
          typename PtrTraits, typename Info>
struct tuple_size<
    llvm::PointerIntPair<PointerTy, IntBits, IntType, PtrTraits, Info>>
    : std::integral_constant<std::size_t, 2> {};

template <std::size_t I, typename PointerTy, unsigned IntBits, typename IntType,
          typename PtrTraits, typename Info>
struct tuple_element<
    I, llvm::PointerIntPair<PointerTy, IntBits, IntType, PtrTraits, Info>>
    : std::conditional<I == 0, PointerTy, IntType> {};
} // namespace std

#endif // LLVM_ADT_POINTERINTPAIR_H
