//===- IRPrintingPasses.h - Passes to print out IR constructs ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines passes to print out IR in various granularities. The
/// PrintModulePass pass simply prints out the entire module when it is
/// executed. The PrintFunctionPass class is designed to be pipelined with
/// other FunctionPass's, and prints out the functions of the module as they
/// are processed.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_IRPRINTER_IRPRINTINGPASSES_H
#define LLVM_IRPRINTER_IRPRINTINGPASSES_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include <string>

namespace llvm {
class raw_ostream;
class Function;
class Module;
class Pass;

/// Pass (for the new pass manager) for printing a Module as
/// LLVM's text IR assembly.
class PrintModulePass : public RequiredPassInfoMixin<PrintModulePass> {
  raw_ostream &OS;
  std::string Banner;
  bool ShouldPreserveUseListOrder;
  bool EmitSummaryIndex;

public:
  /// Construct a module IR printer that writes to dbgs().
  LLVM_ABI PrintModulePass();
  /// Construct a module IR printer that writes to \p OS.
  /// @param OS Output stream that receives the printed IR.
  /// @param Banner Header text printed before the IR, or empty.
  /// @param ShouldPreserveUseListOrder If true, emit uselistorder directives
  ///        so use-lists can be recreated when reading the assembly.
  /// @param EmitSummaryIndex If true, also print the module summary index.
  LLVM_ABI PrintModulePass(raw_ostream &OS, const std::string &Banner = "",
                           bool ShouldPreserveUseListOrder = false,
                           bool EmitSummaryIndex = false);

  /// Print module \p M as LLVM IR assembly.
  /// @param M Module whose IR is printed.
  /// @param AM Module analysis manager; used when a summary index is requested.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, AnalysisManager<Module> &AM);
};

/// Pass (for the new pass manager) for printing a Function as
/// LLVM's text IR assembly.
class PrintFunctionPass : public RequiredPassInfoMixin<PrintFunctionPass> {
  raw_ostream &OS;
  std::string Banner;

public:
  /// Construct a function IR printer that writes to dbgs().
  LLVM_ABI PrintFunctionPass();
  /// Construct a function IR printer that writes to \p OS.
  /// @param OS Output stream that receives the printed IR.
  /// @param Banner Header text printed before each function, or empty.
  LLVM_ABI PrintFunctionPass(raw_ostream &OS, const std::string &Banner = "");

  /// Print function \p F as LLVM IR assembly.
  /// @param F Function whose IR is printed.
  /// @param AM Function analysis manager (unused).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, AnalysisManager<Function> &AM);
};

} // namespace llvm

#endif
