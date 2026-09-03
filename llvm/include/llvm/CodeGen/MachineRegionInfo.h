//===- llvm/CodeGen/MachineRegionInfo.h -------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEREGIONINFO_H
#define LLVM_CODEGEN_MACHINEREGIONINFO_H

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/Analysis/RegionInfo.h"
#include "llvm/Analysis/RegionIterator.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineDominanceFrontier.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include <cassert>

namespace llvm {

class MachinePostDominatorTree;
class MachineRegion;
class MachineRegionNode;
class MachineRegionInfo;

/// RegionTraits specialization that wires RegionInfo to MachineFunction CFGs.
template <> struct RegionTraits<MachineFunction> {
  /// Machine function type analyzed by the region info.
  using FuncT = MachineFunction;
  /// Basic block type in the machine CFG.
  using BlockT = MachineBasicBlock;
  /// Concrete region type for machine functions.
  using RegionT = MachineRegion;
  /// Concrete region-node type for machine functions.
  using RegionNodeT = MachineRegionNode;
  /// Region-info analysis type for machine functions.
  using RegionInfoT = MachineRegionInfo;
  /// Dominator-tree type used while building machine regions.
  using DomTreeT = MachineDominatorTree;
  /// Dominator-tree node type used while building machine regions.
  using DomTreeNodeT = MachineDomTreeNode;
  /// Post-dominator-tree type used while building machine regions.
  using PostDomTreeT = MachinePostDominatorTree;
  /// Dominance-frontier type used while building machine regions.
  using DomFrontierT = MachineDominanceFrontier;
  /// Instruction type in the machine CFG.
  using InstT = MachineInstr;
  /// Loop type used alongside machine region analysis.
  using LoopT = MachineLoop;
  /// Loop-info type used alongside machine region analysis.
  using LoopInfoT = MachineLoopInfo;

  /// Return the number of CFG successors of machine basic block \p BB.
  /// @param BB Machine basic block whose successors are counted.
  /// @return The number of CFG successors of \p BB.
  static unsigned getNumSuccessors(MachineBasicBlock *BB) {
    return BB->succ_size();
  }
};

/// Region node that holds either a MachineBasicBlock or a nested MachineRegion.
class MachineRegionNode : public RegionNodeBase<RegionTraits<MachineFunction>> {
public:
  /// Construct a machine region node for \p Entry under \p Parent.
  /// @param Parent Parent machine region that owns this node.
  /// @param Entry Entry block of this node or of the nested subregion.
  /// @param isSubRegion True if this node represents a nested MachineRegion.
  inline MachineRegionNode(MachineRegion *Parent, MachineBasicBlock *Entry,
                           bool isSubRegion = false)
      : RegionNodeBase<RegionTraits<MachineFunction>>(Parent, Entry,
                                                      isSubRegion) {}

  /// Return true if this node is the same object as region \p RN.
  /// @param RN Machine region compared by object identity.
  /// @return True if this node is the same object as \p RN.
  bool operator==(const MachineRegion &RN) const {
    return this == reinterpret_cast<const MachineRegionNode *>(&RN);
  }
};

/// Single-entry single-exit region in a MachineFunction CFG.
class MachineRegion : public RegionBase<RegionTraits<MachineFunction>> {
public:
  /// Construct a machine region from \p Entry to \p Exit.
  /// @param Entry Entry machine basic block of the region.
  /// @param Exit Exit machine basic block of the region.
  /// @param RI Owning machine region info.
  /// @param DT Dominator tree used to manage the region.
  /// @param Parent Optional parent region; null for the top-level region.
  LLVM_ABI MachineRegion(MachineBasicBlock *Entry, MachineBasicBlock *Exit,
                         MachineRegionInfo *RI, MachineDominatorTree *DT,
                         MachineRegion *Parent = nullptr);
  /// Destroy this machine region and its owned subregions.
  LLVM_ABI ~MachineRegion();

  /// Return true if this region is the same object as region node \p RN.
  /// @param RN Machine region node compared by object identity.
  /// @return True if this region is the same object as \p RN.
  bool operator==(const MachineRegionNode &RN) const {
    return &RN == reinterpret_cast<const MachineRegionNode *>(this);
  }
};

/// Detects single-entry single-exit regions in a MachineFunction.
class LLVM_ABI MachineRegionInfo
    : public RegionInfoBase<RegionTraits<MachineFunction>> {
public:
  /// Base RegionInfoBase specialization used by this analysis.
  using Base = RegionInfoBase<RegionTraits<MachineFunction>>;

  /// Construct an empty machine region info.
  explicit MachineRegionInfo();
  /// Destroy the machine region tree.
  ~MachineRegionInfo() override;

  /// Move-construct machine region info from \p Arg.
  /// @param Arg Region info to move from.
  MachineRegionInfo(MachineRegionInfo &&Arg)
      : Base(std::move(static_cast<Base &>(Arg))) {
    updateRegionTree(*this, TopLevelRegion);
  }

  /// Move-assign machine region info from \p RHS.
  /// @param RHS Region info to move from.
  /// @return A reference to this machine region info.
  MachineRegionInfo &operator=(MachineRegionInfo &&RHS) {
    Base::operator=(std::move(static_cast<Base &>(RHS)));
    updateRegionTree(*this, TopLevelRegion);
    return *this;
  }

  /// Handle invalidation explicitly.
  /// @param F Machine function whose analysis result may be invalidated.
  /// @param PA Set of analyses preserved by the transform.
  /// @param Inv Invalidator for resolving analysis dependencies.
  /// @return True if this analysis result should be invalidated.
  bool invalidate(MachineFunction &F, const PreservedAnalyses &PA,
                  MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Update statistics about the created region \p R.
  /// @param R Machine region that was just created.
  void updateStatistics(MachineRegion *R) final;

  /// Recalculate machine regions for function \p F.
  /// @param F Machine function to analyze.
  /// @param DT Dominator tree for \p F.
  /// @param PDT Post-dominator tree for \p F.
  /// @param DF Dominance frontier for \p F.
  void recalculate(MachineFunction &F, MachineDominatorTree *DT,
                   MachinePostDominatorTree *PDT, MachineDominanceFrontier *DF);
};

/// Analysis pass that exposes the \c MachineRegionInfo for a machine function.
class LLVM_ABI MachineRegionInfoAnalysis
    : public AnalysisInfoMixin<MachineRegionInfoAnalysis> {
  friend AnalysisInfoMixin<MachineRegionInfoAnalysis>;

  static AnalysisKey Key;

public:
  /// Result type produced by this analysis.
  using Result = MachineRegionInfo;

  /// Run the analysis over machine function \p F.
  /// @param F Machine function to analyze.
  /// @param AM Machine function analysis manager providing dominator info.
  /// @return The MachineRegionInfo computed for \p F.
  Result run(MachineFunction &F, MachineFunctionAnalysisManager &AM);
};

/// Printer pass for the \c MachineRegionInfo analysis results.
class LLVM_ABI MachineRegionInfoPrinterPass
    : public RequiredPassInfoMixin<MachineRegionInfoPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// @param OS Output stream for the printed machine region info.
  explicit MachineRegionInfoPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print the MachineRegionInfo for \p MF and return all analyses preserved.
  /// @param MF Machine function whose MachineRegionInfo is printed.
  /// @param MFAM Machine function analysis manager providing MachineRegionInfo.
  /// @return All analyses preserved.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM);
};

/// Legacy MachineFunctionPass wrapper that computes MachineRegionInfo.
class LLVM_ABI MachineRegionInfoPass : public MachineFunctionPass {
  MachineRegionInfo RI;

public:
  /// Pass identification, replacement for typeid.
  static char ID;

  /// Construct the legacy MachineRegionInfo wrapper pass.
  explicit MachineRegionInfoPass();
  /// Destroy the legacy MachineRegionInfo wrapper pass.
  ~MachineRegionInfoPass() override;

  /// Return the MachineRegionInfo computed by this pass.
  /// @return The MachineRegionInfo computed by this pass.
  MachineRegionInfo &getRegionInfo() { return RI; }

  /// Return the MachineRegionInfo computed by this pass.
  /// @return The MachineRegionInfo computed by this pass.
  const MachineRegionInfo &getRegionInfo() const { return RI; }

  /// @name MachineFunctionPass interface
  //@{
  /// Compute MachineRegionInfo for machine function \p F.
  /// @param F Machine function to analyze.
  /// @return True if the machine function was modified.
  bool runOnMachineFunction(MachineFunction &F) override;
  /// Release the MachineRegionInfo owned by this pass.
  void releaseMemory() override;
  /// Verify the MachineRegionInfo computed by this pass.
  void verifyAnalysis() const override;
  /// Report analysis usage for this pass.
  /// @param AU Analysis usage to populate with required and preserved analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override;
  /// Print the MachineRegionInfo computed by this pass.
  /// @param OS Output stream.
  /// @param M Optional module (unused).
  void print(raw_ostream &OS, const Module *M) const override;
  /// Dump the MachineRegionInfo computed by this pass to dbgs().
  void dump() const;
  //@}
};

/// Return the MachineBasicBlock contents of this region node.
/// @return The MachineBasicBlock contents of this region node.
template <>
template <>
inline MachineBasicBlock *
RegionNodeBase<RegionTraits<MachineFunction>>::getNodeAs<MachineBasicBlock>()
    const {
  assert(!isSubRegion() && "This is not a MachineBasicBlock RegionNode!");
  return getEntry();
}

/// Return the nested MachineRegion contents of this region node.
/// @return The nested MachineRegion contents of this region node.
template <>
template <>
inline MachineRegion *
RegionNodeBase<RegionTraits<MachineFunction>>::getNodeAs<MachineRegion>()
    const {
  assert(isSubRegion() && "This is not a subregion RegionNode!");
  auto Unconst =
      const_cast<RegionNodeBase<RegionTraits<MachineFunction>> *>(this);
  return reinterpret_cast<MachineRegion *>(Unconst);
}

/// GraphTraits specialization for mutable MachineRegionNode pointers.
template <> struct GraphTraits<MachineRegionNode *> {
  /// Graph node type for a mutable machine region node pointer.
  using NodeRef = MachineRegionNode *;
  /// Iterator over hierarchical successors of a machine region node.
  using ChildIteratorType =
      RNSuccIterator<NodeRef, MachineBasicBlock, MachineRegion>;
  /// Return \p N as the graph entry node.
  /// @param N Machine region node used as the entry.
  /// @return \p N as the graph entry node.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over children of \p N.
  /// @param N Machine region node whose children are walked.
  /// @return The begin iterator over children of \p N.
  static inline ChildIteratorType child_begin(NodeRef N) {
    return RNSuccIterator<NodeRef, MachineBasicBlock, MachineRegion>(N);
  }
  /// Return the end iterator over children of \p N.
  /// @param N Machine region node whose children are walked.
  /// @return The end iterator over children of \p N.
  static inline ChildIteratorType child_end(NodeRef N) {
    return RNSuccIterator<NodeRef, MachineBasicBlock, MachineRegion>(N, true);
  }
};

/// GraphTraits specialization for flat iteration of MachineRegionNode graphs.
template <> struct GraphTraits<FlatIt<MachineRegionNode *>> {
  /// Graph node type for a mutable machine region node pointer.
  using NodeRef = MachineRegionNode *;
  /// Iterator over flat successors of a machine region node.
  using ChildIteratorType =
      RNSuccIterator<FlatIt<NodeRef>, MachineBasicBlock, MachineRegion>;
  /// Return \p N as the graph entry node.
  /// @param N Machine region node used as the entry.
  /// @return \p N as the graph entry node.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over flat children of \p N.
  /// @param N Machine region node whose children are walked.
  /// @return The begin iterator over flat children of \p N.
  static inline ChildIteratorType child_begin(NodeRef N) {
    return RNSuccIterator<FlatIt<NodeRef>, MachineBasicBlock, MachineRegion>(N);
  }
  /// Return the end iterator over flat children of \p N.
  /// @param N Machine region node whose children are walked.
  /// @return The end iterator over flat children of \p N.
  static inline ChildIteratorType child_end(NodeRef N) {
    return RNSuccIterator<FlatIt<NodeRef>, MachineBasicBlock, MachineRegion>(
        N, true);
  }
};

/// GraphTraits specialization for const MachineRegionNode pointers.
template <> struct GraphTraits<const MachineRegionNode *> {
  /// Graph node type for a const machine region node pointer.
  using NodeRef = const MachineRegionNode *;
  /// Iterator over hierarchical successors of a const machine region node.
  using ChildIteratorType =
      RNSuccIterator<NodeRef, MachineBasicBlock, MachineRegion>;
  /// Return \p N as the graph entry node.
  /// @param N Machine region node used as the entry.
  /// @return \p N as the graph entry node.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over children of \p N.
  /// @param N Machine region node whose children are walked.
  /// @return The begin iterator over children of \p N.
  static inline ChildIteratorType child_begin(NodeRef N) {
    return RNSuccIterator<NodeRef, MachineBasicBlock, MachineRegion>(N);
  }
  /// Return the end iterator over children of \p N.
  /// @param N Machine region node whose children are walked.
  /// @return The end iterator over children of \p N.
  static inline ChildIteratorType child_end(NodeRef N) {
    return RNSuccIterator<NodeRef, MachineBasicBlock, MachineRegion>(N, true);
  }
};

/// GraphTraits specialization for flat iteration of const MachineRegionNode
/// graphs.
template <> struct GraphTraits<FlatIt<const MachineRegionNode *>> {
  /// Graph node type for a const machine region node pointer.
  using NodeRef = const MachineRegionNode *;
  /// Iterator over flat successors of a const machine region node.
  using ChildIteratorType =
      RNSuccIterator<FlatIt<NodeRef>, MachineBasicBlock, MachineRegion>;
  /// Return \p N as the graph entry node.
  /// @param N Machine region node used as the entry.
  /// @return \p N as the graph entry node.
  static NodeRef getEntryNode(NodeRef N) { return N; }
  /// Return the begin iterator over flat children of \p N.
  /// @param N Machine region node whose children are walked.
  /// @return The begin iterator over flat children of \p N.
  static inline ChildIteratorType child_begin(NodeRef N) {
    return RNSuccIterator<FlatIt<NodeRef>, MachineBasicBlock, MachineRegion>(N);
  }
  /// Return the end iterator over flat children of \p N.
  /// @param N Machine region node whose children are walked.
  /// @return The end iterator over flat children of \p N.
  static inline ChildIteratorType child_end(NodeRef N) {
    return RNSuccIterator<FlatIt<NodeRef>, MachineBasicBlock, MachineRegion>(
        N, true);
  }
};

/// GraphTraits specialization so MachineRegion can be walked as a graph.
template <>
struct GraphTraits<MachineRegion *> : public GraphTraits<MachineRegionNode *> {
  /// Depth-first iterator over nodes in a machine region graph.
  using nodes_iterator = df_iterator<NodeRef>;
  /// Return the entry node of machine region \p R.
  /// @param R Machine region whose entry node is requested.
  /// @return The entry node of machine region \p R.
  static NodeRef getEntryNode(MachineRegion *R) {
    return R->getNode(R->getEntry());
  }
  /// Return the begin iterator over nodes in machine region \p R.
  /// @param R Machine region whose nodes are walked.
  /// @return The begin iterator over nodes in machine region \p R.
  static nodes_iterator nodes_begin(MachineRegion *R) {
    return nodes_iterator::begin(getEntryNode(R));
  }
  /// Return the end iterator over nodes in machine region \p R.
  /// @param R Machine region whose nodes are walked.
  /// @return The end iterator over nodes in machine region \p R.
  static nodes_iterator nodes_end(MachineRegion *R) {
    return nodes_iterator::end(getEntryNode(R));
  }
};

/// GraphTraits specialization for flat iteration of MachineRegion graphs.
template <>
struct GraphTraits<FlatIt<MachineRegion *>>
    : public GraphTraits<FlatIt<MachineRegionNode *>> {
  /// Depth-first iterator over flat nodes in a machine region graph.
  using nodes_iterator =
      df_iterator<NodeRef, df_iterator_default_set<NodeRef>, false,
                  GraphTraits<FlatIt<NodeRef>>>;
  /// Return the entry basic-block node of machine region \p R.
  /// @param R Machine region whose entry node is requested.
  /// @return The entry basic-block node of machine region \p R.
  static NodeRef getEntryNode(MachineRegion *R) {
    return R->getBBNode(R->getEntry());
  }
  /// Return the begin iterator over flat nodes in machine region \p R.
  /// @param R Machine region whose nodes are walked.
  /// @return The begin iterator over flat nodes in machine region \p R.
  static nodes_iterator nodes_begin(MachineRegion *R) {
    return nodes_iterator::begin(getEntryNode(R));
  }
  /// Return the end iterator over flat nodes in machine region \p R.
  /// @param R Machine region whose nodes are walked.
  /// @return The end iterator over flat nodes in machine region \p R.
  static nodes_iterator nodes_end(MachineRegion *R) {
    return nodes_iterator::end(getEntryNode(R));
  }
};

/// GraphTraits specialization so const MachineRegion can be walked as a graph.
template <>
struct GraphTraits<const MachineRegion *>
    : public GraphTraits<const MachineRegionNode *> {
  /// Depth-first iterator over nodes in a const machine region graph.
  using nodes_iterator = df_iterator<NodeRef>;
  /// Return the entry node of const machine region \p R.
  /// @param R Machine region whose entry node is requested.
  /// @return The entry node of const machine region \p R.
  static NodeRef getEntryNode(const MachineRegion *R) {
    return R->getNode(R->getEntry());
  }
  /// Return the begin iterator over nodes in const machine region \p R.
  /// @param R Machine region whose nodes are walked.
  /// @return The begin iterator over nodes in const machine region \p R.
  static nodes_iterator nodes_begin(const MachineRegion *R) {
    return nodes_iterator::begin(getEntryNode(R));
  }
  /// Return the end iterator over nodes in const machine region \p R.
  /// @param R Machine region whose nodes are walked.
  /// @return The end iterator over nodes in const machine region \p R.
  static nodes_iterator nodes_end(const MachineRegion *R) {
    return nodes_iterator::end(getEntryNode(R));
  }
};

/// GraphTraits specialization for flat iteration of const MachineRegion graphs.
template <>
struct GraphTraits<FlatIt<const MachineRegion *>>
    : public GraphTraits<FlatIt<const MachineRegionNode *>> {
  /// Depth-first iterator over flat nodes in a const machine region graph.
  using nodes_iterator =
      df_iterator<NodeRef, df_iterator_default_set<NodeRef>, false,
                  GraphTraits<FlatIt<NodeRef>>>;
  /// Return the entry basic-block node of const machine region \p R.
  /// @param R Machine region whose entry node is requested.
  /// @return The entry basic-block node of const machine region \p R.
  static NodeRef getEntryNode(const MachineRegion *R) {
    return R->getBBNode(R->getEntry());
  }
  /// Return the begin iterator over flat nodes in const machine region \p R.
  /// @param R Machine region whose nodes are walked.
  /// @return The begin iterator over flat nodes in const machine region \p R.
  static nodes_iterator nodes_begin(const MachineRegion *R) {
    return nodes_iterator::begin(getEntryNode(R));
  }
  /// Return the end iterator over flat nodes in const machine region \p R.
  /// @param R Machine region whose nodes are walked.
  /// @return The end iterator over flat nodes in const machine region \p R.
  static nodes_iterator nodes_end(const MachineRegion *R) {
    return nodes_iterator::end(getEntryNode(R));
  }
};

/// GraphTraits specialization so MachineRegionInfo can be walked as a flat
/// region graph.
template <>
struct GraphTraits<MachineRegionInfo *>
    : public GraphTraits<FlatIt<MachineRegionNode *>> {
  /// Depth-first iterator over flat nodes in a MachineRegionInfo graph.
  using nodes_iterator = df_iterator<NodeRef, df_iterator_default_set<NodeRef>,
                                     false, GraphTraits<FlatIt<NodeRef>>>;

  /// Return the entry node of the top-level region in \p RI.
  /// @param RI Machine region info whose top-level region is the entry.
  /// @return The entry node of the top-level region in \p RI.
  static NodeRef getEntryNode(MachineRegionInfo *RI) {
    return GraphTraits<FlatIt<MachineRegion *>>::getEntryNode(
        RI->getTopLevelRegion());
  }

  /// Return the begin iterator over flat nodes in \p RI.
  /// @param RI Machine region info whose nodes are walked.
  /// @return The begin iterator over flat nodes in \p RI.
  static nodes_iterator nodes_begin(MachineRegionInfo *RI) {
    return nodes_iterator::begin(getEntryNode(RI));
  }

  /// Return the end iterator over flat nodes in \p RI.
  /// @param RI Machine region info whose nodes are walked.
  /// @return The end iterator over flat nodes in \p RI.
  static nodes_iterator nodes_end(MachineRegionInfo *RI) {
    return nodes_iterator::end(getEntryNode(RI));
  }
};

/// GraphTraits specialization so MachineRegionInfoPass can be walked as a flat
/// region graph.
template <>
struct GraphTraits<MachineRegionInfoPass *>
    : public GraphTraits<MachineRegionInfo *> {
  /// Depth-first iterator over flat nodes in a MachineRegionInfoPass graph.
  using nodes_iterator = df_iterator<NodeRef, df_iterator_default_set<NodeRef>,
                                     false, GraphTraits<FlatIt<NodeRef>>>;

  /// Return the entry node of the MachineRegionInfo owned by \p RI.
  /// @param RI Pass whose MachineRegionInfo provides the entry node.
  /// @return The entry node of the MachineRegionInfo owned by \p RI.
  static NodeRef getEntryNode(MachineRegionInfoPass *RI) {
    return GraphTraits<MachineRegionInfo *>::getEntryNode(&RI->getRegionInfo());
  }

  /// Return the begin iterator over flat nodes in the MachineRegionInfo of
  /// \p RI.
  /// @param RI Pass whose MachineRegionInfo nodes are walked.
  /// @return The begin iterator over flat nodes in the MachineRegionInfo of
  /// \p RI.
  static nodes_iterator nodes_begin(MachineRegionInfoPass *RI) {
    return GraphTraits<MachineRegionInfo *>::nodes_begin(&RI->getRegionInfo());
  }

  /// Return the end iterator over flat nodes in the MachineRegionInfo of \p RI.
  /// @param RI Pass whose MachineRegionInfo nodes are walked.
  /// @return The end iterator over flat nodes in the MachineRegionInfo of \p RI.
  static nodes_iterator nodes_end(MachineRegionInfoPass *RI) {
    return GraphTraits<MachineRegionInfo *>::nodes_end(&RI->getRegionInfo());
  }
};

extern template class RegionBase<RegionTraits<MachineFunction>>;
extern template class RegionNodeBase<RegionTraits<MachineFunction>>;
extern template class RegionInfoBase<RegionTraits<MachineFunction>>;

} // end namespace llvm

#endif // LLVM_CODEGEN_MACHINEREGIONINFO_H
