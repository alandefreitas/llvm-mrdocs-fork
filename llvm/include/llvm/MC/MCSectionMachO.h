//===- MCSectionMachO.h - MachO Machine Code Sections -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares the MCSectionMachO class.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_MC_MCSECTIONMACHO_H
#define LLVM_MC_MCSECTIONMACHO_H

#include "llvm/ADT/StringRef.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/MC/MCSection.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// This represents a section on a Mach-O system (used by Mac OS X).  On a Mac
/// system, these are also described in /usr/include/mach-o/loader.h.
class LLVM_ABI MCSectionMachO final : public MCSection {
  friend class MCContext;
  friend class MCAsmInfoDarwin;
  char SegmentName[16];  // Not necessarily null terminated!

  /// This is the SECTION_TYPE and SECTION_ATTRIBUTES field of a section, drawn
  /// from the enums below.
  unsigned TypeAndAttributes;

  /// The 'reserved2' field of a section, used to represent the size of stubs,
  /// for example.
  unsigned Reserved2;

  // The index of this section in MachObjectWriter::SectionOrder, which is
  // different from MCSection::Ordinal.
  unsigned LayoutOrder = 0;

  // The defining non-temporary symbol for each fragment.
  SmallVector<const MCSymbol *, 0> Atoms;

  MCSectionMachO(StringRef Segment, StringRef Section, unsigned TAA,
                 unsigned reserved2, SectionKind K, MCSymbol *Begin);
public:

  /// Return the Mach-O segment name for this section.
  /// @return The Mach-O segment name for this section.
  StringRef getSegmentName() const {
    // SegmentName is not necessarily null terminated!
    if (SegmentName[15])
      return StringRef(SegmentName, 16);
    return StringRef(SegmentName);
  }

  /// Return the combined SECTION_TYPE and SECTION_ATTRIBUTES value.
  /// @return The combined SECTION_TYPE and SECTION_ATTRIBUTES value.
  unsigned getTypeAndAttributes() const { return TypeAndAttributes; }
  /// Return the stub size stored in the section's reserved2 field.
  /// @return The stub size stored in the section's reserved2 field.
  unsigned getStubSize() const { return Reserved2; }

  /// Return the SECTION_TYPE portion of TypeAndAttributes.
  /// @return The SECTION_TYPE portion of TypeAndAttributes.
  MachO::SectionType getType() const {
    return static_cast<MachO::SectionType>(TypeAndAttributes &
                                           MachO::SECTION_TYPE);
  }
  /// Return true if TypeAndAttributes includes the given attribute bit.
  /// @param Value SECTION_ATTRIBUTES flag to test.
  /// @return True if TypeAndAttributes includes the given attribute bit.
  bool hasAttribute(unsigned Value) const {
    return (TypeAndAttributes & Value) != 0;
  }

  /// Parse a Mach-O `.section` specifier string into its out parameters.
  ///
  /// Spec is a string that can appear after a .section directive in a mach-o
  /// flavored .s file. If successful, this fills in the specified Out
  /// parameters and returns success. When an invalid section specifier is
  /// present, this returns an Error indicating the problem. If no TAA was
  /// parsed, TAA is not altered, and TAAParsed becomes false.
  /// @param Spec Section specifier string to parse.
  /// @param Segment Set to the segment name from Spec.
  /// @param Section Set to the section name from Spec.
  /// @param TAA Set to the combined SECTION_TYPE and SECTION_ATTRIBUTES value.
  /// @param TAAParsed Set to true if a type/attributes field was present.
  /// @param StubSize Set to the stub size when Spec includes one.
  /// @return Success, or an Error describing an invalid section specifier.
  static Error ParseSectionSpecifier(StringRef Spec,      // In.
                                     StringRef &Segment,  // Out.
                                     StringRef &Section,  // Out.
                                     unsigned &TAA,       // Out.
                                     bool &TAAParsed,     // Out.
                                     unsigned &StubSize); // Out.

  /// Allocate atom storage for each fragment in this section.
  void allocAtoms();
  /// Return the defining atom symbol for fragment index \p I, or null.
  /// @param I Fragment layout-order index.
  /// @return The defining atom symbol for fragment index \p I, or null.
  const MCSymbol *getAtom(size_t I) const;
  /// Set the defining atom symbol for fragment index \p I.
  /// @param I Fragment layout-order index.
  /// @param Sym Defining non-temporary symbol for the fragment.
  void setAtom(size_t I, const MCSymbol *Sym);

  /// Return this section's index in MachObjectWriter::SectionOrder.
  /// @return This section's index in MachObjectWriter::SectionOrder.
  unsigned getLayoutOrder() const { return LayoutOrder; }
  /// Set this section's index in MachObjectWriter::SectionOrder.
  /// @param Value New layout order index.
  void setLayoutOrder(unsigned Value) { LayoutOrder = Value; }
};

} // end namespace llvm

#endif
