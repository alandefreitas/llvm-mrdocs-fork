//===- DWARFTypeUnit.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFTYPEUNIT_H
#define LLVM_DEBUGINFO_DWARF_DWARFTYPEUNIT_H

#include "llvm/ADT/StringRef.h"
#include "llvm/DebugInfo/DWARF/DWARFUnit.h"
#include <cstdint>

namespace llvm {

struct DIDumpOptions;
class DWARFContext;
class DWARFDebugAbbrev;
struct DWARFSection;
class raw_ostream;

/// A DWARF type unit and its parsed DIEs.
class LLVM_ABI DWARFTypeUnit : public DWARFUnit {
public:
  /// Construct a type unit for \p Section using \p Header and related DWARF
  /// sections.
  ///
  /// \param Context DWARF context that owns this unit.
  /// \param Section .debug_info or .debug_types section containing this unit.
  /// \param Header Parsed unit header for this type unit.
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
  DWARFTypeUnit(DWARFContext &Context, const DWARFSection &Section,
                const DWARFUnitHeader &Header, const DWARFDebugAbbrev *DA,
                const DWARFSection *RS, const DWARFSection *LocSection,
                StringRef SS, const DWARFSection &SOS, const DWARFSection *AOS,
                const DWARFSection &LS, bool LE, bool IsDWO,
                const DWARFUnitVector &UnitVector)
      : DWARFUnit(Context, Section, Header, DA, RS, LocSection, SS, SOS, AOS,
                  LS, LE, IsDWO, UnitVector) {}

  /// Type signature hash for this type unit.
  ///
  /// \return The type signature hash from the unit header.
  uint64_t getTypeHash() const { return getHeader().getTypeHash(); }
  /// Offset of the type DIE within this type unit.
  ///
  /// \return The offset of the type DIE within this type unit.
  uint64_t getTypeOffset() const { return getHeader().getTypeOffset(); }

  /// Dump this type unit to \p OS.
  ///
  /// \param OS Output stream to write the dump to.
  /// \param DumpOpts Options controlling dump formatting.
  void dump(raw_ostream &OS, DIDumpOptions DumpOpts = {}) override;
  /// Enable LLVM-style RTTI.
  ///
  /// \param U The DWARF unit to test.
  /// \return True if \p U is a type unit rather than a compile unit.
  static bool classof(const DWARFUnit *U) { return U->isTypeUnit(); }
};

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFTYPEUNIT_H
