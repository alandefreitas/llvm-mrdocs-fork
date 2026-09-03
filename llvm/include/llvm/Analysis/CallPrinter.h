//===-- CallPrinter.h - Call graph printer external interface ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines external functions that can be called to explicitly
// instantiate the call graph printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_CALLPRINTER_H
#define LLVM_ANALYSIS_CALLPRINTER_H

#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

class ModulePass;

/// Pass for printing the call graph to a dot file
class CallGraphDOTPrinterPass
    : public RequiredPassInfoMixin<CallGraphDOTPrinterPass> {
public:
  /// Print the module call graph as a DOT file.
  /// @param M Module whose call graph is printed.
  /// @param AM Module analysis manager providing supporting analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Pass for viewing the call graph
class CallGraphViewerPass : public RequiredPassInfoMixin<CallGraphViewerPass> {
public:
  /// Display the module call graph in a viewer.
  /// @param M Module whose call graph is viewed.
  /// @param AM Module analysis manager providing supporting analyses.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

/// Create a legacy pass that views the call graph.
/// @return A ModulePass that displays the call graph in a viewer.
LLVM_ABI ModulePass *createCallGraphViewerPass();

/// Create a legacy pass that prints the call graph as DOT.
/// @return A ModulePass that writes the call graph to a DOT file.
LLVM_ABI ModulePass *createCallGraphDOTPrinterPass();

} // end namespace llvm

#endif
