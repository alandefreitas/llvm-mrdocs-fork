//===- GenericCycleInfo.h - Info for Cycles in any IR ------*- C++ -*------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// \brief Find all cycles in a control-flow graph, including irreducible loops.
///
/// See docs/CycleTerminology.md for a formal definition of cycles.
///
/// Briefly:
/// - A cycle is a generalization of a loop which can represent
///   irreducible control flow.
/// - Cycles identified in a program are implementation defined,
///   depending on the DFS traversal chosen.
/// - Cycles are well-nested, and form a forest with a parent-child
///   relationship.
/// - In any choice of DFS, every natural loop L is represented by a
///   unique cycle C which is a superset of L.
/// - In the absence of irreducible control flow, the cycles are
///   exactly the natural loops in the program.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ADT_GENERICCYCLEINFO_H
#define LLVM_ADT_GENERICCYCLEINFO_H

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMapInfo.h"
#include "llvm/ADT/GenericSSAContext.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/Sequence.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/iterator.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
#include <memory>
#include <type_traits>

namespace llvm {

template <typename ContextT> class GenericCycleInfo;
template <typename ContextT> class GenericCycleInfoCompute;

/// Opaque handle to a cycle within a GenericCycleInfo that wraps the cycle's
/// preorder index. Handles remain valid as long as the cycle forest is not
/// recomputed; addBlockToCycle() adds a block but never adds, removes, or
/// reorders cycles, so it leaves every handle valid.
class CycleRef {
  static constexpr unsigned InvalidIndex = ~0u;
  unsigned Index = InvalidIndex;

  explicit CycleRef(unsigned Index) : Index(Index) {}
  template <typename ContextT> friend class GenericCycleInfo;
  template <typename ContextT> friend class GenericCycleInfoCompute;
  friend struct DenseMapInfo<CycleRef>;

public:
  /// Construct an invalid cycle handle.
  CycleRef() = default;
  /// Return true if this handle names a real cycle.
  bool isValid() const { return Index != InvalidIndex; }
  /// Return true if this handle names a real cycle.
  explicit operator bool() const { return isValid(); }
  /// Return true if both handles name the same cycle index.
  /// @param O Other cycle handle.
  bool operator==(CycleRef O) const { return Index == O.Index; }
  /// Return true if the handles name different cycle indices.
  /// @param O Other cycle handle.
  bool operator!=(CycleRef O) const { return Index != O.Index; }
};

template <> struct DenseMapInfo<CycleRef> {
  static unsigned getHashValue(CycleRef C) {
    return DenseMapInfo<unsigned>::getHashValue(C.Index);
  }
  static bool isEqual(CycleRef A, CycleRef B) { return A.Index == B.Index; }
};

/// \brief Cycle information for a function.
template <typename ContextT> class GenericCycleInfo {
public:
  /// Basic-block type from the SSA context.
  using BlockT = typename ContextT::BlockT;
  /// Function type from the SSA context.
  using FunctionT = typename ContextT::FunctionT;
  /// Compute helper that builds the cycle forest for a function.
  template <typename> friend class GenericCycleInfoCompute;

private:
  /// Internal, data-only storage for a cycle. Consumers name a cycle by a
  /// CycleRef handle and query it through GenericCycleInfo.
  class Cycle {
  public:
    /// The parent cycle; invalid for a top-level cycle.
    CycleRef Parent;

    /// This cycle's blocks (its own and its nested cycles') occupy the
    /// half-open range [IdxBegin, IdxEnd) of BlockLayout, nested like an Euler
    /// tour of the cycle tree, so containment is an interval test (see
    /// contains()).
    unsigned IdxBegin = 0, IdxEnd = 0;

    /// Depth of the cycle in the tree: top-level cycles are at depth 1 and each
    /// nested cycle is one deeper (getCycleDepth() returns 0 for blocks outside
    /// any cycle). Sibling cycles share a depth.
    unsigned Depth = 0;

    /// Number of cycles nested inside this one: the subtree occupies
    /// [this, this + 1 + NumDescendants) of Cycles.
    unsigned NumDescendants = 0;

    /// The entry blocks (header first) are BlockLayout[EntryBegin,
    /// EntryBegin+EntrySize). A reducible cycle has a single entry at IdxBegin.
    /// An irreducible one appends its list past the Euler tour.
    unsigned EntryBegin = 0, EntrySize = 0;

    /// Whether this cycle has a parent, i.e. is not top-level.
    bool hasParent() const { return Parent.isValid(); }
  };
  static_assert(std::is_trivially_destructible_v<Cycle>);
  using CycleT = Cycle;

  ContextT Context;
  unsigned BlockNumberEpoch;

  /// Map each basic block number to its inner-most containing cycle, or an
  /// invalid handle if none.
  SmallVector<CycleRef> BlockMap;

  /// Euler tour of the cycle forest: every cycle's blocks form a contiguous
  /// slice [IdxBegin, IdxEnd), nested inside its parent's. Entry lists for
  /// irreducible cycles are appended past the tour (see EntryBegin).
  SmallVector<BlockT *, 8> BlockLayout;

  /// All cycles in forest preorder: every cycle is immediately followed by
  /// its descendants, and skipping a top-level cycle's subtree lands on the
  /// next top-level cycle.
  std::unique_ptr<CycleT[]> Cycles;
  unsigned NumCycles = 0;

  /// getExitBlocks caches, indexed by the cycle's preorder index. Empty until
  /// the first query, then sized to NumCycles.
  mutable SmallVector<SmallVector<BlockT *, 0>, 0> ExitBlocksCaches;

  /// The preorder index of \p C, i.e. its offset in the Cycles array.
  unsigned getCycleIndex(const CycleT &C) const { return &C - Cycles.get(); }

  /// Resolve a handle to its stored cycle. The assert catches deref of an
  /// invalid handle and (partially) of a handle from another CycleInfo.
  CycleT &deref(CycleRef C) {
    assert(C.Index < NumCycles);
    return Cycles[C.Index];
  }
  const CycleT &deref(CycleRef C) const {
    assert(C.Index < NumCycles);
    return Cycles[C.Index];
  }
  /// The handle for a stored cycle.
  CycleRef ref(const CycleT &C) const { return CycleRef(getCycleIndex(C)); }

  void verifyBlockNumberEpoch(const FunctionT *Fn) const {
    assert(BlockNumberEpoch ==
               GraphTraits<const FunctionT *>::getNumberEpoch(Fn) &&
           "CycleInfo used with outdated block number epoch");
  }
  void addToBlockMap(BlockT *Block, CycleRef C);

public:
  /// Iteration over child cycles, yielding handles.
  ///
  /// The first child (if any) immediately follows this cycle in the preorder
  /// array, and each next sibling follows the previous child's subtree.
  struct const_child_iterator
      : iterator_facade_base<const_child_iterator, std::forward_iterator_tag,
                             CycleRef, std::ptrdiff_t, CycleRef, CycleRef> {
    /// Owning cycle-info instance being iterated, or null for a singular iterator.
    const GenericCycleInfo *CI = nullptr;
    /// Preorder index of the child cycle currently pointed to.
    unsigned Index = 0;

    /// Construct a singular child iterator.
    const_child_iterator() = default;
    /// Construct a child iterator at preorder index \p Index in \p CI.
    /// @param CI Cycle info whose forest is iterated.
    /// @param Index Preorder index of the child cycle.
    const_child_iterator(const GenericCycleInfo &CI, unsigned Index)
        : CI(&CI), Index(Index) {}

    /// Return a handle to the child cycle at the current preorder index.
    CycleRef operator*() const { return CycleRef(Index); }
    /// Advance to the next sibling child, skipping the current child's subtree.
    const_child_iterator &operator++() {
      Index += 1 + CI->Cycles[Index].NumDescendants;
      return *this;
    }
    /// Return true if both iterators point at the same preorder index.
    /// @param Other Iterator to compare with.
    bool operator==(const const_child_iterator &Other) const {
      return Index == Other.Index;
    }
  };

  /// Construct empty cycle info with no computed cycles.
  GenericCycleInfo() = default;
  /// Move-construct cycle info, leaving the source empty.
  GenericCycleInfo(GenericCycleInfo &&) = default;
  /// Move-assign cycle info, leaving the source empty.
  GenericCycleInfo &operator=(GenericCycleInfo &&) = default;

  /// Discard all computed cycles and block maps.
  void clear();
  /// Compute the cycle forest for function \p F.
  /// @param F Function whose CFG cycles will be analyzed.
  void compute(FunctionT &F);
  /// Update cycle membership after splitting edge \p Pred\to\p Succ with \p New.
  ///
  /// Adds \p New to the innermost cycle that contained both endpoints, if any.
  /// @param Pred Predecessor block of the original edge.
  /// @param Succ Successor block of the original edge.
  /// @param New New block inserted on the edge.
  void splitCriticalEdge(BlockT *Pred, BlockT *Succ, BlockT *New);

  /// Return the function this cycle info was computed for.
  const FunctionT *getFunction() const { return Context.getFunction(); }
  /// Return the SSA context used to interpret blocks and values.
  const ContextT &getSSAContext() const { return Context; }

  /// All cycles in forest preorder.
  auto cycles() const {
    return map_range(seq(0u, NumCycles),
                     [](unsigned I) { return CycleRef(I); });
  }

  /// \brief Find the innermost cycle containing \p Block.
  ///
  /// \returns the innermost cycle containing \p Block or an invalid handle if
  ///          it is not contained in any cycle.
  CycleRef getCycle(const BlockT *Block) const {
    verifyBlockNumberEpoch(Block->getParent());
    unsigned Number = GraphTraits<const BlockT *>::getNumber(Block);
    // A block added after compute() that no cycle contains (e.g. a critical
    // edge MachineSink split outside every cycle) has a number beyond BlockMap.
    if (Number >= BlockMap.size())
      return CycleRef();
    return BlockMap[Number];
  }

  /// Return the header (first entry) block of cycle \p C.
  /// @param C Cycle whose header is requested.
  BlockT *getHeader(CycleRef C) const {
    return BlockLayout[deref(C).EntryBegin];
  }
  /// Return true if \p C has exactly one entry (reducible).
  /// @param C Cycle to query.
  bool isReducible(CycleRef C) const { return deref(C).EntrySize == 1; }
  /// Return the parent of \p C, or an invalid handle for a top-level cycle.
  /// @param C Cycle whose parent is requested.
  CycleRef getParentCycle(CycleRef C) const { return deref(C).Parent; }
  /// Return the nesting depth of \p C (top-level cycles have depth 1).
  /// @param C Cycle whose depth is requested.
  unsigned getDepth(CycleRef C) const { return deref(C).Depth; }
  /// Return how many blocks (including nested cycles') belong to \p C.
  /// @param C Cycle whose block count is requested.
  size_t getNumBlocks(CycleRef C) const {
    const CycleT &Cyc = deref(C);
    return Cyc.IdxEnd - Cyc.IdxBegin;
  }

  /// Return the entry blocks of \p C (header first).
  /// @param C Cycle whose entries are requested.
  ArrayRef<BlockT *> getEntries(CycleRef C) const {
    const CycleT &Cyc = deref(C);
    return ArrayRef(BlockLayout).slice(Cyc.EntryBegin, Cyc.EntrySize);
  }
  /// Return true if \p Block is an entry of cycle \p C.
  /// @param C Cycle to query.
  /// @param Block Candidate entry block.
  bool isEntry(CycleRef C, const BlockT *Block) const {
    return is_contained(getEntries(C), Block);
  }
  /// Record \p Block as the sole entry of reducible cycle \p C.
  /// Appends a one-element entry list past the Euler tour; storing Block at
  /// IdxBegin instead would disturb the block order.
  /// @param C Cycle to update.
  /// @param Block New single entry block.
  void setSingleEntry(CycleRef C, BlockT *Block) {
    CycleT &Cyc = deref(C);
    Cyc.EntryBegin = BlockLayout.size();
    BlockLayout.push_back(Block);
    Cyc.EntrySize = 1;
  }
  /// Returns true iff \p Outer contains \p Inner. O(1). Non-strict.
  bool contains(CycleRef Outer, CycleRef Inner) const {
    const CycleT &O = deref(Outer);
    const CycleT &I = deref(Inner);
    return O.IdxBegin <= I.IdxBegin && I.IdxEnd <= O.IdxEnd;
  }
  /// Return a range over the immediate child cycles of \p C.
  /// @param C Parent cycle whose children are requested.
  iterator_range<const_child_iterator> children(CycleRef C) const {
    unsigned First = C.Index + 1;
    return llvm::make_range(
        const_child_iterator(*this, First),
        const_child_iterator(*this, First + deref(C).NumDescendants));
  }
  /// Return a printable view of the entry blocks of cycle \p C.
  /// @param C Cycle whose entries are printed.
  /// @param Ctx SSA context used to print blocks.
  Printable printEntries(CycleRef C, const ContextT &Ctx) const {
    return Printable([this, C, &Ctx](raw_ostream &Out) {
      ListSeparator LS(" ");
      for (auto *Entry : getEntries(C))
        Out << LS << Ctx.print(Entry);
    });
  }

  /// \brief Return whether \p Block is contained in \p C. O(1).
  bool contains(CycleRef C, const BlockT *Block) const {
    CycleRef Inner = getCycle(Block);
    return Inner.isValid() && contains(C, Inner);
  }

  /// \brief Return the blocks of \p C, including those of nested cycles.
  ArrayRef<BlockT *> getBlocks(CycleRef C) const {
    const CycleT &Cyc = deref(C);
    return ArrayRef<BlockT *>(BlockLayout.begin() + Cyc.IdxBegin,
                              BlockLayout.begin() + Cyc.IdxEnd);
  }

  /// Return the innermost cycle containing both \p A and \p B, if any.
  /// @param A First cycle handle.
  /// @param B Second cycle handle.
  CycleRef getSmallestCommonCycle(CycleRef A, CycleRef B) const;
  /// Return the innermost cycle containing both blocks \p A and \p B, if any.
  /// @param A First basic block.
  /// @param B Second basic block.
  CycleRef getSmallestCommonCycle(BlockT *A, BlockT *B) const;

  /// \brief Return the depth of the innermost cycle containing \p Block, or 0
  /// if it is not contained in any cycle.
  unsigned getCycleDepth(const BlockT *Block) const {
    CycleRef C = getCycle(Block);
    return C.isValid() ? getDepth(C) : 0;
  }

  /// Return the outermost cycle containing \p Block, or invalid if none.
  /// @param Block Basic block whose top-level cycle is requested.
  CycleRef getTopLevelParentCycle(const BlockT *Block) const {
    CycleRef C = getCycle(Block);
    if (!C)
      return C;
    while (CycleRef P = getParentCycle(C))
      C = P;
    return C;
  }

  /// Return all of the successor blocks of \p C: the blocks outside of \p C
  /// which are branched to from within it.
  void getExitBlocks(CycleRef C, SmallVectorImpl<BlockT *> &TmpStorage) const;

  /// Return all blocks of \p C that have a successor outside of \p C.
  void getExitingBlocks(CycleRef C,
                        SmallVectorImpl<BlockT *> &TmpStorage) const;

  /// Return the preheader block for \p C. Pre-header is well-defined for
  /// reducible cycle in docs/LoopTerminology.md as: the only one entering
  /// block and its only edge is to the entry block. Return null for
  /// irreducible cycles.
  BlockT *getCyclePreheader(CycleRef C) const;

  /// If \p C has exactly one entry with exactly one predecessor, return it,
  /// otherwise return nullptr.
  BlockT *getCyclePredecessor(CycleRef C) const;

  /// Verify that \p C is actually a well-formed cycle in the CFG.
  void verifyCycle(CycleRef C) const;

  /// Verify the parent-child relations of \p C.
  ///
  /// Note that this does \em not check that \p C is really a cycle in the CFG.
  void verifyCycleNest(CycleRef C) const;

  /// Assumes that \p C is the innermost cycle containing \p Block.
  /// \p Block will be appended to \p C and all of its parent cycles.
  /// \p Block will be added to BlockMap with \p C.
  void addBlockToCycle(BlockT *Block, CycleRef C);

  /// Methods for debug and self-test.
  //@{
  /// Verify parent/child nesting; optionally run full CFG checks when
  /// \p VerifyFull is true.
  /// @param VerifyFull When true, also verify each cycle against the CFG.
  void verifyCycleNest(bool VerifyFull = false) const;
  /// Run full self-checks on the computed cycle forest.
  void verify() const;
  /// Print the full cycle forest to \p Out, indented by depth.
  /// @param Out Stream to write to.
  void print(raw_ostream &Out) const;
  /// Dump the cycle forest to the debug stream.
  void dump() const { print(dbgs()); }
  /// Return a printable view of cycle \p C for streaming.
  /// @param C Cycle to print.
  Printable print(CycleRef C) const;
  //@}

  /// Iteration over top-level cycles.
  //@{
  /// Iterator over top-level (depth-1) cycles.
  using const_toplevel_iterator = const_child_iterator;

  /// Return an iterator to the first top-level cycle.
  const_toplevel_iterator toplevel_begin() const {
    return const_toplevel_iterator(*this, 0);
  }
  /// Return an iterator past the last top-level cycle.
  const_toplevel_iterator toplevel_end() const {
    return const_toplevel_iterator(*this, NumCycles);
  }

  /// Return a range over all top-level (depth-1) cycles.
  iterator_range<const_toplevel_iterator> toplevel_cycles() const {
    return llvm::make_range(toplevel_begin(), toplevel_end());
  }
  //@}
};

} // namespace llvm

#endif // LLVM_ADT_GENERICCYCLEINFO_H
