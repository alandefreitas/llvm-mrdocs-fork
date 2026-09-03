//===-- llvm/MC/MCValue.h - MCValue class -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the declaration of the MCValue class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCVALUE_H
#define LLVM_MC_MCVALUE_H

#include "llvm/MC/MCExpr.h"
#include "llvm/Support/DataTypes.h"

namespace llvm {
class raw_ostream;

/// Represents a relocatable expression in its most general form:
/// \c Specifier(SymA - SymB + Imm).
///
/// Not all targets support SymB. For PC-relative relocations, a specifier is
/// typically used instead of setting SymB to DOT.
///
/// This class must remain a simple POD value class, as it needs to reside in
/// unions and similar structures.
class MCValue {
  const MCSymbol *SymA = nullptr, *SymB = nullptr;
  int64_t Cst = 0;
  uint32_t Specifier = 0;

  void print(raw_ostream &OS) const;

  /// Print the value to stderr.
  void dump() const;

public:
  friend class MCAssembler;
  friend class MCExpr;
  /// Construct a zero absolute MCValue.
  MCValue() = default;
  /// Return the constant immediate of this value.
  ///
  /// \return The constant immediate of this value.
  int64_t getConstant() const { return Cst; }
  /// Set the constant immediate of this value.
  ///
  /// \param C - New constant immediate.
  void setConstant(int64_t C) { Cst = C; }
  /// Return the relocation specifier of this value.
  ///
  /// \return The relocation specifier of this value.
  uint32_t getSpecifier() const { return Specifier; }
  /// Set the relocation specifier of this value.
  ///
  /// \param S - New relocation specifier.
  void setSpecifier(uint32_t S) { Specifier = S; }

  /// Return the additive symbol (SymA) of this value.
  ///
  /// \return The additive symbol, or nullptr.
  const MCSymbol *getAddSym() const { return SymA; }
  /// Set the additive symbol (SymA) of this value.
  ///
  /// \param A - New additive symbol, or nullptr.
  void setAddSym(const MCSymbol *A) { SymA = A; }
  /// Return the subtractive symbol (SymB) of this value.
  ///
  /// \return The subtractive symbol, or nullptr.
  const MCSymbol *getSubSym() const { return SymB; }

  /// Is this an absolute (as opposed to relocatable) value.
  ///
  /// \return True if this value has no additive or subtractive symbol.
  bool isAbsolute() const { return !SymA && !SymB; }

  /// Construct an MCValue from symbols, a constant, and a specifier.
  ///
  /// \param SymA - Additive symbol, or nullptr.
  /// \param SymB - Subtractive symbol, or nullptr.
  /// \param Val - Constant immediate.
  /// \param Specifier - Relocation specifier.
  /// \return An MCValue for Specifier(SymA - SymB + Val).
  static MCValue get(const MCSymbol *SymA, const MCSymbol *SymB = nullptr,
                     int64_t Val = 0, uint32_t Specifier = 0) {
    MCValue R;
    R.Cst = Val;
    R.SymA = SymA;
    R.SymB = SymB;
    R.Specifier = Specifier;
    return R;
  }

  /// Construct an absolute MCValue from a constant immediate.
  ///
  /// \param Val - Constant immediate.
  /// \return An absolute MCValue holding \p Val.
  static MCValue get(int64_t Val) {
    MCValue R;
    R.Cst = Val;
    R.SymA = nullptr;
    R.SymB = nullptr;
    R.Specifier = 0;
    return R;
  }

};

} // end namespace llvm

#endif
