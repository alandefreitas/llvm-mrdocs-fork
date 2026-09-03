//===-- llvm/Support/Alignment.h - Useful alignment functions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains types to represent alignments.
// They are instrumented to guarantee some invariants are preserved and prevent
// invalid manipulations.
//
// - Align represents an alignment in bytes, it is always set and always a valid
// power of two, its minimum value is 1 which means no alignment requirements.
//
// - MaybeAlign is an optional type, it may be undefined or set. When it's set
// you can get the underlying Align type by using the value() method.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_ALIGNMENT_H_
#define LLVM_SUPPORT_ALIGNMENT_H_

#include "llvm/Support/MathExtras.h"
#include <cassert>
#include <optional>
#ifndef NDEBUG
#include <string>
#endif // NDEBUG

namespace llvm {

#define ALIGN_CHECK_ISPOSITIVE(decl)                                           \
  assert(decl > 0 && (#decl " should be defined"))

/// This struct is a compact representation of a valid (non-zero power of two)
/// alignment.
/// It is suitable for use as static global constants.
struct Align {
private:
  uint8_t ShiftValue = 0; /// The log2 of the required alignment.
                          /// ShiftValue is less than 64 by construction.

  friend struct MaybeAlign;
  friend unsigned Log2(Align);
  /// Return true if alignments \p Lhs and \p Rhs are equal.
  ///
  /// \return True if the alignments are equal.
  friend bool operator==(Align Lhs, Align Rhs);
  /// Return true if alignments \p Lhs and \p Rhs differ.
  ///
  /// \return True if the alignments differ.
  friend bool operator!=(Align Lhs, Align Rhs);
  /// Return true if alignment \p Lhs is less than or equal to \p Rhs.
  ///
  /// \return True if \p Lhs is less than or equal to \p Rhs.
  friend bool operator<=(Align Lhs, Align Rhs);
  /// Return true if alignment \p Lhs is greater than or equal to \p Rhs.
  ///
  /// \return True if \p Lhs is greater than or equal to \p Rhs.
  friend bool operator>=(Align Lhs, Align Rhs);
  /// Return true if alignment \p Lhs is strictly less than \p Rhs.
  ///
  /// \return True if \p Lhs is strictly less than \p Rhs.
  friend bool operator<(Align Lhs, Align Rhs);
  /// Return true if alignment \p Lhs is strictly greater than \p Rhs.
  ///
  /// \return True if \p Lhs is strictly greater than \p Rhs.
  friend bool operator>(Align Lhs, Align Rhs);
  friend unsigned encode(struct MaybeAlign A);
  friend struct MaybeAlign decodeMaybeAlign(unsigned Value);

  struct FromShiftValue {};
  constexpr Align(FromShiftValue, uint8_t Shift) : ShiftValue(Shift) {}

public:
  /// Default is byte-aligned.
  constexpr Align() = default;
  /// Copy-construct without re-checking that the value is a power of two.
  ///
  /// \param Other Alignment to copy.
  constexpr Align(const Align &Other) = default;
  /// Move-construct without re-checking that the value is a power of two.
  ///
  /// \param Other Alignment to move from.
  constexpr Align(Align &&Other) = default;
  /// Copy-assign without re-checking that the value is a power of two.
  ///
  /// \param Other Alignment to copy.
  /// \return A reference to this alignment.
  constexpr Align &operator=(const Align &Other) = default;
  /// Move-assign without re-checking that the value is a power of two.
  ///
  /// \param Other Alignment to move from.
  /// \return A reference to this alignment.
  constexpr Align &operator=(Align &&Other) = default;

  /// Construct from a positive power-of-two byte count \p Value.
  ///
  /// \param Value Byte alignment; must be a positive power of two.
  explicit Align(uint64_t Value) {
    assert(Value > 0 && "Value must not be 0");
    assert(llvm::isPowerOf2_64(Value) && "Alignment is not a power of 2");
    ShiftValue = Log2_64(Value);
    assert(ShiftValue < 64 && "Broken invariant");
  }

  /// This is a hole in the type system and should not be abused.
  /// Needed to interact with C for instance.
  ///
  /// \return The alignment in bytes as a power of two.
  constexpr uint64_t value() const { return uint64_t(1) << ShiftValue; }

  /// Return the next-smaller power-of-two alignment.
  ///
  /// \return The alignment that is half of this one.
  Align previous() const {
    assert(ShiftValue != 0 && "Undefined operation");
    Align Out;
    Out.ShiftValue = ShiftValue - 1;
    return Out;
  }

  /// Allow constructions of constexpr Align.
  ///
  /// \return A constexpr Align with value \p kValue.
  template <size_t kValue> constexpr static Align Constant() {
    return Align(FromShiftValue{}, ConstantLog2<kValue>());
  }

  /// Allow constructions of constexpr Align from types.
  /// Compile time equivalent to Align(alignof(T)).
  ///
  /// \return A constexpr Align equal to \c alignof(T).
  template <typename T> constexpr static Align Of() {
    return Constant<std::alignment_of_v<T>>();
  }
};

/// Treats the value 0 as a 1, so Align is always at least 1.
///
/// \param Value Byte count to treat as an alignment; 0 becomes byte alignment.
/// \return An Align for \p Value, or byte alignment when \p Value is 0.
inline Align assumeAligned(uint64_t Value) {
  return Value ? Align(Value) : Align();
}

/// This struct is a compact representation of a valid (power of two) or
/// undefined (0) alignment.
///
/// Inherits the optional storage privately and re-exports the public
/// \c std::optional interface with documentation.
struct MaybeAlign : private std::optional<Align> {
private:
  using UP = std::optional<Align>;

public:
  /// Contained alignment type when set.
  using value_type = Align;

  /// Returns whether an alignment value is present.
  using UP::has_value;
  /// Clears any stored alignment.
  using UP::reset;
  /// Constructs an alignment in place.
  using UP::emplace;
  /// Swaps the contained alignment with another optional.
  using UP::swap;
  /// Returns the alignment or throws if unset.
  using UP::value;
  /// Returns the alignment, or a fallback when unset.
  using UP::value_or;
  /// True when an alignment is set.
  using UP::operator bool;
  /// Accesses the stored alignment.
  using UP::operator*;
  /// Accesses members of the stored alignment.
  using UP::operator->;
  /// Assigns from another optional alignment.
  using UP::operator=;
#if defined(__cpp_lib_optional) && __cpp_lib_optional >= 202110L
  /// Chains a continuation when an alignment is set.
  using UP::and_then;
  /// Supplies an alternate optional when unset.
  using UP::or_else;
  /// Transforms the stored alignment when set.
  using UP::transform;
#endif

  /// Default is undefined.
  MaybeAlign() = default;
  /// Copy-construct without re-validating the optional alignment.
  ///
  /// \param Other Optional alignment to copy.
  MaybeAlign(const MaybeAlign &Other) = default;
  /// Copy-assign without re-validating the optional alignment.
  ///
  /// \param Other Optional alignment to copy.
  /// \return A reference to this optional alignment.
  MaybeAlign &operator=(const MaybeAlign &Other) = default;
  /// Move-construct from another MaybeAlign.
  ///
  /// \param Other Optional alignment to move from.
  MaybeAlign(MaybeAlign &&Other) = default;
  /// Move-assign from another MaybeAlign.
  ///
  /// \param Other Optional alignment to move from.
  /// \return A reference to this optional alignment.
  MaybeAlign &operator=(MaybeAlign &&Other) = default;

  /// Construct an unset MaybeAlign from \c std::nullopt.
  ///
  /// \param None Tag selecting the unset state.
  constexpr MaybeAlign(std::nullopt_t None) : UP(None) {}
  /// Construct a MaybeAlign holding known alignment \p Value.
  ///
  /// \param Value Alignment to store.
  constexpr MaybeAlign(Align Value) : UP(Value) {}
  /// Construct from byte count \p Value (0 means unset; otherwise a power of two).
  ///
  /// \param Value Byte alignment, or 0 for unset.
  explicit MaybeAlign(uint64_t Value) {
    assert((Value == 0 || llvm::isPowerOf2_64(Value)) &&
           "Alignment is neither 0 nor a power of 2");
    if (Value)
      emplace(Value);
  }

  /// For convenience, returns a valid alignment or 1 if undefined.
  ///
  /// \return The stored alignment, or byte alignment when unset.
  Align valueOrOne() const { return value_or(Align()); }
};

/// Checks that SizeInBytes is a multiple of the alignment.
///
/// \param Lhs Alignment that must divide \p SizeInBytes.
/// \param SizeInBytes Size in bytes to test.
/// \return True if \p SizeInBytes is a multiple of \p Lhs.
inline bool isAligned(Align Lhs, uint64_t SizeInBytes) {
  return SizeInBytes % Lhs.value() == 0;
}

/// Checks that Addr is a multiple of the alignment.
///
/// \param Lhs Alignment that must divide \p Addr.
/// \param Addr Address to test.
/// \return True if \p Addr is a multiple of \p Lhs.
inline bool isAddrAligned(Align Lhs, const void *Addr) {
  return isAligned(Lhs, reinterpret_cast<uintptr_t>(Addr));
}

/// Returns a multiple of A needed to store `Size` bytes.
///
/// \param Size Byte count to round up.
/// \param A Alignment to round up to.
/// \return \p Size rounded up to the next multiple of \p A.
constexpr inline uint64_t alignTo(uint64_t Size, Align A) {
  const uint64_t Value = A.value();
  // The following line is equivalent to `(Size + Value - 1) / Value * Value`.

  // The division followed by a multiplication can be thought of as a right
  // shift followed by a left shift which zeros out the extra bits produced in
  // the bump; `~(Value - 1)` is a mask where all those bits being zeroed out
  // are just zero.

  // Most compilers can generate this code but the pattern may be missed when
  // multiple functions gets inlined.
  return (Size + Value - 1) & ~(Value - 1U);
}

/// Returns the smallest value at least \p Size congruent to \p Skew modulo \p A.
///
/// If non-zero \p Skew is specified, the return value will be a minimal integer
/// that is greater than or equal to \p Size and equal to \p A * N + \p Skew for
/// some integer N. If \p Skew is larger than \p A, its value is adjusted to '\p
/// Skew mod \p A'.
///
/// Examples:
/// \code
///   alignTo(5, Align(8), 7) = 7
///   alignTo(17, Align(8), 1) = 17
///   alignTo(~0LL, Align(8), 3) = 3
/// \endcode
///
/// \param Size Lower bound for the result.
/// \param A Alignment stride.
/// \param Skew Desired residue modulo \p A.
/// \return The smallest value at least \p Size congruent to \p Skew modulo \p A.
inline uint64_t alignTo(uint64_t Size, Align A, uint64_t Skew) {
  const uint64_t Value = A.value();
  Skew %= Value;
  return alignTo(Size - Skew, A) + Skew;
}

/// Aligns `Addr` to `Alignment` bytes, rounding up.
///
/// \param Addr Address to align.
/// \param Alignment Alignment to round up to.
/// \return \p Addr rounded up to the next multiple of \p Alignment.
inline uintptr_t alignAddr(const void *Addr, Align Alignment) {
  uintptr_t ArithAddr = reinterpret_cast<uintptr_t>(Addr);
  assert(static_cast<uintptr_t>(ArithAddr + Alignment.value() - 1) >=
             ArithAddr &&
         "Overflow");
  return alignTo(ArithAddr, Alignment);
}

/// Returns the offset to the next integer (mod 2**64) that is greater than
/// or equal to \p Value and is a multiple of \p Align.
///
/// \param Value Starting value.
/// \param Alignment Alignment that the result must be a multiple of.
/// \return The number of bytes to add to \p Value to reach the next aligned value.
inline uint64_t offsetToAlignment(uint64_t Value, Align Alignment) {
  return alignTo(Value, Alignment) - Value;
}

/// Returns the necessary adjustment for aligning `Addr` to `Alignment`
/// bytes, rounding up.
///
/// \param Addr Address to align.
/// \param Alignment Alignment to round up to.
/// \return The number of bytes to add to \p Addr to reach the next aligned address.
inline uint64_t offsetToAlignedAddr(const void *Addr, Align Alignment) {
  return offsetToAlignment(reinterpret_cast<uintptr_t>(Addr), Alignment);
}

/// Returns the log2 of the alignment.
///
/// \param A Alignment whose shift value is returned.
/// \return The base-2 logarithm of \p A.
inline unsigned Log2(Align A) { return A.ShiftValue; }

/// Returns the alignment that satisfies both alignments.
/// Same semantic as MinAlign.
///
/// \param A Known alignment of a pointer.
/// \param Offset Byte offset from that pointer.
/// \return The greatest alignment that divides both \p A and \p Offset.
inline Align commonAlignment(Align A, uint64_t Offset) {
  return Align(MinAlign(A.value(), Offset));
}

/// Returns a representation of the alignment that encodes undefined as 0.
///
/// \param A Optional alignment to encode.
/// \return An encoding where 0 means unset and positive values encode Align.
inline unsigned encode(MaybeAlign A) { return A ? A->ShiftValue + 1 : 0; }

/// Dual operation of the encode function above.
///
/// \param Value Encoded alignment from \c encode; 0 means unset.
/// \return The decoded optional alignment.
inline MaybeAlign decodeMaybeAlign(unsigned Value) {
  if (Value == 0)
    return MaybeAlign();
  Align Out;
  Out.ShiftValue = Value - 1;
  return Out;
}

/// Returns a representation of the alignment, the encoded value is positive by
/// definition.
///
/// \param A Alignment to encode.
/// \return A positive encoding of \p A.
inline unsigned encode(Align A) { return encode(MaybeAlign(A)); }

/// Comparisons between Align and scalars. Rhs must be positive.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Positive byte count on the right-hand side.
/// \return True if \p Lhs equals \p Rhs bytes.
inline bool operator==(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() == Rhs;
}
/// Return true if alignment \p Lhs differs from byte count \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Positive byte count on the right-hand side.
/// \return True if \p Lhs differs from \p Rhs bytes.
inline bool operator!=(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() != Rhs;
}
/// Return true if alignment \p Lhs is less than or equal to byte count \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Positive byte count on the right-hand side.
/// \return True if \p Lhs is less than or equal to \p Rhs bytes.
inline bool operator<=(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() <= Rhs;
}
/// Return true if alignment \p Lhs is greater than or equal to byte count \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Positive byte count on the right-hand side.
/// \return True if \p Lhs is greater than or equal to \p Rhs bytes.
inline bool operator>=(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() >= Rhs;
}
/// Return true if alignment \p Lhs is strictly less than byte count \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Positive byte count on the right-hand side.
/// \return True if \p Lhs is strictly less than \p Rhs bytes.
inline bool operator<(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() < Rhs;
}
/// Return true if alignment \p Lhs is strictly greater than byte count \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Positive byte count on the right-hand side.
/// \return True if \p Lhs is strictly greater than \p Rhs bytes.
inline bool operator>(Align Lhs, uint64_t Rhs) {
  ALIGN_CHECK_ISPOSITIVE(Rhs);
  return Lhs.value() > Rhs;
}

/// Comparisons operators between Align.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
/// \return True if the alignments are equal.
inline bool operator==(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue == Rhs.ShiftValue;
}
/// Return true if alignments \p Lhs and \p Rhs differ.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
/// \return True if the alignments differ.
inline bool operator!=(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue != Rhs.ShiftValue;
}
/// Return true if alignment \p Lhs is less than or equal to \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
/// \return True if \p Lhs is less than or equal to \p Rhs.
inline bool operator<=(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue <= Rhs.ShiftValue;
}
/// Return true if alignment \p Lhs is greater than or equal to \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
/// \return True if \p Lhs is greater than or equal to \p Rhs.
inline bool operator>=(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue >= Rhs.ShiftValue;
}
/// Return true if alignment \p Lhs is strictly less than \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
/// \return True if \p Lhs is strictly less than \p Rhs.
inline bool operator<(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue < Rhs.ShiftValue;
}
/// Return true if alignment \p Lhs is strictly greater than \p Rhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
/// \return True if \p Lhs is strictly greater than \p Rhs.
inline bool operator>(Align Lhs, Align Rhs) {
  return Lhs.ShiftValue > Rhs.ShiftValue;
}

// Don't allow relational comparisons with MaybeAlign.
/// Relational less-or-equal is deleted for Align vs MaybeAlign.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
bool operator<=(Align Lhs, MaybeAlign Rhs) = delete;
/// Relational greater-or-equal is deleted for Align vs MaybeAlign.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
bool operator>=(Align Lhs, MaybeAlign Rhs) = delete;
/// Relational less-than is deleted for Align vs MaybeAlign.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
bool operator<(Align Lhs, MaybeAlign Rhs) = delete;
/// Relational greater-than is deleted for Align vs MaybeAlign.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
bool operator>(Align Lhs, MaybeAlign Rhs) = delete;

/// Relational less-or-equal is deleted for MaybeAlign vs Align.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
bool operator<=(MaybeAlign Lhs, Align Rhs) = delete;
/// Relational greater-or-equal is deleted for MaybeAlign vs Align.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
bool operator>=(MaybeAlign Lhs, Align Rhs) = delete;
/// Relational less-than is deleted for MaybeAlign vs Align.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
bool operator<(MaybeAlign Lhs, Align Rhs) = delete;
/// Relational greater-than is deleted for MaybeAlign vs Align.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
bool operator>(MaybeAlign Lhs, Align Rhs) = delete;

/// Relational less-or-equal is deleted for MaybeAlign vs MaybeAlign.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
bool operator<=(MaybeAlign Lhs, MaybeAlign Rhs) = delete;
/// Relational greater-or-equal is deleted for MaybeAlign vs MaybeAlign.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
bool operator>=(MaybeAlign Lhs, MaybeAlign Rhs) = delete;
/// Relational less-than is deleted for MaybeAlign vs MaybeAlign.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
bool operator<(MaybeAlign Lhs, MaybeAlign Rhs) = delete;
/// Relational greater-than is deleted for MaybeAlign; compare Align values instead.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
bool operator>(MaybeAlign Lhs, MaybeAlign Rhs) = delete;

// Allow equality comparisons between Align and MaybeAlign.
/// Return true if optional alignment \p Lhs is set and equals \p Rhs.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
/// \return True if \p Lhs is set and equals \p Rhs.
inline bool operator==(MaybeAlign Lhs, Align Rhs) { return Lhs && *Lhs == Rhs; }
/// Return true if optional alignment \p Lhs is unset or differs from \p Rhs.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Alignment on the right-hand side.
/// \return True if \p Lhs is unset or differs from \p Rhs.
inline bool operator!=(MaybeAlign Lhs, Align Rhs) { return !(Lhs == Rhs); }
/// Return true if optional alignment \p Rhs is set and equals \p Lhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
/// \return True if \p Rhs is set and equals \p Lhs.
inline bool operator==(Align Lhs, MaybeAlign Rhs) { return Rhs == Lhs; }
/// Return true if optional alignment \p Rhs is unset or differs from \p Lhs.
///
/// \param Lhs Alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
/// \return True if \p Rhs is unset or differs from \p Lhs.
inline bool operator!=(Align Lhs, MaybeAlign Rhs) { return !(Rhs == Lhs); }
// Allow equality comparisons with MaybeAlign.
/// Return true if optional alignments \p Lhs and \p Rhs are both unset or equal.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
/// \return True if both are unset or both are set and equal.
inline bool operator==(MaybeAlign Lhs, MaybeAlign Rhs) {
  return (Lhs && Rhs && (*Lhs == *Rhs)) || (!Lhs && !Rhs);
}
/// Return true if optional alignments \p Lhs and \p Rhs differ.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param Rhs Optional alignment on the right-hand side.
/// \return True if the optional alignments differ.
inline bool operator!=(MaybeAlign Lhs, MaybeAlign Rhs) { return !(Lhs == Rhs); }
// Allow equality comparisons with std::nullopt.
/// Return true if optional alignment \p Lhs is unset.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param None Tag representing an unset optional.
/// \return True if \p Lhs is unset.
inline bool operator==(MaybeAlign Lhs, std::nullopt_t None) {
  (void)None;
  return !bool(Lhs);
}
/// Return true if optional alignment \p Lhs is set.
///
/// \param Lhs Optional alignment on the left-hand side.
/// \param None Tag representing an unset optional.
/// \return True if \p Lhs is set.
inline bool operator!=(MaybeAlign Lhs, std::nullopt_t None) {
  (void)None;
  return bool(Lhs);
}
/// Return true if optional alignment \p Rhs is unset.
///
/// \param None Tag representing an unset optional.
/// \param Rhs Optional alignment on the right-hand side.
/// \return True if \p Rhs is unset.
inline bool operator==(std::nullopt_t None, MaybeAlign Rhs) {
  (void)None;
  return !bool(Rhs);
}
/// Return true if optional alignment \p Rhs is set.
///
/// \param None Tag representing an unset optional.
/// \param Rhs Optional alignment on the right-hand side.
/// \return True if \p Rhs is set.
inline bool operator!=(std::nullopt_t None, MaybeAlign Rhs) {
  (void)None;
  return bool(Rhs);
}

#ifndef NDEBUG
// For usage in LLVM_DEBUG macros.
/// Format alignment \p A as a decimal string for debug logging.
///
/// \param A Alignment to format.
/// \return The alignment in bytes as a decimal string.
inline std::string DebugStr(const Align &A) {
  return std::to_string(A.value());
}
// For usage in LLVM_DEBUG macros.
/// Format optional alignment \p MA as a decimal string or "None" for debug logging.
///
/// \param MA Optional alignment to format.
/// \return The alignment in bytes as a decimal string, or "None" when unset.
inline std::string DebugStr(const MaybeAlign &MA) {
  if (MA)
    return std::to_string(MA->value());
  return "None";
}
#endif // NDEBUG

#undef ALIGN_CHECK_ISPOSITIVE

} // namespace llvm

#endif // LLVM_SUPPORT_ALIGNMENT_H_
