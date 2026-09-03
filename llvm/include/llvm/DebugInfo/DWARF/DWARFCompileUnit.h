//===- DWARFCompileUnit.h ---------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFCOMPILEUNIT_H
#define LLVM_DEBUGINFO_DWARF_DWARFCOMPILEUNIT_H

#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class DWARFContext;
class DWARFDebugAbbrev;
class raw_ostream;
struct DIDumpOptions;
struct DWARFSection;

/// A DWARF compile unit and its parsed DIEs.
class LLVM_ABI DWARFCompileUnit : public DWARFUnit {
public:
  /// Construct a compile unit for \p Section using \p Header and related DWARF
  /// sections.
  ///
  /// \param Context DWARF context that owns this unit.
  /// \param Section .debug_info section containing this unit.
  /// \param Header Parsed unit header for this compile unit.
  /// \param DA Abbreviation table for this unit.
  /// \param RS .debug_ranges / .debug_rnglists section, if present.
  /// \param LocSection .debug_loc / .debug_loclists section, if present.
  /// \param SS .debug_str string section contents.
  /// \param SOS .debug_str_offsets section.
  /// \param AOS .debug_addr section, if present.
  /// \param LS .debug_line section.
  /// \param LE True if the DWARF data is little-endian.
  /// \param IsDWO True if this unit came from a .dwo/.dwp object.
  /// \param UnitVector Collection of units that contains this unit.
  DWARFCompileUnit(DWARFContext &Context, const DWARFSection &Section,
                   const DWARFUnitHeader &Header, const DWARFDebugAbbrev *DA,
                   const DWARFSection *RS, const DWARFSection *LocSection,
                   StringRef SS, const DWARFSection &SOS,
                   const DWARFSection *AOS, const DWARFSection &LS, bool LE,
                   bool IsDWO, const DWARFUnitVector &UnitVector)
      : DWARFUnit(Context, Section, Header, DA, RS, LocSection, SS, SOS, AOS,
                  LS, LE, IsDWO, UnitVector) {}

  /// VTable anchor.
  ~DWARFCompileUnit() override;
  /// Dump this compile unit to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param DumpOpts Options controlling dump formatting.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts) override;
  /// Enable LLVM-style RTTI.
  ///
  /// \param U The DWARF unit to test.
  /// \return True if \p U is a compile unit rather than a type unit.
  static bool classof(const DWARFUnit *U) { return !U->isTypeUnit(); }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFCOMPILEUNIT_H
