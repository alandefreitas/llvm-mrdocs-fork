//===--------- LoopIterator.h - Iterate over loop blocks --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
// This file defines iterators to visit the basic blocks within a loop.
//
// These iterators currently visit blocks within subloops as well.
// Unfortunately we have no efficient way of summarizing loop exits which would
// allow skipping subloops during traversal.
//
// If you want to visit all blocks in a loop and don't need an ordered traveral,
// use Loop::block_begin() instead.
//
// This is intentionally designed to work with ill-formed loops in which the
// backedge has been deleted. The only prerequisite is that all blocks
// contained within the loop according to the most recent LoopInfo analysis are
// reachable from the loop header.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPITERATOR_H
#define LLVM_ANALYSIS_LOOPITERATOR_H

#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/Analysis/LoopInfo.h"

namespace llvm {

class LoopBlocksTraversal;

/// Graph traits for traversing basic blocks in a loop body.
///
/// The graph starts at the loop header and visits the BasicBlocks that are in
/// the loop body, but not the loop header. Since the loop header is skipped,
/// the back edges are excluded.
///
/// TODO: Explore the possibility to implement LoopBlocksTraversal in terms of
///       LoopBodyTraits, so that insertEdge doesn't have to be specialized.
struct LoopBodyTraits {
  /// Node type pairing a loop with one of its basic blocks.
  using NodeRef = std::pair<const Loop *, BasicBlock *>;

  /// Successor iterator that carries the enclosing loop for edge filtering.
  ///
  /// This wraps a const Loop * into the iterator, so we know which edges to
  /// filter out.
  class WrappedSuccIterator
      : public iterator_adaptor_base<
            WrappedSuccIterator, succ_iterator,
            std::iterator_traits<succ_iterator>::iterator_category, NodeRef,
            std::ptrdiff_t, NodeRef *, NodeRef> {
    using BaseT = iterator_adaptor_base<
        WrappedSuccIterator, succ_iterator,
        std::iterator_traits<succ_iterator>::iterator_category, NodeRef,
        std::ptrdiff_t, NodeRef *, NodeRef>;

    const Loop *L;

  public:
    /// Construct a wrapped successor iterator for \p L.
    /// @param Begin Underlying successor iterator position.
    /// @param L Loop used to filter successor edges.
    WrappedSuccIterator(succ_iterator Begin, const Loop *L)
        : BaseT(Begin), L(L) {}

    /// Return the current successor as a loop/block node.
    /// @return Loop/block node for the current successor.
    NodeRef operator*() const { return {L, *I}; }
  };

  /// Predicate that keeps only non-header blocks contained in the loop.
  struct LoopBodyFilter {
    /// Return true if \p N is a loop-body block other than the header.
    /// @param N Loop/block node to test.
    /// @return True if \p N is in the loop body and is not the header.
    bool operator()(NodeRef N) const {
      const Loop *L = N.first;
      return N.second != L->getHeader() && L->contains(N.second);
    }
  };

  /// Filtered iterator over loop-body successor nodes.
  using ChildIteratorType =
      filter_iterator<WrappedSuccIterator, LoopBodyFilter>;

  /// Return the graph entry node for loop \p G.
  /// @param G Loop whose header is the graph entry.
  /// @return Loop/block node for the header of \p G.
  static NodeRef getEntryNode(const Loop &G) { return {&G, G.getHeader()}; }

  /// Return an iterator to the first child of \p Node.
  /// @param Node Loop/block node whose successors are traversed.
  /// @return Iterator to the first child of \p Node.
  static ChildIteratorType child_begin(NodeRef Node) {
    return make_filter_range(make_range<WrappedSuccIterator>(
                                 {succ_begin(Node.second), Node.first},
                                 {succ_end(Node.second), Node.first}),
                             LoopBodyFilter{})
        .begin();
  }

  /// Return an iterator past the last child of \p Node.
  /// @param Node Loop/block node whose successors are traversed.
  /// @return Iterator past the last child of \p Node.
  static ChildIteratorType child_end(NodeRef Node) {
    return make_filter_range(make_range<WrappedSuccIterator>(
                                 {succ_begin(Node.second), Node.first},
                                 {succ_end(Node.second), Node.first}),
                             LoopBodyFilter{})
        .end();
  }
};

/// Store the result of a depth first search within basic blocks contained by a
/// single loop.
///
/// TODO: This could be generalized for any CFG region, or the entire CFG.
class LoopBlocksDFS {
public:
  /// Postorder list iterators.
  typedef std::vector<BasicBlock*>::const_iterator POIterator;
  /// Reverse-postorder list iterator.
  typedef std::vector<BasicBlock*>::const_reverse_iterator RPOIterator;

  friend class LoopBlocksTraversal;

private:
  Loop *L;

  /// Map each block to its postorder number. A block is only mapped after it is
  /// preorder visited by DFS. It's postorder number is initially zero and set
  /// to nonzero after it is finished by postorder traversal.
  DenseMap<BasicBlock*, unsigned> PostNumbers;
  std::vector<BasicBlock*> PostBlocks;

public:
  /// Construct a DFS result store for \p Container.
  /// @param Container Loop whose blocks will be traversed.
  LoopBlocksDFS(Loop *Container) :
    L(Container), PostNumbers(NextPowerOf2(Container->getNumBlocks())) {
    PostBlocks.reserve(Container->getNumBlocks());
  }

  /// Return the loop being traversed.
  /// @return Loop whose blocks are stored in this DFS result.
  Loop *getLoop() const { return L; }

  /// Traverse the loop blocks and store the DFS result.
  /// @param LI LoopInfo used to identify blocks belonging to the loop.
  LLVM_ABI void perform(const LoopInfo *LI);

  /// Return true if postorder numbers are assigned to all loop blocks.
  /// @return True if every loop block has a postorder number.
  bool isComplete() const { return PostBlocks.size() == L->getNumBlocks(); }

  /// Iterate over the cached postorder blocks.
  /// @return Postorder iterator to the first block.
  POIterator beginPostorder() const {
    assert(isComplete() && "bad loop DFS");
    return PostBlocks.begin();
  }
  /// Return an iterator past the last postorder block.
  /// @return Postorder iterator past the last block.
  POIterator endPostorder() const { return PostBlocks.end(); }

  /// Reverse iterate over the cached postorder blocks.
  /// @return Reverse-postorder iterator to the first block.
  RPOIterator beginRPO() const {
    assert(isComplete() && "bad loop DFS");
    return PostBlocks.rbegin();
  }
  /// Return an iterator past the last reverse-postorder block.
  /// @return Reverse-postorder iterator past the last block.
  RPOIterator endRPO() const { return PostBlocks.rend(); }

  /// Return true if this block has been preorder visited.
  /// @param BB Basic block to query.
  /// @return True if \p BB has been preorder visited.
  bool hasPreorder(BasicBlock *BB) const { return PostNumbers.count(BB); }

  /// Return true if this block has a postorder number.
  /// @param BB Basic block to query.
  /// @return True if \p BB has a nonzero postorder number.
  bool hasPostorder(BasicBlock *BB) const {
    auto I = PostNumbers.find(BB);
    return I != PostNumbers.end() && I->second;
  }

  /// Get a block's postorder number.
  /// @param BB Basic block whose postorder number is returned.
  /// @return Postorder number of \p BB.
  unsigned getPostorder(BasicBlock *BB) const {
    auto I = PostNumbers.find(BB);
    assert(I != PostNumbers.end() && "block not visited by DFS");
    assert(I->second && "block not finished by DFS");
    return I->second;
  }

  /// Get a block's reverse postorder number.
  /// @param BB Basic block whose reverse-postorder number is returned.
  /// @return Reverse-postorder number of \p BB.
  unsigned getRPO(BasicBlock *BB) const {
    return 1 + PostBlocks.size() - getPostorder(BB);
  }

  /// Clear the cached DFS numbering and block lists.
  void clear() {
    PostNumbers.clear();
    PostBlocks.clear();
  }
};

/// Wrapper class to LoopBlocksDFS that provides a standard begin()/end()
/// interface for the DFS reverse post-order traversal of blocks in a loop body.
class LoopBlocksRPO {
private:
  LoopBlocksDFS DFS;

public:
  /// Construct an RPO wrapper for \p Container.
  /// @param Container Loop whose blocks will be traversed.
  LoopBlocksRPO(Loop *Container) : DFS(Container) {}

  /// Traverse the loop blocks and store the DFS result.
  /// @param LI LoopInfo used to identify blocks belonging to the loop.
  void perform(const LoopInfo *LI) {
    DFS.perform(LI);
  }

  /// Reverse iterate over the cached postorder blocks.
  /// @return Reverse-postorder iterator to the first block.
  LoopBlocksDFS::RPOIterator begin() const { return DFS.beginRPO(); }
  /// Return an iterator past the last reverse-postorder block.
  /// @return Reverse-postorder iterator past the last block.
  LoopBlocksDFS::RPOIterator end() const { return DFS.endRPO(); }
};

/// Traverse the blocks in a loop using a depth-first search.
class LoopBlocksTraversal
    : public PostOrderTraversalBase<LoopBlocksTraversal,
                                    GraphTraits<Function *>> {
  LoopBlocksDFS &DFS;
  const LoopInfo *LI;

public:
  /// Construct a traversal that records results into \p Storage.
  /// @param Storage DFS result store updated during traversal.
  /// @param LInfo LoopInfo used to identify blocks belonging to the loop.
  LoopBlocksTraversal(LoopBlocksDFS &Storage, const LoopInfo *LInfo)
      : DFS(Storage), LI(LInfo) {}

  /// Begin a postorder traversal over the loop blocks.
  ///
  /// This only needs to be done once. PostOrderTraversalBase "automatically"
  /// calls back to insertEdge and finishPostorder to record the DFS result.
  /// @return Iterator to the first block in postorder.
  iterator begin() {
    assert(DFS.PostBlocks.empty() && "Need clear DFS result before traversing");
    assert(DFS.L->getNumBlocks() && "cannot handle an empty graph");
    init(DFS.L->getHeader());
    return PostOrderTraversalBase::begin();
  }
  /// Return an iterator past the last block in the postorder traversal.
  /// @return Iterator past the last postorder block.
  iterator end() { return PostOrderTraversalBase::end(); }

  /// Decide whether to traverse a CFG edge into a loop block.
  ///
  /// Called upon reaching a block via a CFG edge. If this block is contained
  /// in the loop and has not been visited, then mark it preorder visited and
  /// return true (i.e., traverse the edge).
  ///
  /// TODO: If anyone is interested, we could record preorder numbers here.
  /// @param From Optional predecessor block of the CFG edge.
  /// @param BB Destination block of the CFG edge.
  /// @return True if the edge should be traversed.
  bool insertEdge(std::optional<BasicBlock *> From, BasicBlock *BB) {
    if (!DFS.L->contains(LI->getLoopFor(BB)))
      return false;

    return DFS.PostNumbers.insert(std::make_pair(BB, 0)).second;
  }

  /// Record that \p BB has finished postorder visitation.
  ///
  /// Called each time the iterator advances, indicating a block's postorder.
  /// @param BB Block that has completed postorder visitation.
  void finishPostorder(BasicBlock *BB) {
    assert(DFS.PostNumbers.count(BB) && "Loop DFS skipped preorder");
    DFS.PostBlocks.push_back(BB);
    DFS.PostNumbers[BB] = DFS.PostBlocks.size();
  }
};

} // End namespace llvm

#endif
