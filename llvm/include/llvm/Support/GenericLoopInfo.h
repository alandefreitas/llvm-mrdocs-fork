//===- GenericLoopInfo - Generic Loop Info for graphs -----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the LoopInfoBase class that is used to identify natural
// loops and determine the loop depth of various nodes in a generic graph of
// blocks.  A natural loop has exactly one entry-point, which is called the
// header. Note that natural loops may actually be several loops that share the
// same header node.
//
// This analysis calculates the nesting structure of loops in a function.  For
// each natural loop identified, this analysis identifies natural loops
// contained entirely within the loop and the basic blocks that make up the
// loop.
//
// It can calculate on the fly various bits of information, for example:
//
//  * whether there is a preheader for the loop
//  * the number of back edges to the header
//  * whether or not a particular block branches out of the loop
//  * the successor blocks of the loop
//  * the loop depth
//  * etc...
//
// Note that this analysis specifically identifies *Loops* not cycles or SCCs
// in the graph.  There can be strongly connected components in the graph which
// this analysis will not recognize and that will not be represented by a Loop
// instance.  In particular, a Loop might be inside such a non-loop SCC, or a
// non-loop SCC might contain a sub-SCC which is a Loop.
//
// For an overview of terminology used in this API (and thus all of our loop
// analyses or transforms), see docs/LoopTerminology.md.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_SUPPORT_GENERICLOOPINFO_H
#define LLVM_SUPPORT_GENERICLOOPINFO_H

#include "llvm/ADT/DenseSet.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SetOperations.h"
#include "llvm/Support/Allocator.h"
#include "llvm/Support/GenericDomTree.h"

namespace llvm {

/// Analysis that builds and owns the loop forest for a function.
///
/// This class builds and contains all of the top-level loop structures in the
/// specified function.
///
/// \tparam N Graph node type (typically a basic-block type).
/// \tparam M Loop type representing loops in the forest.
template <class N, class M> class LoopInfoBase;
template <class N, class M> class LoopBase;

//===----------------------------------------------------------------------===//
/// Instances of this class are used to represent loops that are detected in the
/// flow graph.
///
template <class BlockT, class LoopT> class LoopBase {
  LoopT *ParentLoop;
  // Loops contained entirely within this one.
  std::vector<LoopT *> SubLoops;

  // The list of blocks in this loop; first entry is the header. Either borrows
  // a slice of the owning LoopInfo's BlockLayout, marked by the
  // BorrowedCapacity sentinel, or is a private allocation of BlockCapacity
  // slots from its allocator.
  //
  // Until analyze()'s layout carve runs, PendingHeader stashes the loop header
  // (see pendingHeader()).
  union {
    /// Header stashed while the loop is under construction.
    BlockT *PendingHeader;
    /// Contiguous block list for this loop; first entry is the header.
    BlockT **BlockData = nullptr;
  };
  unsigned BlockLen = 0;
  unsigned BlockCapacity = 0;

  static constexpr unsigned BorrowedCapacity = -1u;

  // The LoopInfo that owns this loop. Used to answer contains(BlockT *) from
  // the central block-to-loop map.
  LoopInfoBase<BlockT, LoopT> *LI = nullptr;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  /// Indicator that this loop is no longer a valid loop.
  bool IsInvalid = false;
#endif

  LoopBase(const LoopBase<BlockT, LoopT> &) = delete;
  const LoopBase<BlockT, LoopT> &
  operator=(const LoopBase<BlockT, LoopT> &) = delete;

public:
  /// Return the nesting level of this loop.
  ///
  /// An outer-most loop has depth 1, for consistency with loop depth values
  /// used for basic blocks, where depth 0 is used for blocks not inside any
  /// loops.
  ///
  /// \returns The nesting level of this loop.
  unsigned getLoopDepth() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    unsigned D = 1;
    for (const LoopT *CurLoop = ParentLoop; CurLoop;
         CurLoop = CurLoop->ParentLoop)
      ++D;
    return D;
  }
  /// Return the header block of this loop.
  ///
  /// \returns The header block of this loop.
  BlockT *getHeader() const { return getBlocks().front(); }

  /// Return the parent loop, or nullptr for top-level loops.
  ///
  /// A loop is either top-level in a function (that is, it is not contained in
  /// any other loop) or it is entirely enclosed in some other loop. If a loop
  /// is top-level, it has no parent, otherwise its parent is the innermost loop
  /// in which it is enclosed.
  ///
  /// \returns The parent loop, or nullptr for top-level loops.
  LoopT *getParentLoop() const { return ParentLoop; }

  /// Return the outermost loop containing this loop (possibly itself).
  ///
  /// \returns The outermost loop containing this loop.
  const LoopT *getOutermostLoop() const {
    const LoopT *L = static_cast<const LoopT *>(this);
    while (L->ParentLoop)
      L = L->ParentLoop;
    return L;
  }

  /// Return the outermost loop containing this loop (possibly itself).
  ///
  /// \returns The outermost loop containing this loop.
  LoopT *getOutermostLoop() {
    LoopT *L = static_cast<LoopT *>(this);
    while (L->ParentLoop)
      L = L->ParentLoop;
    return L;
  }

  /// Set the parent loop pointer without updating child lists.
  ///
  /// This is a raw interface for bypassing addChildLoop.
  ///
  /// \param L New parent loop, or nullptr for a top-level loop.
  void setParentLoop(LoopT *L) {
    assert(!isInvalid() && "Loop not in a valid state!");
    ParentLoop = L;
  }

  /// Return true if the specified loop is contained within this loop.
  ///
  /// This walks the parent chain and is O(depth). Deep nesting is not a
  /// performance target (yet).
  ///
  /// \param L Loop that may be nested inside this loop.
  /// \returns True if the specified loop is contained within this loop.
  bool contains(const LoopT *L) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    for (;;) {
      if (L == this)
        return true;
      if (!L)
        return false;
      L = L->getParentLoop();
    }
  }

  /// Return true if the specified basic block is in this loop.
  ///
  /// Uses LoopInfo's block-to-loop map. This is only valid when that map agrees
  /// with the block lists. Avoid when the loop nest is being restructured, when
  /// a block may appear in a loop's block list before it is mapped to that
  /// loop. Code in such a transient state must scan getBlocks() directly
  /// instead.
  ///
  /// \param BB Basic block that may belong to this loop.
  /// \returns True if the basic block is in this loop.
  bool contains(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    // A block from another function is never contained, and its number would
    // otherwise index this function's map.
    if (BB->getParent() != LI->ParentPtr)
      return false;
    return contains(LI->lookupLoopFor(BB));
  }

  /// Return true if the specified instruction is in this loop.
  ///
  /// \param Inst Instruction whose parent block is tested for membership.
  /// \returns True if the instruction is in this loop.
  template <class InstT> bool contains(const InstT *Inst) const {
    return contains(Inst->getParent());
  }

  /// Return the loops contained entirely within this loop.
  ///
  /// \returns The loops contained entirely within this loop.
  const std::vector<LoopT *> &getSubLoops() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return SubLoops;
  }
  /// Iterator over immediate child loops.
  using iterator = typename std::vector<LoopT *>::const_iterator;
  /// Reverse iterator over immediate child loops.
  using reverse_iterator =
      typename std::vector<LoopT *>::const_reverse_iterator;
  /// Return an iterator to the first child loop.
  ///
  /// \returns An iterator to the first child loop.
  iterator begin() const { return getSubLoops().begin(); }
  /// Return an iterator past the last child loop.
  ///
  /// \returns An iterator past the last child loop.
  iterator end() const { return getSubLoops().end(); }
  /// Return a reverse iterator to the last child loop.
  ///
  /// \returns A reverse iterator to the last child loop.
  reverse_iterator rbegin() const { return getSubLoops().rbegin(); }
  /// Return a reverse iterator past the first child loop.
  ///
  /// \returns A reverse iterator past the first child loop.
  reverse_iterator rend() const { return getSubLoops().rend(); }

  // LoopInfo does not detect irreducible control flow, just natural
  // loops. That is, it is possible that there is cyclic control
  // flow within the "innermost loop" or around the "outermost
  // loop".

  /// Return true if the loop does not contain any (natural) loops.
  ///
  /// \returns True if this loop contains no nested loops.
  bool isInnermost() const { return getSubLoops().empty(); }
  /// Return true if the loop does not have a parent (natural) loop.
  ///
  /// \returns True if this loop has no parent loop.
  // (i.e. it is outermost, which is the same as top-level).
  bool isOutermost() const { return getParentLoop() == nullptr; }

  /// Get a list of the basic blocks which make up this loop.
  ///
  /// \returns The basic blocks that make up this loop.
  ArrayRef<BlockT *> getBlocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return ArrayRef<BlockT *>(BlockData, BlockLen);
  }
  /// Iterator over the basic blocks in this loop.
  using block_iterator = typename ArrayRef<BlockT *>::const_iterator;
  /// Return an iterator to the first block in this loop.
  ///
  /// \returns An iterator to the first block in this loop.
  block_iterator block_begin() const { return getBlocks().begin(); }
  /// Return an iterator past the last block in this loop.
  ///
  /// \returns An iterator past the last block in this loop.
  block_iterator block_end() const { return getBlocks().end(); }
  /// Return a range over the basic blocks in this loop.
  ///
  /// \returns A range over the basic blocks in this loop.
  inline iterator_range<block_iterator> blocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return make_range(block_begin(), block_end());
  }

  /// Get the number of blocks in this loop in constant time.
  /// Invalidate the loop, indicating that it is no longer a loop.
  ///
  /// \returns The number of blocks in this loop.
  unsigned getNumBlocks() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return BlockLen;
  }

  /// Return true if this loop is no longer valid.
  ///
  /// The only valid use of this helper is "assert(L.isInvalid())" or
  /// equivalent, since IsInvalid is set to true by the destructor. In other
  /// words, if this accessor returns true, the caller has already triggered UB
  /// by calling this accessor; and so it can only be called in a context where
  /// a return value of true indicates a programmer error.
  ///
  /// \returns True if this loop is no longer valid.
  bool isInvalid() const {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    return IsInvalid;
#else
    return false;
#endif
  }

  /// Return true if \p BB can branch to a block outside this loop.
  ///
  /// \p BB must be inside the loop.
  ///
  /// \param BB Exiting-candidate block; must belong to this loop.
  /// \returns True if \p BB can branch outside this loop.
  bool isLoopExiting(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(contains(BB) && "Exiting block must be part of the loop");
    for (const auto *Succ : children<const BlockT *>(BB)) {
      if (!contains(Succ))
        return true;
    }
    return false;
  }

  /// Return true if \p BB is a latch of this loop.
  ///
  /// A latch block is a block that contains a branch back to the header. This
  /// function is useful when there are multiple latches in a loop because
  /// \fn getLoopLatch will return nullptr in that case.
  ///
  /// \param BB Block that may be a latch; must belong to this loop.
  /// \returns True if \p BB is a latch of this loop.
  bool isLoopLatch(const BlockT *BB) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(contains(BB) && "block does not belong to the loop");
    return llvm::is_contained(inverse_children<BlockT *>(getHeader()), BB);
  }

  /// Calculate the number of back edges to the loop header.
  ///
  /// \returns The number of back edges to the loop header.
  unsigned getNumBackEdges() const {
    assert(!isInvalid() && "Loop not in a valid state!");
    return llvm::count_if(inverse_children<BlockT *>(getHeader()),
                          [&](BlockT *Pred) { return contains(Pred); });
  }

  //===--------------------------------------------------------------------===//
  // APIs for simple analysis of the loop.
  //
  // Note that all of these methods can fail on general loops (ie, there may not
  // be a preheader, etc).  For best success, the loop simplification and
  // induction variable canonicalization pass should be used to normalize loops
  // for easy analysis.  These methods assume canonical loops.

  /// Return all blocks inside the loop that have successors outside the loop.
  ///
  /// These are the blocks _inside of the current loop_ which branch out. The
  /// returned list is always unique.
  ///
  /// \param ExitingBlocks Output vector filled with unique exiting blocks.
  void getExitingBlocks(SmallVectorImpl<BlockT *> &ExitingBlocks) const;

  /// If getExitingBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  ///
  /// \returns The unique exiting block, or null.
  BlockT *getExitingBlock() const;

  /// Return all of the successor blocks of this loop.
  ///
  /// These are the blocks _outside of the current loop_ which are branched to.
  ///
  /// \param ExitBlocks Output vector filled with exit blocks.
  void getExitBlocks(SmallVectorImpl<BlockT *> &ExitBlocks) const;

  /// If getExitBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  ///
  /// \returns The unique exit block, or null.
  BlockT *getExitBlock() const;

  /// Return true if no exit block for the loop has a predecessor that is
  /// outside the loop.
  ///
  /// \returns True if every exit block has only predecessors inside the loop.
  bool hasDedicatedExits() const;

  /// Return all unique successor blocks of this loop.
  ///
  /// These are the blocks _outside of the current loop_ which are branched to.
  ///
  /// \param ExitBlocks Output vector filled with unique exit blocks.
  void getUniqueExitBlocks(SmallVectorImpl<BlockT *> &ExitBlocks) const;

  /// Return unique exit blocks of this loop, ignoring exits from the latch.
  ///
  /// If an exit reached from the latch also has a non-latch predecessor in the
  /// loop, it is still added to ExitBlocks. These are the blocks _outside of
  /// the current loop_ which are branched to.
  ///
  /// \param ExitBlocks Output vector filled with unique non-latch exit blocks.
  void getUniqueNonLatchExitBlocks(SmallVectorImpl<BlockT *> &ExitBlocks) const;

  /// If getUniqueExitBlocks would return exactly one block, return that block.
  /// Otherwise return null.
  ///
  /// \returns The unique exit block, or null.
  BlockT *getUniqueExitBlock() const;

  /// Return the preheader block for this loop, or null if there is none.
  ///
  /// A loop has a preheader if there is only one edge to the header of the loop
  /// from outside of the loop and it is legal to hoist instructions into the
  /// predecessor. If this is the case, the block branching to the header of the
  /// loop is the preheader node.
  ///
  /// \returns The preheader block, or null if there is none.
  BlockT *getLoopPreheader() const;

  /// Return the unique predecessor of the loop header outside the loop, or null.
  ///
  /// This is less strict than the loop "preheader" concept, which requires the
  /// predecessor to have exactly one successor.
  ///
  /// \returns The unique external predecessor of the header, or null.
  BlockT *getLoopPredecessor() const;

  /// If there is a single latch block for this loop, return it.
  /// A latch block is a block that contains a branch back to the header.
  ///
  /// \returns The unique latch block, or null if there is not exactly one.
  BlockT *getLoopLatch() const;

  /// Return all loop latch blocks of this loop.
  ///
  /// A latch block is a block that contains a branch back to the header.
  ///
  /// \param LoopLatches Output vector filled with latch blocks.
  void getLoopLatches(SmallVectorImpl<BlockT *> &LoopLatches) const {
    assert(!isInvalid() && "Loop not in a valid state!");
    BlockT *H = getHeader();
    for (const auto Pred : inverse_children<BlockT *>(H))
      if (contains(Pred))
        LoopLatches.push_back(Pred);
  }

  /// Append inner loops of \p L to \p PreOrderLoops in preorder.
  ///
  /// Siblings are visited in forward program order.
  ///
  /// \param L Root of the loop nest whose inner loops are collected.
  /// \param PreOrderLoops Output vector that receives the inner loops.
  template <class Type>
  static void getInnerLoopsInPreorder(const LoopT &L,
                                      SmallVectorImpl<Type> &PreOrderLoops) {
    SmallVector<LoopT *, 4> PreOrderWorklist;
    PreOrderWorklist.append(L.rbegin(), L.rend());

    while (!PreOrderWorklist.empty()) {
      LoopT *L = PreOrderWorklist.pop_back_val();
      // Sub-loops are stored in forward program order, but will process the
      // worklist backwards so append them in reverse order.
      PreOrderWorklist.append(L->rbegin(), L->rend());
      PreOrderLoops.push_back(L);
    }
  }

  /// Return all loops in this nest in preorder, siblings in program order.
  ///
  /// \returns All loops in this nest in preorder.
  SmallVector<const LoopT *, 4> getLoopsInPreorder() const {
    SmallVector<const LoopT *, 4> PreOrderLoops;
    const LoopT *CurLoop = static_cast<const LoopT *>(this);
    PreOrderLoops.push_back(CurLoop);
    getInnerLoopsInPreorder(*CurLoop, PreOrderLoops);
    return PreOrderLoops;
  }
  /// Return all loops in this nest in preorder, siblings in program order.
  ///
  /// \returns All loops in this nest in preorder.
  SmallVector<LoopT *, 4> getLoopsInPreorder() {
    SmallVector<LoopT *, 4> PreOrderLoops;
    LoopT *CurLoop = static_cast<LoopT *>(this);
    PreOrderLoops.push_back(CurLoop);
    getInnerLoopsInPreorder(*CurLoop, PreOrderLoops);
    return PreOrderLoops;
  }

  //===--------------------------------------------------------------------===//
  // APIs for updating loop information after changing the CFG
  //

  /// Add \p NewBB as a member of this loop and all parent loops.
  ///
  /// This method is used by other analyses to update loop information. NewBB is
  /// also recorded in \p LI as belonging to this loop. It is not valid to
  /// replace the loop header with this method.
  ///
  /// \param NewBB Basic block to add to this loop and its parents.
  /// \param LI LoopInfo used to update the block-to-loop mapping.
  void addBasicBlockToLoop(BlockT *NewBB, LoopInfoBase<BlockT, LoopT> &LI);

  /// Replace child loop \p OldChild with \p NewChild in this loop's children.
  ///
  /// Used when splitting loops up. Updates the parent pointer of OldChild to
  /// null and of NewChild to this loop, and updates the loop depth of the new
  /// child.
  ///
  /// \param OldChild Existing child loop to replace.
  /// \param NewChild Loop that takes its place as a child of this loop.
  void replaceChildLoopWith(LoopT *OldChild, LoopT *NewChild);

  /// Add \p NewChild as a child of this loop.
  ///
  /// This updates the loop depth of the new child.
  ///
  /// \param NewChild Loop with no current parent to add as a subloop.
  void addChildLoop(LoopT *NewChild) {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(!NewChild->ParentLoop && "NewChild already has a parent!");
    NewChild->ParentLoop = static_cast<LoopT *>(this);
    SubLoops.push_back(NewChild);
  }

  /// Remove the child at iterator \p I from this loop's subloops.
  ///
  /// The loop is not deleted, as it will presumably be inserted into another
  /// loop.
  ///
  /// \param I Iterator to the child loop to remove.
  /// \returns The removed child loop.
  LoopT *removeChildLoop(iterator I) {
    assert(!isInvalid() && "Loop not in a valid state!");
    assert(I != SubLoops.end() && "Cannot remove end iterator!");
    LoopT *Child = *I;
    assert(Child->ParentLoop == this && "Child is not a child of this loop!");
    SubLoops.erase(SubLoops.begin() + (I - begin()));
    Child->ParentLoop = nullptr;
    return Child;
  }

  /// Remove \p Child from being a subloop of this loop.
  ///
  /// The loop is not deleted, as it will presumably be inserted into another
  /// loop.
  ///
  /// \param Child Child loop to remove.
  /// \returns The removed child loop.
  LoopT *removeChildLoop(LoopT *Child) {
    return removeChildLoop(llvm::find(*this, Child));
  }

  /// Add \p BB directly to this loop's basic block list.
  ///
  /// This should only be used by transformations that create new loops. Other
  /// transformations should use addBasicBlockToLoop.
  ///
  /// \param BB Basic block to append to the block list.
  void addBlockEntry(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    // A borrowed slice or a full private allocation grows into fresh private
    // storage before appending.
    if (BlockCapacity == BorrowedCapacity || BlockLen == BlockCapacity)
      LI->reallocBlocks(*static_cast<LoopT *>(this),
                        std::max(2 * BlockLen, 4u));
    BlockData[BlockLen++] = BB;
  }

  /// Reserve capacity for at least \p Size blocks in this loop's block list.
  ///
  /// \param Size Minimum number of block slots to reserve.
  void reserveBlocks(unsigned Size) {
    assert(!isInvalid() && "Loop not in a valid state!");
    if (BlockCapacity < Size)
      LI->reallocBlocks(*static_cast<LoopT *>(this), Size);
  }

  /// Move \p BB to be the header of this loop.
  ///
  /// \p BB must already be part of this loop; after the call it dominates all
  /// other blocks in the loop's block list.
  ///
  /// \param BB Block that becomes the new loop header.
  void moveToHeader(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    if (BlockData[0] == BB)
      return;
    LI->materializeBlocks(*static_cast<LoopT *>(this));
    for (unsigned i = 0;; ++i) {
      assert(i != BlockLen && "Loop does not contain BB!");
      if (BlockData[i] == BB) {
        BlockData[i] = BlockData[0];
        BlockData[0] = BB;
        return;
      }
    }
  }

  /// Remove \p BB from this loop's block list.
  ///
  /// This does not update the mapping in the LoopInfo class.
  ///
  /// \param BB Basic block to remove from the block list.
  void removeBlockFromLoop(BlockT *BB) {
    assert(!isInvalid() && "Loop not in a valid state!");
    LI->materializeBlocks(*static_cast<LoopT *>(this));
    MutableArrayRef<BlockT *> Blocks(BlockData, BlockLen);
    auto *I = llvm::find(Blocks, BB);
    assert(I != Blocks.end() && "N is not in this list!");
    std::move(I + 1, Blocks.end(), I);
    --BlockLen;
  }

  /// Verify loop structure
  void verifyLoop() const;

  /// Verify loop structure of this loop and all nested loops.
  ///
  /// \param Loops Optional set that accumulates every verified loop pointer.
  void verifyLoopNest(DenseSet<const LoopT *> *Loops) const;

  /// Returns true if the loop is annotated parallel.
  ///
  /// Derived classes can override this method using static template
  /// polymorphism.
  ///
  /// \returns True if the loop is annotated parallel.
  bool isAnnotatedParallel() const { return false; }

  /// Print this loop and optionally its nested loops.
  ///
  /// \param OS Output stream.
  /// \param Verbose If true, print additional details for each block.
  /// \param PrintNested If true, recursively print child loops.
  /// \param Depth Indentation depth used when printing nested structure.
  void print(raw_ostream &OS, bool Verbose = false, bool PrintNested = true,
             unsigned Depth = 0) const;

protected:
  friend class LoopInfoBase<BlockT, LoopT>;

  /// This creates an empty loop.
  LoopBase() : ParentLoop(nullptr) {}

  // Since loop passes like SCEV are allowed to key analysis results off of
  // `Loop` pointers, we cannot re-use pointers within a loop pass manager.
  // This means loop passes should not be `delete` ing `Loop` objects directly
  // (and risk a later `Loop` allocation re-using the address of a previous one)
  // but should be using LoopInfo::markAsRemoved, which keeps around the `Loop`
  // pointer till the end of the lifetime of the `LoopInfo` object.
  //
  // To make it easier to follow this rule, we mark the destructor as
  // non-public.
  /// Destroy this loop and its subloops without reclaiming allocator storage.
  ~LoopBase() {
    for (auto *SubLoop : SubLoops)
      SubLoop->~LoopT();

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    IsInvalid = true;
#endif
    SubLoops.clear();
    // The block storage is reclaimed by the owning LoopInfo.
    BlockData = nullptr;
    BlockLen = 0;
    BlockCapacity = 0;
    ParentLoop = nullptr;
  }
};

/// Print \p Loop to stream \p OS.
///
/// \param OS Output stream.
/// \param Loop Loop to print.
/// \returns The output stream \p OS.
template <class BlockT, class LoopT>
raw_ostream &operator<<(raw_ostream &OS, const LoopBase<BlockT, LoopT> &Loop) {
  Loop.print(OS);
  return OS;
}

//===----------------------------------------------------------------------===//
/// Analysis that builds and owns the loop forest for a function.
///
/// This class builds and contains all of the top-level loop structures in the
/// specified function.
///
/// \tparam BlockT Graph node type (typically a basic-block type).
/// \tparam LoopT Loop type representing loops in the forest.
template <class BlockT, class LoopT> class LoopInfoBase {
  static_assert(GraphHasNodeNumbers<const BlockT *>,
                "LoopInfo requires GraphTraits<BlockT *>::getNumber (see "
                "GraphHasNodeNumbers)");

  // Mapping of each block, indexed by its number, to the innermost loop it
  // occurs in (or null).
  SmallVector<LoopT *> BBMap;

  using ParentT = decltype(std::declval<BlockT *>()->getParent());
  ParentT ParentPtr = nullptr;
  unsigned BlockNumberEpoch;

  std::vector<LoopT *> TopLevelLoops;

  // Shared reverse postorder layout of the in-loop blocks. Each initial loop is
  // a slice of this array, subloop slices nested inside their parent's.
  std::unique_ptr<BlockT *[]> BlockLayout;

  BumpPtrAllocator LoopAllocator;

  friend class LoopBase<BlockT, LoopT>;
  friend class LoopInfo;

  void operator=(const LoopInfoBase &) = delete;
  LoopInfoBase(const LoopInfoBase &) = delete;

public:
  /// Construct an empty LoopInfo with no loops.
  LoopInfoBase() = default;
  /// Destroy all owned loops and release related memory.
  ~LoopInfoBase() { releaseMemory(); }

  /// Move-construct from \p Arg, taking ownership of its loops.
  ///
  /// \param Arg LoopInfo to move from; left empty.
  LoopInfoBase(LoopInfoBase &&Arg)
      : BBMap(std::move(Arg.BBMap)),
        TopLevelLoops(std::move(Arg.TopLevelLoops)),
        BlockLayout(std::move(Arg.BlockLayout)),
        LoopAllocator(std::move(Arg.LoopAllocator)) {
    ParentPtr = Arg.ParentPtr;
    BlockNumberEpoch = Arg.BlockNumberEpoch;
    resetLoopInfoOwners();
    // We have to clear the arguments top level loops as we've taken ownership.
    Arg.TopLevelLoops.clear();
  }
  /// Move-assign from \p RHS, taking ownership of its loops.
  ///
  /// \param RHS LoopInfo to move from; left empty.
  /// \returns A reference to this LoopInfo.
  LoopInfoBase &operator=(LoopInfoBase &&RHS) {
    BBMap = std::move(RHS.BBMap);
    ParentPtr = RHS.ParentPtr;
    BlockNumberEpoch = RHS.BlockNumberEpoch;

    for (auto *L : TopLevelLoops)
      L->~LoopT();

    TopLevelLoops = std::move(RHS.TopLevelLoops);
    BlockLayout = std::move(RHS.BlockLayout);
    LoopAllocator = std::move(RHS.LoopAllocator);
    resetLoopInfoOwners();
    RHS.TopLevelLoops.clear();
    return *this;
  }

  /// Destroy all loops and clear block maps and layout storage.
  void releaseMemory() {
    BBMap.clear();

    for (auto *L : TopLevelLoops)
      L->~LoopT();
    TopLevelLoops.clear();
    BlockLayout.reset();
    LoopAllocator.Reset();
  }

  /// Allocate and construct a new empty loop owned by this LoopInfo.
  ///
  /// \returns A newly allocated empty loop owned by this LoopInfo.
  LoopT *AllocateLoop() {
    LoopT *Storage = LoopAllocator.Allocate<LoopT>();
    LoopT *L = new (Storage) LoopT();
    L->LI = this;
    return L;
  }

  /// Iterator over the top-level loops in the current function.
  using iterator = typename std::vector<LoopT *>::const_iterator;
  /// Reverse iterator over the top-level loops in the current function.
  using reverse_iterator =
      typename std::vector<LoopT *>::const_reverse_iterator;
  /// Return an iterator to the first top-level loop.
  ///
  /// \returns An iterator to the first top-level loop.
  iterator begin() const { return TopLevelLoops.begin(); }
  /// Return an iterator past the last top-level loop.
  ///
  /// \returns An iterator past the last top-level loop.
  iterator end() const { return TopLevelLoops.end(); }
  /// Return a reverse iterator to the last top-level loop.
  ///
  /// \returns A reverse iterator to the last top-level loop.
  reverse_iterator rbegin() const { return TopLevelLoops.rbegin(); }
  /// Return a reverse iterator past the first top-level loop.
  ///
  /// \returns A reverse iterator past the first top-level loop.
  reverse_iterator rend() const { return TopLevelLoops.rend(); }
  /// Return true if there are no top-level loops.
  ///
  /// \returns True if there are no top-level loops.
  bool empty() const { return TopLevelLoops.empty(); }

  /// Return all of the loops in the function in preorder across the loop
  /// nests, with siblings in forward program order.
  ///
  /// Note that because loops form a forest of trees, preorder is equivalent to
  /// reverse postorder.
  ///
  /// \returns All loops in preorder with siblings in forward program order.
  SmallVector<LoopT *, 4> getLoopsInPreorder() const;

  /// Return all of the loops in the function in preorder across the loop
  /// nests, with siblings in *reverse* program order.
  ///
  /// Note that because loops form a forest of trees, preorder is equivalent to
  /// reverse postorder.
  ///
  /// Also note that this is *not* a reverse preorder. Only the siblings are in
  /// reverse program order.
  ///
  /// \returns All loops in preorder with siblings in reverse program order.
  SmallVector<LoopT *, 4> getLoopsInReverseSiblingPreorder() const;

private:
  // Point every loop's owning-LoopInfo back-pointer at this object. Called
  // after a move.
  void resetLoopInfoOwners() {
    SmallVector<LoopT *, 8> Worklist(TopLevelLoops.begin(),
                                     TopLevelLoops.end());
    while (!Worklist.empty()) {
      LoopT *L = Worklist.pop_back_val();
      L->LI = this;
      Worklist.append(L->begin(), L->end());
    }
  }

  /// Verify that used block numbers are still valid.
  void
  verifyBlockNumberEpoch(const std::remove_pointer_t<ParentT> *BBParent) const {
    assert(ParentPtr == BBParent &&
           "loop info queried with block of other function");
    assert(BlockNumberEpoch ==
               GraphTraits<ParentT>::getNumberEpoch(ParentPtr) &&
           "loop info used with outdated block numbers");
  }

  // Look up BB's innermost loop in the block-to-loop map; BB must belong to
  // this function.
  LoopT *lookupLoopFor(const BlockT *BB) const {
    unsigned Number = GraphTraits<const BlockT *>::getNumber(BB);
    return Number < BBMap.size() ? BBMap[Number] : nullptr;
  }

  /// AllocateLoop for analyze(): stash \p Header (see pendingHeader).
  /// getHeader() only works once the layout carve has replaced the stash with
  /// the loop's block list.
  LoopT *allocateLoop(BlockT *Header) {
    LoopT *L = AllocateLoop();
    L->PendingHeader = Header;
    return L;
  }

  /// The header of a loop under construction, stashed until the layout carve
  /// builds the block list.
  static BlockT *pendingHeader(const LoopT *L) { return L->PendingHeader; }

  /// True if \p L borrows its block list from BlockLayout.
  static bool hasBorrowedBlocks(const LoopT &L) {
    return L.BlockCapacity == LoopT::BorrowedCapacity;
  }

  /// Replace \p L's block list with a private allocation of NewCapacity
  /// slots. The old storage is abandoned in place so slices sharing it stay
  /// intact; it is reclaimed when this LoopInfo is cleared.
  void reallocBlocks(LoopT &L, unsigned NewCapacity) {
    assert(NewCapacity >= L.BlockLen && "capacity below size");
    BlockT **New = LoopAllocator.Allocate<BlockT *>(NewCapacity);
    llvm::copy(L.getBlocks(), New);
    L.BlockData = New;
    L.BlockCapacity = NewCapacity;
  }

  /// Copy \p L's borrowed block list into private storage before a mutation.
  void materializeBlocks(LoopT &L) {
    if (hasBorrowedBlocks(L))
      reallocBlocks(L, L.BlockLen);
  }

public:
  /// Return the innermost loop that \p BB lives in.
  ///
  /// If a basic block is in no loop (for example the entry node), null is
  /// returned.
  ///
  /// \param BB Basic block whose innermost loop is requested.
  /// \returns The innermost loop containing \p BB, or null.
  LoopT *getLoopFor(const BlockT *BB) const {
    verifyBlockNumberEpoch(BB->getParent());
    return lookupLoopFor(BB);
  }

  /// Same as getLoopFor.
  ///
  /// \param BB Basic block whose innermost loop is requested.
  /// \returns The innermost loop containing \p BB, or null.
  const LoopT *operator[](const BlockT *BB) const { return getLoopFor(BB); }

  /// Return the loop nesting level of the specified block.
  ///
  /// A depth of 0 means the block is not inside any loop.
  ///
  /// \param BB Basic block whose loop depth is requested.
  /// \returns The loop nesting depth of \p BB, or 0 if not in a loop.
  unsigned getLoopDepth(const BlockT *BB) const {
    const LoopT *L = getLoopFor(BB);
    return L ? L->getLoopDepth() : 0;
  }

  /// Edge type.
  using Edge = std::pair<BlockT *, BlockT *>;

  /// Return true if \p L does not have any exit blocks.
  ///
  /// \param L Loop to query.
  /// \returns True if \p L has no exit blocks.
  bool hasNoExitBlocks(const LoopT &L) const;

  /// Return all pairs of (_inside_block_,_outside_block_).
  ///
  /// \param L Loop whose exit edges are collected.
  /// \param ExitEdges Output vector filled with exiting edges.
  void getExitEdges(const LoopT &L, SmallVectorImpl<Edge> &ExitEdges) const;

  /// Return the unique exit block for the latch of \p L, or null.
  ///
  /// Returns null if there are multiple different exit blocks or the latch is
  /// not exiting.
  ///
  /// \param L Loop whose latch exit is queried.
  /// \returns The unique latch exit block, or null.
  BlockT *getUniqueLatchExitBlock(const LoopT &L) const;

  /// Remove blocks satisfying \p Pred from \p L's block list.
  ///
  /// Preserves the order of the remaining blocks. Only \p L itself is updated,
  /// not its ancestors or descendants, and not the block-to-loop mapping.
  ///
  /// \param L Loop whose block list is filtered.
  /// \param Pred Predicate that returns true for blocks to remove.
  template <typename PredicateT>
  void removeBlocksIf(LoopT &L, PredicateT Pred) {
    materializeBlocks(L);
    L.BlockLen = llvm::remove_if(
                     MutableArrayRef<BlockT *>(L.BlockData, L.BlockLen), Pred) -
                 L.BlockData;
  }

  /// Remove blocks satisfying \p Pred from \p Start and its ancestors.
  ///
  /// Walks from \p Start up to but not including \p Stop. \p Stop must be null
  /// or an ancestor of \p Start; a null \p Stop walks to the top level.
  ///
  /// \param Start Innermost loop at which removal begins.
  /// \param Stop Ancestor loop at which to stop (exclusive), or null.
  /// \param Pred Predicate that returns true for blocks to remove.
  template <typename PredicateT>
  void removeBlocksFromLoopAndAncestors(LoopT *Start, LoopT *Stop,
                                        PredicateT Pred) {
    for (LoopT *Cur = Start; Cur != Stop; Cur = Cur->getParentLoop())
      removeBlocksIf(*Cur, Pred);
  }

  /// Detach and return children of \p Parent that satisfy \p Pred.
  ///
  /// If \p Parent is null, operates on the top-level loops. Clears parent
  /// pointers of the taken children. Both the remaining and the returned
  /// children keep their relative order.
  ///
  /// \param Parent Parent loop whose children are filtered, or null for top level.
  /// \param Pred Predicate that returns true for children to detach.
  /// \returns The detached child loops.
  template <typename PredicateT>
  SmallVector<LoopT *, 4> takeChildrenIf(LoopT *Parent, PredicateT Pred) {
    std::vector<LoopT *> &List = Parent ? Parent->SubLoops : TopLevelLoops;
    SmallVector<LoopT *, 4> Taken;
    llvm::erase_if(List, [&](LoopT *Child) {
      if (!Pred(Child))
        return false;
      Child->ParentLoop = nullptr;
      Taken.push_back(Child);
      return true;
    });
    return Taken;
  }

  /// Find the innermost loop containing both given loops.
  ///
  /// \param A First loop.
  /// \param B Second loop.
  /// \returns The innermost loop containing both \p A and \p B, or nullptr if
  ///          there is no such loop.
  LoopT *getSmallestCommonLoop(LoopT *A, LoopT *B) const;
  /// Find the innermost loop containing both given blocks.
  ///
  /// \param A First basic block.
  /// \param B Second basic block.
  /// \returns The innermost loop containing both \p A and \p B, or nullptr if
  ///          there is no such loop.
  LoopT *getSmallestCommonLoop(BlockT *A, BlockT *B) const;

  /// Return true if \p BB is a loop header.
  ///
  /// \param BB Basic block to test.
  /// \returns True if \p BB is a loop header.
  bool isLoopHeader(const BlockT *BB) const {
    const LoopT *L = getLoopFor(BB);
    return L && L->getHeader() == BB;
  }

  /// Return the top-level loops.
  ///
  /// \returns The top-level loops.
  const std::vector<LoopT *> &getTopLevelLoops() const { return TopLevelLoops; }

  /// Remove the specified top-level loop from this loop info object.
  ///
  /// The loop is not deleted, as it will presumably be inserted into another
  /// loop.
  ///
  /// \param I Iterator to the top-level loop to remove.
  /// \returns The removed loop.
  LoopT *removeLoop(iterator I) {
    assert(I != end() && "Cannot remove end iterator!");
    LoopT *L = *I;
    assert(L->isOutermost() && "Not a top-level loop!");
    TopLevelLoops.erase(TopLevelLoops.begin() + (I - begin()));
    return L;
  }

  /// Change the innermost loop that contains \p BB to \p L.
  ///
  /// This should be used by transformations that restructure the loop hierarchy
  /// tree.
  ///
  /// \param BB Basic block whose loop membership is updated.
  /// \param L New innermost loop for \p BB, or null if it is no longer in a loop.
  void changeLoopFor(const BlockT *BB, LoopT *L) {
    verifyBlockNumberEpoch(BB->getParent());
    unsigned Number = GraphTraits<const BlockT *>::getNumber(BB);
    if (Number >= BBMap.size()) {
      unsigned Max =
          GraphTraits<decltype(BB->getParent())>::getMaxNumber(BB->getParent());
      assert(Number < Max);
      BBMap.resize(Max);
    }
    BBMap[Number] = L;
  }

  /// Replace \p OldLoop with \p NewLoop in the top-level loops list.
  ///
  /// \param OldLoop Existing top-level loop to replace.
  /// \param NewLoop Loop that takes its place at the top level.
  void changeTopLevelLoop(LoopT *OldLoop, LoopT *NewLoop) {
    auto I = find(TopLevelLoops, OldLoop);
    assert(I != TopLevelLoops.end() && "Old loop not at top level!");
    *I = NewLoop;
    assert(!NewLoop->ParentLoop && !OldLoop->ParentLoop &&
           "Loops already embedded into a subloop!");
  }

  /// Add \p New to the collection of top-level loops.
  ///
  /// \param New Outermost loop to add.
  void addTopLevelLoop(LoopT *New) {
    assert(New->isOutermost() && "Loop already in subloop!");
    TopLevelLoops.push_back(New);
  }

  /// Completely remove \p BB from all loop data structures.
  ///
  /// Removes it from every Loop it is nested in and from the BasicBlock-to-loop
  /// mapping.
  ///
  /// \param BB Basic block to remove from loop info.
  void removeBlock(BlockT *BB) {
    verifyBlockNumberEpoch(BB->getParent());
    unsigned Number = GraphTraits<BlockT *>::getNumber(BB);
    if (Number >= BBMap.size())
      return;

    for (LoopT *L = BBMap[Number]; L; L = L->getParentLoop())
      L->removeBlockFromLoop(BB);
    BBMap[Number] = nullptr;
  }

  /// Return true if \p SubLoop is not already contained in \p ParentLoop.
  ///
  /// \param SubLoop Loop that may be nested under \p ParentLoop, or null.
  /// \param ParentLoop Candidate ancestor loop.
  /// \returns True if \p SubLoop is not already contained in \p ParentLoop.
  static bool isNotAlreadyContainedIn(const LoopT *SubLoop,
                                      const LoopT *ParentLoop) {
    if (!SubLoop)
      return true;
    if (SubLoop == ParentLoop)
      return false;
    return isNotAlreadyContainedIn(SubLoop->getParentLoop(), ParentLoop);
  }

  /// Create the loop forest for a function.
  ///
  /// A dominator tree is needed only for an irreducible CFG, where dominance
  /// reduces a loop that an edge re-enters to the natural loop of its header's
  /// backedges. Builds a dominator tree if one is needed.
  ///
  /// \param F Function (or parent graph) whose loops are computed.
  void analyze(ParentT F);
  /// Create the loop forest for a function, obtaining a dominator tree on demand.
  ///
  /// Calls \p GetDomTree only if a dominator tree is needed (irreducible CFG).
  ///
  /// \param F Function (or parent graph) whose loops are computed.
  /// \param GetDomTree Callback that returns a dominator tree when needed.
  void
  analyze(ParentT F,
          function_ref<const DominatorTreeBase<BlockT, false> &()> GetDomTree);
  /// Create the loop forest for the function described by \p DomTree.
  ///
  /// \param DomTree Dominator tree of the function to analyze.
  void analyze(const DominatorTreeBase<BlockT, false> &DomTree);

  /// Print the top-level loops and their nests to \p OS.
  ///
  /// \param OS Output stream.
  void print(raw_ostream &OS) const;

  /// Verify the loop forest and block-to-loop mapping for consistency.
  void verify() const;

  /// Destroy a loop that has been removed from the `LoopInfo` nest.
  ///
  /// This runs the destructor of the loop object making it invalid to
  /// reference afterward. The memory is retained so that the *pointer* to the
  /// loop remains valid.
  ///
  /// The caller is responsible for removing this loop from the loop nest and
  /// otherwise disconnecting it from the broader `LoopInfo` data structures.
  /// Callers that don't naturally handle this themselves should probably call
  /// `erase' instead.
  ///
  /// \param L Loop to destroy; must already be disconnected from the nest.
  void destroy(LoopT *L) {
    L->~LoopT();

    // Since LoopAllocator is a BumpPtrAllocator, this Deallocate only poisons
    // \c L, but the pointer remains valid for non-dereferencing uses.
    LoopAllocator.Deallocate(L);
  }
};

} // namespace llvm

#endif // LLVM_SUPPORT_GENERICLOOPINFO_H
