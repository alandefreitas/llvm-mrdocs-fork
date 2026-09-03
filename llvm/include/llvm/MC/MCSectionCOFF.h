//===- MCSectionCOFF.h - COFF Machine Code Sections -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionCOFF class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONCOFF_H
#define LLVM_MC_MCSECTIONCOFF_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/COFF.h"
#include "llvm/MC/MCSection.h"
#include "llvm/MC/SectionKind.h"
#include <cassert>

namespace llvm {

class MCSymbol;

/// This represents a section on Windows
class MCSectionCOFF final : public MCSection {
  // FIXME: The following fields should not be mutable, but are for now so the
  // asm parser can honor the .linkonce directive.

  /// This is the Characteristics field of a section, drawn from the enums
  /// below.
  mutable unsigned Characteristics;

  /// The unique IDs used with the .pdata and .xdata sections created internally
  /// by the assembler. This ID is used to ensure that for every .text section,
  /// there is exactly one .pdata and one .xdata section, which is required by
  /// the Microsoft incremental linker. This data is mutable because this ID is
  /// not notionally part of the section.
  mutable unsigned WinCFISectionID = ~0U;

  /// The COMDAT symbol of this section. Only valid if this is a COMDAT section.
  /// Two COMDAT sections are merged if they have the same COMDAT symbol.
  MCSymbol *COMDATSymbol;

  /// This is the Selection field for the section symbol, if it is a COMDAT
  /// section (Characteristics & IMAGE_SCN_LNK_COMDAT) != 0
  mutable int Selection;

  unsigned UniqueID;

private:
  friend class MCContext;
  friend class MCAsmInfoCOFF;
  // The storage of Name is owned by MCContext's COFFUniquingMap.
  MCSectionCOFF(StringRef Name, unsigned Characteristics,
                MCSymbol *COMDATSymbol, int Selection, unsigned UniqueID,
                MCSymbol *Begin)
      : MCSection(Name, Characteristics & COFF::IMAGE_SCN_CNT_CODE,
                  Characteristics & COFF::IMAGE_SCN_CNT_UNINITIALIZED_DATA,
                  Begin),
        Characteristics(Characteristics), COMDATSymbol(COMDATSymbol),
        Selection(Selection), UniqueID(UniqueID) {
    assert((Characteristics & 0x00F00000) == 0 &&
           "alignment must not be set upon section creation");
  }

public:
  /// Decides whether a '.section' directive should be printed before the
  /// section name.
  /// @param Name Section name being considered for emission.
  /// @return True if the `.section` directive should be omitted.
  LLVM_ABI bool shouldOmitSectionDirective(StringRef Name) const;

  /// Return the COFF section Characteristics flags.
  /// @return The COFF section Characteristics flags.
  unsigned getCharacteristics() const { return Characteristics; }
  /// Return the COMDAT symbol, or null if this is not a COMDAT section.
  /// @return The COMDAT symbol, or null if this is not a COMDAT section.
  MCSymbol *getCOMDATSymbol() const { return COMDATSymbol; }
  /// Return the COMDAT Selection field, or zero if not a COMDAT section.
  /// @return The COMDAT Selection field, or zero if not a COMDAT section.
  int getSelection() const { return Selection; }

  /// Set the COMDAT Selection type and mark the section as COMDAT.
  /// @param Selection COFF COMDAT selection type (must be non-zero).
  LLVM_ABI void setSelection(int Selection) const;

  /// Return true if this section was created with a unique ID.
  /// @return True if this section was created with a unique ID.
  bool isUnique() const { return UniqueID != NonUniqueID; }
  /// Return the unique ID assigned to this section.
  /// @return The unique ID assigned to this section.
  unsigned getUniqueID() const { return UniqueID; }

  /// Return this section's WinCFI ID, assigning one from \p NextID if needed.
  /// @param NextID Counter used to allocate a new ID when none is assigned yet.
  /// @return The section's WinCFI ID.
  unsigned getOrAssignWinCFISectionID(unsigned *NextID) const {
    if (WinCFISectionID == ~0U)
      WinCFISectionID = (*NextID)++;
    return WinCFISectionID;
  }

  /// Return true if \p Name implies IMAGE_SCN_MEM_DISCARDABLE (e.g. `.debug*`).
  /// @param Name Section name to test.
  /// @return True if \p Name implies IMAGE_SCN_MEM_DISCARDABLE.
  static bool isImplicitlyDiscardable(StringRef Name) {
    return Name.starts_with(".debug");
  }
};

} // end namespace llvm

#endif // LLVM_MC_MCSECTIONCOFF_H
