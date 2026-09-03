//===-- llvm/MC/MCSectionGOFF.h - GOFF Machine Code Sections ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file declares the MCSectionGOFF class, which contains all of the
/// necessary machine code sections for the GOFF file format.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONGOFF_H
#define LLVM_MC_MCSECTIONGOFF_H

#include "llvm/BinaryFormat/GOFF.h"
#include "llvm/MC/MCGOFFAttributes.h"
#include "llvm/MC/MCSection.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class MCExpr;

/// A machine code section for the GOFF (z/OS) object file format.
class LLVM_ABI MCSectionGOFF final : public MCSection {
  StringRef ExternalName; // Alternate external name.

  // Parent of this section. Implies that the parent is emitted first.
  MCSectionGOFF *Parent;

  // The attributes of the GOFF symbols.
  union {
    /// Section-definition (SD) attributes for this section.
    GOFF::SDAttr SDAttributes;
    /// Element-definition (ED) attributes for this section.
    GOFF::EDAttr EDAttributes;
    /// Part-reference (PR) attributes for this section.
    GOFF::PRAttr PRAttributes;
  };

  // The type of this section.
  GOFF::ESDSymbolType SymbolType;

  // This section is a BSS section.
  unsigned IsBSS : 1;

  // Indicates that the PR symbol needs to set the length of the section to a
  // non-zero value. This is only a problem with the ADA PR - the binder will
  // generate an error in this case.
  unsigned RequiresNonZeroLength : 1;

  // Set to true if the section definition was already emitted.
  mutable unsigned Emitted : 1;

  friend class MCContext;
  friend class MCSymbolGOFF;

  MCSectionGOFF(StringRef Name, SectionKind K, bool IsVirtual,
                GOFF::SDAttr SDAttributes, MCSectionGOFF *Parent)
      : MCSection(Name, K.isText(), IsVirtual, nullptr), Parent(Parent),
        SDAttributes(SDAttributes), SymbolType(GOFF::ESD_ST_SectionDefinition),
        IsBSS(K.isBSS()), RequiresNonZeroLength(0), Emitted(0) {}

  MCSectionGOFF(StringRef Name, SectionKind K, bool IsVirtual,
                GOFF::EDAttr EDAttributes, MCSectionGOFF *Parent)
      : MCSection(Name, K.isText(), IsVirtual, nullptr), Parent(Parent),
        EDAttributes(EDAttributes), SymbolType(GOFF::ESD_ST_ElementDefinition),
        IsBSS(K.isBSS()), RequiresNonZeroLength(0), Emitted(0) {}

  MCSectionGOFF(StringRef Name, SectionKind K, bool IsVirtual,
                GOFF::PRAttr PRAttributes, MCSectionGOFF *Parent)
      : MCSection(Name, K.isText(), IsVirtual, nullptr), Parent(Parent),
        PRAttributes(PRAttributes), SymbolType(GOFF::ESD_ST_PartReference),
        IsBSS(K.isBSS()), RequiresNonZeroLength(0), Emitted(0) {}

public:
  /// Return the parent section, which is emitted before this one.
  /// @return The parent section, or null if none.
  MCSectionGOFF *getParent() const { return Parent; }

  /// Return true if this is a BSS section.
  /// @return True if this is a BSS section.
  bool isBSS() const { return IsBSS; }

  /// Return the GOFF ESD symbol type of this section.
  /// @return The GOFF ESD symbol type of this section.
  GOFF::ESDSymbolType getSymbolType() const { return SymbolType; }

  /// Return true if this is a section-definition (SD) section.
  /// @return True if this is a section-definition (SD) section.
  bool isSD() const { return SymbolType == GOFF::ESD_ST_SectionDefinition; }
  /// Return true if this is an element-definition (ED) section.
  /// @return True if this is an element-definition (ED) section.
  bool isED() const { return SymbolType == GOFF::ESD_ST_ElementDefinition; }
  /// Return true if this is a part-reference (PR) section.
  /// @return True if this is a part-reference (PR) section.
  bool isPR() const { return SymbolType == GOFF::ESD_ST_PartReference; }

  /// Return the section-definition attributes; only valid for SD sections.
  /// @return The section-definition attributes for this SD section.
  GOFF::SDAttr getSDAttributes() const {
    assert(isSD() && "Not a SD section");
    return SDAttributes;
  }
  /// Return the element-definition attributes; only valid for ED sections.
  /// @return The element-definition attributes for this ED section.
  GOFF::EDAttr getEDAttributes() const {
    assert(isED() && "Not a ED section");
    return EDAttributes;
  }

  /// Return the ESD alignment for an ED section from the MCSection alignment.
  ///
  /// Only defined for ED sections.
  /// @return The ESD alignment derived from this section's alignment.
  GOFF::ESDAlignment getEDAlignment() const {
    assert(isED() && "Not a ED section");
    uint8_t Log = Log2(getAlign());
    if (Log > GOFF::ESD_ALIGN_4Kpage)
      reportFatalUsageError("Unsupported alignment");
    return static_cast<GOFF::ESDAlignment>(Log);
  }

  /// Return the part-reference attributes; only valid for PR sections.
  /// @return The part-reference attributes for this PR section.
  GOFF::PRAttr getPRAttributes() const {
    assert(isPR() && "Not a PR section");
    return PRAttributes;
  }

  /// Return the text style for this section.
  ///
  /// Only defined for ED and PR sections.
  /// @return The ESD text style for this section.
  GOFF::ESDTextStyle getTextStyle() const {
    assert((isED() || isPR() || isBssSection()) && "Expect ED or PR section");
    if (isED())
      return EDAttributes.TextStyle;
    if (isPR())
      return getParent()->getEDAttributes().TextStyle;
    // Virtual sections have no data, so byte orientation is fine.
    return GOFF::ESD_TS_ByteOriented;
  }

  /// Return true if this PR section must be emitted with a non-zero length.
  /// @return True if this PR section must be emitted with a non-zero length.
  bool requiresNonZeroLength() const { return RequiresNonZeroLength; }

  /// Return true if the section definition has already been emitted.
  /// @return True if the section definition has already been emitted.
  bool isEmitted() const { return Emitted; }
  /// Mark the section definition as emitted.
  void setEmitted() const { Emitted = true; }

  /// Set the section name.
  /// @param SectionName New name for this section.
  void setName(StringRef SectionName) { Name = SectionName; }

  /// Return true if an alternate external name has been set.
  /// @return True if an alternate external name has been set.
  bool hasExternalName() const { return !ExternalName.empty(); }
  /// Set the alternate external name for this section.
  /// @param Name Alternate external name to store.
  void setExternalName(StringRef Name) { ExternalName = Name; }
  /// Return the external name, or the section name if none was set.
  /// @return The external name, or the section name if none was set.
  StringRef getExternalName() const {
    return hasExternalName() ? ExternalName : getName();
  }
};
} // end namespace llvm

#endif
