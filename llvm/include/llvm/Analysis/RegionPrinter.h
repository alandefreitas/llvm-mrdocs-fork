//===-- RegionPrinter.h - Region printer external interface -----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines external functions that can be called to explicitly
// instantiate the region printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_REGIONPRINTER_H
#define LLVM_ANALYSIS_REGIONPRINTER_H

#include "llvm/Support/Compiler.h"
#include "llvm/Support/DOTGraphTraits.h"

namespace llvm {
class FunctionPass;
class Function;
class RegionInfo;
class RegionNode;

/// Create a legacy pass that views the regions of a function.
/// @return A FunctionPass that displays the region graph in a viewer.
LLVM_ABI FunctionPass *createRegionViewerPass();

/// Create a legacy pass that views a label-only region graph.
/// @return A FunctionPass that displays a label-only region graph in a viewer.
LLVM_ABI FunctionPass *createRegionOnlyViewerPass();

/// Create a legacy pass that prints the regions of a function as DOT.
/// @return A FunctionPass that writes the region graph to a DOT file.
LLVM_ABI FunctionPass *createRegionPrinterPass();

/// Create a legacy pass that prints a label-only region graph as DOT.
/// @return A FunctionPass that writes a label-only region graph to a DOT file.
LLVM_ABI FunctionPass *createRegionOnlyPrinterPass();

/// DOTGraphTraits specialization for rendering a region node.
template <> struct DOTGraphTraits<RegionNode *> : public DefaultDOTGraphTraits {
  /// Construct DOT traits for a region node, optionally in simple mode.
  /// @param isSimple True to emit simple node labels without block bodies.
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  /// Return the DOT label for region node \p Node.
  /// @param Node Region node whose label is requested.
  /// @param Graph Root of the region graph (unused for labeling).
  /// @return Label for \p Node derived from its region or basic block.
  LLVM_ABI std::string getNodeLabel(RegionNode *Node, RegionNode *Graph);
};

#ifndef NDEBUG
/// Open a viewer to display the GraphViz vizualization of the analysis
/// result.
///
/// Practical to call in the debugger.
/// Includes the instructions in each BasicBlock.
///
/// @param RI The analysis to display.
void viewRegion(llvm::RegionInfo *RI);

/// Analyze the regions of a function and open its GraphViz
/// visualization in a viewer.
///
/// Useful to call in the debugger.
/// Includes the instructions in each BasicBlock.
/// The result of a new analysis may differ from the RegionInfo the pass
/// manager currently holds.
///
/// @param F Function to analyze.
void viewRegion(const llvm::Function *F);

/// Open a viewer to display the GraphViz vizualization of the analysis
/// result.
///
/// Useful to call in the debugger.
/// Shows only the BasicBlock names without their instructions.
///
/// @param RI The analysis to display.
void viewRegionOnly(llvm::RegionInfo *RI);

/// Analyze the regions of a function and open its GraphViz
/// visualization in a viewer.
///
/// Useful to call in the debugger.
/// Shows only the BasicBlock names without their instructions.
/// The result of a new analysis may differ from the RegionInfo the pass
/// manager currently holds.
///
/// @param F Function to analyze.
void viewRegionOnly(const llvm::Function *F);
#endif // NDEBUG

} // namespace llvm

#endif // LLVM_ANALYSIS_REGIONPRINTER_H
