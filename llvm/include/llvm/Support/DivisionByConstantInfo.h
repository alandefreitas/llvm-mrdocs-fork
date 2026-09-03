//===- llvm/Support/DivisionByConstantInfo.h ---------------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// This file implements support for optimizing divisions by a constant
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_DIVISIONBYCONSTANTINFO_H
#define LLVM_SUPPORT_DIVISIONBYCONSTANTINFO_H

#include "llvm/ADT/APInt.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// Magic data for optimising signed division by a constant.
struct SignedDivisionByConstantInfo {
  /// Compute magic numbers for signed division by a constant.
  ///
  /// Calculates the values needed to implement signed integer division by a
  /// constant as a sequence of multiplies, adds and shifts. Requires that the
  /// divisor not be 0, 1, or -1. Taken from "Hacker's Delight", Henry S.
  /// Warren, Jr., Chapter 10.
  ///
  /// \param D The constant divisor (must not be 0, 1, or -1).
  /// \return Magic numbers for implementing signed division by \p D.
  LLVM_ABI static SignedDivisionByConstantInfo get(const APInt &D);
  APInt Magic;          ///< magic number
  unsigned ShiftAmount; ///< shift amount
};

/// Magic data for optimising unsigned division by a constant.
struct UnsignedDivisionByConstantInfo {
  /// Compute magic numbers for unsigned division by a constant.
  ///
  /// Calculates the values needed to implement unsigned integer division by a
  /// constant as a sequence of multiplies, adds and shifts. Requires that the
  /// divisor not be 0. Taken from "Hacker's Delight", Henry S. Warren, Jr.,
  /// chapter 10.
  ///
  /// \param D The constant divisor (must not be 0 or 1).
  /// \param LeadingZeros Number of known leading zero bits in the dividend;
  ///        used to simplify the calculation when upper bits are known zero.
  /// \param AllowEvenDivisorOptimization When true, even divisors may use a
  ///        pre-shift optimization that avoids an add.
  /// \param AllowWidenOptimization When true, IsAdd cases may use a widened
  ///        multiply (e.g. 32-bit division via 64-bit multiplication).
  /// \return Magic numbers for implementing unsigned division by \p D.
  LLVM_ABI static UnsignedDivisionByConstantInfo
  get(const APInt &D, unsigned LeadingZeros = 0,
      bool AllowEvenDivisorOptimization = true,
      bool AllowWidenOptimization = false);
  APInt Magic;          ///< magic number
  bool IsAdd;           ///< add indicator
  unsigned PostShift;   ///< post-shift amount
  unsigned PreShift;    ///< pre-shift amount
  bool Widen;           ///< use widen optimization
};

} // namespace llvm

#endif
