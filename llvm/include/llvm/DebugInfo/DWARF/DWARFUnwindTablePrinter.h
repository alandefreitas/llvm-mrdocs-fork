//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFUNWINDTABLEPRINTER_H
#define LLVM_DEBUGINFO_DWARF_DWARFUNWINDTABLEPRINTER_H

#include "llvm/DebugInfo/DWARF/LowLevel/DWARFUnwindTable.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

struct DIDumpOptions;

namespace dwarf {

/// Print unwind location \p R to \p OS.
///
/// \param OS Output stream to write to.
/// \param R Unwind location to print.
///
/// \returns \p OS after printing \p R.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const UnwindLocation &R);

/// Print register locations \p RL to \p OS.
///
/// \param OS Output stream to write to.
/// \param RL Register locations to print.
///
/// \returns \p OS after printing \p RL.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const RegisterLocations &RL);

/// Print unwind row \p Row to \p OS.
///
/// \param OS Output stream to write to.
/// \param Row Unwind row to print.
///
/// \returns \p OS after printing \p Row.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const UnwindRow &Row);

/// Print a UnwindTable to the stream.
///
/// \param Rows the UnwindTable to print.
///
/// \param OS the stream to use for output.
///
/// \param DumpOpts Options controlling dump formatting, including register
/// name resolution and whether the DWARF Call Frame Information is from
/// .eh_frame instead of .debug_frame (needed because some register numbers
/// differ between the two sections for certain architectures like x86).
///
/// \param IndentLevel specify the indent level as an integer. The UnwindRow
/// will be output to the stream preceded by 2 * IndentLevel number of spaces.
LLVM_ABI void printUnwindTable(const UnwindTable &Rows, raw_ostream &OS,
                               DIDumpOptions DumpOpts,
                               unsigned IndentLevel = 0);

/// Print unwind table \p Rows to \p OS.
///
/// \param OS Output stream to write to.
/// \param Rows Unwind table to print.
///
/// \returns \p OS after printing \p Rows.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const UnwindTable &Rows);

} // end namespace dwarf

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFUNWINDTABLEPRINTER_H
