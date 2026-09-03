//===-- llvm/CodeGen/Register.h ---------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTER_H
#define LLVM_CODEGEN_REGISTER_H

#include "llvm/MC/MCRegister.h"
#include "llvm/Support/MathExtras.h"
#include <cassert>

namespace llvm {

/// Wrapper class representing virtual and physical registers. Should be passed
/// by value.
class Register {
  unsigned Reg;

public:
  /// Construct a register from unsigned value \p Val, defaulting to no register.
  /// @param Val Raw register number to store.
  constexpr Register(unsigned Val = 0) : Reg(Val) {}
  /// Construct a register from physical register \p Val.
  /// @param Val Physical register whose id is stored.
  constexpr Register(MCRegister Val) : Reg(Val.id()) {}

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
  /// Bit width available for encoding a frame index in a stack-slot value.
  static constexpr unsigned MaxFrameIndexBitwidth = 30;
  /// Lowest unsigned value in the stack-slot register range.
  static constexpr unsigned StackSlotZero = 1u << MaxFrameIndexBitwidth;
  /// Mask for the frame-index bits within a stack-slot register value.
  static constexpr const unsigned StackSlotMask = StackSlotZero - 1;
  static_assert(StackSlotZero >= MCRegister::LastPhysicalReg);
  /// High bit set on all virtual register numbers.
  static constexpr unsigned VirtualRegFlag = 1u << 31;

  /// Return true if this is a stack slot.
  /// @return True if this register encodes a stack slot.
  constexpr bool isStack() const {
    return Register::StackSlotZero <= Reg && Reg < Register::VirtualRegFlag;
  }

  /// Convert a frame index to a stack slot register value.
  /// @param FI Frame index to encode as a stack-slot register.
  /// @return A register value representing stack slot \p FI.
  static Register index2StackSlot(int FI) {
    assert(isInt<MaxFrameIndexBitwidth>(FI) &&
           "Frame index must be at most 30 bits.");
    unsigned FIMasked = FI & Register::StackSlotMask;
    return Register(FIMasked | Register::StackSlotZero);
  }

  /// Return true if the specified register number is in
  /// the physical register namespace.
  /// @param Reg Register number to test.
  /// @return True if \p Reg is a physical register number.
  static constexpr bool isPhysicalRegister(unsigned Reg) {
    return MCRegister::isPhysicalRegister(Reg);
  }

  /// Return true if the specified register number is in
  /// the virtual register namespace.
  /// @param Reg Register number to test.
  /// @return True if \p Reg is a virtual register number.
  static constexpr bool isVirtualRegister(unsigned Reg) {
    return Reg & Register::VirtualRegFlag;
  }

  /// Convert a 0-based index to a virtual register number.
  /// This is the inverse operation of VirtReg2IndexFunctor below.
  /// @param Index Zero-based virtual register index to convert.
  /// @return The virtual register corresponding to \p Index.
  static Register index2VirtReg(unsigned Index) {
    assert(Index < (1u << 31) && "Index too large for virtual register range.");
    return Index | Register::VirtualRegFlag;
  }

  /// Return true if the specified register number is in the virtual register
  /// namespace.
  /// @return True if this is a virtual register.
  constexpr bool isVirtual() const { return isVirtualRegister(Reg); }

  /// Return true if the specified register number is in the physical register
  /// namespace.
  /// @return True if this is a physical register.
  constexpr bool isPhysical() const { return isPhysicalRegister(Reg); }

  /// Convert a virtual register number to a 0-based index. The first virtual
  /// register in a function will get the index 0.
  /// @return The zero-based index of this virtual register.
  unsigned virtRegIndex() const {
    assert(isVirtual() && "Not a virtual register");
    return Reg & ~Register::VirtualRegFlag;
  }

  /// Compute the frame index from a register value representing a stack slot.
  /// @return The frame index encoded in this stack-slot register.
  int stackSlotIndex() const {
    assert(isStack() && "Not a stack slot");
    return SignExtend32<MaxFrameIndexBitwidth>(Reg & Register::StackSlotMask);
  }

  /// Convert this register to its underlying unsigned register number.
  /// @return The underlying unsigned register number.
  constexpr operator unsigned() const { return Reg; }

  /// Return the underlying unsigned register number.
  /// @return The underlying unsigned register number.
  constexpr unsigned id() const { return Reg; }

  /// Convert this register to an \c MCRegister with the same number.
  /// @return An \c MCRegister wrapping this register's number.
  constexpr operator MCRegister() const { return MCRegister(Reg); }

  /// Utility to check-convert this value to a MCRegister. The caller is
  /// expected to have already validated that this Register is, indeed,
  /// physical.
  /// @return An \c MCRegister wrapping this physical register.
  MCRegister asMCReg() const {
    assert(!isValid() || isPhysical());
    return MCRegister(Reg);
  }

  /// Return true if this is a real register (not \c MCRegister::NoRegister).
  /// @return True if this is not \c MCRegister::NoRegister.
  constexpr bool isValid() const { return Reg != MCRegister::NoRegister; }

  /// Return true if this register equals \p Other.
  /// @param Other Register to compare against.
  /// @return True if the registers are equal.
  constexpr bool operator==(const Register &Other) const {
    return Reg == Other.Reg;
  }
  /// Return true if this register differs from \p Other.
  /// @param Other Register to compare against.
  /// @return True if the registers differ.
  constexpr bool operator!=(const Register &Other) const {
    return Reg != Other.Reg;
  }
  /// Return true if this register equals \p Other.
  /// @param Other Physical register to compare against.
  /// @return True if the registers are equal.
  constexpr bool operator==(const MCRegister &Other) const {
    return Reg == Other.id();
  }
  /// Return true if this register differs from \p Other.
  /// @param Other Physical register to compare against.
  /// @return True if the registers differ.
  constexpr bool operator!=(const MCRegister &Other) const {
    return Reg != Other.id();
  }

  /// Return true if this register's number equals unsigned constant \p Other.
  ///
  /// Comparisons against register constants. E.g.
  /// * R == AArch64::WZR
  /// * R == 0
  /// @param Other Unsigned register constant to compare against.
  /// @return True if the register number equals \p Other.
  constexpr bool operator==(unsigned Other) const { return Reg == Other; }
  /// Return true if this register's number differs from unsigned constant \p Other.
  /// @param Other Unsigned register constant to compare against.
  /// @return True if the register number differs from \p Other.
  constexpr bool operator!=(unsigned Other) const { return Reg != Other; }
  /// Return true if this register's number equals integer constant \p Other.
  /// @param Other Integer register constant to compare against.
  /// @return True if the register number equals \p Other.
  constexpr bool operator==(int Other) const { return Reg == unsigned(Other); }
  /// Return true if this register's number differs from integer constant \p Other.
  /// @param Other Integer register constant to compare against.
  /// @return True if the register number differs from \p Other.
  constexpr bool operator!=(int Other) const { return Reg != unsigned(Other); }
  // MSVC requires that we explicitly declare these two as well.
  /// Compare this register to a physical register constant.
  /// @param Other Physical register constant to compare against.
  /// @return True if the register equals \p Other.
  constexpr bool operator==(MCPhysReg Other) const {
    return Reg == unsigned(Other);
  }
  /// Return true if this register differs from physical register constant \p Other.
  /// @param Other Physical register constant to compare against.
  /// @return True if the register differs from \p Other.
  constexpr bool operator!=(MCPhysReg Other) const {
    return Reg != unsigned(Other);
  }

  /// Operators to move from one register to another nearby register by adding
  /// an offset.
  /// @return Reference to this register after incrementing.
  Register &operator++() {
    assert(isValid());
    ++Reg;
    return *this;
  }

  /// Post-increment this register number and return the previous value.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return Copy of this register before incrementing.
  Register operator++(int Unused) {
    Register R(*this);
    ++(*this);
    return R;
  }

  /// Advance this register number by \p RHS.
  /// @param RHS Amount to add to the register number.
  /// @return Reference to this register after adding \p RHS.
  Register &operator+=(unsigned RHS) {
    assert(isValid());
    Reg += RHS;
    return *this;
  }
};

/// DenseMapInfo specialization for Register.
template <> struct DenseMapInfo<Register> {
  /// Compute a hash value for register \p Val.
  /// @param Val Register to hash.
  /// @return A hash of the register's underlying number.
  static unsigned getHashValue(const Register &Val) {
    return DenseMapInfo<unsigned>::getHashValue(Val.id());
  }
  /// Return true if \p LHS and \p RHS denote the same register.
  /// @param LHS First register.
  /// @param RHS Second register.
  /// @return True if \p LHS and \p RHS are equal.
  static bool isEqual(const Register &LHS, const Register &RHS) {
    return LHS == RHS;
  }
};

/// Wrapper class representing a virtual register or register unit.
class VirtRegOrUnit {
  unsigned VRegOrUnit;

public:
  /// Construct from register unit \p Unit.
  /// @param Unit Register unit to store.
  constexpr explicit VirtRegOrUnit(MCRegUnit Unit)
      : VRegOrUnit(static_cast<unsigned>(Unit)) {
    assert(!Register::isVirtualRegister(VRegOrUnit));
  }

  /// Construct from virtual register \p Reg.
  /// @param Reg Virtual register to store.
  constexpr explicit VirtRegOrUnit(Register Reg) : VRegOrUnit(Reg.id()) {
    assert(Reg.isVirtual());
  }

  /// Deleted constructor that rejects implicit conversions to \c Register.
  /// @param Ignored Value of a type that would otherwise convert to Register.
  template <typename T> explicit VirtRegOrUnit(T Ignored) = delete;

  /// Return true if this holds a virtual register rather than a register unit.
  /// @return True if this holds a virtual register.
  constexpr bool isVirtualReg() const {
    return Register::isVirtualRegister(VRegOrUnit);
  }

  /// Return the held value as a register unit.
  /// @return The held register unit.
  constexpr MCRegUnit asMCRegUnit() const {
    assert(!isVirtualReg() && "Not a register unit");
    return static_cast<MCRegUnit>(VRegOrUnit);
  }

  /// Return the held value as a virtual register.
  /// @return The held virtual register.
  constexpr Register asVirtualReg() const {
    assert(isVirtualReg() && "Not a virtual register");
    return Register(VRegOrUnit);
  }

  /// Return true if this equals \p Other.
  /// @param Other Value to compare against.
  /// @return True if the values are equal.
  constexpr bool operator==(const VirtRegOrUnit &Other) const {
    return VRegOrUnit == Other.VRegOrUnit;
  }

  /// Return true if this is ordered before \p Other.
  /// @param Other Value to compare against.
  /// @return True if this is ordered before \p Other.
  constexpr bool operator<(const VirtRegOrUnit &Other) const {
    return VRegOrUnit < Other.VRegOrUnit;
  }
};

} // namespace llvm

#endif // LLVM_CODEGEN_REGISTER_H
