//==- llvm/CodeGen/MachineDominators.h - Machine Dom Calculation -*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines classes mirroring those in llvm/Analysis/Dominators.h,
// but for target-specific code rather than target-independent IR.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEDOMINATORS_H
#define LLVM_CODEGEN_MACHINEDOMINATORS_H

#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBundleIterator.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/GenericDomTree.h"
#include <cassert>
#include <optional>

namespace llvm {
class AnalysisUsage;
class MachineFunction;
class Module;
class raw_ostream;

/// Explicit instantiation of DomTreeNodeBase for MachineBasicBlock.
extern template class LLVM_TEMPLATE_ABI DomTreeNodeBase<MachineBasicBlock>;
extern template class LLVM_TEMPLATE_ABI
    DominatorTreeBase<MachineBasicBlock, false>; // DomTree

using MachineDomTreeNode = DomTreeNodeBase<MachineBasicBlock>;

/// Helpers for building and updating MachineBasicBlock dominator trees.
namespace DomTreeBuilder {
/// Dominator tree over a MachineFunction CFG of MachineBasicBlocks.
using MBBDomTree = DomTreeBase<MachineBasicBlock>;
/// Batch of CFG updates applied to a MachineBasicBlock dominator tree.
using MBBUpdates = ArrayRef<llvm::cfg::Update<MachineBasicBlock *>>;
/// GraphDiff view of a MachineBasicBlock dominator tree CFG.
using MBBDomTreeGraphDiff = GraphDiff<MachineBasicBlock *, false>;

extern template LLVM_TEMPLATE_ABI void Calculate<MBBDomTree>(MBBDomTree &DT);
extern template LLVM_TEMPLATE_ABI void
CalculateWithUpdates<MBBDomTree>(MBBDomTree &DT, MBBUpdates U);

extern template LLVM_TEMPLATE_ABI void
InsertEdge<MBBDomTree>(MBBDomTree &DT, MachineBasicBlock *From,
                       MachineBasicBlock *To);

extern template LLVM_TEMPLATE_ABI void
DeleteEdge<MBBDomTree>(MBBDomTree &DT, MachineBasicBlock *From,
                       MachineBasicBlock *To);

extern template LLVM_TEMPLATE_ABI void
ApplyUpdates<MBBDomTree>(MBBDomTree &DT, MBBDomTreeGraphDiff &,
                         MBBDomTreeGraphDiff *);

extern template LLVM_TEMPLATE_ABI bool
Verify<MBBDomTree>(const MBBDomTree &DT, MBBDomTree::VerificationLevel VL);
} // namespace DomTreeBuilder

//===-------------------------------------
/// DominatorTree Class - Concrete subclass of DominatorTreeBase that is used to
/// compute a normal dominator tree.
///
class MachineDominatorTree : public DomTreeBase<MachineBasicBlock> {

public:
  /// Base dominator-tree type specialized for MachineBasicBlock.
  using Base = DomTreeBase<MachineBasicBlock>;

  /// Construct an empty machine dominator tree.
  MachineDominatorTree() = default;
  /// Construct a machine dominator tree for \p MF.
  /// @param MF Machine function whose CFG is analyzed.
  explicit MachineDominatorTree(MachineFunction &MF) { recalculate(MF); }

  /// Handle invalidation explicitly.
  /// @param MF Machine function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this result should be discarded.
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Bring base-class dominates overloads into this class.
  using Base::dominates;

  /// Return true if instruction \p A dominates instruction \p B.
  ///
  /// Performs the special checks necessary when \p A and \p B are in the same
  /// basic block.
  /// @param A Instruction that may dominate.
  /// @param B Instruction to test for dominance.
  /// @return True if \p A dominates \p B.
  bool dominates(const MachineInstr *A, const MachineInstr *B) const {
    const MachineBasicBlock *BBA = A->getParent(), *BBB = B->getParent();
    if (BBA != BBB)
      return Base::dominates(BBA, BBB);

    // Loop through the basic block until we find A or B.
    MachineBasicBlock::const_iterator I = BBA->begin();
    for (; &*I != A && &*I != B; ++I)
      /*empty*/ ;

    return &*I == A;
  }
};

/// \brief Analysis pass which computes a \c MachineDominatorTree.
class MachineDominatorTreeAnalysis
    : public AnalysisInfoMixin<MachineDominatorTreeAnalysis> {
  friend AnalysisInfoMixin<MachineDominatorTreeAnalysis>;

  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = MachineDominatorTree;

  /// Run the analysis pass over a machine function and produce a dominator tree.
  /// @param MF Machine function to analyze.
  /// @param MFAM Machine function analysis manager (unused).
  /// @return The MachineDominatorTree computed for \p MF.
  LLVM_ABI Result run(MachineFunction &MF, MachineFunctionAnalysisManager &MFAM);
};

/// \brief Machine function pass which print \c MachineDominatorTree.
class MachineDominatorTreePrinterPass
    : public RequiredPassInfoMixin<MachineDominatorTreePrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed machine dominator tree.
  explicit MachineDominatorTreePrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print the MachineDominatorTree for \p MF and return all analyses preserved.
  /// @param MF Machine function whose MachineDominatorTree is printed.
  /// @param MFAM Machine function analysis manager providing MachineDominatorTree.
  /// @return A PreservedAnalyses set with all analyses preserved.
  LLVM_ABI PreservedAnalyses run(MachineFunction &MF,
                                 MachineFunctionAnalysisManager &MFAM);
};

/// \brief Analysis pass which computes a \c MachineDominatorTree.
class LLVM_ABI MachineDominatorTreeWrapperPass : public MachineFunctionPass {
  // MachineFunctionPass may verify the analysis result without running pass,
  // e.g. when `F.hasAvailableExternallyLinkage` is true.
  std::optional<MachineDominatorTree> DT;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy MachineDominatorTree wrapper pass.
  MachineDominatorTreeWrapperPass();

  /// Return the MachineDominatorTree computed by this pass.
  /// @return The MachineDominatorTree owned by this pass.
  MachineDominatorTree &getDomTree() { return *DT; }
  /// Return the MachineDominatorTree computed by this pass.
  /// @return The MachineDominatorTree owned by this pass.
  const MachineDominatorTree &getDomTree() const { return *DT; }

  /// Compute the MachineDominatorTree for \p MF.
  /// @param MF Machine function to analyze.
  /// @return False; this analysis does not modify \p MF.
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Verify the MachineDominatorTree computed by this pass.
  void verifyAnalysis() const override;

  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate; this pass preserves all analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  /// Release the MachineDominatorTree owned by this pass.
  void releaseMemory() override;

  /// Print the MachineDominatorTree computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M = nullptr) const override;
};

//===-------------------------------------
/// DominatorTree GraphTraits specialization so the DominatorTree can be
/// iterable by generic graph iterators.
///

/// GraphTraits helpers for walking machine dominator-tree nodes depth-first.
template <class Node, class ChildIterator>
struct MachineDomTreeGraphTraitsBase {
  /// Graph node type for a machine dominator-tree node pointer.
  using NodeRef = Node *;
  /// Iterator over immediate children in the machine dominator tree.
  using ChildIteratorType = ChildIterator;

  /// Return \p N as the graph entry node.
  /// @param N Machine dominator-tree node used as the entry.
  /// @return \p N as the entry NodeRef.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over children of \p N.
  /// @param N Machine dominator-tree node whose children are walked.
  /// @return Begin iterator over the immediate children of \p N.
  static ChildIteratorType child_begin(NodeRef N) { return N->begin(); }
  /// Return the end iterator over children of \p N.
  /// @param N Machine dominator-tree node whose children are walked.
  /// @return End iterator over the immediate children of \p N.
  static ChildIteratorType child_end(NodeRef N) { return N->end(); }
};

template <class T> struct GraphTraits;

/// GraphTraits specialization for mutable MachineDomTreeNode pointers.
template <>
struct GraphTraits<MachineDomTreeNode *>
    : public MachineDomTreeGraphTraitsBase<MachineDomTreeNode,
                                           MachineDomTreeNode::const_iterator> {
};

/// GraphTraits specialization for const MachineDomTreeNode pointers.
template <>
struct GraphTraits<const MachineDomTreeNode *>
    : public MachineDomTreeGraphTraitsBase<const MachineDomTreeNode,
                                           MachineDomTreeNode::const_iterator> {
};

/// GraphTraits specialization so MachineDominatorTree can be walked as a graph.
template <> struct GraphTraits<MachineDominatorTree*>
  : public GraphTraits<MachineDomTreeNode *> {
  /// Return the root node of machine dominator tree \p DT.
  /// @param DT Machine dominator tree whose root is the entry.
  /// @return The root MachineDomTreeNode of \p DT.
  static NodeRef getEntryNode(MachineDominatorTree *DT) {
    return DT->getRootNode();
  }
};

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEDOMINATORS_H
