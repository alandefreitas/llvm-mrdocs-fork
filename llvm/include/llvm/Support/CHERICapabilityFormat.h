//===--- CHERICapabilityFormat.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_CHERICAPABILITYFORMAT_H
#define LLVM_SUPPORT_CHERICAPABILITYFORMAT_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/Alignment.h"

namespace llvm {

/// CRTP base for CHERI capability format helpers.
///
/// Provides static utilities for alignment masks, required alignments, and
/// representable lengths for a concrete capability encoding.
///
/// \tparam Derived Concrete format that implements getAlignmentMaskImpl.
/// \tparam AddressType Unsigned integer type for addresses and lengths.
template <typename Derived, typename AddressType>
struct CHERICapabilityFormatBase {
  /// Deleted; this type exposes only static members.
  CHERICapabilityFormatBase() = delete;

  /// Bitmask with all bits of AddressType set.
  static constexpr AddressType AddressMask = ~static_cast<AddressType>(0);

  /// Returns the alignment mask for an allocation of size \p Length.
  ///
  /// This mask is 0 where the capability format alignment requires the
  /// address to be 0, and 1 otherwise.
  ///
  /// \param Length Allocation size in bytes.
  /// \return The alignment mask for an allocation of size \p Length.
  static AddressType getAlignmentMask(AddressType Length) {
    return Derived::getAlignmentMaskImpl(Length);
  }

  /// Returns the required alignment for an allocation of size \p Length.
  ///
  /// \param Length Allocation size in bytes.
  /// \return The minimum alignment required for an allocation of size \p Length.
  static Align getRequiredAlignment(AddressType Length) {
    return Align((~getAlignmentMask(Length) + 1) & AddressMask);
  }

  /// Returns \p Length rounded up to the nearest representable allocation
  /// length.
  ///
  /// \param Length Requested allocation size in bytes.
  /// \return The smallest representable length at least \p Length.
  static AddressType getRepresentableLength(AddressType Length) {
    AddressType Mask = getAlignmentMask(Length);
    return (Length + ~Mask) & Mask;
  }
};

/// RISC-V Y (CHERI) compressed capability format.
///
/// \tparam AddressType Unsigned integer type for addresses and lengths.
/// \tparam MW Mantissa width in bits.
/// \tparam MAX_E Maximum representable exponent.
template <typename AddressType, unsigned MW, unsigned MAX_E>
struct RVYCapabilityFormat
    : public CHERICapabilityFormatBase<
          RVYCapabilityFormat<AddressType, MW, MAX_E>, AddressType> {
  friend struct CHERICapabilityFormatBase<
      RVYCapabilityFormat<AddressType, MW, MAX_E>, AddressType>;

private:
  LLVM_ABI static AddressType getAlignmentMaskImpl(uint64_t Length);
};

/// 32-bit RISC-V Y capability format (MW=10, MAX_E=24).
using RV32YCapabilityFormat = RVYCapabilityFormat<uint32_t, 10, 24>;
/// 64-bit RISC-V Y capability format (MW=14, MAX_E=52).
using RV64YCapabilityFormat = RVYCapabilityFormat<uint64_t, 14, 52>;

/// CHERIoT compressed capability format helpers.
struct CHERIoTCapabilityFormat
    : public CHERICapabilityFormatBase<CHERIoTCapabilityFormat, uint32_t> {
  friend struct CHERICapabilityFormatBase<CHERIoTCapabilityFormat, uint32_t>;

private:
  LLVM_ABI static uint32_t getAlignmentMaskImpl(uint32_t Length);
};

} // namespace llvm

#endif
