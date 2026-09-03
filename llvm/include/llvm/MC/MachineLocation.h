//===- llvm/MC/MachineLocation.h --------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// The MachineLocation class is used to represent a simple location in a machine
// frame.  Locations will be one of two forms; a register or an address formed
// from a base address plus an offset.  Register indirection can be specified by
// explicitly passing an offset to the constructor.
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MACHINELOCATION_H
#define LLVM_MC_MACHINELOCATION_H

#include <cstdint>
#include <cassert>

namespace llvm {

/// Represents a simple location in a machine frame.
///
/// Locations are either a register or an address formed from a base address
/// plus an offset. Register indirection can be specified by explicitly passing
/// an offset to the constructor.
class MachineLocation {
private:
  bool IsRegister = false;              ///< True if location is a register.
  unsigned Register = 0;                ///< gcc/gdb register number.

public:
  /// Named constants for special register numbers used by locations.
  enum : uint32_t {
    /// Target register number for an abstract frame pointer.
    ///
    /// The value is an arbitrary value that doesn't collide with any real
    /// target register.
    VirtualFP = ~0U
  };

  /// Construct an empty machine location.
  MachineLocation() = default;
  /// Create a direct register location.
  /// @param R Register number for this location.
  /// @param Indirect If true, treat \p R as a register-indirect location.
  explicit MachineLocation(unsigned R, bool Indirect = false)
      : IsRegister(!Indirect), Register(R) {}

  /// Return true if this location equals \p Other.
  /// @param Other Location to compare against.
  /// @return true iff both locations have the same register and form.
  bool operator==(const MachineLocation &Other) const {
    return IsRegister == Other.IsRegister && Register == Other.Register;
  }

  // Accessors.
  /// Return true if this is a register-indirect location.
  /// @return true iff this is a register-indirect location.
  bool isIndirect()      const { return !IsRegister; }
  /// Return true if this location is a register.
  /// @return true iff this location is a register.
  bool isReg()           const { return IsRegister; }
  /// Return the register number for this location.
  /// @return The gcc/gdb register number for this location.
  unsigned getReg()      const { return Register; }
  /// Set whether this location is a register.
  /// @param Is True if the location should be treated as a register.
  void setIsRegister(bool Is)  { IsRegister = Is; }
  /// Set the register number for this location.
  /// @param R Register number to store.
  void setRegister(unsigned R) { Register = R; }
};

/// Return true if \p LHS and \p RHS denote different locations.
/// @param LHS First location to compare.
/// @param RHS Second location to compare.
/// @return true iff \p LHS and \p RHS are not equal.
inline bool operator!=(const MachineLocation &LHS, const MachineLocation &RHS) {
  return !(LHS == RHS);
}

} // end namespace llvm

#endif // LLVM_MC_MACHINELOCATION_H
