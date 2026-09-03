//===--- DWARFExpressionPRinter.h - DWARF Expression Printing ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFEXPRESSIONPRINTER_H
#define LLVM_DEBUGINFO_DWARF_DWARFEXPRESSIONPRINTER_H

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/iterator.h"
#include "llvm/DebugInfo/DWARF/LowLevel/DWARFExpression.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

// This functionality is separated from the main data structure so that nothing
// in DWARFExpression.cpp needs build-time dependencies on DWARFUnit or other
// higher-level Dwarf structures. This approach creates better layering and
// allows DWARFExpression to be used from code which can't have dependencies on
// those higher-level structures.

class DWARFUnit;
struct DIDumpOptions;
class raw_ostream;

/// Print a DWARF expression.
/// \param E to be printed
/// \param OS to this stream
/// \param DumpOpts Options controlling dump formatting.
/// \param U DWARF unit providing context for the expression, if any.
/// \param IsEH True if the expression comes from .eh_frame rather than
/// .debug_frame.
LLVM_ABI void printDwarfExpression(const DWARFExpression *E, raw_ostream &OS,
                                   DIDumpOptions DumpOpts, DWARFUnit *U,
                                   bool IsEH = false);

/// Print a DWARF expression in a compact, user-oriented format.
///
/// The format is intended to be compact and useful to a user, but not
/// perfectly unambiguous, or capable of representing every valid DWARF
/// expression.
///
/// \param E to be printed
/// \param OS to this stream
/// \param GetNameForDWARFReg side-effect-free callback to return a target
/// register name, or an empty string if none is available. The printer may try
/// another name-resolution method before reporting failure.
///
/// \returns true if the expression was successfully printed
LLVM_ABI bool printDwarfExpressionCompact(
    const DWARFExpression *E, raw_ostream &OS,
    std::function<StringRef(uint64_t RegNum, bool IsEH)> GetNameForDWARFReg =
        nullptr);

/// Pretty print a register opcode and operands.
/// \param U within the context of this Dwarf unit, if any.
/// \param OS to this stream
/// \param DumpOpts with these options
/// \param Opcode to print
/// \param Operands to the opcode
///
/// \returns true if the Op was successfully printed
LLVM_ABI bool prettyPrintRegisterOp(DWARFUnit *U, raw_ostream &OS,
                                    DIDumpOptions DumpOpts, uint8_t Opcode,
                                    ArrayRef<uint64_t> Operands);

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFEXPRESSIONPRINTER_H
