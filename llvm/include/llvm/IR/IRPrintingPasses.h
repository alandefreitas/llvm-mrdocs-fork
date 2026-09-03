//===- IRPrintingPasses.h - Passes to print out IR constructs ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file contains an interface for creating legacy passes to print out IR
/// in various granularities.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IR_IRPRINTINGPASSES_H
#define LLVM_IR_IRPRINTINGPASSES_H

#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {
class raw_ostream;
class StringRef;
class FunctionPass;
class ModulePass;
class Pass;

/// Create and return a pass that writes the module to the specified
/// \c raw_ostream.
/// \param OS The stream to write the module IR to.
/// \param Banner Header text printed before the IR, or empty.
/// \param ShouldPreserveUseListOrder If true, emit uselistorder directives so
///        use-lists can be recreated when reading the assembly.
/// \return A module pass that writes the module IR to \p OS.
LLVM_ABI ModulePass *
createPrintModulePass(raw_ostream &OS, const std::string &Banner = "",
                      bool ShouldPreserveUseListOrder = false);

/// Create and return a pass that prints functions to the specified
/// \c raw_ostream as they are processed.
/// \param OS The stream to print functions to.
/// \param Banner Header text printed before each function, or empty.
/// \return A function pass that prints each processed function to \p OS.
LLVM_ABI FunctionPass *createPrintFunctionPass(raw_ostream &OS,
                                               const std::string &Banner = "");

/// Print out a name of an LLVM value without any prefixes.
///
/// The name is surrounded with ""'s and escaped if it has any special or
/// non-printable characters in it.
/// \param OS The stream to write the name to.
/// \param Name The LLVM value name to print.
LLVM_ABI void printLLVMNameWithoutPrefix(raw_ostream &OS, StringRef Name);

/// Return true if a pass is for IR printing.
/// \param P The pass to test.
/// \return True if \p P is an IR printing pass.
LLVM_ABI bool isIRPrintingPass(Pass *P);

} // namespace llvm

#endif
