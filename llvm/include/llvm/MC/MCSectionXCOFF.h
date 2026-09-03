//===- MCSectionXCOFF.h - XCOFF Machine Code Sections -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionXCOFF class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONXCOFF_H
#define LLVM_MC_MCSECTIONXCOFF_H

#include "llvm/BinaryFormat/XCOFF.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/MCSymbolXCOFF.h"

namespace llvm {

/// Represents an XCOFF control section (csect) or DWARF section.
///
/// A csect is the smallest unit of data/code relocated as a single block. A
/// csect can either be:
/// 1) Initialized: The Type will be XTY_SD, and the symbols inside the csect
///    will have a label definition representing their offset within the csect.
/// 2) Uninitialized: The Type will be XTY_CM, it will contain a single symbol,
///    and may not contain label definitions.
/// 3) An external reference providing a symbol table entry for a symbol
///    contained in another XCOFF object file. External reference csects are not
///    implemented yet.
class MCSectionXCOFF final : public MCSection {
  friend class MCContext;
  friend class MCAsmInfoXCOFF;

  std::optional<XCOFF::CsectProperties> CsectProp;
  MCSymbolXCOFF *const QualName;
  StringRef SymbolTableName;
  std::optional<XCOFF::DwarfSectionSubtypeFlags> DwarfSubtypeFlags;
  bool MultiSymbolsAllowed;
  SectionKind Kind;
  static constexpr unsigned DefaultAlignVal = 4;
  static constexpr unsigned DefaultTextAlignVal = 32;

  // XTY_CM sections are virtual except for toc-data symbols.
  MCSectionXCOFF(StringRef Name, XCOFF::StorageMappingClass SMC,
                 XCOFF::SymbolType ST, SectionKind K, MCSymbolXCOFF *QualName,
                 MCSymbol *Begin, StringRef SymbolTableName,
                 bool MultiSymbolsAllowed)
      : MCSection(Name, K.isText(),
                  /*IsVirtual=*/ST == XCOFF::XTY_CM && SMC != XCOFF::XMC_TD,
                  Begin),
        CsectProp(XCOFF::CsectProperties(SMC, ST)), QualName(QualName),
        SymbolTableName(SymbolTableName), DwarfSubtypeFlags(std::nullopt),
        MultiSymbolsAllowed(MultiSymbolsAllowed), Kind(K) {
    assert(
        (ST == XCOFF::XTY_SD || ST == XCOFF::XTY_CM || ST == XCOFF::XTY_ER) &&
        "Invalid or unhandled type for csect.");
    assert(QualName != nullptr && "QualName is needed.");
    if (SMC == XCOFF::XMC_UL)
      assert((ST == XCOFF::XTY_CM || ST == XCOFF::XTY_ER) &&
             "Invalid csect type for storage mapping class XCOFF::XMC_UL");

    QualName->setRepresentedCsect(this);
    QualName->setStorageClass(XCOFF::C_HIDEXT);
    if (ST != XCOFF::XTY_ER) {
      // For a csect for program code, set the alignment to 32 bytes by default.
      // For other csects, set the alignment to 4 bytes by default.
      if (SMC == XCOFF::XMC_PR)
        setAlignment(Align(DefaultTextAlignVal));
      else
        setAlignment(Align(DefaultAlignVal));
    }
  }

  // DWARF sections are never virtual.
  MCSectionXCOFF(StringRef Name, SectionKind K, MCSymbolXCOFF *QualName,
                 XCOFF::DwarfSectionSubtypeFlags DwarfSubtypeFlags,
                 MCSymbol *Begin, StringRef SymbolTableName,
                 bool MultiSymbolsAllowed)
      : MCSection(Name, K.isText(), /*IsVirtual=*/false, Begin),
        QualName(QualName), SymbolTableName(SymbolTableName),
        DwarfSubtypeFlags(DwarfSubtypeFlags),
        MultiSymbolsAllowed(MultiSymbolsAllowed), Kind(K) {
    assert(QualName != nullptr && "QualName is needed.");

    // FIXME: use a more meaningful name for non csect sections.
    QualName->setRepresentedCsect(this);

    // Use default text alignment as the alignment for DWARF sections.
    setAlignment(Align(DefaultTextAlignVal));
  }

  void printCsectDirective(raw_ostream &OS) const;

public:
  /// Destroy this XCOFF section.
  LLVM_ABI ~MCSectionXCOFF();

  /// Return the XCOFF storage mapping class for this csect.
  /// @return The XCOFF storage mapping class for this csect.
  XCOFF::StorageMappingClass getMappingClass() const {
    assert(isCsect() && "Only csect section has mapping class property!");
    return CsectProp->MappingClass;
  }
  /// Return the XCOFF storage class of the qualifying name symbol.
  /// @return The XCOFF storage class of the qualifying name symbol.
  XCOFF::StorageClass getStorageClass() const {
    return QualName->getStorageClass();
  }
  /// Return the XCOFF visibility type of the qualifying name symbol.
  /// @return The XCOFF visibility type of the qualifying name symbol.
  XCOFF::VisibilityType getVisibilityType() const {
    return QualName->getVisibilityType();
  }
  /// Return the XCOFF symbol type for this csect.
  /// @return The XCOFF symbol type for this csect.
  XCOFF::SymbolType getCSectType() const {
    assert(isCsect() && "Only csect section has symbol type property!");
    return CsectProp->Type;
  }
  /// Return the qualifying name symbol for this section.
  /// @return The qualifying name symbol for this section.
  MCSymbolXCOFF *getQualNameSymbol() const { return QualName; }

  /// Return the name used for this section in the symbol table.
  /// @return The name used for this section in the symbol table.
  StringRef getSymbolTableName() const { return SymbolTableName; }
  /// Set the name used for this section in the symbol table.
  /// @param STN New symbol table name.
  void setSymbolTableName(StringRef STN) { SymbolTableName = STN; }
  /// Return true if this section may contain multiple symbols.
  /// @return True if this section may contain multiple symbols.
  bool isMultiSymbolsAllowed() const { return MultiSymbolsAllowed; }
  /// Return true if this section is a csect.
  /// @return True if this section is a csect.
  bool isCsect() const { return CsectProp.has_value(); }
  /// Return true if this section is a DWARF section.
  /// @return True if this section is a DWARF section.
  bool isDwarfSect() const { return DwarfSubtypeFlags.has_value(); }
  /// Return the DWARF section subtype flags, if this is a DWARF section.
  /// @return The DWARF section subtype flags, if this is a DWARF section.
  std::optional<XCOFF::DwarfSectionSubtypeFlags> getDwarfSubtypeFlags() const {
    return DwarfSubtypeFlags;
  }
  /// Return the csect properties, if this section is a csect.
  /// @return The csect properties, if this section is a csect.
  std::optional<XCOFF::CsectProperties> getCsectProp() const {
    return CsectProp;
  }
  /// Return the SectionKind for this section.
  /// @return The SectionKind for this section.
  SectionKind getKind() const { return Kind; }
};

} // end namespace llvm

#endif
