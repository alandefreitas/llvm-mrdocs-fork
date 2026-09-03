//===- llvm/Analysis/DDGPrinter.h -------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
//
// This file defines the DOT printer for the Data-Dependence Graph (DDG).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DDGPRINTER_H
#define LLVM_ANALYSIS_DDGPRINTER_H

#include "llvm/Analysis/DDG.h"
#include "llvm/Support/DOTGraphTraits.h"

namespace llvm {
class LPMUpdater;
class Loop;

//===--------------------------------------------------------------------===//
// Implementation of DDG DOT Printer for a loop.
//===--------------------------------------------------------------------===//
/// Pass that writes the data-dependence graph of a loop as DOT.
class DDGDotPrinterPass : public RequiredPassInfoMixin<DDGDotPrinterPass> {
public:
  /// Write the DDG of \p L to a DOT file.
  /// @param L Loop whose data-dependence graph is printed.
  /// @param AM Loop analysis manager providing the DDG and related analyses.
  /// @param AR Standard loop analysis results available to the pass.
  /// @param U Loop pass manager updater (unused by this printer).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

//===--------------------------------------------------------------------===//
// Specialization of DOTGraphTraits.
//===--------------------------------------------------------------------===//
/// DOTGraphTraits specialization for rendering a data-dependence graph.
template <>
struct DOTGraphTraits<const DataDependenceGraph *>
    : public DefaultDOTGraphTraits {

  /// Construct DOT traits for a DDG, optionally in simple mode.
  /// @param IsSimple True to emit concise labels without verbose dependence detail.
  DOTGraphTraits(bool IsSimple = false) : DefaultDOTGraphTraits(IsSimple) {}

  /// Generate a title for the graph in DOT format.
  /// @param G Data-dependence graph being rendered.
  /// @return Graph title naming the DDG.
  std::string getGraphName(const DataDependenceGraph *G) {
    assert(G && "expected a valid pointer to the graph.");
    return "DDG for '" + std::string(G->getName()) + "'";
  }

  /// Print a DDG node either in concise form (-ddg-dot-only) or
  /// verbose mode (-ddg-dot).
  /// @param Node DDG node whose label is requested.
  /// @param Graph Data-dependence graph that owns \p Node.
  /// @return DOT label for \p Node in simple or verbose form.
  LLVM_ABI std::string getNodeLabel(const DDGNode *Node,
                                    const DataDependenceGraph *Graph);

  /// Return DOT attributes for an edge in the DDG.
  ///
  /// If the edge is a MemoryDependence edge, then detailed dependence
  /// info available from DependenceAnalysis is displayed.
  /// @param Node Source DDG node of the edge.
  /// @param I Child iterator identifying the outgoing edge.
  /// @param G Data-dependence graph that owns the edge.
  /// @return DOT attribute string for the edge.
  LLVM_ABI std::string
  getEdgeAttributes(const DDGNode *Node,
                    GraphTraits<const DDGNode *>::ChildIteratorType I,
                    const DataDependenceGraph *G);

  /// Do not print nodes that are part of a pi-block separately. They
  /// will be printed when their containing pi-block is being printed.
  /// @param Node DDG node to test for hiding.
  /// @param G Data-dependence graph that owns \p Node.
  /// @return True if \p Node should be omitted from the rendered graph.
  LLVM_ABI bool isNodeHidden(const DDGNode *Node, const DataDependenceGraph *G);

  /// Return DOT attributes for a node (e.g. border and fill for pi-blocks).
  /// @param Node DDG node whose attributes are requested.
  /// @param G Data-dependence graph that owns \p Node.
  /// @return DOT attribute string for \p Node.
  LLVM_ABI static std::string getNodeAttributes(const DDGNode *Node,
                                                const DataDependenceGraph *G);

private:
  /// Print a DDG node in concise form.
  static std::string getSimpleNodeLabel(const DDGNode *Node,
                                        const DataDependenceGraph *G);

  /// Print a DDG node with more information including containing instructions
  /// and detailed information about the dependence edges.
  static std::string getVerboseNodeLabel(const DDGNode *Node,
                                         const DataDependenceGraph *G);

  /// Print a DDG edge in concise form.
  static std::string getSimpleEdgeAttributes(const DDGNode *Src,
                                             const DDGEdge *Edge,
                                             const DataDependenceGraph *G);

  /// Print a DDG edge with more information including detailed information
  /// about the dependence edges.
  static std::string getVerboseEdgeAttributes(const DDGNode *Src,
                                              const DDGEdge *Edge,
                                              const DataDependenceGraph *G);
};

/// Convenience alias for DOTGraphTraits specialized on a const DDG.
using DDGDotGraphTraits = DOTGraphTraits<const DataDependenceGraph *>;

} // namespace llvm

#endif // LLVM_ANALYSIS_DDGPRINTER_H
