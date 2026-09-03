//===- MCSymbolELF.h -  -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLELF_H
#define LLVM_MC_MCSYMBOLELF_H

#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"

namespace llvm {
/// Represents an ELF machine code symbol.
class MCSymbolELF : public MCSymbol {
  friend class MCAsmInfoELF;
  /// An expression describing how to calculate the size of a symbol. If a
  /// symbol has no size this field will be NULL.
  const MCExpr *SymbolSize = nullptr;

public:
  /// Construct an ELF symbol, optionally named, possibly as a temporary.
  ///
  /// \param Name - Name table entry, or null for an unnamed symbol.
  /// \param isTemporary - True if this is an assembler temporary label.
  MCSymbolELF(const MCSymbolTableEntry *Name, bool isTemporary)
      : MCSymbol(Name, isTemporary) {}
  /// Set the expression that computes this symbol's size.
  ///
  /// \param SS - Size expression, or null if the symbol has no size.
  void setSize(const MCExpr *SS) { SymbolSize = SS; }

  /// Return the expression that computes this symbol's size.
  ///
  /// \return The size expression, or null if the symbol has no size.
  const MCExpr *getSize() const { return SymbolSize; }

  /// Set the ELF symbol visibility (STV_*).
  ///
  /// \param Visibility - One of \c ELF::STV_DEFAULT, \c STV_INTERNAL,
  /// \c STV_HIDDEN, or \c STV_PROTECTED.
  LLVM_ABI void setVisibility(unsigned Visibility);
  /// Return the ELF symbol visibility (STV_*).
  ///
  /// \return The ELF symbol visibility (STV_*).
  LLVM_ABI unsigned getVisibility() const;

  /// Set the non-visibility bits of the ELF \c st_other field (STO_*).
  ///
  /// \param Other - \c st_other value; the low 5 bits must be zero.
  LLVM_ABI void setOther(unsigned Other);
  /// Return the non-visibility bits of the ELF \c st_other field (STO_*).
  ///
  /// \return The non-visibility bits of the ELF \c st_other field (STO_*).
  LLVM_ABI unsigned getOther() const;

  /// Set the ELF symbol type (STT_*).
  ///
  /// \param Type - One of \c ELF::STT_NOTYPE, \c STT_OBJECT, \c STT_FUNC,
  /// \c STT_SECTION, \c STT_COMMON, \c STT_TLS, or \c STT_GNU_IFUNC.
  LLVM_ABI void setType(unsigned Type) const;
  /// Return the ELF symbol type (STT_*).
  ///
  /// \return The ELF symbol type (STT_*).
  LLVM_ABI unsigned getType() const;

  /// Set the ELF symbol binding (STB_*).
  ///
  /// \param Binding - One of \c ELF::STB_LOCAL, \c STB_GLOBAL, \c STB_WEAK, or
  /// \c STB_GNU_UNIQUE.
  LLVM_ABI void setBinding(unsigned Binding) const;
  /// Return the ELF symbol binding (STB_*).
  ///
  /// \return The ELF symbol binding (STB_*).
  LLVM_ABI unsigned getBinding() const;

  /// Return true if an explicit binding has been set.
  ///
  /// \return True if an explicit binding has been set.
  LLVM_ABI bool isBindingSet() const;

  /// Mark this symbol as a \c .weakref alias.
  LLVM_ABI void setIsWeakref() const;
  /// Return true if this symbol is a \c .weakref alias.
  ///
  /// \return True if this symbol is a \c .weakref alias.
  LLVM_ABI bool isWeakref() const;

  /// Mark this symbol as an ELF section group signature.
  LLVM_ABI void setIsSignature() const;
  /// Return true if this symbol is an ELF section group signature.
  ///
  /// \return True if this symbol is an ELF section group signature.
  LLVM_ABI bool isSignature() const;

  /// Set whether this symbol is memory-tagged.
  ///
  /// \param Tagged - True if the symbol is memory-tagged.
  LLVM_ABI void setMemtag(bool Tagged);
  /// Return true if this symbol is memory-tagged.
  ///
  /// \return True if this symbol is memory-tagged.
  LLVM_ABI bool isMemtag() const;

private:
  void setIsBindingSet() const;
};
}

#endif
