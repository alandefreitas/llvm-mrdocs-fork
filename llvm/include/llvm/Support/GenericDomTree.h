//===- GenericDomTree.h - Generic dominator trees for graphs ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This file defines a set of templates that efficiently compute a dominator
/// tree over a generic graph. This is used typically in LLVM for fast
/// dominance queries on the CFG, but is fully generic w.r.t. the underlying
/// graph types.
///
/// Unlike ADT/* graph algorithms, generic dominator tree has more requirements
/// on the graph's NodeRef. The NodeRef should be a pointer and,
/// either NodeRef->getParent() must return the parent node that is also a
/// pointer or DomTreeNodeTraits needs to be specialized.
///
/// FIXME: Maybe GenericDomTree needs a TreeTraits, instead of GraphTraits.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GENERICDOMTREE_H
#define LLVM_SUPPORT_GENERICDOMTREE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/CFGDiff.h"
#include "llvm/Support/CFGUpdate.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

namespace llvm {

/// Core dominator tree base class.
///
/// This class is a generic template over graph nodes. It is instantiated for
/// various graphs in the LLVM IR or in the code generator.
///
/// \tparam NodeT Graph node type (typically a basic-block type).
/// \tparam IsPostDom True for a post-dominator tree; false for a dominator tree.
template <typename NodeT, bool IsPostDom>
class DominatorTreeBase;

template <class BlockT, class LoopT> class LoopInfoBase;

namespace DomTreeBuilder {
template <typename DomTreeT>
struct SemiNCAInfo;
}  // namespace DomTreeBuilder

/// Base class for the actual dominator tree node.
template <class NodeT> class DomTreeNodeBase {
  friend class PostDominatorTree;
  friend class DominatorTreeBase<NodeT, false>;
  friend class DominatorTreeBase<NodeT, true>;
  friend struct DomTreeBuilder::SemiNCAInfo<DominatorTreeBase<NodeT, false>>;
  friend struct DomTreeBuilder::SemiNCAInfo<DominatorTreeBase<NodeT, true>>;

  NodeT *TheBB;
  DomTreeNodeBase *IDom;
  unsigned Level;
  DomTreeNodeBase *FirstChild = nullptr;
  DomTreeNodeBase *Sibling = nullptr;
  mutable unsigned DFSNumIn = ~0;
  mutable unsigned DFSNumOut = ~0;

 public:
  /// Construct a dominator tree node for block \p BB under immediate dominator \p iDom.
  ///
  /// \param BB CFG block represented by this node, or null for a virtual root.
  /// \param iDom Immediate dominator of this node, or null for a root.
  DomTreeNodeBase(NodeT *BB, DomTreeNodeBase *iDom)
      : TheBB(BB), IDom(iDom), Level(IDom ? IDom->Level + 1 : 0) {}

  /// Copy construction is deleted; nodes are owned by the tree allocator.
  ///
  /// \param Unused Ignored; copy construction is not supported.
  DomTreeNodeBase(const DomTreeNodeBase &Unused) = delete;
  /// Copy assignment is deleted; nodes are owned by the tree allocator.
  ///
  /// \param Unused Ignored; copy assignment is not supported.
  DomTreeNodeBase &operator=(const DomTreeNodeBase &Unused) = delete;

  /// Forward iterator over the children of a dominator tree node.
  class const_iterator
      : public iterator_facade_base<const_iterator, std::forward_iterator_tag,
                                    DomTreeNodeBase *> {
    DomTreeNodeBase *Node;

  public:
    /// Construct an iterator positioned at child \p Node.
    ///
    /// \param Node Child to point at, or null for end.
    const_iterator(DomTreeNodeBase *Node = nullptr) : Node(Node) {}
    /// Return true if this iterator equals \p Other.
    ///
    /// \param Other Iterator to compare against.
    /// \returns True if both iterators point to the same child.
    bool operator==(const const_iterator &Other) const {
      return Other.Node == Node;
    }
    /// Return the child node currently pointed to.
    ///
    /// \returns The child dominator tree node at the current position.
    DomTreeNodeBase *operator*() const { return Node; }
    /// Advance to the next sibling and return this iterator.
    ///
    /// \returns A reference to this iterator after advancing.
    const_iterator &operator++() {
      Node = Node->Sibling;
      return *this;
    }
    /// Advance to the next sibling and return the previous position.
    ///
    /// \param Unused Unused postfix-discriminator parameter.
    /// \returns A copy of the iterator before advancing.
    const_iterator operator++(int Unused) {
      const_iterator cp = *this;
      ++*this;
      return cp;
    }
  };
  // We don't permit modifications through the iterator.
  /// Iterator type over child dominator tree nodes (const-only).
  using iterator = const_iterator;

  /// Return an iterator to the first child.
  ///
  /// \returns An iterator to the first child node.
  iterator begin() const { return iterator{FirstChild}; }
  /// Return an iterator past the last child.
  ///
  /// \returns An end iterator past the last child.
  iterator end() const { return iterator{}; }

  /// Return a range over the child nodes.
  ///
  /// \returns A range covering this node's children.
  iterator_range<iterator> children() { return make_range(begin(), end()); }
  /// Return a const range over the child nodes.
  ///
  /// \returns A const range covering this node's children.
  iterator_range<const_iterator> children() const {
    return make_range(begin(), end());
  }

  /// Return the CFG block associated with this node, or null for a virtual root.
  ///
  /// \returns The CFG block, or null for a virtual root.
  NodeT *getBlock() const { return TheBB; }
  /// Return the immediate dominator of this node.
  ///
  /// \returns The immediate dominator node, or null for a root.
  DomTreeNodeBase *getIDom() const { return IDom; }
  /// Return the depth of this node in the tree (root is level 0).
  ///
  /// \returns The depth of this node (root is level 0).
  unsigned getLevel() const { return Level; }

  /// Return true if this node has no children.
  ///
  /// \returns True if this node has no children.
  bool isLeaf() const { return FirstChild == nullptr; }

  /// Return true if this node's children differ from those of \p Other.
  ///
  /// \param Other Node whose children are compared against this node.
  /// \returns True if the children sets differ; false if they match.
  bool compare(const DomTreeNodeBase *Other) const {
    if (Level != Other->Level) return true;

    SmallPtrSet<const NodeT *, 4> OtherChildren;
    for (const DomTreeNodeBase *I : *Other) {
      const NodeT *Nd = I->getBlock();
      OtherChildren.insert(Nd);
    }

    size_t OwnCount = 0;
    for (const DomTreeNodeBase *I : *this) {
      const NodeT *N = I->getBlock();
      if (OtherChildren.count(N) == 0)
        return true;
      ++OwnCount;
    }
    return OwnCount != OtherChildren.size();
  }

  /// Set the immediate dominator of this node to \p NewIDom.
  ///
  /// \param NewIDom New immediate dominator; must be non-null.
  void setIDom(DomTreeNodeBase *NewIDom) {
    assert(IDom && "No immediate dominator?");
    if (IDom == NewIDom) return;
    IDom->removeChild(this);

    // Switch to new dominator
    IDom = NewIDom;
    IDom->addChild(this);

    UpdateLevel();
  }

  /// Return the DFS discovery number of this node.
  ///
  /// Valid only after \c updateDFSNumbers() has been called.
  ///
  /// \returns The DFS discovery (in) number of this node.
  unsigned getDFSNumIn() const { return DFSNumIn; }
  /// Return the DFS finish number of this node.
  ///
  /// Valid only after \c updateDFSNumbers() has been called.
  ///
  /// \returns The DFS finish (out) number of this node.
  unsigned getDFSNumOut() const { return DFSNumOut; }

private:
  void addChild(DomTreeNodeBase *C) {
    assert(!C->Sibling && "cannot add child that already has siblings");
    C->Sibling = FirstChild;
    FirstChild = C;
  }

  void removeChild(DomTreeNodeBase *C) {
    DomTreeNodeBase **It = &FirstChild;
    while (*It != C) {
      assert(*It != nullptr && "Not in immediate dominator children list!");
      It = &(*It)->Sibling;
    }
    *It = C->Sibling;
    C->Sibling = nullptr;
  }

  // Return true if this node is dominated by other. Use this only if DFS info
  // is valid.
  bool DominatedBy(const DomTreeNodeBase *other) const {
    return this->DFSNumIn >= other->DFSNumIn &&
           this->DFSNumOut <= other->DFSNumOut;
  }

  void UpdateLevel() {
    assert(IDom);
    if (Level == IDom->Level + 1) return;

    SmallVector<DomTreeNodeBase *, 64> WorkStack = {this};

    while (!WorkStack.empty()) {
      DomTreeNodeBase *Current = WorkStack.pop_back_val();
      Current->Level = Current->IDom->Level + 1;

      for (DomTreeNodeBase *C : *Current) {
        assert(C->IDom);
        if (C->Level != C->IDom->Level + 1) WorkStack.push_back(C);
      }
    }
  }
};

/// Print dominator tree node \p Node to stream \p O.
///
/// \param O Output stream.
/// \param Node Tree node to print.
/// \returns The output stream \p O.
template <class NodeT>
raw_ostream &operator<<(raw_ostream &O, const DomTreeNodeBase<NodeT> *Node) {
  if (Node->getBlock())
    Node->getBlock()->printAsOperand(O, false);
  else
    O << " <<exit node>>";

  O << " {" << Node->getDFSNumIn() << "," << Node->getDFSNumOut() << "} ["
    << Node->getLevel() << "]\n";

  return O;
}

/// Recursively print the subtree rooted at \p N indented by level \p Lev.
///
/// \param N Root of the subtree to print.
/// \param O Output stream.
/// \param Lev Current indentation / depth level.
template <class NodeT>
void PrintDomTree(const DomTreeNodeBase<NodeT> *N, raw_ostream &O,
                  unsigned Lev) {
  O.indent(2 * Lev) << "[" << Lev << "] " << N;
  for (const auto &I : *N)
    PrintDomTree<NodeT>(I, O, Lev + 1);
}

namespace DomTreeBuilder {
// The routines below are provided in a separate header but referenced here.
template <typename DomTreeT>
void Calculate(DomTreeT &DT);

template <typename DomTreeT>
void CalculateWithUpdates(DomTreeT &DT,
                          ArrayRef<typename DomTreeT::UpdateType> Updates);

template <typename DomTreeT>
void InsertEdge(DomTreeT &DT, typename DomTreeT::NodePtr From,
                typename DomTreeT::NodePtr To);

template <typename DomTreeT>
void DeleteEdge(DomTreeT &DT, typename DomTreeT::NodePtr From,
                typename DomTreeT::NodePtr To);

template <typename DomTreeT>
void ApplyUpdates(DomTreeT &DT,
                  GraphDiff<typename DomTreeT::NodePtr,
                            DomTreeT::IsPostDominator> &PreViewCFG,
                  GraphDiff<typename DomTreeT::NodePtr,
                            DomTreeT::IsPostDominator> *PostViewCFG);

template <typename DomTreeT>
bool Verify(const DomTreeT &DT, typename DomTreeT::VerificationLevel VL);
}  // namespace DomTreeBuilder

/// Default DomTreeNode traits for NodeT. The default implementation assume a
/// Function-like NodeT. Can be specialized to support different node types.
template <typename NodeT> struct DomTreeNodeTraits {
  /// Concrete CFG node / basic-block type.
  using NodeType = NodeT;
  /// Pointer to a CFG node / basic block.
  using NodePtr = NodeT *;
  /// Pointer to the parent of a CFG node (e.g. a Function).
  using ParentPtr = decltype(std::declval<NodePtr>()->getParent());
  static_assert(std::is_pointer_v<ParentPtr>,
                "Currently NodeT's parent must be a pointer type");
  /// Type of the parent of a CFG node (e.g. Function).
  using ParentType = std::remove_pointer_t<ParentPtr>;

  /// Return the entry node of parent \p Parent.
  ///
  /// \param Parent Parent whose first / entry node is returned.
  /// \returns The entry CFG node of \p Parent.
  static NodeT *getEntryNode(ParentPtr Parent) { return &Parent->front(); }
  /// Return the parent of CFG node \p BB.
  ///
  /// \param BB CFG node whose parent is requested.
  /// \returns The parent of \p BB.
  static ParentPtr getParent(NodePtr BB) { return BB->getParent(); }
};

/// Core dominator tree base class.
///
/// This class is a generic template over graph nodes. It is instantiated for
/// various graphs in the LLVM IR or in the code generator.
///
/// \tparam NodeT Graph node type (typically a basic-block type).
/// \tparam IsPostDom True for a post-dominator tree; false for a dominator tree.
template <typename NodeT, bool IsPostDom> class DominatorTreeBase {
public:
  static_assert(GraphHasNodeNumbers<NodeT *>,
                "DominatorTreeBase requires graphs with numbered nodes");
  static_assert(std::is_pointer_v<typename GraphTraits<NodeT *>::NodeRef>,
                "Currently DominatorTreeBase supports only pointer nodes");
  /// Node traits used to access parents and entry nodes.
  using NodeTrait = DomTreeNodeTraits<NodeT>;
  /// Concrete CFG node / basic-block type.
  using NodeType = typename NodeTrait::NodeType;
  /// Pointer to a CFG node / basic block.
  using NodePtr = typename NodeTrait::NodePtr;
  /// Pointer to the parent of a CFG node (e.g. a Function).
  using ParentPtr = typename NodeTrait::ParentPtr;
  static_assert(std::is_pointer_v<ParentPtr>,
                "Currently NodeT's parent must be a pointer type");
  /// Type of the parent of a CFG node (e.g. Function).
  using ParentType = std::remove_pointer_t<ParentPtr>;
  /// True when this tree encodes post-dominance rather than dominance.
  static constexpr bool IsPostDominator = IsPostDom;

  /// CFG update describing an edge insertion or deletion.
  using UpdateType = cfg::Update<NodePtr>;
  /// Kind of CFG update (insert or delete).
  using UpdateKind = cfg::UpdateKind;
  /// CFG update kind for inserting an edge.
  static constexpr UpdateKind Insert = UpdateKind::Insert;
  /// CFG update kind for deleting an edge.
  static constexpr UpdateKind Delete = UpdateKind::Delete;

  /// How thoroughly \ref verify checks the tree.
  enum class VerificationLevel {
    /// Cheap structural checks against a freshly built tree.
    Fast,
    /// Stronger checks that still avoid the full sibling property.
    Basic,
    /// Full verification including parent and sibling properties.
    Full
  };

protected:
  // Dominators always have a single root, postdominators can have more.
  /// Root CFG blocks of this (post-)dominator tree.
  SmallVector<NodeT *, IsPostDom ? 4 : 1> Roots;

  /// Storage type for the dense map from block number to tree node.
  using DomTreeNodeStorageTy = SmallVector<DomTreeNodeBase<NodeT> *>;
  /// Tree nodes indexed by CFG block number (plus a slot for nullptr in PDT).
  DomTreeNodeStorageTy DomTreeNodes;
  /// Root node of the dominator tree (possibly a virtual root for PDT).
  DomTreeNodeBase<NodeT> *RootNode = nullptr;
  /// Parent of the CFG nodes (e.g. Function) this tree was built for.
  ParentPtr Parent = nullptr;

  // Use small slab size to reduce memory waste for modules with many small
  // functions. Compensate with a short GrowthDelay. This is relevant for
  // ThinLTO on modules with many functions (not uncommon in C++), where all
  // dominator trees are live at the same time.
  /// Bytes per bump-allocator slab used for tree nodes.
  static constexpr size_t SlabSize = 8 * sizeof(DomTreeNodeBase<NodeT>);
  /// Allocator backing DomTreeNodeBase instances.
  BumpPtrAllocatorImpl<MallocAllocator, SlabSize, /*SizeThreshold=*/SlabSize,
                       /*GrowthDelay=*/2>
      NodeAllocator;

  /// True when DFS in/out numbers on nodes are currently valid.
  mutable bool DFSInfoValid = false;
  /// Count of slow dominance queries since DFS numbers were last refreshed.
  mutable unsigned int SlowQueries = 0;
  /// Epoch of GraphTraits block numbers used when the tree was built/updated.
  unsigned BlockNumberEpoch = 0;

  friend struct DomTreeBuilder::SemiNCAInfo<DominatorTreeBase>;
  template <class BlockT, class LoopT> friend class LoopInfoBase;

public:
  /// Construct an empty dominator tree.
  DominatorTreeBase() = default;

  /// Copy construction is deleted; trees are moved or recalculated.
  ///
  /// \param Unused Ignored; copy construction is not supported.
  DominatorTreeBase(const DominatorTreeBase &Unused) = delete;
  /// Copy assignment is deleted; trees are moved or recalculated.
  ///
  /// \param Unused Ignored; copy assignment is not supported.
  DominatorTreeBase &operator=(const DominatorTreeBase &Unused) = delete;

  /// Move-construct, taking ownership of \p Arg's tree state.
  ///
  /// \param Arg Tree to move from.
  DominatorTreeBase(DominatorTreeBase &&Arg) = default;
  /// Move-assign, taking ownership of \p RHS's tree state.
  ///
  /// \param RHS Tree to move from.
  /// \returns A reference to this tree.
  DominatorTreeBase &operator=(DominatorTreeBase &&RHS) = default;

  /// Iteration over roots.
  ///
  /// This may include multiple blocks if we are computing post dominators.
  /// For forward dominators, this will always be a single block (the entry
  /// block).
  using root_iterator = typename SmallVectorImpl<NodeT *>::iterator;
  /// Const iterator over root CFG blocks.
  using const_root_iterator = typename SmallVectorImpl<NodeT *>::const_iterator;

  /// Return an iterator to the first root block.
  ///
  /// \returns An iterator to the first root block.
  root_iterator root_begin() { return Roots.begin(); }
  /// Return a const iterator to the first root block.
  ///
  /// \returns A const iterator to the first root block.
  const_root_iterator root_begin() const { return Roots.begin(); }
  /// Return an iterator past the last root block.
  ///
  /// \returns An iterator past the last root block.
  root_iterator root_end() { return Roots.end(); }
  /// Return a const iterator past the last root block.
  ///
  /// \returns A const iterator past the last root block.
  const_root_iterator root_end() const { return Roots.end(); }

  /// Return the number of root blocks.
  ///
  /// \returns The number of root blocks.
  size_t root_size() const { return Roots.size(); }

  /// Return a range over the root blocks.
  ///
  /// \returns A range covering the root blocks.
  iterator_range<root_iterator> roots() {
    return make_range(root_begin(), root_end());
  }
  /// Return a const range over the root blocks.
  ///
  /// \returns A const range covering the root blocks.
  iterator_range<const_root_iterator> roots() const {
    return make_range(root_begin(), root_end());
  }

  /// Return true if this tree encodes post-dominance rather than dominance.
  ///
  /// \returns True if this is a post-dominator tree.
  bool isPostDominator() const { return IsPostDominator; }

  /// Return true if this tree differs from \p Other.
  ///
  /// Returns false if the other dominator tree base matches this dominator tree
  /// base. Otherwise return true.
  ///
  /// \param Other Dominator tree to compare against.
  /// \returns True if the trees differ; false if they match.
  bool compare(const DominatorTreeBase &Other) const {
    if (Parent != Other.Parent) return true;

    if (Roots.size() != Other.Roots.size())
      return true;

    if (!std::is_permutation(Roots.begin(), Roots.end(), Other.Roots.begin()))
      return true;

    size_t NumNodes = 0;
    // All nodes we have must exist and be equal in the other tree.
    for (const auto &Node : DomTreeNodes) {
      if (!Node)
        continue;
      if (Node->compare(Other.getNode(Node->getBlock())))
        return true;
      NumNodes++;
    }

    // If the other tree has more nodes than we have, they're not equal.
    size_t NumOtherNodes = 0;
    for (const auto &OtherNode : Other.DomTreeNodes)
      if (OtherNode)
        NumOtherNodes++;
    return NumNodes != NumOtherNodes;
  }

private:
  // For LoopInfoBase's use in deriving a reverse-preorder traversal.
  auto nodes() const {
    return make_filter_range(DomTreeNodes, [](const DomTreeNodeBase<NodeT> *N) {
      return N != nullptr;
    });
  }

  unsigned getNodeIndex(const NodeT *BB) const {
    assert(BlockNumberEpoch == GraphTraits<ParentPtr>::getNumberEpoch(Parent) &&
           "dominator tree used with outdated block numbers");
    if constexpr (IsPostDom) {
      if (!BB)
        return 0; // BB may be nullptr for post-dominator tree, map to 0.
    } else
      assert(BB && "dominator tree block must be non-null");
    return GraphTraits<const NodeT *>::getNumber(BB) + IsPostDom;
  }

public:
  /// Return the (post-)dominator tree node for basic block \p BB.
  ///
  /// This is the same as using operator[] on this class. The result may (but is
  /// not required to) be null for a forward (backwards) statically unreachable
  /// block.
  ///
  /// \param BB Basic block whose tree node is requested.
  /// \returns The tree node for \p BB, or null if unreachable / absent.
  DomTreeNodeBase<NodeT> *getNode(const NodeT *BB) const {
    assert((!BB || Parent == NodeTrait::getParent(const_cast<NodeT *>(BB))) &&
           "cannot get DomTreeNode of block with different parent");
    if (unsigned Idx = getNodeIndex(BB); Idx < DomTreeNodes.size())
      return DomTreeNodes[Idx];
    return nullptr;
  }

  /// Return the (post-)dominator tree node for basic block \p BB.
  ///
  /// \param BB Basic block whose tree node is requested.
  /// \returns The tree node for \p BB, or null if unreachable / absent.
  DomTreeNodeBase<NodeT> *operator[](const NodeT *BB) const {
    return getNode(BB);
  }

  /// Return the root node of this (post-)dominator tree.
  ///
  /// This returns the entry node for the CFG of the function. If this tree
  /// represents the post-dominance relations for a function, however, this root
  /// may be a node with the block == NULL. This is the case when there are
  /// multiple exit nodes from a particular function. Consumers of post-dominance
  /// information must be capable of dealing with this possibility.
  ///
  /// \returns The root tree node (possibly a virtual root for post-dominators).
  DomTreeNodeBase<NodeT> *getRootNode() { return RootNode; }
  /// Return the root node of this (post-)dominator tree.
  ///
  /// \returns The root tree node (possibly a virtual root for post-dominators).
  const DomTreeNodeBase<NodeT> *getRootNode() const { return RootNode; }

  /// Get all nodes dominated by \p R, including \p R itself.
  ///
  /// \param R Root of the dominated subtree to collect.
  /// \param Result Cleared and filled with blocks dominated by \p R.
  void getDescendants(NodeT *R, SmallVectorImpl<NodeT *> &Result) const {
    Result.clear();
    const DomTreeNodeBase<NodeT> *RN = getNode(R);
    if (!RN)
      return; // If R is unreachable, it will not be present in the DOM tree.
    SmallVector<const DomTreeNodeBase<NodeT> *, 8> WL;
    WL.push_back(RN);

    while (!WL.empty()) {
      const DomTreeNodeBase<NodeT> *N = WL.pop_back_val();
      Result.push_back(N->getBlock());
      WL.append(N->begin(), N->end());
    }
  }

  /// Return true iff tree node \p A properly dominates tree node \p B.
  ///
  /// Note that this is not a constant time operation!
  ///
  /// \param A Potential proper dominator node.
  /// \param B Node that may be properly dominated by \p A.
  /// \returns True if \p A properly dominates \p B.
  bool properlyDominates(const DomTreeNodeBase<NodeT> *A,
                         const DomTreeNodeBase<NodeT> *B) const {
    if (!A || !B)
      return false;
    if (A == B)
      return false;
    return dominates(A, B);
  }

  /// Return true iff block \p A properly dominates block \p B.
  ///
  /// \param A Potential proper dominator block.
  /// \param B Block that may be properly dominated by \p A.
  /// \returns True if \p A properly dominates \p B.
  bool properlyDominates(const NodeT *A, const NodeT *B) const;

  /// Return true if \p A is dominated by the entry block of its function.
  ///
  /// \param A Block whose reachability from entry is queried.
  /// \returns True if \p A is reachable from the function entry.
  bool isReachableFromEntry(const NodeT *A) const {
    assert(!this->isPostDominator() &&
           "This is not implemented for post dominators");
    return getNode(A) != nullptr;
  }

  /// Return true iff tree node \p A dominates tree node \p B.
  ///
  /// Note that this is not a constant time operation!
  ///
  /// \param A Potential dominator node.
  /// \param B Node that may be dominated by \p A.
  /// \returns True if \p A dominates \p B.
  bool dominates(const DomTreeNodeBase<NodeT> *A,
                 const DomTreeNodeBase<NodeT> *B) const {
    // A node trivially dominates itself.
    if (B == A)
      return true;

    // An unreachable node is dominated by anything.
    if (!B)
      return true;

    // And dominates nothing.
    if (!A)
      return false;

    if (B->getIDom() == A) return true;

    if (A->getIDom() == B) return false;

    // A can only dominate B if it is higher in the tree.
    if (A->getLevel() >= B->getLevel()) return false;

    // Compare the result of the tree walk and the dfs numbers, if expensive
    // checks are enabled.
#ifdef EXPENSIVE_CHECKS
    assert((!DFSInfoValid ||
            (dominatedBySlowTreeWalk(A, B) == B->DominatedBy(A))) &&
           "Tree walk disagrees with dfs numbers!");
#endif

    if (DFSInfoValid)
      return B->DominatedBy(A);

    // If we end up with too many slow queries, just update the
    // DFS numbers on the theory that we are going to keep querying.
    SlowQueries++;
    if (SlowQueries > 32) {
      updateDFSNumbers();
      return B->DominatedBy(A);
    }

    return dominatedBySlowTreeWalk(A, B);
  }

  /// Return true iff block \p A dominates block \p B.
  ///
  /// \param A Potential dominator block.
  /// \param B Block that may be dominated by \p A.
  /// \returns True if \p A dominates \p B.
  bool dominates(const NodeT *A, const NodeT *B) const;

  /// Return the single root block of a forward dominator tree.
  ///
  /// \returns The sole root CFG block of a forward dominator tree.
  NodeT *getRoot() const {
    assert(this->Roots.size() == 1 && "Should always have entry node!");
    return this->Roots[0];
  }

  /// Find the nearest common dominator of blocks \p A and \p B.
  ///
  /// A and B must have tree nodes.
  ///
  /// \param A First block; must be present in the tree.
  /// \param B Second block; must be present in the tree.
  /// \returns The nearest common dominator of \p A and \p B.
  NodeT *findNearestCommonDominator(NodeT *A, NodeT *B) const {
    assert(A && B && "Pointers are not valid");
    assert(NodeTrait::getParent(A) == NodeTrait::getParent(B) &&
           "Two blocks are not in same function");

    // If either A or B is a entry block then it is nearest common dominator
    // (for forward-dominators).
    if (!isPostDominator()) {
      NodeT &Entry =
          *DomTreeNodeTraits<NodeT>::getEntryNode(NodeTrait::getParent(A));
      if (A == &Entry || B == &Entry)
        return &Entry;
    }

    DomTreeNodeBase<NodeT> *NodeA = getNode(A);
    DomTreeNodeBase<NodeT> *NodeB = getNode(B);
    assert(NodeA && "A must be in the tree");
    assert(NodeB && "B must be in the tree");

    // Use level information to go up the tree until the levels match. Then
    // continue going up til we arrive at the same node.
    while (NodeA != NodeB) {
      if (NodeA->getLevel() < NodeB->getLevel()) std::swap(NodeA, NodeB);

      NodeA = NodeA->IDom;
    }

    return NodeA->getBlock();
  }

  /// Find the nearest common dominator of const blocks \p A and \p B.
  ///
  /// \param A First block; must be present in the tree.
  /// \param B Second block; must be present in the tree.
  /// \returns The nearest common dominator of \p A and \p B.
  const NodeT *findNearestCommonDominator(const NodeT *A,
                                          const NodeT *B) const {
    // Cast away the const qualifiers here. This is ok since
    // const is re-introduced on the return type.
    return findNearestCommonDominator(const_cast<NodeT *>(A),
                                      const_cast<NodeT *>(B));
  }

  /// Return true if \p A is the virtual root of a post-dominator tree.
  ///
  /// \param A Tree node that may represent the virtual (null-block) root.
  /// \returns True if \p A is the virtual (null-block) post-dominator root.
  bool isVirtualRoot(const DomTreeNodeBase<NodeT> *A) const {
    return isPostDominator() && !A->getBlock();
  }

  /// Find the nearest common dominator of every block in \p Nodes.
  ///
  /// \param Nodes Non-empty range of blocks that must be present in the tree.
  /// \returns The nearest common dominator of all blocks in \p Nodes, or null
  ///          if the virtual root is reached.
  template <typename IteratorTy>
  NodeT *findNearestCommonDominator(iterator_range<IteratorTy> Nodes) const {
    assert(!Nodes.empty() && "Nodes list is empty!");

    NodeT *NCD = *Nodes.begin();
    for (NodeT *Node : llvm::drop_begin(Nodes)) {
      NCD = findNearestCommonDominator(NCD, Node);

      // Stop when the root is reached.
      if (isVirtualRoot(getNode(NCD)))
        return nullptr;
    }

    return NCD;
  }

  //===--------------------------------------------------------------------===//
  // API to update (Post)DominatorTree information based on modifications to
  // the CFG...

  /// Inform the dominator tree about a sequence of CFG edge insertions and
  /// deletions and perform a batch update on the tree.
  ///
  /// This function should be used when there were multiple CFG updates after
  /// the last dominator tree update. It takes care of performing the updates
  /// in sync with the CFG and optimizes away the redundant operations that
  /// cancel each other.
  /// The functions expects the sequence of updates to be balanced. Eg.:
  ///  - {{Insert, A, B}, {Delete, A, B}, {Insert, A, B}} is fine, because
  ///    logically it results in a single insertions.
  ///  - {{Insert, A, B}, {Insert, A, B}} is invalid, because it doesn't make
  ///    sense to insert the same edge twice.
  ///
  /// What's more, the functions assumes that it's safe to ask every node in the
  /// CFG about its children and inverse children. This implies that deletions
  /// of CFG edges must not delete the CFG nodes before calling this function.
  ///
  /// The applyUpdates function can reorder the updates and remove redundant
  /// ones internally (as long as it is done in a deterministic fashion). The
  /// batch updater is also able to detect sequences of zero and exactly one
  /// update -- it's optimized to do less work in these cases.
  ///
  /// Note that for postdominators it automatically takes care of applying
  /// updates on reverse edges internally (so there's no need to swap the
  /// From and To pointers when constructing DominatorTree::UpdateType).
  /// The type of updates is the same for DomTreeBase<T> and PostDomTreeBase<T>
  /// with the same template parameter T.
  ///
  /// \param Updates An ordered sequence of updates to perform. The current CFG
  /// and the reverse of these updates provides the pre-view of the CFG.
  ///
  void applyUpdates(ArrayRef<UpdateType> Updates);

  /// Apply CFG updates with an additional post-view of the CFG.
  ///
  /// \param Updates An ordered sequence of updates to perform. The current CFG
  /// and the reverse of these updates provides the pre-view of the CFG.
  /// \param PostViewUpdates An ordered sequence of update to perform in order
  /// to obtain a post-view of the CFG. The DT will be updated assuming the
  /// obtained PostViewCFG is the desired end state.
  void applyUpdates(ArrayRef<UpdateType> Updates,
                    ArrayRef<UpdateType> PostViewUpdates);

  /// Inform the dominator tree about a CFG edge insertion and update the tree.
  ///
  /// This function has to be called just before or just after making the update
  /// on the actual CFG. There cannot be any other updates that the dominator
  /// tree doesn't know about.
  ///
  /// Note that for postdominators it automatically takes care of inserting
  /// a reverse edge internally (so there's no need to swap the parameters).
  ///
  /// \param From Source block of the inserted edge.
  /// \param To Destination block of the inserted edge.
  void insertEdge(NodeT *From, NodeT *To);

  /// Inform the dominator tree about a CFG edge deletion and update the tree.
  ///
  /// This function has to be called just after making the update on the actual
  /// CFG. An internal functions checks if the edge doesn't exist in the CFG in
  /// DEBUG mode. There cannot be any other updates that the
  /// dominator tree doesn't know about.
  ///
  /// Note that for postdominators it automatically takes care of deleting
  /// a reverse edge internally (so there's no need to swap the parameters).
  ///
  /// \param From Source block of the deleted edge.
  /// \param To Destination block of the deleted edge.
  void deleteEdge(NodeT *From, NodeT *To);

  /// Add a new node to the dominator tree information.
  ///
  /// This creates a new node as a child of DomBB dominator node, linking it
  /// into the children list of the immediate dominator.
  ///
  /// \param BB New node in CFG.
  /// \param DomBB CFG node that is dominator for BB.
  /// \returns New dominator tree node that represents new CFG node.
  ///
  DomTreeNodeBase<NodeT> *addNewBlock(NodeT *BB, NodeT *DomBB) {
    assert(getNode(BB) == nullptr && "Block already in dominator tree!");
    DomTreeNodeBase<NodeT> *IDomNode = getNode(DomBB);
    assert(IDomNode && "Not immediate dominator specified for block!");
    DFSInfoValid = false;
    return createNode(BB, IDomNode);
  }

  /// Add a new node to the forward dominator tree and make it a new root.
  ///
  /// \param BB New node in CFG.
  /// \returns New dominator tree node that represents new CFG node.
  ///
  DomTreeNodeBase<NodeT> *setNewRoot(NodeT *BB) {
    assert(getNode(BB) == nullptr && "Block already in dominator tree!");
    assert(!this->isPostDominator() &&
           "Cannot change root of post-dominator tree");
    DFSInfoValid = false;
    DomTreeNodeBase<NodeT> *NewNode = createNode(BB);
    if (Roots.empty()) {
      addRoot(BB);
    } else {
      assert(Roots.size() == 1);
      NodeT *OldRoot = Roots.front();
      DomTreeNodeBase<NodeT> *OldNode = getNode(OldRoot);
      NewNode->addChild(OldNode);
      OldNode->IDom = NewNode;
      OldNode->UpdateLevel();
      Roots[0] = BB;
    }
    return RootNode = NewNode;
  }

  /// Change the immediate dominator of tree node \p N to \p NewIDom.
  ///
  /// \param N Node whose immediate dominator is updated.
  /// \param NewIDom New immediate dominator of \p N.
  void changeImmediateDominator(DomTreeNodeBase<NodeT> *N,
                                DomTreeNodeBase<NodeT> *NewIDom) {
    assert(N && NewIDom && "Cannot change null node pointers!");
    DFSInfoValid = false;
    N->setIDom(NewIDom);
  }

  /// Change the immediate dominator of block \p BB to block \p NewBB.
  ///
  /// \param BB Block whose immediate dominator is updated.
  /// \param NewBB Block that becomes the new immediate dominator.
  void changeImmediateDominator(NodeT *BB, NodeT *NewBB) {
    changeImmediateDominator(getNode(BB), getNode(NewBB));
  }

  /// Remove a leaf node for block \p BB from the dominator tree.
  ///
  /// Block must not dominate any other blocks. Removes node from its immediate
  /// dominator's children list. Deletes dominator node associated with basic
  /// block BB.
  ///
  /// \param BB Block whose tree node is erased; must be a leaf in the tree.
  void eraseNode(NodeT *BB) {
    unsigned Idx = getNodeIndex(BB);
    DomTreeNodeBase<NodeT> *Node = DomTreeNodes[Idx];
    assert(Node && "Removing node that isn't in dominator tree.");
    assert(Node->isLeaf() && "Node is not a leaf node.");

    DFSInfoValid = false;

    // Remove node from immediate dominator's children list.
    if (DomTreeNodeBase<NodeT> *IDom = Node->getIDom())
      IDom->removeChild(Node);

    DomTreeNodes[Idx] = nullptr;

    if (!IsPostDom) return;

    // Remember to update PostDominatorTree roots.
    auto RIt = llvm::find(Roots, BB);
    if (RIt != Roots.end()) {
      std::swap(*RIt, Roots.back());
      Roots.pop_back();
    }
  }

  /// Update the tree after \p NewBB is split and has a single successor.
  ///
  /// \param NewBB Newly split block with exactly one successor.
  void splitBlock(NodeT *NewBB) {
    if (IsPostDominator)
      Split<Inverse<NodeT *>>(NewBB);
    else
      Split<NodeT *>(NewBB);
  }

  /// Print the dominator tree in human-readable form.
  ///
  /// \param O Output stream to write to.
  void print(raw_ostream &O) const {
    O << "=============================--------------------------------\n";
    if (IsPostDominator)
      O << "Inorder PostDominator Tree: ";
    else
      O << "Inorder Dominator Tree: ";
    if (!DFSInfoValid)
      O << "DFSNumbers invalid: " << SlowQueries << " slow queries.";
    O << "\n";

    // The postdom tree can have a null root if there are no returns.
    if (getRootNode()) PrintDomTree<NodeT>(getRootNode(), O, 1);
    O << "Roots: ";
    for (const NodePtr Block : Roots) {
      Block->printAsOperand(O, false);
      O << " ";
    }
    O << "\n";
  }

public:
  /// updateDFSNumbers - Assign In and Out numbers to the nodes while walking
  /// dominator tree in dfs order.
  void updateDFSNumbers() const {
    if (DFSInfoValid) {
      SlowQueries = 0;
      return;
    }

    SmallVector<std::pair<const DomTreeNodeBase<NodeT> *,
                          typename DomTreeNodeBase<NodeT>::const_iterator>,
                32> WorkStack;

    const DomTreeNodeBase<NodeT> *ThisRoot = getRootNode();
    assert((!Parent || ThisRoot) && "Empty constructed DomTree");
    if (!ThisRoot)
      return;

    // Both dominators and postdominators have a single root node. In the case
    // case of PostDominatorTree, this node is a virtual root.
    WorkStack.push_back({ThisRoot, ThisRoot->begin()});

    unsigned DFSNum = 0;
    ThisRoot->DFSNumIn = DFSNum++;

    while (!WorkStack.empty()) {
      const DomTreeNodeBase<NodeT> *Node = WorkStack.back().first;
      const auto ChildIt = WorkStack.back().second;

      // If we visited all of the children of this node, "recurse" back up the
      // stack setting the DFOutNum.
      if (ChildIt == Node->end()) {
        Node->DFSNumOut = DFSNum;
        WorkStack.pop_back();
      } else {
        // Otherwise, recursively visit this child.
        const DomTreeNodeBase<NodeT> *Child = *ChildIt;
        ++WorkStack.back().second;

        WorkStack.push_back({Child, Child->begin()});
        Child->DFSNumIn = DFSNum++;
      }
    }

    SlowQueries = 0;
    DFSInfoValid = true;
  }

private:
  void updateBlockNumberEpoch() {
    BlockNumberEpoch = GraphTraits<ParentPtr>::getNumberEpoch(Parent);
  }

public:
  /// Compute a dominator tree for the given function.
  ///
  /// \param Func Parent function (or similar) whose CFG is analyzed.
  void recalculate(ParentType &Func);

  /// Recompute the tree for \p Func starting from the given CFG updates.
  ///
  /// \param Func Parent function (or similar) whose CFG is analyzed.
  /// \param Updates Ordered CFG updates used to seed incremental construction.
  void recalculate(ParentType &Func, ArrayRef<UpdateType> Updates);

  /// Update dominator tree after renumbering blocks.
  void updateBlockNumbers() {
    updateBlockNumberEpoch();

    unsigned MaxNumber = GraphTraits<ParentPtr>::getMaxNumber(Parent);
    DomTreeNodeStorageTy NewVector;
    NewVector.resize(MaxNumber + IsPostDom); // index 0 is for nullptr
    for (DomTreeNodeBase<NodeT> *Node : DomTreeNodes) {
      if (Node)
        NewVector[getNodeIndex(Node->getBlock())] = Node;
    }
    DomTreeNodes = std::move(NewVector);
  }

  /// Check whether the dominator tree is correct at the given verification level.
  ///
  /// There are 3 levels of verification:
  ///  - Full --  verifies if the tree is correct by making sure all the
  ///             properties (including the parent and the sibling property)
  ///             hold.
  ///             Takes O(N^3) time.
  ///
  ///  - Basic -- checks if the tree is correct, but compares it to a freshly
  ///             constructed tree instead of checking the sibling property.
  ///             Takes O(N^2) time.
  ///
  ///  - Fast  -- checks basic tree structure and compares it with a freshly
  ///             constructed tree.
  ///             Takes O(N^2) time worst case, but is faster in practise (same
  ///             as tree construction).
  ///
  /// \param VL How thoroughly to verify the tree.
  /// \returns True if the tree passes verification at level \p VL.
  bool verify(VerificationLevel VL = VerificationLevel::Full) const;

  /// Clear all tree nodes, roots, and allocator state.
  void reset() {
    DomTreeNodes.clear();
    Roots.clear();
    RootNode = nullptr;
    Parent = nullptr;
    DFSInfoValid = false;
    NodeAllocator.Reset();
    SlowQueries = 0;
  }

protected:
  /// Append \p BB to the list of root blocks.
  ///
  /// \param BB Root CFG block to record.
  inline void addRoot(NodeT *BB) { this->Roots.push_back(BB); }

  /// Create a node for \p BB; the caller must link it with addChild.
  ///
  /// \param BB CFG block represented by the new tree node.
  /// \param IDom Immediate dominator node, or null for a root.
  /// \returns The newly allocated tree node (not yet linked as a child).
  DomTreeNodeBase<NodeT> *createNodeUnlinked(NodeT *BB,
                                             DomTreeNodeBase<NodeT> *IDom) {
    static_assert(std::is_trivially_destructible_v<DomTreeNodeBase<NodeT>>);
    auto *Node = new (NodeAllocator) DomTreeNodeBase<NodeT>(BB, IDom);
    unsigned Idx = getNodeIndex(BB);
    if (Idx >= DomTreeNodes.size()) {
      // Add 1 for post-dominator trees, 0 is nullptr block.
      unsigned Max = GraphTraits<ParentPtr>::getMaxNumber(Parent) + IsPostDom;
      assert(Idx < Max && "getMaxNumber returned too small value");
      DomTreeNodes.resize(Max);
    }
    DomTreeNodes[Idx] = Node;
    return Node;
  }

  /// Create a node for \p BB and link it under \p IDom when provided.
  ///
  /// \param BB CFG block represented by the new tree node.
  /// \param IDom Immediate dominator node, or null for a root.
  /// \returns The newly allocated tree node, linked under \p IDom if given.
  DomTreeNodeBase<NodeT> *createNode(NodeT *BB,
                                     DomTreeNodeBase<NodeT> *IDom = nullptr) {
    auto *Node = createNodeUnlinked(BB, IDom);
    if (IDom)
      IDom->addChild(Node);
    return Node;
  }

  /// Update the tree after \p NewBB is split and has a single successor.
  ///
  /// \param NewBB Newly split block with exactly one successor.
  template <class N>
  void Split(typename GraphTraits<N>::NodeRef NewBB) {
    using GraphT = GraphTraits<N>;
    using NodeRef = typename GraphT::NodeRef;
    assert(llvm::hasSingleElement(children<N>(NewBB)) &&
           "NewBB should have a single successor!");
    NodeRef NewBBSucc = *GraphT::child_begin(NewBB);

    SmallVector<NodeRef, 4> PredBlocks(inverse_children<N>(NewBB));

    assert(!PredBlocks.empty() && "No predblocks?");

    bool NewBBDominatesNewBBSucc = true;
    for (auto *Pred : inverse_children<N>(NewBBSucc)) {
      if (Pred != NewBB && !dominates(NewBBSucc, Pred) &&
          isReachableFromEntry(Pred)) {
        NewBBDominatesNewBBSucc = false;
        break;
      }
    }

    // Find NewBB's immediate dominator and create new dominator tree node for
    // NewBB.
    NodeT *NewBBIDom = nullptr;
    unsigned i = 0;
    for (i = 0; i < PredBlocks.size(); ++i)
      if (isReachableFromEntry(PredBlocks[i])) {
        NewBBIDom = PredBlocks[i];
        break;
      }

    // It's possible that none of the predecessors of NewBB are reachable;
    // in that case, NewBB itself is unreachable, so nothing needs to be
    // changed.
    if (!NewBBIDom) return;

    for (i = i + 1; i < PredBlocks.size(); ++i) {
      if (isReachableFromEntry(PredBlocks[i]))
        NewBBIDom = findNearestCommonDominator(NewBBIDom, PredBlocks[i]);
    }

    // Create the new dominator tree node... and set the idom of NewBB.
    DomTreeNodeBase<NodeT> *NewBBNode = addNewBlock(NewBB, NewBBIDom);

    // If NewBB strictly dominates other blocks, then it is now the immediate
    // dominator of NewBBSucc.  Update the dominator tree as appropriate.
    if (NewBBDominatesNewBBSucc) {
      DomTreeNodeBase<NodeT> *NewBBSuccNode = getNode(NewBBSucc);
      changeImmediateDominator(NewBBSuccNode, NewBBNode);
    }
  }

 private:
  bool dominatedBySlowTreeWalk(const DomTreeNodeBase<NodeT> *A,
                               const DomTreeNodeBase<NodeT> *B) const {
    assert(A != B);
    assert(A && B);

    const unsigned ALevel = A->getLevel();
    const DomTreeNodeBase<NodeT> *IDom;

    // Don't walk nodes above A's subtree. When we reach A's level, we must
    // either find A or be in some other subtree not dominated by A.
    while ((IDom = B->getIDom()) != nullptr && IDom->getLevel() >= ALevel)
      B = IDom;  // Walk up the tree

    return B == A;
  }
};

/// Dominator tree specialization (not post-dominator).
template <typename T>
using DomTreeBase = DominatorTreeBase<T, false>;

/// Post-dominator tree specialization.
template <typename T>
using PostDomTreeBase = DominatorTreeBase<T, true>;

// These two functions are declared out of line as a workaround for building
// with old (< r147295) versions of clang because of pr11642.

/// Return true if block \p A dominates block \p B.
///
/// \param A Potential dominator block.
/// \param B Block that may be dominated by \p A.
/// \returns True if \p A dominates \p B.
template <typename NodeT, bool IsPostDom>
bool DominatorTreeBase<NodeT, IsPostDom>::dominates(const NodeT *A,
                                                    const NodeT *B) const {
  if (A == B)
    return true;

  return dominates(getNode(A), getNode(B));
}

/// Return true if block \p A properly dominates block \p B.
///
/// \param A Potential proper dominator block.
/// \param B Block that may be properly dominated by \p A.
/// \returns True if \p A properly dominates \p B.
template <typename NodeT, bool IsPostDom>
bool DominatorTreeBase<NodeT, IsPostDom>::properlyDominates(
    const NodeT *A, const NodeT *B) const {
  if (A == B)
    return false;

  return dominates(getNode(A), getNode(B));
}

} // end namespace llvm

#endif // LLVM_SUPPORT_GENERICDOMTREE_H
