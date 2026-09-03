//===-- llvm/ADT/Bitfield.h - Get and Set bits in an integer ---*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements methods to test, set and extract typed bits from packed
/// unsigned integers.
///
/// Why not C++ bitfields?
/// ----------------------
/// C++ bitfields do not offer control over the bit layout nor consistent
/// behavior when it comes to out of range values.
/// For instance, the layout is implementation defined and adjacent bits may be
/// packed together but are not required to. This is problematic when storage is
/// sparse and data must be stored in a particular integer type.
///
/// The methods provided in this file ensure precise control over the
/// layout/storage as well as protection against out of range values.
///
/// Usage example
/// -------------
/// \code{.cpp}
///  uint8_t Storage = 0;
///
///  // Store and retrieve a single bit as bool.
///  using Bool = Bitfield::Element<bool, 0, 1>;
///  Bitfield::set<Bool>(Storage, true);
///  EXPECT_EQ(Storage, 0b00000001);
///  //                          ^
///  EXPECT_EQ(Bitfield::get<Bool>(Storage), true);
///
///  // Store and retrieve a 2 bit typed enum.
///  // Note: enum underlying type must be unsigned.
///  enum class SuitEnum : uint8_t { CLUBS, DIAMONDS, HEARTS, SPADES };
///  // Note: enum maximum value needs to be passed in as last parameter.
///  using Suit = Bitfield::Element<SuitEnum, 1, 2, SuitEnum::SPADES>;
///  Bitfield::set<Suit>(Storage, SuitEnum::HEARTS);
///  EXPECT_EQ(Storage, 0b00000101);
///  //                        ^^
///  EXPECT_EQ(Bitfield::get<Suit>(Storage), SuitEnum::HEARTS);
///
///  // Store and retrieve a 5 bit value as unsigned.
///  using Value = Bitfield::Element<unsigned, 3, 5>;
///  Bitfield::set<Value>(Storage, 10);
///  EXPECT_EQ(Storage, 0b01010101);
///  //                   ^^^^^
///  EXPECT_EQ(Bitfield::get<Value>(Storage), 10U);
///
///  // Interpret the same 5 bit value as signed.
///  using SignedValue = Bitfield::Element<int, 3, 5>;
///  Bitfield::set<SignedValue>(Storage, -2);
///  EXPECT_EQ(Storage, 0b11110101);
///  //                   ^^^^^
///  EXPECT_EQ(Bitfield::get<SignedValue>(Storage), -2);
///
///  // Ability to efficiently test if a field is non zero.
///  EXPECT_TRUE(Bitfield::test<Value>(Storage));
///
///  // Alter Storage changes value.
///  Storage = 0;
///  EXPECT_EQ(Bitfield::get<Bool>(Storage), false);
///  EXPECT_EQ(Bitfield::get<Suit>(Storage), SuitEnum::CLUBS);
///  EXPECT_EQ(Bitfield::get<Value>(Storage), 0U);
///  EXPECT_EQ(Bitfield::get<SignedValue>(Storage), 0);
///
///  Storage = 255;
///  EXPECT_EQ(Bitfield::get<Bool>(Storage), true);
///  EXPECT_EQ(Bitfield::get<Suit>(Storage), SuitEnum::SPADES);
///  EXPECT_EQ(Bitfield::get<Value>(Storage), 31U);
///  EXPECT_EQ(Bitfield::get<SignedValue>(Storage), -1);
/// \endcode
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_BITFIELDS_H
#define LLVM_ADT_BITFIELDS_H

#include <cassert>
#include <climits> // CHAR_BIT
#include <cstddef> // size_t
#include <cstdint> // uintXX_t
#include <limits>  // numeric_limits
#include <type_traits>

#include "llvm/Support/MathExtras.h"

namespace llvm {

/// Implementation helpers that pack and unpack typed bitfield elements into
/// unsigned storage words.
namespace bitfields_details {

/// Impl is where Bifield description and Storage are put together to interact
/// with values.
template <typename Bitfield, typename StorageType> struct Impl {
  static_assert(std::is_unsigned<StorageType>::value,
                "Storage must be unsigned");
  /// Integer type used to transfer values into and out of the packed field.
  using IntegerType = typename Bitfield::IntegerType;

  /// Number of bits available in \c StorageType.
  static constexpr size_t StorageBits = sizeof(StorageType) * CHAR_BIT;
  static_assert(Bitfield::FirstBit < StorageBits, "Data must fit in mask");
  static_assert(Bitfield::LastBit < StorageBits, "Data must fit in mask");
  /// Mask of the low \c Bitfield::Bits bits before shifting into place.
  static constexpr StorageType LowMask =
      maskTrailingOnes<StorageType>(Bitfield::Bits);
  /// Mask covering the field's bits at their shifted position in storage.
  static constexpr StorageType Mask = LowMask << Bitfield::Shift;

  /// Validates that `UserValue` fits within the bitfield's range.
  ///
  /// \param UserValue    The value to store in the field.
  /// \param UserMaxValue The maximum value the field is allowed to hold.
  static void checkValue(IntegerType UserValue, IntegerType UserMaxValue) {
    assert(UserValue <= UserMaxValue && "value is too big");
    if constexpr (std::is_unsigned_v<IntegerType>) {
      assert(isUInt<Bitfield::Bits>(UserValue) && "value is too big");
    } else {
      static_assert(std::is_signed_v<IntegerType>,
                    "IntegerType must be signed");
      assert(isInt<Bitfield::Bits>(UserValue) && "value is out of range");
    }
  }

  /// Checks `UserValue` is within bounds and packs it between `FirstBit` and
  /// `LastBit` of `Packed` leaving the rest unchanged.
  ///
  /// \param Packed    The storage word to update.
  /// \param UserValue The unpacked value to write into the field.
  static void update(StorageType &Packed, IntegerType UserValue) {
    checkValue(UserValue, Bitfield::UserMaxValue);
    const StorageType StorageValue = UserValue & LowMask;
    Packed &= ~Mask;
    Packed |= StorageValue << Bitfield::Shift;
  }

  /// Interprets bits between `FirstBit` and `LastBit` of `Packed` as
  /// an`IntegerType`.
  ///
  /// \param Packed The storage word to unpack the field from.
  /// \return The unpacked field value as \c IntegerType.
  static IntegerType extract(StorageType Packed) {
    const StorageType StorageValue = (Packed & Mask) >> Bitfield::Shift;
    if constexpr (std::is_signed_v<IntegerType>)
      return SignExtend64<Bitfield::Bits>(StorageValue);
    return StorageValue;
  }

  /// Interprets bits between `FirstBit` and `LastBit` of `Packed` as
  /// an`IntegerType`.
  ///
  /// \param Packed The storage word to test.
  /// \return The masked field bits from \p Packed, or zero if the field is clear.
  static StorageType test(StorageType Packed) { return Packed & Mask; }
};

/// Map a bitfield value type to the integer type used for packing.
///
/// Bitfields accept unsigned enums, signed and unsigned integers, and bool.
/// Internally only integers with well-defined semantics are manipulated; typed
/// enums and bool are replaced with unsigned counterparts. The public API
/// restores the correct type.
template <typename T, bool = std::is_enum<T>::value>
struct ResolveUnderlyingType {
  /// Underlying integer type of enum \c T.
  using type = std::underlying_type_t<T>;
};
/// Maps a non-enum type \c T to the integer type used when packing.
template <typename T> struct ResolveUnderlyingType<T, false> {
  static_assert(!std::is_same_v<T, bool> || sizeof(bool) == 1,
                "T being bool requires sizeof(bool) == 1.");
  /// Integer type used when packing non-enum \c T (uint8_t for bool).
  using type = std::conditional_t<std::is_same_v<T, bool>, uint8_t, T>;
};

} // namespace bitfields_details

/// Holds functions to get, set or test bitfields.
struct Bitfield {
  /// Describes an element of a Bitfield. This type is then used with the
  /// Bitfield static member functions.
  /// \tparam T         The type of the field once in unpacked form.
  /// \tparam Offset    The position of the first bit.
  /// \tparam Size      The size of the field.
  /// \tparam MaxValue  For enums the maximum enum allowed.
  template <typename T, unsigned Offset, unsigned Size,
            T MaxValue = std::is_enum<T>::value
                             ? T(0) // coupled with static_assert below
                             : std::numeric_limits<T>::max()>
  struct Element {
    /// Unpacked value type of this bitfield element.
    using Type = T;
    /// Integer type used when packing and unpacking this element.
    using IntegerType =
        typename bitfields_details::ResolveUnderlyingType<T>::type;
    /// Bit offset where this field begins in the packed storage word.
    static constexpr unsigned Shift = Offset;
    /// Width of this field in bits.
    static constexpr unsigned Bits = Size;
    /// Index of the first bit occupied by this field.
    static constexpr unsigned FirstBit = Offset;
    /// Index of the last bit occupied by this field.
    static constexpr unsigned LastBit = Shift + Bits - 1;
    /// Index of the first bit after this field (start of a contiguous neighbor).
    static constexpr unsigned NextBit = Shift + Bits;

  private:
    template <typename, typename> friend struct bitfields_details::Impl;

    static_assert(Bits > 0, "Bits must be non zero");
    static constexpr size_t TypeBits = sizeof(IntegerType) * CHAR_BIT;
    static_assert(Bits <= TypeBits, "Bits may not be greater than T size");
    static_assert(!std::is_enum<T>::value || MaxValue != T(0),
                  "Enum Bitfields must provide a MaxValue");
    static_assert(!std::is_enum<T>::value ||
                      std::is_unsigned<IntegerType>::value,
                  "Enum must be unsigned");
    static_assert(std::is_integral<IntegerType>::value &&
                      std::numeric_limits<IntegerType>::is_integer,
                  "IntegerType must be an integer type");

    static constexpr IntegerType UserMaxValue =
        static_cast<IntegerType>(MaxValue);
  };

  /// Unpacks the field from the `Packed` value.
  ///
  /// \param Packed The storage word to unpack the field from.
  /// \return The field value as type \c Bitfield::Type.
  template <typename Bitfield, typename StorageType>
  static typename Bitfield::Type get(StorageType Packed) {
    using I = bitfields_details::Impl<Bitfield, StorageType>;
    return static_cast<typename Bitfield::Type>(I::extract(Packed));
  }

  /// Return a non-zero value if the field is non-zero.
  /// It is more efficient than `getField`.
  ///
  /// \param Packed The storage word to test.
  /// \return The masked field bits from \p Packed, or zero if the field is clear.
  template <typename Bitfield, typename StorageType>
  static StorageType test(StorageType Packed) {
    using I = bitfields_details::Impl<Bitfield, StorageType>;
    return I::test(Packed);
  }

  /// Sets the typed value in the provided `Packed` value.
  /// The method will asserts if the provided value is too big to fit in.
  ///
  /// \param Packed The storage word to write the field into.
  /// \param Value  The unpacked value to store.
  template <typename Bitfield, typename StorageType>
  static void set(StorageType &Packed, typename Bitfield::Type Value) {
    using I = bitfields_details::Impl<Bitfield, StorageType>;
    I::update(Packed, static_cast<typename Bitfield::IntegerType>(Value));
  }

  /// Returns whether the two bitfields share common bits.
  ///
  /// \return True if \c A and \c B occupy overlapping bit ranges.
  template <typename A, typename B> static constexpr bool isOverlapping() {
    return A::LastBit >= B::FirstBit && B::LastBit >= A::FirstBit;
  }

  /// Base case: a single bitfield element is trivially contiguous.
  ///
  /// \return Always true.
  template <typename A> static constexpr bool areContiguous() { return true; }
  /// Returns true if \c A, \c B, and \c Others occupy adjacent bit ranges
  /// with no gaps between consecutive elements.
  ///
  /// \return True if the bitfield elements form a contiguous range.
  template <typename A, typename B, typename... Others>
  static constexpr bool areContiguous() {
    return A::NextBit == B::FirstBit && areContiguous<B, Others...>();
  }
};

} // namespace llvm

#endif // LLVM_ADT_BITFIELDS_H
