//===- MCSymbolCOFF.h -  ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSYMBOLCOFF_H
#define LLVM_MC_MCSYMBOLCOFF_H

#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"
#include <cstdint>

namespace llvm {

/// MCSymbol specialization for COFF object files.
class MCSymbolCOFF : public MCSymbol {
  /// This corresponds to the e_type field of the COFF symbol.
  mutable uint16_t Type = 0;

  enum SymbolFlags : uint16_t {
    SF_ClassMask = 0x00FF,
    SF_ClassShift = 0,

    SF_SafeSEH = 0x0100,
    SF_WeakExternalCharacteristicsMask = 0x0E00,
    SF_WeakExternalCharacteristicsShift = 9,
  };

public:
  /// Construct a COFF symbol, optionally named, possibly as a temporary.
  ///
  /// \param Name - Name table entry, or null for an unnamed symbol.
  /// \param isTemporary - True if this is an assembler temporary label.
  MCSymbolCOFF(const MCSymbolTableEntry *Name, bool isTemporary)
      : MCSymbol(Name, isTemporary) {}

  /// Return true if this symbol is visible outside this translation unit.
  ///
  /// \return True if the symbol is external.
  bool isExternal() const { return IsExternal; }
  /// Set whether this symbol is visible outside this translation unit.
  ///
  /// \param Value - True if the symbol is external.
  void setExternal(bool Value) const { IsExternal = Value; }

  /// Return the COFF symbol type (the e_type field).
  ///
  /// \return The COFF symbol type value.
  uint16_t getType() const {
    return Type;
  }
  /// Set the COFF symbol type (the e_type field).
  ///
  /// \param Ty - The new COFF type value.
  void setType(uint16_t Ty) const {
    Type = Ty;
  }

  /// Return the COFF storage class encoded in the symbol flags.
  ///
  /// \return The COFF storage class value.
  uint16_t getClass() const {
    return (getFlags() & SF_ClassMask) >> SF_ClassShift;
  }
  /// Set the COFF storage class encoded in the symbol flags.
  ///
  /// \param StorageClass - The COFF storage class value.
  void setClass(uint16_t StorageClass) const {
    modifyFlags(StorageClass << SF_ClassShift, SF_ClassMask);
  }

  /// Return the weak-external characteristics encoded in the symbol flags.
  ///
  /// \return The weak-external characteristics value.
  COFF::WeakExternalCharacteristics getWeakExternalCharacteristics() const {
    return static_cast<COFF::WeakExternalCharacteristics>((getFlags() & SF_WeakExternalCharacteristicsMask) >>
           SF_WeakExternalCharacteristicsShift);
  }
  /// Set the weak-external characteristics encoded in the symbol flags.
  ///
  /// \param Characteristics - The weak-external characteristics value.
  void setWeakExternalCharacteristics(COFF::WeakExternalCharacteristics Characteristics) const {
    modifyFlags(Characteristics << SF_WeakExternalCharacteristicsShift,
                SF_WeakExternalCharacteristicsMask);
  }
  /// Set whether this symbol is a weak external.
  ///
  /// \param WeakExt - True if the symbol is weak external.
  void setIsWeakExternal(bool WeakExt) const {
    IsWeakExternal = WeakExt;
  }

  /// Return true if this symbol is registered for SafeSEH.
  ///
  /// \return True if the symbol is registered for SafeSEH.
  bool isSafeSEH() const {
    return getFlags() & SF_SafeSEH;
  }
  /// Mark this symbol as registered for SafeSEH.
  void setIsSafeSEH() const {
    modifyFlags(SF_SafeSEH, SF_SafeSEH);
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCSYMBOLCOFF_H
