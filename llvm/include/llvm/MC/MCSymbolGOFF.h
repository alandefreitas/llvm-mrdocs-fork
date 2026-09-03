//===-- llvm/MC/MCSymbolGOFF.h - GOFF Machine Code Symbols ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file contains the MCSymbolGOFF class
///
//===----------------------------------------------------------------------===//
#ifndef LLVM_MC_MCSYMBOLGOFF_H
#define LLVM_MC_MCSYMBOLGOFF_H

#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/MC/MCDirectives.h"
#include "llvm/MC/MCGOFFAttributes.h"
#include "llvm/MC/MCSectionGOFF.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/MC/MCSymbolTableEntry.h"
#include "llvm/Support/Alignment.h"

namespace llvm {

class MCContext;

/// A machine code symbol for the GOFF (z/OS) object file format.
class MCSymbolGOFF : public MCSymbol {

  StringRef ExternalName; // Alternate external name.

  // Associated data area of the section. Needs to be emitted first.
  MCSectionGOFF *ADA = nullptr;

  GOFF::ESDExecutable CodeData = GOFF::ESDExecutable::ESD_EXE_Unspecified;
  GOFF::ESDLinkageType Linkage = GOFF::ESDLinkageType::ESD_LT_XPLink;

  enum SymbolFlags : uint16_t {
    SF_Hidden = 0x01,  // Symbol is hidden, aka not exported.
    SF_Weak = 0x02,    // Symbol is weak.
    SF_Indirect = 0x4, // Symbol referenced indirectly.
  };

public:
  /// Construct a GOFF symbol with the given name and temporary flag.
  /// @param Name Symbol table entry that owns the symbol name, or null.
  /// @param IsTemporary True if this is an assembler temporary symbol.
  MCSymbolGOFF(const MCSymbolTableEntry *Name, bool IsTemporary)
      : MCSymbol(Name, IsTemporary) {}

  /// Set the associated data area (ADA) section for this symbol.
  /// @param AssociatedDataArea Non-null ADA section that must be emitted first.
  void setADA(MCSectionGOFF *AssociatedDataArea) {
    assert(AssociatedDataArea && "ADA must be non-null");
    ADA = AssociatedDataArea;
    AssociatedDataArea->RequiresNonZeroLength = true;
  }
  /// Return the associated data area section, or null if none was set.
  /// @return The ADA section, or null if none was set.
  MCSectionGOFF *getADA() const { return ADA; }

  /// Return true if this symbol is visible outside this translation unit.
  /// @return True if the symbol is external.
  bool isExternal() const { return IsExternal; }
  /// Set whether this symbol is visible outside this translation unit.
  /// @param Value True if the symbol is external.
  void setExternal(bool Value) const { IsExternal = Value; }

  /// Return true if an alternate external name has been set.
  /// @return True if an alternate external name has been set.
  bool hasExternalName() const { return !ExternalName.empty(); }
  /// Set the alternate external name for this symbol.
  /// @param Name Alternate external name to store.
  void setExternalName(StringRef Name) { ExternalName = Name; }
  /// Return the external name, or the symbol name if none was set.
  /// @return The alternate external name, or the symbol name if none was set.
  StringRef getExternalName() const {
    return hasExternalName() ? ExternalName : getName();
  }

  /// Set whether this symbol is hidden (not exported).
  /// @param Value True to mark the symbol hidden; defaults to true.
  void setHidden(bool Value = true) {
    modifyFlags(Value ? SF_Hidden : 0, SF_Hidden);
  }
  /// Return true if this symbol is hidden (not exported).
  /// @return True if the symbol is hidden.
  bool isHidden() const { return getFlags() & SF_Hidden; }
  /// Return true if this symbol is exported (not hidden).
  /// @return True if the symbol is exported.
  bool isExported() const { return !isHidden(); }

  /// Set whether this symbol is referenced indirectly.
  /// @param Value True to mark the symbol indirect; defaults to true.
  void setIndirect(bool Value = true) {
    modifyFlags(Value ? SF_Indirect : 0, SF_Indirect);
  }
  /// Return true if this symbol is referenced indirectly.
  /// @return True if the symbol is referenced indirectly.
  bool isIndirect() const { return getFlags() & SF_Indirect; }

  /// Set whether this symbol is weak.
  /// @param Value True to mark the symbol weak; defaults to true.
  void setWeak(bool Value = true) { modifyFlags(Value ? SF_Weak : 0, SF_Weak); }
  /// Return true if this symbol is weak.
  /// @return True if the symbol is weak.
  bool isWeak() const { return getFlags() & SF_Weak; }

  /// Set whether this symbol represents code or data.
  /// @param Value GOFF ESD executable classification for the symbol.
  void setCodeData(GOFF::ESDExecutable Value) { CodeData = Value; }
  /// Return whether this symbol represents code or data.
  /// @return The GOFF ESD executable classification for the symbol.
  GOFF::ESDExecutable getCodeData() const { return CodeData; }

  /// Set the GOFF linkage type for this symbol.
  /// @param Value Linkage type to store (for example XPLink or OS).
  void setLinkage(GOFF::ESDLinkageType Value) { Linkage = Value; }
  /// Return the GOFF linkage type for this symbol.
  /// @return The GOFF linkage type for this symbol.
  GOFF::ESDLinkageType getLinkage() const { return Linkage; }

  /// Return the GOFF binding scope derived from definition and visibility.
  /// @return The GOFF binding scope for this symbol.
  GOFF::ESDBindingScope getBindingScope() const {
    return (isExternal() || !isDefined()) ? isExported()
                                                ? GOFF::ESD_BSC_ImportExport
                                                : GOFF::ESD_BSC_Library
                                          : GOFF::ESD_BSC_Section;
  }

  /// Return the GOFF binding strength (weak or strong).
  /// @return The GOFF binding strength (weak or strong).
  GOFF::ESDBindingStrength getBindingStrength() const {
    return isWeak() ? GOFF::ESDBindingStrength::ESD_BST_Weak
                    : GOFF::ESDBindingStrength::ESD_BST_Strong;
  }

  /// Apply a machine-code symbol attribute if supported for GOFF.
  /// @param Attribute Attribute to apply to this symbol.
  /// @return True if the attribute was recognized and applied.
  LLVM_ABI bool setSymbolAttribute(MCSymbolAttr Attribute);

  /// Return the PR section to use when emitting this symbol as a common symbol.
  /// @param Ctx Context used to create the GOFF SD/ED/PR section nest.
  /// @param ByteAlignment Alignment to apply to the element-definition section.
  /// @return The PR section for emitting this symbol as a common symbol.
  LLVM_ABI MCSectionGOFF *getSectionForCommonSymbol(MCContext &Ctx,
                                                    Align ByteAlignment);

  /// Return true if this symbol is defined in an element-definition section.
  /// @return True if the symbol is defined in an ED section.
  bool isInEDSection() const {
    return isInSection() && static_cast<MCSectionGOFF &>(getSection()).isED();
  }
};
} // end namespace llvm

#endif
