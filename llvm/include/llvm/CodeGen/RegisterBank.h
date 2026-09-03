//==-- llvm/CodeGen/RegisterBank.h - Register Bank ---------------*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
/// \file This file declares the API of register banks.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_REGISTERBANK_H
#define LLVM_CODEGEN_REGISTERBANK_H

#include "llvm/Support/Compiler.h"
#include <cstdint>

namespace llvm {
// Forward declarations.
class RegisterBankInfo;
class raw_ostream;
class MCRegisterClass;
using TargetRegisterClass = MCRegisterClass;
class TargetRegisterInfo;

/// This class implements the register bank concept.
/// Two instances of RegisterBank must have different ID.
/// This property is enforced by the RegisterBankInfo class.
class RegisterBank {
private:
  unsigned ID;
  unsigned NumRegClasses;
  const char *Name;
  const uint32_t *CoveredClasses;

  /// Only the RegisterBankInfo can initialize RegisterBank properly.
  friend RegisterBankInfo;

public:
  /// Construct a register bank with the given identity and coverage.
  ///
  /// \param ID Unique identifier for this register bank.
  /// \param Name User-friendly name, intended for debugging.
  /// \param CoveredClasses Bit mask of register classes covered by this bank.
  /// \param NumRegClasses Number of register classes represented in
  /// \p CoveredClasses.
  constexpr RegisterBank(unsigned ID, const char *Name,
                         const uint32_t *CoveredClasses, unsigned NumRegClasses)
      : ID(ID), NumRegClasses(NumRegClasses), Name(Name),
        CoveredClasses(CoveredClasses) {}

  /// Get the identifier of this register bank.
  ///
  /// \return The unique identifier of this register bank.
  unsigned getID() const { return ID; }

  /// Get a user friendly name of this register bank.
  /// Should be used only for debugging purposes.
  ///
  /// \return The name of this register bank.
  const char *getName() const { return Name; }

  /// Check if this register bank is valid. In other words,
  /// if it has been properly constructed.
  ///
  /// \note This method does not check anything when assertions are disabled.
  ///
  /// \param RBI Register bank info used to validate this bank.
  /// \param TRI Target register info used to validate covered classes.
  /// \return True is the check was successful.
  LLVM_ABI bool verify(const RegisterBankInfo &RBI,
                       const TargetRegisterInfo &TRI) const;

  /// Check whether this register bank covers \p RC.
  ///
  /// In other words, check if this register bank fully covers
  /// the registers that \p RC contains.
  ///
  /// \param RC Register class to test for coverage.
  /// \return True if this register bank covers \p RC.
  LLVM_ABI bool covers(const TargetRegisterClass &RC) const;

  /// Check whether \p OtherRB is the same as this.
  ///
  /// \param OtherRB Register bank to compare against.
  /// \return True if \p OtherRB is the same as this.
  LLVM_ABI bool operator==(const RegisterBank &OtherRB) const;
  /// Check whether \p OtherRB is different from this.
  ///
  /// \param OtherRB Register bank to compare against.
  /// \return True if \p OtherRB is different from this.
  bool operator!=(const RegisterBank &OtherRB) const {
    return !this->operator==(OtherRB);
  }

  /// Dump the register mask on dbgs() stream.
  ///
  /// The dump is verbose.
  ///
  /// \param TRI Optional target register info used to name covered classes.
  LLVM_ABI void dump(const TargetRegisterInfo *TRI = nullptr) const;

  /// Print the register mask on \p OS.
  ///
  /// If \p IsForDebug is false, then only the name of the register bank
  /// is printed. Otherwise, all the fields are printed.
  /// \p TRI is then used to print the name of the register classes that
  /// this register bank covers.
  ///
  /// \param OS Output stream to print to.
  /// \param IsForDebug When true, print all fields; otherwise only the name.
  /// \param TRI Optional target register info used to name covered classes.
  LLVM_ABI void print(raw_ostream &OS, bool IsForDebug = false,
                      const TargetRegisterInfo *TRI = nullptr) const;
};

/// Write \p RegBank to \p OS.
///
/// \param OS Output stream.
/// \param RegBank Register bank to print.
/// \return A reference to \p OS.
inline raw_ostream &operator<<(raw_ostream &OS, const RegisterBank &RegBank) {
  RegBank.print(OS);
  return OS;
}
} // End namespace llvm.

#endif
