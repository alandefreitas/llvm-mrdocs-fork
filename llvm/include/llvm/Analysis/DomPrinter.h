//===-- DomPrinter.h - Dom printer external interface ------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines external functions that can be called to explicitly
// instantiate the dominance tree printer.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_DOMPRINTER_H
#define LLVM_ANALYSIS_DOMPRINTER_H

#include "llvm/Analysis/DOTGraphTraitsPass.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// DOTGraphTraits specialization for rendering a dominator-tree node.
template <>
struct DOTGraphTraits<DomTreeNode *> : public DefaultDOTGraphTraits {

  /// Construct DOT traits for a dominator-tree node, optionally in simple mode.
  /// @param isSimple True to emit simple node labels without block bodies.
  DOTGraphTraits(bool isSimple = false) : DefaultDOTGraphTraits(isSimple) {}

  /// Return the DOT label for dominator-tree node \p Node.
  /// @param Node Dominator-tree node whose label is requested.
  /// @param Graph Root of the dominator tree (unused for labeling).
  /// @return Label for \p Node, or a placeholder when it has no block.
  std::string getNodeLabel(DomTreeNode *Node, DomTreeNode *Graph) {

    BasicBlock *BB = Node->getBlock();

    if (!BB)
      return "Post dominance root node";

    if (isSimple())
      return DOTGraphTraits<DOTFuncInfo *>::getSimpleNodeLabel(BB, nullptr);

    return DOTGraphTraits<DOTFuncInfo *>::getCompleteNodeLabel(BB, nullptr);
  }
};

/// DOTGraphTraits specialization for rendering a dominator tree.
template <>
struct DOTGraphTraits<DominatorTree *>
    : public DOTGraphTraits<DomTreeNode *> {

  /// Construct DOT traits for a dominator tree, optionally in simple mode.
  /// @param isSimple True to emit simple node labels without block bodies.
  DOTGraphTraits(bool isSimple = false)
      : DOTGraphTraits<DomTreeNode *>(isSimple) {}

  /// Return the DOT graph title for a dominator tree.
  /// @param DT Dominator tree being rendered (unused for the fixed title).
  /// @return Graph title naming the dominator tree.
  static std::string getGraphName(DominatorTree *DT) {
    return "Dominator tree";
  }

  /// Return the DOT label for dominator-tree node \p Node in tree \p G.
  /// @param Node Dominator-tree node whose label is requested.
  /// @param G Dominator tree that owns \p Node.
  /// @return Label for \p Node derived from its basic block.
  std::string getNodeLabel(DomTreeNode *Node, DominatorTree *G) {
    return DOTGraphTraits<DomTreeNode *>::getNodeLabel(Node,
                                                             G->getRootNode());
  }
};

/// DOTGraphTraits specialization for rendering a post-dominator tree.
template<>
struct DOTGraphTraits<PostDominatorTree *>
  : public DOTGraphTraits<DomTreeNode*> {

  /// Construct DOT traits for a post-dominator tree, optionally in simple mode.
  /// @param isSimple True to emit simple node labels without block bodies.
  DOTGraphTraits (bool isSimple=false)
    : DOTGraphTraits<DomTreeNode*>(isSimple) {}

  /// Return the DOT graph title for a post-dominator tree.
  /// @param DT Post-dominator tree being rendered (unused for the fixed title).
  /// @return Graph title naming the post-dominator tree.
  static std::string getGraphName(PostDominatorTree *DT) {
    return "Post dominator tree";
  }

  /// Return the DOT label for post-dominator-tree node \p Node in tree \p G.
  /// @param Node Dominator-tree node whose label is requested.
  /// @param G Post-dominator tree that owns \p Node.
  /// @return Label for \p Node derived from its basic block.
  std::string getNodeLabel(DomTreeNode *Node,
                           PostDominatorTree *G) {
    return DOTGraphTraits<DomTreeNode*>::getNodeLabel(Node, G->getRootNode());
  }
};

/// Pass that displays a function dominator tree in a Graphviz viewer.
struct DomViewer final : DOTGraphTraitsViewer<DominatorTreeAnalysis, false> {
  /// Construct a pass that views the full dominator tree.
  DomViewer() : DOTGraphTraitsViewer<DominatorTreeAnalysis, false>("dom") {}
};

/// Pass that displays a function dominator tree without block bodies.
struct DomOnlyViewer final : DOTGraphTraitsViewer<DominatorTreeAnalysis, true> {
  /// Construct a pass that views a label-only dominator tree.
  DomOnlyViewer()
      : DOTGraphTraitsViewer<DominatorTreeAnalysis, true>("domonly") {}
};

/// Pass that displays a function post-dominator tree in a Graphviz viewer.
struct PostDomViewer final
    : DOTGraphTraitsViewer<PostDominatorTreeAnalysis, false> {
  /// Construct a pass that views the full post-dominator tree.
  PostDomViewer()
      : DOTGraphTraitsViewer<PostDominatorTreeAnalysis, false>("postdom") {}
};

/// Pass that displays a function post-dominator tree without block bodies.
struct PostDomOnlyViewer final
    : DOTGraphTraitsViewer<PostDominatorTreeAnalysis, true> {
  /// Construct a pass that views a label-only post-dominator tree.
  PostDomOnlyViewer()
      : DOTGraphTraitsViewer<PostDominatorTreeAnalysis, true>("postdomonly") {}
};

/// Pass that writes a function dominator tree to a DOT file.
struct DomPrinter final : DOTGraphTraitsPrinter<DominatorTreeAnalysis, false> {
  /// Construct a pass that prints the full dominator tree.
  DomPrinter() : DOTGraphTraitsPrinter<DominatorTreeAnalysis, false>("dom") {}
};

/// Pass that writes a function dominator tree to a DOT file without block bodies.
struct DomOnlyPrinter final
    : DOTGraphTraitsPrinter<DominatorTreeAnalysis, true> {
  /// Construct a pass that prints a label-only dominator tree.
  DomOnlyPrinter()
      : DOTGraphTraitsPrinter<DominatorTreeAnalysis, true>("domonly") {}
};

/// Pass that writes a function post-dominator tree to a DOT file.
struct PostDomPrinter final
    : DOTGraphTraitsPrinter<PostDominatorTreeAnalysis, false> {
  /// Construct a pass that prints the full post-dominator tree.
  PostDomPrinter()
      : DOTGraphTraitsPrinter<PostDominatorTreeAnalysis, false>("postdom") {}
};

/// Pass that writes a function post-dominator tree without block bodies.
struct PostDomOnlyPrinter final
    : DOTGraphTraitsPrinter<PostDominatorTreeAnalysis, true> {
  /// Construct a pass that prints a label-only post-dominator tree.
  PostDomOnlyPrinter()
      : DOTGraphTraitsPrinter<PostDominatorTreeAnalysis, true>("postdomonly") {}
};
} // namespace llvm

namespace llvm {
  class FunctionPass;

  /// Create a legacy pass that prints the dominator tree as DOT.
  /// @return A FunctionPass that writes the dominator tree to a DOT file.
  LLVM_ABI FunctionPass *createDomPrinterWrapperPassPass();

  /// Create a legacy pass that prints a label-only dominator tree as DOT.
  /// @return A FunctionPass that writes a label-only dominator tree to a DOT
  /// file.
  LLVM_ABI FunctionPass *createDomOnlyPrinterWrapperPassPass();

  /// Create a legacy pass that views the dominator tree.
  /// @return A FunctionPass that displays the dominator tree in a viewer.
  LLVM_ABI FunctionPass *createDomViewerWrapperPassPass();

  /// Create a legacy pass that views a label-only dominator tree.
  /// @return A FunctionPass that displays a label-only dominator tree in a
  /// viewer.
  LLVM_ABI FunctionPass *createDomOnlyViewerWrapperPassPass();

  /// Create a legacy pass that prints the post-dominator tree as DOT.
  /// @return A FunctionPass that writes the post-dominator tree to a DOT file.
  LLVM_ABI FunctionPass *createPostDomPrinterWrapperPassPass();

  /// Create a legacy pass that prints a label-only post-dominator tree as DOT.
  /// @return A FunctionPass that writes a label-only post-dominator tree to a
  /// DOT file.
  LLVM_ABI FunctionPass *createPostDomOnlyPrinterWrapperPassPass();

  /// Create a legacy pass that views the post-dominator tree.
  /// @return A FunctionPass that displays the post-dominator tree in a viewer.
  LLVM_ABI FunctionPass *createPostDomViewerWrapperPassPass();

  /// Create a legacy pass that views a label-only post-dominator tree.
  /// @return A FunctionPass that displays a label-only post-dominator tree in a
  /// viewer.
  LLVM_ABI FunctionPass *createPostDomOnlyViewerWrapperPassPass();
} // End llvm namespace

#endif
