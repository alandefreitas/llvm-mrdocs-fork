//===- RegionIterator.h - Iterators to iteratate over Regions ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines the iterators to iterate over the elements of a Region.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_REGIONITERATOR_H
#define LLVM_ANALYSIS_REGIONITERATOR_H

#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/PointerIntPair.h"
#include "llvm/Analysis/RegionInfo.h"
#include <cassert>
#include <iterator>
#include <type_traits>

namespace llvm {

class BasicBlock;
class RegionInfo;

//===----------------------------------------------------------------------===//
/// Hierarchical RegionNode successor iterator.
///
/// This iterator iterates over all successors of a RegionNode.
///
/// For a BasicBlock RegionNode it skips all BasicBlocks that are not part of
/// the parent Region.  Furthermore for BasicBlocks that start a subregion, a
/// RegionNode representing the subregion is returned.
///
/// For a subregion RegionNode there is just one successor. The RegionNode
/// representing the exit of the subregion.
template <class NodeRef, class BlockT, class RegionT> class RNSuccIterator {
public:
  /// Forward-iterator category for RegionNode successors.
  using iterator_category = std::forward_iterator_tag;
  /// Successor RegionNode reference yielded by this iterator.
  using value_type = NodeRef;
  /// Signed distance between RegionNode successor iterators.
  using difference_type = std::ptrdiff_t;
  /// Pointer to a successor RegionNode reference.
  using pointer = value_type *;
  /// Reference to a successor RegionNode reference.
  using reference = value_type &;

private:
  using BlockTraits = GraphTraits<BlockT *>;
  using SuccIterTy = typename BlockTraits::ChildIteratorType;

  // The iterator works in two modes, bb mode or region mode.
  enum ItMode {
    // In BB mode it returns all successors of this BasicBlock as its
    // successors.
    ItBB,
    // In region mode there is only one successor, thats the regionnode mapping
    // to the exit block of the regionnode
    ItRgBegin, // At the beginning of the regionnode successor.
    ItRgEnd    // At the end of the regionnode successor.
  };

  static_assert(std::is_pointer<NodeRef>::value,
                "FIXME: Currently RNSuccIterator only supports NodeRef as "
                "pointers due to the use of pointer-specific data structures "
                "(e.g. PointerIntPair and SmallPtrSet) internally. Generalize "
                "it to support non-pointer types");

  // Use two bit to represent the mode iterator.
  PointerIntPair<NodeRef, 2, ItMode> Node;

  // The block successor iterator.
  SuccIterTy BItor;

  // advanceRegionSucc - A region node has only one successor. It reaches end
  // once we advance it.
  void advanceRegionSucc() {
    assert(Node.getInt() == ItRgBegin && "Cannot advance region successor!");
    Node.setInt(ItRgEnd);
  }

  NodeRef getNode() const { return Node.getPointer(); }

  // isRegionMode - Is the current iterator in region mode?
  bool isRegionMode() const { return Node.getInt() != ItBB; }

  // Get the immediate successor. This function may return a Basic Block
  // RegionNode or a subregion RegionNode.
  NodeRef getISucc(BlockT *BB) const {
    NodeRef succ;
    succ = getNode()->getParent()->getNode(BB);
    assert(succ && "BB not in Region or entered subregion!");
    return succ;
  }

  // getRegionSucc - Return the successor basic block of a SubRegion RegionNode.
  inline BlockT* getRegionSucc() const {
    assert(Node.getInt() == ItRgBegin && "Cannot get the region successor!");
    return getNode()->template getNodeAs<RegionT>()->getExit();
  }

  // isExit - Is this the exit BB of the Region?
  inline bool isExit(BlockT* BB) const {
    return getNode()->getParent()->getExit() == BB;
  }

public:
  /// This iterator type (self alias).
  using Self = RNSuccIterator<NodeRef, BlockT, RegionT>;

  /// Create begin iterator of a RegionNode.
  /// @param node RegionNode whose successors are iterated.
  inline RNSuccIterator(NodeRef node)
      : Node(node, node->isSubRegion() ? ItRgBegin : ItBB),
        BItor(BlockTraits::child_begin(node->getEntry())) {
    // Skip the exit block
    if (!isRegionMode())
      while (BlockTraits::child_end(node->getEntry()) != BItor && isExit(*BItor))
        ++BItor;

    if (isRegionMode() && isExit(getRegionSucc()))
      advanceRegionSucc();
  }

  /// Create an end iterator.
  /// @param node RegionNode whose successor range is closed.
  /// @param IsEnd Discriminator selecting the end-iterator constructor.
  inline RNSuccIterator(NodeRef node, bool IsEnd)
      : Node(node, node->isSubRegion() ? ItRgEnd : ItBB),
        BItor(BlockTraits::child_end(node->getEntry())) {}

  /// Return true if both iterators are at the same successor position.
  /// @param x Iterator to compare with.
  /// @return True if both iterators point to the same successor.
  inline bool operator==(const Self& x) const {
    assert(isRegionMode() == x.isRegionMode() && "Broken iterator!");
    if (isRegionMode())
      return Node.getInt() == x.Node.getInt();
    else
      return BItor == x.BItor;
  }

  /// Return true if the iterators differ in successor position.
  /// @param x Iterator to compare with.
  /// @return True if the iterators point to different successors.
  inline bool operator!=(const Self& x) const { return !operator==(x); }

  /// Return the current successor RegionNode.
  /// @return The current successor RegionNode.
  inline value_type operator*() const {
    BlockT *BB = isRegionMode() ? getRegionSucc() : *BItor;
    assert(!isExit(BB) && "Iterator out of range!");
    return getISucc(BB);
  }

  /// Advance to the next successor and return this iterator.
  /// @return A reference to this iterator after advancing.
  inline Self& operator++() {
    if(isRegionMode()) {
      // The Region only has 1 successor.
      advanceRegionSucc();
    } else {
      // Skip the exit.
      do
        ++BItor;
      while (BItor != BlockTraits::child_end(getNode()->getEntry())
          && isExit(*BItor));
    }
    return *this;
  }

  /// Advance to the next successor and return the prior iterator position.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return A copy of the iterator before advancing.
  inline Self operator++(int Unused) {
    Self tmp = *this;
    ++*this;
    return tmp;
  }
};

//===----------------------------------------------------------------------===//
/// Flat RegionNode iterator.
///
/// The Flat Region iterator will iterate over all BasicBlock RegionNodes that
/// are contained in the Region and its subregions. This is close to a virtual
/// control flow graph of the Region.
template <class NodeRef, class BlockT, class RegionT>
class RNSuccIterator<FlatIt<NodeRef>, BlockT, RegionT> {
  using BlockTraits = GraphTraits<BlockT *>;
  using SuccIterTy = typename BlockTraits::ChildIteratorType;

  NodeRef Node;
  SuccIterTy Itor;

public:
  /// Forward-iterator category for flat RegionNode successors.
  using iterator_category = std::forward_iterator_tag;
  /// Flat successor RegionNode reference yielded by this iterator.
  using value_type = NodeRef;
  /// Signed distance between flat RegionNode successor iterators.
  using difference_type = std::ptrdiff_t;
  /// Pointer to a flat successor RegionNode reference.
  using pointer = value_type *;
  /// Reference to a flat successor RegionNode reference.
  using reference = value_type &;

  /// This flat iterator type (self alias).
  using Self = RNSuccIterator<FlatIt<NodeRef>, BlockT, RegionT>;

  /// Create the iterator from a RegionNode.
  ///
  /// Note that the incoming node must be a bb node, otherwise it will trigger
  /// an assertion when we try to get a BasicBlock.
  /// @param node Basic-block RegionNode whose flat successors are iterated.
  inline RNSuccIterator(NodeRef node)
      : Node(node), Itor(BlockTraits::child_begin(node->getEntry())) {
    assert(!Node->isSubRegion() &&
           "Subregion node not allowed in flat iterating mode!");
    assert(Node->getParent() && "A BB node must have a parent!");

    // Skip the exit block of the iterating region.
    while (BlockTraits::child_end(Node->getEntry()) != Itor &&
           Node->getParent()->getExit() == *Itor)
      ++Itor;
  }

  /// Create an end iterator.
  /// @param node Basic-block RegionNode whose flat successor range is closed.
  /// @param IsEnd Discriminator selecting the end-iterator constructor.
  inline RNSuccIterator(NodeRef node, bool IsEnd)
      : Node(node), Itor(BlockTraits::child_end(node->getEntry())) {
    assert(!Node->isSubRegion() &&
           "Subregion node not allowed in flat iterating mode!");
  }

  /// Return true if both iterators are at the same flat successor position.
  /// @param x Iterator to compare with.
  /// @return True if both iterators point to the same flat successor.
  inline bool operator==(const Self& x) const {
    assert(Node->getParent() == x.Node->getParent()
           && "Cannot compare iterators of different regions!");

    return Itor == x.Itor && Node == x.Node;
  }

  /// Return true if the iterators differ in flat successor position.
  /// @param x Iterator to compare with.
  /// @return True if the iterators point to different flat successors.
  inline bool operator!=(const Self& x) const { return !operator==(x); }

  /// Return the current flat successor RegionNode.
  /// @return The current flat successor RegionNode.
  inline value_type operator*() const {
    BlockT *BB = *Itor;

    // Get the iterating region.
    RegionT *Parent = Node->getParent();

    // The only case that the successor reaches out of the region is it reaches
    // the exit of the region.
    assert(Parent->getExit() != BB && "iterator out of range!");

    return Parent->getBBNode(BB);
  }

  /// Advance to the next flat successor and return this iterator.
  /// @return A reference to this iterator after advancing.
  inline Self& operator++() {
    // Skip the exit block of the iterating region.
    do
      ++Itor;
    while (Itor != succ_end(Node->getEntry())
        && Node->getParent()->getExit() == *Itor);

    return *this;
  }

  /// Advance to the next flat successor and return the prior iterator position.
  /// @param Unused Unused postfix-discriminator parameter.
  /// @return A copy of the iterator before advancing.
  inline Self operator++(int Unused) {
    Self tmp = *this;
    ++*this;
    return tmp;
  }
};

/// Return a begin successor iterator for RegionNode \p Node.
/// @param Node RegionNode whose successors are iterated.
/// @return A begin successor iterator for \p Node.
template <class NodeRef, class BlockT, class RegionT>
inline RNSuccIterator<NodeRef, BlockT, RegionT> succ_begin(NodeRef Node) {
  return RNSuccIterator<NodeRef, BlockT, RegionT>(Node);
}

/// Return an end successor iterator for RegionNode \p Node.
/// @param Node RegionNode whose successor range is closed.
/// @return An end successor iterator for \p Node.
template <class NodeRef, class BlockT, class RegionT>
inline RNSuccIterator<NodeRef, BlockT, RegionT> succ_end(NodeRef Node) {
  return RNSuccIterator<NodeRef, BlockT, RegionT>(Node, true);
}

//===--------------------------------------------------------------------===//
// RegionNode GraphTraits specialization so the bbs in the region can be
// iterate by generic graph iterators.
//
// NodeT can either be region node or const region node, otherwise child_begin
// and child_end fail.

#define RegionNodeGraphTraits(NodeT, BlockT, RegionT)                          \
  /** GraphTraits specialization for hierarchical RegionNode successors. */ \
  template <> struct GraphTraits<NodeT *> {                                    \
    /** Graph node type for a RegionNode. */ \
    using NodeRef = NodeT *;                                                   \
    /** Iterator over hierarchical RegionNode successors. */ \
    using ChildIteratorType = RNSuccIterator<NodeRef, BlockT, RegionT>;        \
    /** Return \p N as the graph entry node. \
     *  @param N RegionNode used as the entry. \
     *  @return The entry RegionNode \p N. */ \
    static NodeRef getEntryNode(NodeRef N) { return N; }                       \
    /** Return the begin iterator over successors of \p N. \
     *  @param N Parent RegionNode. \
     *  @return A begin successor iterator for \p N. */ \
    static inline ChildIteratorType child_begin(NodeRef N) {                   \
      return RNSuccIterator<NodeRef, BlockT, RegionT>(N);                      \
    }                                                                          \
    /** Return the end iterator over successors of \p N. \
     *  @param N Parent RegionNode. \
     *  @return An end successor iterator for \p N. */ \
    static inline ChildIteratorType child_end(NodeRef N) {                     \
      return RNSuccIterator<NodeRef, BlockT, RegionT>(N, true);                \
    }                                                                          \
  };                                                                           \
  /** GraphTraits specialization for flat RegionNode successors. */ \
  template <> struct GraphTraits<FlatIt<NodeT *>> {                            \
    /** Graph node type for a flat RegionNode. */ \
    using NodeRef = NodeT *;                                                   \
    /** Iterator over flat RegionNode successors. */ \
    using ChildIteratorType =                                                  \
        RNSuccIterator<FlatIt<NodeRef>, BlockT, RegionT>;                      \
    /** Return \p N as the graph entry node. \
     *  @param N RegionNode used as the entry. \
     *  @return The entry RegionNode \p N. */ \
    static NodeRef getEntryNode(NodeRef N) { return N; }                       \
    /** Return the begin iterator over flat successors of \p N. \
     *  @param N Parent RegionNode. \
     *  @return A begin flat successor iterator for \p N. */ \
    static inline ChildIteratorType child_begin(NodeRef N) {                   \
      return RNSuccIterator<FlatIt<NodeRef>, BlockT, RegionT>(N);              \
    }                                                                          \
    /** Return the end iterator over flat successors of \p N. \
     *  @param N Parent RegionNode. \
     *  @return An end flat successor iterator for \p N. */ \
    static inline ChildIteratorType child_end(NodeRef N) {                     \
      return RNSuccIterator<FlatIt<NodeRef>, BlockT, RegionT>(N, true);        \
    }                                                                          \
  }

#define RegionGraphTraits(RegionT, NodeT)                                      \
  /** GraphTraits specialization that walks a Region as a graph of nodes. */ \
  template <> struct GraphTraits<RegionT *> : public GraphTraits<NodeT *> {    \
    /** Depth-first iterator over RegionNodes in the region. */ \
    using nodes_iterator = df_iterator<NodeRef>;                               \
    /** Return the RegionNode for the entry block of \p R. \
     *  @param R Region whose entry node is requested. \
     *  @return The RegionNode for the entry block of \p R. */ \
    static NodeRef getEntryNode(RegionT *R) {                                  \
      return R->getNode(R->getEntry());                                        \
    }                                                                          \
    /** Return a depth-first begin iterator over nodes of \p R. \
     *  @param R Region to walk. \
     *  @return A depth-first begin iterator over nodes of \p R. */ \
    static nodes_iterator nodes_begin(RegionT *R) {                            \
      return nodes_iterator::begin(getEntryNode(R));                           \
    }                                                                          \
    /** Return a depth-first end iterator over nodes of \p R. \
     *  @param R Region to walk. \
     *  @return A depth-first end iterator over nodes of \p R. */ \
    static nodes_iterator nodes_end(RegionT *R) {                              \
      return nodes_iterator::end(getEntryNode(R));                             \
    }                                                                          \
  };                                                                           \
  /** GraphTraits specialization that walks a Region in flat CFG mode. */ \
  template <>                                                                  \
  struct GraphTraits<FlatIt<RegionT *>>                                        \
      : public GraphTraits<FlatIt<NodeT *>> {                                  \
    /** Depth-first iterator over flat RegionNodes in the region. */ \
    using nodes_iterator =                                                     \
        df_iterator<NodeRef, df_iterator_default_set<NodeRef>, false,          \
                    GraphTraits<FlatIt<NodeRef>>>;                             \
    /** Return the basic-block RegionNode for the entry of \p R. \
     *  @param R Region whose entry node is requested. \
     *  @return The basic-block RegionNode for the entry of \p R. */ \
    static NodeRef getEntryNode(RegionT *R) {                                  \
      return R->getBBNode(R->getEntry());                                      \
    }                                                                          \
    /** Return a depth-first begin iterator over flat nodes of \p R. \
     *  @param R Region to walk. \
     *  @return A depth-first begin iterator over flat nodes of \p R. */ \
    static nodes_iterator nodes_begin(RegionT *R) {                            \
      return nodes_iterator::begin(getEntryNode(R));                           \
    }                                                                          \
    /** Return a depth-first end iterator over flat nodes of \p R. \
     *  @param R Region to walk. \
     *  @return A depth-first end iterator over flat nodes of \p R. */ \
    static nodes_iterator nodes_end(RegionT *R) {                              \
      return nodes_iterator::end(getEntryNode(R));                             \
    }                                                                          \
  }

RegionNodeGraphTraits(RegionNode, BasicBlock, Region);
RegionNodeGraphTraits(const RegionNode, BasicBlock, Region);

RegionGraphTraits(Region, RegionNode);
RegionGraphTraits(const Region, const RegionNode);

/// GraphTraits specialization that walks RegionInfo as a flat region graph.
template <> struct GraphTraits<RegionInfo*>
  : public GraphTraits<FlatIt<RegionNode*>> {
  /// Depth-first iterator over RegionNodes from RegionInfo.
  using nodes_iterator =
      df_iterator<NodeRef, df_iterator_default_set<NodeRef>, false,
                  GraphTraits<FlatIt<NodeRef>>>;

  /// Return the entry RegionNode of the top-level region in \p RI.
  /// @param RI RegionInfo whose top-level region provides the entry.
  /// @return The entry RegionNode of the top-level region in \p RI.
  static NodeRef getEntryNode(RegionInfo *RI) {
    return GraphTraits<FlatIt<Region*>>::getEntryNode(RI->getTopLevelRegion());
  }

  /// Return a depth-first begin iterator over nodes of \p RI.
  /// @param RI RegionInfo to walk.
  /// @return A depth-first begin iterator over nodes of \p RI.
  static nodes_iterator nodes_begin(RegionInfo* RI) {
    return nodes_iterator::begin(getEntryNode(RI));
  }

  /// Return a depth-first end iterator over nodes of \p RI.
  /// @param RI RegionInfo to walk.
  /// @return A depth-first end iterator over nodes of \p RI.
  static nodes_iterator nodes_end(RegionInfo *RI) {
    return nodes_iterator::end(getEntryNode(RI));
  }
};

/// GraphTraits specialization that walks RegionInfoPass via its RegionInfo.
template <> struct GraphTraits<RegionInfoPass*>
  : public GraphTraits<RegionInfo *> {
  /// Depth-first iterator over RegionNodes from RegionInfoPass.
  using nodes_iterator =
      df_iterator<NodeRef, df_iterator_default_set<NodeRef>, false,
                  GraphTraits<FlatIt<NodeRef>>>;

  /// Return the entry RegionNode for the RegionInfo owned by \p RI.
  /// @param RI RegionInfoPass whose RegionInfo provides the entry.
  /// @return The entry RegionNode for the RegionInfo owned by \p RI.
  static NodeRef getEntryNode(RegionInfoPass *RI) {
    return GraphTraits<RegionInfo*>::getEntryNode(&RI->getRegionInfo());
  }

  /// Return a depth-first begin iterator over nodes of \p RI.
  /// @param RI RegionInfoPass to walk.
  /// @return A depth-first begin iterator over nodes of \p RI.
  static nodes_iterator nodes_begin(RegionInfoPass* RI) {
    return GraphTraits<RegionInfo*>::nodes_begin(&RI->getRegionInfo());
  }

  /// Return a depth-first end iterator over nodes of \p RI.
  /// @param RI RegionInfoPass to walk.
  /// @return A depth-first end iterator over nodes of \p RI.
  static nodes_iterator nodes_end(RegionInfoPass *RI) {
    return GraphTraits<RegionInfo*>::nodes_end(&RI->getRegionInfo());
  }
};

} // end namespace llvm

#endif // LLVM_ANALYSIS_REGIONITERATOR_H
