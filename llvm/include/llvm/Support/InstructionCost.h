//===- InstructionCost.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// This file defines an InstructionCost class that is used when calculating
/// the cost of an instruction, or a group of instructions. In addition to a
/// numeric value representing the cost the class also contains a state that
/// can be used to encode particular properties, such as a cost being invalid.
/// Operations on InstructionCost implement saturation arithmetic, so that
/// accumulating costs on large cost-values don't overflow.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_INSTRUCTIONCOST_H
#define LLVM_SUPPORT_INSTRUCTIONCOST_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <limits>
#include <tuple>

namespace llvm {

class raw_ostream;

/// Class used when calculating the cost of an instruction or a group of
/// instructions.
///
/// In addition to a numeric value representing the cost, the class also
/// contains a state that can encode particular properties such as a cost being
/// invalid. Operations implement saturation arithmetic so accumulating large
/// cost values does not overflow.
class InstructionCost {
public:
  /// Integer type used to store cost values.
  using CostType = int64_t;

  /// CostState describes the state of a cost.
  enum CostState {
    /// The cost value represents a valid cost, even when the cost-value is
    /// large.
    Valid,
    /// Invalid indicates there is no way to represent the cost as a numeric
    /// value. This state exists to represent a possible issue, e.g. if the
    /// cost-model knows the operation cannot be expanded into a valid
    /// code-sequence by the code-generator. While some passes may assert that
    /// the calculated cost must be valid, it is up to individual passes how to
    /// interpret an Invalid cost. For example, a transformation pass could
    /// choose not to perform a transformation if the resulting cost would end
    /// up Invalid. Because some passes may assert a cost is Valid, it is not
    /// recommended to use Invalid costs to model 'Unknown'. Note that Invalid
    /// is semantically different from a (very) high, but valid cost, which
    /// intentionally indicates no issue, but rather a strong preference not to
    /// select a certain operation.
    Invalid
  };

private:
  CostType Value = 0;
  CostState State = Valid;

  void propagateState(const InstructionCost &RHS) {
    if (RHS.State == Invalid)
      State = Invalid;
  }

  // Matches GCC, can use shift rather than multiply/divide to scale
  static constexpr CostType CostGranularity = 4;

  static constexpr CostType MaxValue = std::numeric_limits<CostType>::max();
  static constexpr CostType MinValue = std::numeric_limits<CostType>::min();

public:
  /// Construct a valid zero cost.
  InstructionCost() = default;

  /// Deleted to prevent constructing a cost from a \c CostState.
  ///
  /// \param State Unused; construction from \c CostState is not allowed.
  InstructionCost(CostState State) = delete;

  /// Construct a valid cost with the given numeric value.
  ///
  /// \param Val Numeric cost value; large magnitudes saturate.
  InstructionCost(CostType Val) : Value(), State(Valid) {
    InstructionCost::CostType Result;
    if (MulOverflow(Val, CostGranularity, Result))
      Result = Val > 0 ? MaxValue : MinValue;
    Value = Result;
  }

  /// Return a cost holding the maximum representable value.
  ///
  /// \return A cost holding the maximum representable value.
  static InstructionCost getMax() { return MaxValue; }
  /// Return a cost holding the minimum representable value.
  ///
  /// \return A cost holding the minimum representable value.
  static InstructionCost getMin() { return MinValue; }
  /// Return an invalid cost, optionally carrying \p Val as a placeholder.
  ///
  /// \param Val Numeric value stored alongside the invalid state.
  /// \return An invalid cost, optionally carrying \p Val as a placeholder.
  static InstructionCost getInvalid(CostType Val = 0) {
    InstructionCost Tmp(Val);
    Tmp.setInvalid();
    return Tmp;
  }

  /// Return true if this cost is in the valid state.
  ///
  /// \return True if this cost is in the valid state.
  bool isValid() const { return State == Valid; }
  /// Mark this cost as valid.
  void setValid() { State = Valid; }
  /// Mark this cost as invalid.
  void setInvalid() { State = Invalid; }
  /// Return whether this cost is valid or invalid.
  ///
  /// \return Whether this cost is valid or invalid.
  CostState getState() const { return State; }

  /// Return the numeric cost value; asserts if the cost is invalid.
  ///
  /// This function is intended to be used as sparingly as possible, since the
  /// class provides the full range of operator support required for arithmetic
  /// and comparisons.
  ///
  /// \return The numeric cost value.
  CostType getValue() const {
    assert(isValid());
    return Value / CostGranularity;
  }

  /// Add \p RHS into this cost using saturating arithmetic.
  ///
  /// For all of the arithmetic operators provided here any invalid state is
  /// perpetuated and cannot be removed. Once a cost becomes invalid it stays
  /// invalid, and it also inherits any invalid state from the RHS.
  /// Arithmetic work on the actual values is implemented with saturation,
  /// to avoid overflow when using more extreme cost values.
  ///
  /// \param RHS Cost to add.
  /// \return A reference to this cost after the addition.
  InstructionCost &operator+=(const InstructionCost &RHS) {
    propagateState(RHS);

    // Saturating addition.
    InstructionCost::CostType Result;
    if (AddOverflow(Value, RHS.Value, Result))
      Result = RHS.Value > 0 ? MaxValue : MinValue;

    Value = Result;
    return *this;
  }

  /// Add the numeric cost \p RHS into this cost using saturating arithmetic.
  ///
  /// \param RHS Numeric cost to add.
  /// \return A reference to this cost after the addition.
  InstructionCost &operator+=(const CostType RHS) {
    InstructionCost RHS2(RHS);
    *this += RHS2;
    return *this;
  }

  /// Subtract \p RHS from this cost using saturating arithmetic.
  ///
  /// \param RHS Cost to subtract.
  /// \return A reference to this cost after the subtraction.
  InstructionCost &operator-=(const InstructionCost &RHS) {
    propagateState(RHS);

    // Saturating subtract.
    InstructionCost::CostType Result;
    if (SubOverflow(Value, RHS.Value, Result))
      Result = RHS.Value > 0 ? MinValue : MaxValue;
    Value = Result;
    return *this;
  }

  /// Subtract the numeric cost \p RHS from this cost using saturating arithmetic.
  ///
  /// \param RHS Numeric cost to subtract.
  /// \return A reference to this cost after the subtraction.
  InstructionCost &operator-=(const CostType RHS) {
    InstructionCost RHS2(RHS);
    *this -= RHS2;
    return *this;
  }

  /// Multiply this cost by \p RHS using saturating arithmetic.
  ///
  /// \param RHS Cost to multiply by.
  /// \return A reference to this cost after the multiplication.
  InstructionCost &operator*=(const InstructionCost &RHS) {
    propagateState(RHS);

    // Saturating multiply.
    InstructionCost::CostType Result;
    if (MulOverflow(Value, RHS.Value, Result)) {
      if ((Value > 0 && RHS.Value > 0) || (Value < 0 && RHS.Value < 0))
        Result = MaxValue;
      else
        Result = MinValue;
    } else {
      Result /= CostGranularity;
    }

    Value = Result;
    return *this;
  }

  /// Multiply this cost by the numeric value \p RHS using saturating arithmetic.
  ///
  /// \param RHS Numeric cost to multiply by.
  /// \return A reference to this cost after the multiplication.
  InstructionCost &operator*=(const CostType RHS) {
    InstructionCost RHS2(RHS);
    *this *= RHS2;
    return *this;
  }

  /// Divide this cost by \p RHS using saturating arithmetic.
  ///
  /// \param RHS Cost to divide by.
  /// \return A reference to this cost after the division.
  InstructionCost &operator/=(const InstructionCost &RHS) {
    propagateState(RHS);
    // Saturating multiply.
    InstructionCost::CostType Result;
    if (MulOverflow(Value, CostGranularity, Result))
      Result = Value > 0 ? MaxValue : MinValue;
    Result /= RHS.Value;
    Value = Result;
    return *this;
  }

  /// Divide this cost by the numeric value \p RHS.
  ///
  /// \param RHS Numeric divisor.
  /// \return A reference to this cost after the division.
  InstructionCost &operator/=(const CostType RHS) {
    Value /= RHS;
    return *this;
  }

  /// Increment this cost by one in place.
  ///
  /// \return A reference to this cost after the increment.
  InstructionCost &operator++() {
    *this += 1;
    return *this;
  }

  /// Increment this cost by one and return the previous value.
  ///
  /// \param Unused Dummy parameter distinguishing post-increment.
  /// \return The previous value of this cost.
  InstructionCost operator++(int Unused) {
    InstructionCost Copy = *this;
    ++*this;
    return Copy;
  }

  /// Decrement this cost by one in place.
  ///
  /// \return A reference to this cost after the decrement.
  InstructionCost &operator--() {
    *this -= 1;
    return *this;
  }

  /// Decrement this cost by one and return the previous value.
  ///
  /// \param Unused Dummy parameter distinguishing post-decrement.
  /// \return The previous value of this cost.
  InstructionCost operator--(int Unused) {
    InstructionCost Copy = *this;
    --*this;
    return Copy;
  }

  /// Return true if this cost is lexicographically less than \p RHS.
  ///
  /// For the comparison operators we have chosen to use lexicographical
  /// ordering where valid costs are always considered to be less than invalid
  /// costs. This avoids having to add asserts to the comparison operators that
  /// the states are valid and users can test for validity of the cost
  /// explicitly.
  ///
  /// \param RHS Cost to compare against.
  /// \return True if this cost is lexicographically less than \p RHS.
  bool operator<(const InstructionCost &RHS) const {
    return std::tie(State, Value) < std::tie(RHS.State, RHS.Value);
  }

  /// Return true if this cost equals \p RHS in both state and value.
  ///
  /// \param RHS Cost to compare against.
  /// \return True if this cost equals \p RHS in both state and value.
  bool operator==(const InstructionCost &RHS) const {
    return State == RHS.State && Value == RHS.Value;
  }

  /// Return true if this cost differs from \p RHS in state or value.
  ///
  /// \param RHS Cost to compare against.
  /// \return True if this cost differs from \p RHS in state or value.
  bool operator!=(const InstructionCost &RHS) const { return !(*this == RHS); }

  /// Return true if this cost equals the numeric cost \p RHS.
  ///
  /// \param RHS Numeric cost to compare against.
  /// \return True if this cost equals the numeric cost \p RHS.
  bool operator==(const CostType RHS) const {
    InstructionCost RHS2(RHS);
    return *this == RHS2;
  }

  /// Return true if this cost differs from the numeric cost \p RHS.
  ///
  /// \param RHS Numeric cost to compare against.
  /// \return True if this cost differs from the numeric cost \p RHS.
  bool operator!=(const CostType RHS) const { return !(*this == RHS); }

  /// Return true if this cost is lexicographically greater than \p RHS.
  ///
  /// \param RHS Cost to compare against.
  /// \return True if this cost is lexicographically greater than \p RHS.
  bool operator>(const InstructionCost &RHS) const { return RHS < *this; }

  /// Return true if this cost is less than or equal to \p RHS.
  ///
  /// \param RHS Cost to compare against.
  /// \return True if this cost is less than or equal to \p RHS.
  bool operator<=(const InstructionCost &RHS) const { return !(RHS < *this); }

  /// Return true if this cost is greater than or equal to \p RHS.
  ///
  /// \param RHS Cost to compare against.
  /// \return True if this cost is greater than or equal to \p RHS.
  bool operator>=(const InstructionCost &RHS) const { return !(*this < RHS); }

  /// Return true if this cost is less than the numeric cost \p RHS.
  ///
  /// \param RHS Numeric cost to compare against.
  /// \return True if this cost is less than the numeric cost \p RHS.
  bool operator<(const CostType RHS) const {
    InstructionCost RHS2(RHS);
    return *this < RHS2;
  }

  /// Return true if this cost is greater than the numeric cost \p RHS.
  ///
  /// \param RHS Numeric cost to compare against.
  /// \return True if this cost is greater than the numeric cost \p RHS.
  bool operator>(const CostType RHS) const {
    InstructionCost RHS2(RHS);
    return *this > RHS2;
  }

  /// Return true if this cost is less than or equal to the numeric cost \p RHS.
  ///
  /// \param RHS Numeric cost to compare against.
  /// \return True if this cost is less than or equal to the numeric cost \p RHS.
  bool operator<=(const CostType RHS) const {
    InstructionCost RHS2(RHS);
    return *this <= RHS2;
  }

  /// Return true if this cost is greater than or equal to the numeric cost
  /// \p RHS.
  ///
  /// \param RHS Numeric cost to compare against.
  /// \return True if this cost is greater than or equal to the numeric cost
  /// \p RHS.
  bool operator>=(const CostType RHS) const {
    InstructionCost RHS2(RHS);
    return *this >= RHS2;
  }

  /// Print this cost to \p OS.
  ///
  /// \param OS Stream to write to.
  LLVM_ABI void print(raw_ostream &OS) const;

  /// Apply \p F to the cost value when valid; otherwise return an invalid cost.
  ///
  /// \param F Callable invoked with the internal cost value when this cost is
  /// valid.
  /// \return The result of applying \p F, or an invalid cost.
  template <class Function>
  auto map(const Function &F) const -> InstructionCost {
    if (isValid())
      return F(Value);
    return getInvalid();
  }
};

/// Return the saturating sum of \p LHS and \p RHS.
///
/// \param LHS Left-hand cost.
/// \param RHS Right-hand cost.
/// \return The saturating sum of \p LHS and \p RHS.
inline InstructionCost operator+(const InstructionCost &LHS,
                                 const InstructionCost &RHS) {
  InstructionCost LHS2(LHS);
  LHS2 += RHS;
  return LHS2;
}

/// Return the saturating difference of \p LHS and \p RHS.
///
/// \param LHS Left-hand cost.
/// \param RHS Right-hand cost.
/// \return The saturating difference of \p LHS and \p RHS.
inline InstructionCost operator-(const InstructionCost &LHS,
                                 const InstructionCost &RHS) {
  InstructionCost LHS2(LHS);
  LHS2 -= RHS;
  return LHS2;
}

/// Return the saturating product of \p LHS and \p RHS.
///
/// \param LHS Left-hand cost.
/// \param RHS Right-hand cost.
/// \return The saturating product of \p LHS and \p RHS.
inline InstructionCost operator*(const InstructionCost &LHS,
                                 const InstructionCost &RHS) {
  InstructionCost LHS2(LHS);
  LHS2 *= RHS;
  return LHS2;
}

/// Return the saturating quotient of \p LHS divided by \p RHS.
///
/// \param LHS Left-hand cost.
/// \param RHS Right-hand cost.
/// \return The saturating quotient of \p LHS divided by \p RHS.
inline InstructionCost operator/(const InstructionCost &LHS,
                                 const InstructionCost &RHS) {
  InstructionCost LHS2(LHS);
  LHS2 /= RHS;
  return LHS2;
}

/// Print \p V to \p OS.
///
/// \param OS Stream to write to.
/// \param V Cost to print.
/// \return The output stream \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const InstructionCost &V) {
  V.print(OS);
  return OS;
}

} // namespace llvm

#endif
