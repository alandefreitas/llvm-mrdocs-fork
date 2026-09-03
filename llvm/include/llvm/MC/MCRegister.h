//===-- llvm/MC/Register.h --------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCREGISTER_H
#define LLVM_MC_MCREGISTER_H

#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/Hashing.h"
#include <cassert>
#include <limits>

namespace llvm {

/// An unsigned integer type large enough to represent all physical registers,
/// but not necessarily virtual registers.
using MCPhysReg = uint16_t;

/// Opaque identifier for a register unit used to compute register aliasing.
///
/// Register units are used to compute register aliasing. Every register has at
/// least one register unit, but it can have more. Two registers overlap if and
/// only if they have a common register unit.
///
/// A target with a complicated sub-register structure will typically have many
/// fewer register units than actual registers. MCRI::getNumRegUnits() returns
/// the number of register units in the target.
enum class MCRegUnit : unsigned;

/// Functor that maps an \c MCRegUnit to an unsigned index.
struct MCRegUnitToIndex {
  /// Argument type accepted by this functor.
  using argument_type = MCRegUnit;

  /// Return the unsigned index corresponding to register unit \p Unit.
  /// @param Unit Register unit to convert.
  /// @return The unsigned index of \p Unit.
  unsigned operator()(MCRegUnit Unit) const {
    return static_cast<unsigned>(Unit);
  }
};

/// Wrapper class representing physical registers. Should be passed by value.
class MCRegister {
  friend hash_code hash_value(const MCRegister &);
  unsigned Reg;

public:
  /// Construct a register from \p Val, defaulting to no register.
  /// @param Val Raw register number to store.
  constexpr MCRegister(unsigned Val = 0) : Reg(Val) {}

  // Register numbers can represent physical registers, virtual registers, and
  // sometimes stack slots. The unsigned values are divided into these ranges:
  //
  //   0           Not a register, can be used as a sentinel.
  //   [1;2^30)    Physical registers assigned by TableGen.
  //   [2^30;2^31) Stack slots. (Rarely used.)
  //   [2^31;2^32) Virtual registers assigned by MachineRegisterInfo.
  //
  // Further sentinels can be allocated from the small negative integers.
  // DenseMapInfo<unsigned> uses -1u and -2u.
  static_assert(std::numeric_limits<decltype(Reg)>::max() >= 0xFFFFFFFF,
                "Reg isn't large enough to hold full range.");
  /// Sentinel value meaning "not a register".
  static constexpr unsigned NoRegister = 0u;
  /// First valid physical register number.
  static constexpr unsigned FirstPhysicalReg = 1u;
  /// Last valid physical register number.
  static constexpr unsigned LastPhysicalReg = (1u << 30) - 1;

  /// Return true if the specified register number is in
  /// the physical register namespace.
  /// @param Reg Register number to test.
  /// @return True if \p Reg is a physical register number.
  static constexpr bool isPhysicalRegister(unsigned Reg) {
    return FirstPhysicalReg <= Reg && Reg <= LastPhysicalReg;
  }

  /// Return true if the specified register number is in the physical register
  /// namespace.
  /// @return True if this register is a physical register.
  constexpr bool isPhysical() const { return isPhysicalRegister(Reg); }

  /// Convert this register to its underlying unsigned register number.
  /// @return The underlying unsigned register number.
  constexpr operator unsigned() const { return Reg; }

  /// Check the provided unsigned value is a valid MCRegister.
  /// @param Val Unsigned register number to wrap.
  /// @return An \c MCRegister wrapping \p Val.
  static MCRegister from(unsigned Val) {
    assert(Val == NoRegister || isPhysicalRegister(Val));
    return MCRegister(Val);
  }

  /// Return the underlying unsigned register number.
  /// @return The underlying unsigned register number.
  constexpr unsigned id() const { return Reg; }

  /// Return true if this is a real register (not \c NoRegister).
  /// @return True if this is not \c NoRegister.
  constexpr bool isValid() const { return Reg != NoRegister; }

  /// Return true if this register equals \p Other.
  /// @param Other Register to compare against.
  /// @return True if the registers are equal.
  constexpr bool operator==(const MCRegister &Other) const {
    return Reg == Other.Reg;
  }
  /// Return true if this register differs from \p Other.
  /// @param Other Register to compare against.
  /// @return True if the registers differ.
  constexpr bool operator!=(const MCRegister &Other) const {
    return Reg != Other.Reg;
  }

  /// Return true if this register's number equals unsigned constant \p Other.
  ///
  /// Comparisons against register constants. E.g.
  /// * R == AArch64::WZR
  /// * R == 0
  /// @param Other Unsigned register constant to compare against.
  /// @return True if the register number equals \p Other.
  constexpr bool operator==(unsigned Other) const { return Reg == Other; }
  /// Return true if this register differs from unsigned constant \p Other.
  /// @param Other Unsigned register constant to compare against.
  /// @return True if the register number differs from \p Other.
  constexpr bool operator!=(unsigned Other) const { return Reg != Other; }
  /// Return true if this register's number equals integer constant \p Other.
  /// @param Other Integer register constant to compare against.
  /// @return True if the register number equals \p Other.
  constexpr bool operator==(int Other) const { return Reg == unsigned(Other); }
  /// Return true if this register differs from integer constant \p Other.
  /// @param Other Integer register constant to compare against.
  /// @return True if the register number differs from \p Other.
  constexpr bool operator!=(int Other) const { return Reg != unsigned(Other); }
  // MSVC requires that we explicitly declare these two as well.
  /// Return true if this register equals \c MCPhysReg constant \p Other.
  /// @param Other Physical register constant to compare against.
  /// @return True if the register equals \p Other.
  constexpr bool operator==(MCPhysReg Other) const {
    return Reg == unsigned(Other);
  }
  /// Return true if this register differs from \c MCPhysReg constant \p Other.
  /// @param Other Physical register constant to compare against.
  /// @return True if the register differs from \p Other.
  constexpr bool operator!=(MCPhysReg Other) const {
    return Reg != unsigned(Other);
  }
};

/// DenseMapInfo specialization for MCRegister.
template <> struct DenseMapInfo<MCRegister> {
  /// Compute a hash value for register \p Val.
  /// @param Val Register to hash.
  /// @return A hash of the register's underlying number.
  static unsigned getHashValue(const MCRegister &Val) {
    return DenseMapInfo<unsigned>::getHashValue(Val.id());
  }
  /// Return true if \p LHS and \p RHS denote the same register.
  /// @param LHS First register.
  /// @param RHS Second register.
  /// @return True if \p LHS and \p RHS are equal.
  static bool isEqual(const MCRegister &LHS, const MCRegister &RHS) {
    return LHS == RHS;
  }
};

/// Compute a hash code for register \p Reg.
/// @param Reg Register to hash.
/// @return A hash code for \p Reg.
inline hash_code hash_value(const MCRegister &Reg) {
  return hash_value(Reg.id());
}
} // namespace llvm

#endif // LLVM_MC_MCREGISTER_H
