//===- DWARFCFIPrinter.h ----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_DEBUGINFO_DWARF_DWARFCFIPRINTER_H
#define LLVM_DEBUGINFO_DWARF_DWARFCFIPRINTER_H

#include "llvm/DebugInfo/DWARF/LowLevel/DWARFCFIProgram.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

struct DIDumpOptions;

namespace dwarf {

/// Print a Call Frame Information program to the stream.
///
/// \param P the CFI program to print.
///
/// \param OS the stream to use for output.
///
/// \param DumpOpts options controlling dump formatting, including register
/// name resolution.
///
/// \param IndentLevel specify the indent level as an integer. Each instruction
/// will be output to the stream preceded by 2 * IndentLevel number of spaces.
///
/// \param Address optional initial program counter used when printing factored
/// code offsets as absolute addresses; advanced as address operands are seen.
LLVM_ABI void printCFIProgram(const CFIProgram &P, raw_ostream &OS,
                              const DIDumpOptions &DumpOpts,
                              unsigned IndentLevel,
                              std::optional<uint64_t> Address);

} // end namespace dwarf

} // end namespace llvm

#endif // LLVM_DEBUGINFO_DWARF_DWARFCFIPRINTER_H
