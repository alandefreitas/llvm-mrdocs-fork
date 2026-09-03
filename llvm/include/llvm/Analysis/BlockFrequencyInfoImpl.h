//==- BlockFrequencyInfoImpl.h - Block Frequency Implementation --*- C++ -*-==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Shared implementation of BlockFrequency for IR and Machine Instructions.
// See the documentation below for BlockFrequencyInfoImpl for details.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_BLOCKFREQUENCYINFOIMPL_H
#define LLVM_ANALYSIS_BLOCKFREQUENCYINFOIMPL_H

#include "llvm/ADT/BitVector.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/GenericCycleInfo.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/SparseBitVector.h"
#include "llvm/ADT/Twine.h"
#include "llvm/ADT/iterator_range.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/ValueHandle.h"
#include "llvm/Support/BlockFrequency.h"
#include "llvm/Support/BranchProbability.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/DOTGraphTraits.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/Format.h"
#include "llvm/Support/ScaledNumber.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <list>
#include <optional>
#include <queue>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#define DEBUG_TYPE "block-freq"

namespace llvm {
/// When true, treat queries for unknown blocks as errors for debugging.
extern LLVM_ABI llvm::cl::opt<bool> CheckBFIUnknownBlockQueries;

/// When true, run iterative inference to refine irreducible-loop frequencies.
extern LLVM_ABI llvm::cl::opt<bool> UseIterativeBFIInference;
/// Max iterative-inference updates allowed per reachable block.
extern LLVM_ABI llvm::cl::opt<unsigned> IterativeBFIMaxIterationsPerBlock;
/// Convergence epsilon for iterative block-frequency inference.
extern LLVM_ABI llvm::cl::opt<double> IterativeBFIPrecision;

class BranchProbabilityInfo;
class CycleInfo;
class Function;
class MachineBasicBlock;
class MachineBranchProbabilityInfo;
class MachineCycleInfo;
class MachineFunction;

/// Implementation details for BlockFrequencyInfoImpl.
namespace bfi_detail {

struct IrreducibleGraph;

/// Mass of a block.
///
/// This class implements a sort of fixed-point fraction always between 0.0 and
/// 1.0.  getMass() == std::numeric_limits<uint64_t>::max() indicates a value of
/// 1.0.
///
/// Masses can be added and subtracted.  Simple saturation arithmetic is used,
/// so arithmetic operations never overflow or underflow.
///
/// Masses can be multiplied.  Multiplication treats full mass as 1.0 and uses
/// an inexpensive floating-point algorithm that's off-by-one (almost, but not
/// quite, maximum precision).
///
/// Masses can be scaled by \a BranchProbability at maximum precision.
class BlockMass {
  uint64_t Mass = 0;

public:
  /// Construct an empty mass.
  BlockMass() = default;
  /// Construct a mass with raw value \p Mass.
  /// @param Mass Raw mass in [0, UINT64_MAX], where UINT64_MAX means full.
  explicit BlockMass(uint64_t Mass) : Mass(Mass) {}

  /// Return an empty (zero) mass.
  /// @return An empty mass with value 0.
  static BlockMass getEmpty() { return BlockMass(); }

  /// Return a full (1.0) mass.
  /// @return A mass representing 1.0.
  static BlockMass getFull() {
    return BlockMass(std::numeric_limits<uint64_t>::max());
  }

  /// Return the raw underlying mass value.
  /// @return The raw mass in [0, UINT64_MAX].
  uint64_t getMass() const { return Mass; }

  /// Return true if this mass is full (1.0).
  /// @return True if this mass is full (1.0).
  bool isFull() const { return Mass == std::numeric_limits<uint64_t>::max(); }
  /// Return true if this mass is empty (0.0).
  /// @return True if this mass is empty (0.0).
  bool isEmpty() const { return !Mass; }

  /// Return true if this mass is empty.
  /// @return True if this mass is empty.
  bool operator!() const { return isEmpty(); }

  /// Add another mass.
  ///
  /// Adds another mass, saturating at \a isFull() rather than overflowing.
  /// @param X Mass to add.
  /// @return This mass after adding \p X.
  BlockMass &operator+=(BlockMass X) {
    uint64_t Sum = Mass + X.Mass;
    Mass = Sum < Mass ? std::numeric_limits<uint64_t>::max() : Sum;
    return *this;
  }

  /// Subtract another mass.
  ///
  /// Subtracts another mass, saturating at \a isEmpty() rather than
  /// undeflowing.
  /// @param X Mass to subtract.
  /// @return This mass after subtracting \p X.
  BlockMass &operator-=(BlockMass X) {
    uint64_t Diff = Mass - X.Mass;
    Mass = Diff > Mass ? 0 : Diff;
    return *this;
  }

  /// Scale this mass by branch probability \p P.
  /// @param P Probability used as a [0, 1] scale factor.
  /// @return This mass after scaling by \p P.
  BlockMass &operator*=(BranchProbability P) {
    Mass = P.scale(Mass);
    return *this;
  }

  /// Return true if this mass equals \p X.
  /// @param X Mass to compare against.
  /// @return True if this mass equals \p X.
  bool operator==(BlockMass X) const { return Mass == X.Mass; }
  /// Return true if this mass differs from \p X.
  /// @param X Mass to compare against.
  /// @return True if this mass differs from \p X.
  bool operator!=(BlockMass X) const { return Mass != X.Mass; }
  /// Return true if this mass is less than or equal to \p X.
  /// @param X Mass to compare against.
  /// @return True if this mass is less than or equal to \p X.
  bool operator<=(BlockMass X) const { return Mass <= X.Mass; }
  /// Return true if this mass is greater than or equal to \p X.
  /// @param X Mass to compare against.
  /// @return True if this mass is greater than or equal to \p X.
  bool operator>=(BlockMass X) const { return Mass >= X.Mass; }
  /// Return true if this mass is less than \p X.
  /// @param X Mass to compare against.
  /// @return True if this mass is less than \p X.
  bool operator<(BlockMass X) const { return Mass < X.Mass; }
  /// Return true if this mass is greater than \p X.
  /// @param X Mass to compare against.
  /// @return True if this mass is greater than \p X.
  bool operator>(BlockMass X) const { return Mass > X.Mass; }

  /// Convert to scaled number.
  ///
  /// Convert to \a ScaledNumber.  \a isFull() gives 1.0, while \a isEmpty()
  /// gives slightly above 0.0.
  /// @return This mass as a ScaledNumber, with full mass as 1.0.
  LLVM_ABI ScaledNumber<uint64_t> toScaled() const;

  /// Dump this mass to the debug stream.
  LLVM_ABI void dump() const;
  /// Print this mass to \p OS.
  /// @param OS Output stream to write to.
  /// @return The stream \p OS after writing.
  LLVM_ABI raw_ostream &print(raw_ostream &OS) const;
};

/// Return the saturated sum of \p L and \p R.
/// @param L Left-hand mass.
/// @param R Right-hand mass.
/// @return The saturated sum of \p L and \p R.
inline BlockMass operator+(BlockMass L, BlockMass R) {
  return BlockMass(L) += R;
}
/// Return the saturated difference of \p L and \p R.
/// @param L Left-hand mass.
/// @param R Right-hand mass.
/// @return The saturated difference of \p L and \p R.
inline BlockMass operator-(BlockMass L, BlockMass R) {
  return BlockMass(L) -= R;
}
/// Scale mass \p L by branch probability \p R.
/// @param L Mass to scale.
/// @param R Probability used as a [0, 1] scale factor.
/// @return \p L scaled by \p R.
inline BlockMass operator*(BlockMass L, BranchProbability R) {
  return BlockMass(L) *= R;
}
/// Scale mass \p R by branch probability \p L.
/// @param L Probability used as a [0, 1] scale factor.
/// @param R Mass to scale.
/// @return \p R scaled by \p L.
inline BlockMass operator*(BranchProbability L, BlockMass R) {
  return BlockMass(R) *= L;
}

/// Print \p X to \p OS.
/// @param OS Output stream to write to.
/// @param X Mass to print.
/// @return The stream \p OS after writing.
inline raw_ostream &operator<<(raw_ostream &OS, BlockMass X) {
  return X.print(OS);
}

} // end namespace bfi_detail

/// Base class for BlockFrequencyInfoImpl
///
/// BlockFrequencyInfoImplBase has supporting data structures and some
/// algorithms for BlockFrequencyInfoImplBase.  Only algorithms that depend on
/// the block type (or that call such algorithms) are skipped here.
///
/// Nevertheless, the majority of the overall algorithm documentation lives with
/// BlockFrequencyInfoImpl.  See there for details.
class LLVM_ABI BlockFrequencyInfoImplBase {
public:
  /// 64-bit scaled floating-point frequency.
  using Scaled64 = ScaledNumber<uint64_t>;
  /// Fixed-point probability mass in [0, 1].
  using BlockMass = bfi_detail::BlockMass;

  /// Representative of a block.
  ///
  /// This is a simple wrapper around an index into the reverse-post-order
  /// traversal of the blocks.
  ///
  /// Unlike a block pointer, its order has meaning (location in the
  /// topological sort) and it's class is the same regardless of block type.
  struct BlockNode {
    /// Unsigned index type into the reverse-post-order list.
    using IndexType = uint32_t;

    /// Reverse-post-order index of the block, or max() if invalid.
    IndexType Index;

    /// Construct an invalid block node.
    BlockNode() : Index(std::numeric_limits<uint32_t>::max()) {}
    /// Construct a node for reverse-post-order index \p Index.
    /// @param Index Reverse-post-order index of the block.
    BlockNode(IndexType Index) : Index(Index) {}

    /// Return true if this node equals \p X.
    /// @param X Node to compare against.
    /// @return True if this node equals \p X.
    bool operator==(const BlockNode &X) const { return Index == X.Index; }
    /// Return true if this node differs from \p X.
    /// @param X Node to compare against.
    /// @return True if this node differs from \p X.
    bool operator!=(const BlockNode &X) const { return Index != X.Index; }
    /// Return true if this node's index is less than or equal to \p X.
    /// @param X Node to compare against.
    /// @return True if this node's index is less than or equal to \p X.
    bool operator<=(const BlockNode &X) const { return Index <= X.Index; }
    /// Return true if this node's index is greater than or equal to \p X.
    /// @param X Node to compare against.
    /// @return True if this node's index is greater than or equal to \p X.
    bool operator>=(const BlockNode &X) const { return Index >= X.Index; }
    /// Return true if this node's index is less than \p X.
    /// @param X Node to compare against.
    /// @return True if this node's index is less than \p X.
    bool operator<(const BlockNode &X) const { return Index < X.Index; }
    /// Return true if this node's index is greater than \p X.
    /// @param X Node to compare against.
    /// @return True if this node's index is greater than \p X.
    bool operator>(const BlockNode &X) const { return Index > X.Index; }

    /// Return true if this node refers to a real block index.
    /// @return True if this node refers to a real block index.
    bool isValid() const { return Index <= getMaxIndex(); }

    /// Return the largest valid reverse-post-order index.
    /// @return The largest valid reverse-post-order index.
    static size_t getMaxIndex() {
       return std::numeric_limits<uint32_t>::max() - 1;
    }
  };

  /// Stats about a block itself.
  struct FrequencyData {
    /// Floating-point frequency before final quantization.
    Scaled64 Scaled;
    /// Integer frequency after finalizeMetrics().
    uint64_t Integer;
  };

  /// Data about a loop.
  ///
  /// Contains the data necessary to represent a loop as a pseudo-node once it's
  /// packaged.
  struct LoopData {
    /// Map from exit successor to accumulated exit mass.
    using ExitMap = SmallVector<std::pair<BlockNode, BlockMass>, 4>;
    /// Header followed by immediate loop members in reverse post-order.
    using NodeList = SmallVector<BlockNode, 4>;

    LoopData *Parent;        ///< The parent loop.
    bool IsPackaged = false; ///< Whether this has been packaged.
    /// Whether this loop contains an irreducible SCC among its own nodes;
    /// sub-loops package theirs first.
    bool ContainsIrreducible = false;
    /// Whether this is a multi-entry irreducible SCC rather than a natural loop.
    bool IsIrreducible = false;
    ExitMap Exits;          ///< Successor edges (and weights).
    NodeList Nodes;         ///< Header and the members of the loop.
    BlockMass BackedgeMass; ///< Mass that circulates, not exits.
    /// Mass assigned to this loop package.
    BlockMass Mass;
    /// Loop scale (iterations) derived from backedge mass.
    Scaled64 Scale;

    /// Construct a natural loop headed by \p Header under \p Parent.
    /// @param Parent Enclosing loop, or null for a top-level loop.
    /// @param Header Header block of the natural loop.
    LoopData(LoopData *Parent, const BlockNode &Header)
        : Parent(Parent), Nodes(1, Header) {}

    /// Construct a package for an irreducible SCC.
    ///
    /// An irreducible SCC. Its entries are equivalent as far as the enclosing
    /// region is concerned, so the lowest-RPO member stands for the package
    /// and solveIrreducibleMass distributes mass among them all.
    /// @param Parent Enclosing loop, or null for a top-level region.
    /// @param Members SCC members; the lowest-RPO member is the package header.
    LoopData(LoopData *Parent, NodeList &&Members)
        : Parent(Parent), IsIrreducible(true), Nodes(std::move(Members)) {}

    /// Return true if \p Node is this loop's header (or package representative).
    /// @param Node Block to test.
    /// @return True if \p Node is this loop's header (or package representative).
    bool isHeader(const BlockNode &Node) const { return Node == Nodes[0]; }

    /// Return the header (or irreducible package representative).
    /// @return The header (or irreducible package representative).
    BlockNode getHeader() const { return Nodes[0]; }
    /// Return true if this represents an irreducible SCC.
    /// @return True if this represents an irreducible SCC.
    bool isIrreducible() const { return IsIrreducible; }

    /// Return an iterator to the first non-header member.
    /// @return An iterator to the first non-header member.
    NodeList::const_iterator members_begin() const { return Nodes.begin() + 1; }

    /// Return an iterator past the last member.
    /// @return An iterator past the last member.
    NodeList::const_iterator members_end() const { return Nodes.end(); }
    /// Return the range of non-header members.
    /// @return The range of non-header members.
    iterator_range<NodeList::const_iterator> members() const {
      return make_range(members_begin(), members_end());
    }
  };

  /// Index of loop information.
  struct WorkingData {
    BlockNode Node;           ///< This node.
    LoopData *Loop = nullptr; ///< The loop this block is inside.
    BlockMass Mass;           ///< Mass distribution from the entry block.

    /// Construct working data for \p Node.
    /// @param Node Block this working data describes.
    WorkingData(const BlockNode &Node) : Node(Node) {}

    /// Return true if this block heads its containing loop/package.
    /// @return True if this block heads its containing loop/package.
    bool isLoopHeader() const { return Loop && Loop->isHeader(Node); }

    /// The innermost loop containing Node that Node does not head.
    ///
    /// A block can head several nested loops: an irreducible SCC's
    /// representative may also head a sub-loop.
    /// @return The innermost enclosing loop that this node does not head, or null.
    LoopData *getContainingLoop() const {
      LoopData *L = Loop;
      while (L && L->isHeader(Node))
        L = L->Parent;
      return L;
    }

    /// Resolve a node to its representative.
    ///
    /// Get the node currently representing Node, which could be a containing
    /// loop.
    ///
    /// This function should only be called when distributing mass.  As long as
    /// there are no irreducible edges to Node, then it will have complexity
    /// O(1) in this context.
    ///
    /// In general, the complexity is O(L), where L is the number of loop
    /// headers Node has been packaged into.  Since this method is called in
    /// the context of distributing mass, L will be the number of loop headers
    /// an early exit edge jumps out of.
    /// @return The node currently representing this block (possibly a loop header).
    BlockNode getResolvedNode() const {
      auto *L = getPackagedLoop();
      return L ? L->getHeader() : Node;
    }

    /// The outermost loop containing Node that is currently packaged, if any.
    ///
    /// Packaging is transient state: this answers what represents Node at the
    /// level being processed, not where Node sits in the loop nest.
    /// @return The outermost packaged loop containing this node, or null.
    LoopData *getPackagedLoop() const {
      if (!Loop || !Loop->IsPackaged)
        return nullptr;
      auto *L = Loop;
      while (L->Parent && L->Parent->IsPackaged)
        L = L->Parent;
      return L;
    }

    /// The mass slot for Node: its own, or that of the outermost packaged
    /// loop it heads.
    /// @return A reference to this node's mass, or its packaged loop's mass.
    BlockMass &getMass() {
      BlockMass *M = &Mass;
      for (LoopData *L = Loop; L && L->IsPackaged && L->isHeader(Node);
           L = L->Parent)
        M = &L->Mass;
      return *M;
    }

    /// Has ContainingLoop been packaged up?
    /// @return True if this node's containing loop has been packaged.
    bool isPackaged() const { return getResolvedNode() != Node; }

    /// Has Loop been packaged up?
    /// @return True if this node heads a loop that has been packaged.
    bool isAPackage() const { return isLoopHeader() && Loop->IsPackaged; }
  };

  /// Unscaled probability weight.
  ///
  /// Probability weight for an edge in the graph (including the
  /// successor/target node).
  ///
  /// All edges in the original function are 32-bit.  However, exit edges from
  /// loop packages are taken from 64-bit exit masses, so we need 64-bits of
  /// space in general.
  ///
  /// In addition to the raw weight amount, Weight stores the type of the edge
  /// in the current context (i.e., the context of the loop being processed).
  /// Is this a local edge within the loop, an exit from the loop, or a
  /// backedge to the loop header?
  struct Weight {
    /// Classification of an edge relative to the loop being processed.
    enum DistType {
      /// Edge stays inside the current loop.
      Local,
      /// Edge leaves the current loop.
      Exit,
      /// Edge returns to the current loop header.
      Backedge
    };
    /// Edge kind in the current loop context.
    DistType Type = Local;
    /// Successor block this weight targets.
    BlockNode TargetNode;
    /// Unscaled probability weight of the edge.
    uint64_t Amount = 0;

    /// Construct a default local weight of zero.
    Weight() = default;
    /// Construct a weight of \p Type to \p TargetNode with \p Amount.
    /// @param Type Edge classification relative to the current loop.
    /// @param TargetNode Successor block this weight targets.
    /// @param Amount Unscaled probability weight.
    Weight(DistType Type, BlockNode TargetNode, uint64_t Amount)
        : Type(Type), TargetNode(TargetNode), Amount(Amount) {}
  };

  /// Distribution of unscaled probability weight.
  ///
  /// Distribution of unscaled probability weight to a set of successors.
  ///
  /// This class collates the successor edge weights for later processing.
  ///
  /// \a DidOverflow indicates whether \a Total did overflow while adding to
  /// the distribution.  It should never overflow twice.
  struct Distribution {
    /// List of successor edge weights.
    using WeightList = SmallVector<Weight, 4>;

    WeightList Weights;       ///< Individual successor weights.
    uint64_t Total = 0;       ///< Sum of all weights.
    bool DidOverflow = false; ///< Whether \a Total did overflow.

    /// Construct an empty distribution.
    Distribution() = default;

    /// Add a local edge to \p Node with weight \p Amount.
    /// @param Node Successor inside the current loop.
    /// @param Amount Unscaled probability weight.
    void addLocal(const BlockNode &Node, uint64_t Amount) {
      add(Node, Amount, Weight::Local);
    }

    /// Add an exit edge to \p Node with weight \p Amount.
    /// @param Node Successor outside the current loop.
    /// @param Amount Unscaled probability weight.
    void addExit(const BlockNode &Node, uint64_t Amount) {
      add(Node, Amount, Weight::Exit);
    }

    /// Add a backedge to \p Node with weight \p Amount.
    /// @param Node Loop header targeted by the backedge.
    /// @param Amount Unscaled probability weight.
    void addBackedge(const BlockNode &Node, uint64_t Amount) {
      add(Node, Amount, Weight::Backedge);
    }

    /// Normalize the distribution.
    ///
    /// Combines multiple edges to the same \a Weight::TargetNode and scales
    /// down so that \a Total fits into 32-bits.
    ///
    /// This is linear in the size of \a Weights.  For the vast majority of
    /// cases, adjacent edge weights are combined by sorting WeightList and
    /// combining adjacent weights.  However, for very large edge lists an
    /// auxiliary hash table is used.
    LLVM_ABI void normalize();

  private:
    LLVM_ABI void add(const BlockNode &Node, uint64_t Amount,
                      Weight::DistType Type);
  };

  /// Data about each block.  This is used downstream.
  std::vector<FrequencyData> Freqs;

  /// Whether each block is an irreducible loop header.
  /// This is used downstream.
  SparseBitVector<> IsIrrLoopHeader;

  /// Loop data: see initializeLoops().
  std::vector<WorkingData> Working;

  /// Indexed information about loops.
  std::list<LoopData> Loops;

  /// Has an irreducible SCC outside every loop.
  bool TopContainsIrreducible = false;

  /// Virtual destructor.
  ///
  /// Need a virtual destructor to mask the compiler warning about
  /// getBlockName().
  virtual ~BlockFrequencyInfoImplBase() = default;

  /// Add all edges out of a packaged loop to the distribution.
  ///
  /// Adds all edges from LocalLoopHead to Dist.  Calls addToDist() to add each
  /// successor edge.
  /// @param OuterLoop Enclosing loop context, or null at function scope.
  /// @param Loop Packaged loop whose exit edges are added.
  /// @param Dist Distribution receiving the successor weights.
  void addLoopSuccessorsToDist(const LoopData *OuterLoop, LoopData &Loop,
                               Distribution &Dist);

  /// Add an edge to the distribution.
  ///
  /// Adds an edge to Succ to Dist.  If \c LoopHead.isValid(), then whether the
  /// edge is local/exit/backedge is in the context of LoopHead.  Otherwise,
  /// every edge should be a local edge (since all the loops are packaged up).
  /// @param Dist Distribution receiving the edge.
  /// @param OuterLoop Enclosing loop context, or null at function scope.
  /// @param Pred Predecessor block of the edge.
  /// @param Succ Successor block of the edge.
  /// @param Weight Unscaled probability weight of the edge.
  void addToDist(Distribution &Dist, const LoopData *OuterLoop,
                 const BlockNode &Pred, const BlockNode &Succ, uint64_t Weight);

  /// Analyze irreducible SCCs.
  ///
  /// Separate irreducible SCCs from \c G, which is an explicit graph of \c
  /// OuterLoop (or the top-level function, if \c OuterLoop is \c nullptr).
  /// Insert them into \a Loops before \c Insert.
  ///
  /// \return the \c LoopData nodes representing the irreducible SCCs.
  /// @param G Explicit irreducible control-flow graph to analyze.
  /// @param OuterLoop Enclosing loop, or null for the top-level function.
  /// @param Insert Insertion point in \a Loops for new SCC packages.
  iterator_range<std::list<LoopData>::iterator>
  analyzeIrreducible(const bfi_detail::IrreducibleGraph &G, LoopData *OuterLoop,
                     std::list<LoopData>::iterator Insert);

  /// Distribute mass according to a distribution.
  ///
  /// Distributes the mass in Source according to Dist.  If LoopHead.isValid(),
  /// backedges and exits are stored in its entry in Loops.
  ///
  /// Mass is distributed in parallel from two copies of the source mass.
  /// @param Source Block whose mass is being distributed.
  /// @param OuterLoop Enclosing loop receiving exit/backedge mass, or null.
  /// @param Dist Normalized successor weight distribution.
  void distributeMass(const BlockNode &Source, LoopData *OuterLoop,
                      Distribution &Dist);

  /// Compute the loop scale for a loop.
  /// @param Loop Loop whose backedge mass determines the scale.
  void computeLoopScale(LoopData &Loop);

  /// Package up a loop.
  /// @param Loop Loop to mark packaged after mass distribution.
  void packageLoop(LoopData &Loop);

  /// Unwrap loops.
  void unwrapLoops();

  /// Finalize frequency metrics.
  ///
  /// Calculates final frequencies and cleans up no-longer-needed data
  /// structures.
  void finalizeMetrics();

  /// Clear all memory.
  void clear();

  /// Return a debug name for \p Node.
  /// @param Node Block whose name is requested.
  /// @return A debug name for \p Node.
  virtual std::string getBlockName(const BlockNode &Node) const;
  /// Return a debug name for \p Loop.
  /// @param Loop Loop whose name is requested.
  /// @return A debug name for \p Loop.
  std::string getLoopName(const LoopData &Loop) const;

  /// Print frequency metrics to \p OS.
  /// @param OS Output stream to write to.
  /// @return The stream \p OS after writing.
  virtual raw_ostream &print(raw_ostream &OS) const { return OS; }
  /// Dump frequency metrics to the debug stream.
  void dump() const { print(dbgs()); }

  /// Return the floating-point frequency of \p Node.
  /// @param Node Block whose floating frequency is requested.
  /// @return The floating-point frequency of \p Node.
  Scaled64 getFloatingBlockFreq(const BlockNode &Node) const;

  /// Return the integer frequency of \p Node.
  /// @param Node Block whose frequency is requested.
  /// @return The integer frequency of \p Node.
  BlockFrequency getBlockFreq(const BlockNode &Node) const;
  /// Return the estimated profile count of \p Node in \p F, if available.
  /// @param F Function providing the entry count scale.
  /// @param Node Block whose profile count is requested.
  /// @return The estimated profile count, or nullopt if unavailable.
  std::optional<uint64_t> getBlockProfileCount(const Function &F,
                                               const BlockNode &Node) const;
  /// Scale \p Freq by \p F's entry count into an estimated profile count.
  /// @param F Function providing the entry count scale.
  /// @param Freq Relative block frequency to convert.
  /// @return The estimated profile count, or nullopt if unavailable.
  std::optional<uint64_t> getProfileCountFromFreq(const Function &F,
                                                  BlockFrequency Freq) const;
  /// Return true if \p Node is an irreducible loop header.
  /// @param Node Block to test.
  /// @return True if \p Node is an irreducible loop header.
  bool isIrrLoopHeader(const BlockNode &Node);

  /// Set the frequency of \p Node to \p Freq.
  /// @param Node Block whose frequency is updated.
  /// @param Freq New frequency to store.
  void setBlockFreq(const BlockNode &Node, BlockFrequency Freq);

  /// Return the frequency of the function entry block.
  /// @return The frequency of the function entry block.
  BlockFrequency getEntryFreq() const {
    assert(!Freqs.empty());
    return BlockFrequency(Freqs[0].Integer);
  }
};

namespace bfi_detail {

/// Maps a block type to the related function, BPI, and cycle-info types.
template <class BlockT> struct TypeMap {};
/// TypeMap specialization for LLVM IR basic blocks.
template <> struct TypeMap<BasicBlock> {
  /// Basic block type (LLVM IR).
  using BlockT = BasicBlock;
  /// Function type owning the blocks.
  using FunctionT = Function;
  /// Branch probability analysis for IR.
  using BranchProbabilityInfoT = BranchProbabilityInfo;
  /// Cycle/loop info for IR.
  using CycleInfoT = CycleInfo;
};
/// TypeMap specialization for Machine IR basic blocks.
template <> struct TypeMap<MachineBasicBlock> {
  /// Basic block type (Machine IR).
  using BlockT = MachineBasicBlock;
  /// Function type owning the blocks.
  using FunctionT = MachineFunction;
  /// Branch probability analysis for Machine IR.
  using BranchProbabilityInfoT = MachineBranchProbabilityInfo;
  /// Cycle/loop info for Machine IR.
  using CycleInfoT = MachineCycleInfo;
};

/// Get the name of a MachineBasicBlock.
///
/// Get the name of a MachineBasicBlock.  It's templated so that including from
/// CodeGen is unnecessary (that would be a layering issue).
///
/// This is used mainly for debug output.  The name is similar to
/// MachineBasicBlock::getFullName(), but skips the name of the function.
/// @param BB Machine basic block whose name is requested.
/// @return A debug name for \p BB without the function name.
template <class BlockT> std::string getBlockName(const BlockT *BB) {
  assert(BB && "Unexpected nullptr");
  auto MachineName = "BB" + Twine(BB->getNumber());
  if (BB->getBasicBlock())
    return (MachineName + "[" + BB->getName() + "]").str();
  return MachineName.str();
}
/// Get the name of a BasicBlock.
/// @param BB Basic block whose name is requested.
/// @return The name of \p BB.
template <> inline std::string getBlockName(const BasicBlock *BB) {
  assert(BB && "Unexpected nullptr");
  return BB->getName().str();
}

/// Graph of irreducible control flow.
///
/// This graph is used for determining the SCCs in a loop (or top-level
/// function) that has irreducible control flow.
///
/// During the block frequency algorithm, the local graphs are defined in a
/// light-weight way, deferring to the \a BasicBlock or \a MachineBasicBlock
/// graphs for most edges, but getting others from \a LoopData::ExitMap.  The
/// latter only has successor information.
///
/// \a IrreducibleGraph makes this graph explicit.  It's in a form that can use
/// \a GraphTraits (so that \a analyzeIrreducible() can use \a scc_iterator),
/// and it explicitly lists predecessors and successors.  The initialization
/// that relies on \c MachineBasicBlock is defined in the header.
struct IrreducibleGraph {
  /// BlockFrequencyInfoImplBase providing Working/Loops state.
  using BFIBase = BlockFrequencyInfoImplBase;

  /// Owning analysis used to look up working data and package state.
  BFIBase &BFI;

  /// Block node type from the owning analysis.
  using BlockNode = BFIBase::BlockNode;
  /// Explicit graph node for an irreducible region.
  struct IrrNode {
    /// Block this irreducible node represents.
    BlockNode Node;
    /// Successor irreducible nodes.
    SmallVector<const IrrNode *, 4> Succs;

    /// Construct a node for \p Node with no successors yet.
    /// @param Node Block this IrrNode represents.
    IrrNode(const BlockNode &Node) : Node(Node) {}

    /// Const iterator over successor IrrNodes.
    using iterator = SmallVectorImpl<const IrrNode *>::const_iterator;

    /// Return an iterator to the first successor.
    /// @return An iterator to the first successor.
    iterator succ_begin() const { return Succs.begin(); }
    /// Return an iterator past the last successor.
    /// @return An iterator past the last successor.
    iterator succ_end() const { return Succs.end(); }
  };
  /// Entry block of the region (loop header or function entry).
  BlockNode Start;
  /// IrrNode corresponding to \a Start.
  const IrrNode *StartIrr = nullptr;
  /// All nodes in the explicit irreducible graph.
  std::vector<IrrNode> Nodes;
  /// Map from BlockNode index to IrrNode.
  SmallDenseMap<uint32_t, IrrNode *, 4> Lookup;

  /// The position of \p N in \a Nodes, for indexing side tables.
  /// @param N IrrNode whose index in \a Nodes is requested.
  /// @return The index of \p N in \a Nodes.
  unsigned getIndex(const IrrNode *N) const { return N - Nodes.data(); }

  /// Construct an explicit graph containing irreducible control flow.
  ///
  /// Construct an explicit graph of the control flow in \c OuterLoop (or the
  /// top-level function, if \c OuterLoop is \c nullptr).  Uses \c
  /// addBlockEdges to add block successors that have not been packaged into
  /// loops.
  ///
  /// \a BlockFrequencyInfoImpl::computeIrreducibleMass() is the only expected
  /// user of this.
  /// @param BFI Analysis providing Working data and packaging state.
  /// @param OuterLoop Loop whose region is modeled, or null for the function.
  /// @param addBlockEdges Callable that adds unpackaged block successor edges.
  template <class BlockEdgesAdder>
  IrreducibleGraph(BFIBase &BFI, const BFIBase::LoopData *OuterLoop,
                   BlockEdgesAdder addBlockEdges) : BFI(BFI) {
    initialize(OuterLoop, addBlockEdges);
  }

  /// Populate nodes and edges for \p OuterLoop (or the whole function).
  /// @param OuterLoop Loop whose region is modeled, or null for the function.
  /// @param addBlockEdges Callable that adds unpackaged block successor edges.
  template <class BlockEdgesAdder>
  void initialize(const BFIBase::LoopData *OuterLoop,
                  BlockEdgesAdder addBlockEdges);
  /// Add every block in \p OuterLoop as a node in this graph.
  /// @param OuterLoop Loop whose members become graph nodes.
  LLVM_ABI void addNodesInLoop(const BFIBase::LoopData &OuterLoop);
  /// Add every block in the function as a node in this graph.
  LLVM_ABI void addNodesInFunction();

  /// Append a node for \p Node; mass must still be empty.
  /// @param Node Block to add to the irreducible graph.
  void addNode(const BlockNode &Node) {
    Nodes.emplace_back(Node);
    assert(BFI.Working[Node.Index].getMass().isEmpty() &&
           "mass distributed before the region was packaged");
  }

  /// Build \a Lookup from block index to IrrNode after all nodes are added.
  LLVM_ABI void indexNodes();
  /// Add successor edges for \p Node within \p OuterLoop.
  /// @param Node Block whose edges are added.
  /// @param OuterLoop Loop context used to classify edges, or null.
  /// @param addBlockEdges Callable that adds unpackaged block successor edges.
  template <class BlockEdgesAdder>
  void addEdges(const BlockNode &Node, const BFIBase::LoopData *OuterLoop,
                BlockEdgesAdder addBlockEdges);
  /// Add an edge from \p Irr to \p Succ if \p Succ is in this graph.
  /// @param Irr Source irreducible node.
  /// @param Succ Successor block node.
  /// @param OuterLoop Loop context used to classify the edge, or null.
  LLVM_ABI void addEdge(IrrNode &Irr, const BlockNode &Succ,
                        const BFIBase::LoopData *OuterLoop);
};

template <class BlockEdgesAdder>
void IrreducibleGraph::initialize(const BFIBase::LoopData *OuterLoop,
                                  BlockEdgesAdder addBlockEdges) {
  if (OuterLoop) {
    addNodesInLoop(*OuterLoop);
    for (auto N : OuterLoop->Nodes)
      addEdges(N, OuterLoop, addBlockEdges);
  } else {
    addNodesInFunction();
    for (uint32_t Index = 0; Index < BFI.Working.size(); ++Index)
      addEdges(Index, OuterLoop, addBlockEdges);
  }
  StartIrr = Lookup[Start.Index];
}

template <class BlockEdgesAdder>
void IrreducibleGraph::addEdges(const BlockNode &Node,
                                const BFIBase::LoopData *OuterLoop,
                                BlockEdgesAdder addBlockEdges) {
  auto L = Lookup.find(Node.Index);
  if (L == Lookup.end())
    return;
  IrrNode &Irr = *L->second;
  const auto &Working = BFI.Working[Node.Index];

  if (Working.isAPackage())
    for (const auto &I : Working.Loop->Exits)
      addEdge(Irr, I.first, OuterLoop);
  else
    addBlockEdges(*this, Irr, OuterLoop);
}

} // end namespace bfi_detail

/// Shared implementation for block frequency analysis.
///
/// This is a shared implementation of BlockFrequencyInfo and
/// MachineBlockFrequencyInfo, and calculates the relative frequencies of
/// blocks.
///
/// LoopInfo defines a loop as a "non-trivial" SCC dominated by a single block,
/// which is called the header.  A given loop, L, can have sub-loops, which are
/// loops within the subgraph of L that exclude its header.  (A "trivial" SCC
/// consists of a single block that does not have a self-edge.)
///
/// In addition to loops, this algorithm has limited support for irreducible
/// SCCs, which are SCCs with multiple entry blocks.  Irreducible SCCs are
/// found from CycleInfo before any mass is distributed, and packaged like a
/// loop, with the lowest-RPO member standing for the package.  There is no
/// header to sweep from, so \a solveIrreducibleMass() distributes mass among
/// the members by power iteration instead.
///
/// This algorithm leverages BlockMass and ScaledNumber to maintain precision,
/// separates mass distribution from loop scaling, and dithers to eliminate
/// probability mass loss.
///
/// The implementation is split between BlockFrequencyInfoImpl, which knows the
/// type of graph being modelled (BasicBlock vs. MachineBasicBlock), and
/// BlockFrequencyInfoImplBase, which doesn't.  The base class uses \a
/// BlockNode, a wrapper around a uint32_t.  BlockNode is numbered from 0 in
/// reverse-post order.  This gives two advantages:  it's easy to compare the
/// relative ordering of two nodes, and maps keyed on BlockT can be represented
/// by vectors.
///
/// This algorithm is O(V+E), unless there is irreducible control flow, in
/// which case it's O(V*E) in the worst case.
///
/// These are the main stages:
///
///  0. Reverse post-order traversal (\a initializeRPOT()).
///
///     Run a single post-order traversal and save it (in reverse) in RPOT.
///     All other stages make use of this ordering.  Save a lookup from BlockT
///     to BlockNode (the index into RPOT) in Nodes.
///
///  1. Loop initialization (\a initializeLoops()).
///
///     Translate LoopInfo/MachineLoopInfo into a form suitable for the rest of
///     the algorithm.  In particular, store the immediate members of each loop
///     in reverse post-order.
///
///  2. Calculate mass and scale in loops (\a computeMassInLoops()).
///
///     For each loop (bottom-up), distribute mass through the DAG resulting
///     from ignoring backedges and treating sub-loops as a single pseudo-node.
///     Track the backedge mass distributed to the loop header, and use it to
///     calculate the loop scale (number of loop iterations).  Immediate
///     members that represent sub-loops will already have been visited and
///     packaged into a pseudo-node.
///
///     Distributing mass in a loop is a reverse-post-order traversal through
///     the loop.  Start by assigning full mass to the Loop header.  For each
///     node in the loop:
///
///         - Fetch and categorize the weight distribution for its successors.
///           If this is a packaged-subloop, the weight distribution is stored
///           in \a LoopData::Exits.  Otherwise, fetch it from
///           BranchProbabilityInfo.
///
///         - Each successor is categorized as \a Weight::Local, a local edge
///           within the current loop, \a Weight::Backedge, a backedge to the
///           loop header, or \a Weight::Exit, any successor outside the loop.
///           The weight, the successor, and its category are stored in \a
///           Distribution.  There can be multiple edges to each successor.
///           \a computeIrreducibleMass() has packaged up every irreducible SCC
///           by this point, so no backedge here targets a non-header.
///
///         - Normalize the distribution:  scale weights down so that their sum
///           is 32-bits, and coalesce multiple edges to the same node.
///
///         - Distribute the mass accordingly, dithering to minimize mass loss,
///           as described in \a distributeMass().
///
///     An irreducible SCC is not swept.  \a solveIrreducibleMass() iterates
///     the SCC's internal chain towards its dominant eigenvector and reads the
///     member masses, the exits and the circulating mass off that.
///
///     Finally, calculate the loop scale from the accumulated backedge mass.
///
///  3. Distribute mass in the function (\a computeMassInFunction()).
///
///     Finally, distribute mass through the DAG resulting from packaging all
///     loops in the function.  This uses the same algorithm as distributing
///     mass in a loop, except that there are no exit or backedge edges.
///
///  4. Unpackage loops (\a unwrapLoops()).
///
///     Initialize each block's frequency to a floating point representation of
///     its mass.
///
///     Visit loops top-down, scaling the frequencies of its immediate members
///     by the loop's pseudo-node's frequency.
///
///  5. Convert frequencies to a 64-bit range (\a finalizeMetrics()).
///
///     Using the min and max frequencies as a guide, translate floating point
///     frequencies to an appropriate range in uint64_t.
///
/// It has some known flaws.
///
///   - The model of irreducible control flow is a rough approximation.
///
///     \a solveIrreducibleMass() settles an SCC's internal chain, but the mass
///     entering each entry is unknown until the parent loop is distributed, so
///     it aims at the quasi-stationary vector rather than the true occupancy.
///     To get closer, partially compute mass in the parent loop and stop at
///     the SCC: that gives the correct ratio of entry masses to adjust their
///     relative frequencies with.  Compute mass in the SCC, then continue
///     propagation in the parent.
template <class BT> class BlockFrequencyInfoImpl : BlockFrequencyInfoImplBase {
  using BlockT = typename bfi_detail::TypeMap<BT>::BlockT;
  using FunctionT = typename bfi_detail::TypeMap<BT>::FunctionT;
  using BranchProbabilityInfoT =
      typename bfi_detail::TypeMap<BT>::BranchProbabilityInfoT;
  using CycleInfoT = typename bfi_detail::TypeMap<BT>::CycleInfoT;
  using Successor = GraphTraits<const BlockT *>;
  using Predecessor = GraphTraits<Inverse<const BlockT *>>;

  const BranchProbabilityInfoT *BPI = nullptr;
  const CycleInfoT *CI = nullptr;
  const FunctionT *F = nullptr;

  // All blocks in reverse postorder.
  std::vector<const BlockT *> RPOT;
  /// Map from block number to number on RPOT/Freqs.
  SmallVector<BlockNode, 0> Nodes;
  unsigned BlockNumberEpoch;

  BlockNode getNode(const BlockT *BB) const {
    assert(BlockNumberEpoch ==
           GraphTraits<const FunctionT *>::getNumberEpoch(F));
    unsigned BlockNumber = GraphTraits<const BlockT *>::getNumber(BB);
    return BlockNumber < Nodes.size() ? Nodes[BlockNumber] : BlockNode();
  }

  const BlockT *getBlock(const BlockNode &Node) const {
    assert(Node.Index < RPOT.size());
    return RPOT[Node.Index];
  }

  /// Save a reverse post-order traversal of all the nodes.
  void initializeRPOT();

  /// Initialize loop data.
  ///
  /// Build up \a Loops using \a LoopInfo.  \a LoopInfo gives us a mapping from
  /// each block to the deepest loop it's in, but we need the inverse.  For each
  /// loop, we store in reverse post-order its "immediate" members, defined as
  /// the header, the headers of immediate sub-loops, and all other blocks in
  /// the loop that are not in sub-loops.
  void initializeLoops();

  /// Propagate to a block's successors.
  ///
  /// In the context of distributing mass through \c OuterLoop, divide the mass
  /// currently assigned to \c Node between its successors.
  void propagateMassToSuccessors(LoopData *OuterLoop, const BlockNode &Node);

  /// Compute mass in a particular loop.
  ///
  /// Assign mass to \c Loop's header, and then for each block in \c Loop in
  /// reverse post-order, distribute mass to its successors.  Only visits nodes
  /// that have not been packaged into sub-loops.
  ///
  /// \pre \a computeMassInLoop() has been called for each subloop of \c Loop,
  /// and \a computeIrreducibleMass() for \c Loop if it contains irreducible
  /// control flow.
  void computeMassInLoop(LoopData &Loop);
  void solveIrreducibleMass(LoopData &Loop);

  /// Collect \c Node's successors, resolved through any package, with weights.
  void getSuccWeights(const BlockNode &Node,
                      SmallVectorImpl<std::pair<BlockNode, uint64_t>> &Out);

  /// Compute mass in (and package up) irreducible SCCs.
  ///
  /// Find the irreducible SCCs in \c OuterLoop, add them to \a Loops (in front
  /// of \c Insert), and call \a computeMassInLoop() on each of them.
  ///
  /// If \c OuterLoop is \c nullptr, it refers to the top-level function.
  ///
  /// \pre \a computeMassInLoop() has been called for each subloop of \c
  /// OuterLoop.
  /// \pre \c OuterLoop has irreducible SCCs.
  void computeIrreducibleMass(LoopData *OuterLoop,
                              std::list<LoopData>::iterator Insert);

  /// Compute mass in all loops.
  ///
  /// For each loop bottom-up, call \a computeMassInLoop(), packaging
  /// irreducible SCCs first via \a computeIrreducibleMass() where \a
  /// initializeLoops() found them.
  void computeMassInLoops();

  /// Compute mass in the top-level function.
  ///
  /// Package up any top-level irreducible SCCs, assign mass to the entry
  /// block, and then for each block in reverse post-order, distribute mass to
  /// its successors.  Skips nodes that have been packaged into loops.
  ///
  /// \pre \a computeMassInLoops() has been called.
  void computeMassInFunction();

  std::string getBlockName(const BlockNode &Node) const override {
    return bfi_detail::getBlockName(getBlock(Node));
  }

  /// The current implementation for computing relative block frequencies does
  /// not handle correctly control-flow graphs containing irreducible loops. To
  /// resolve the problem, we apply a post-processing step, which iteratively
  /// updates block frequencies based on the frequencies of their predesessors.
  /// This corresponds to finding the stationary point of the Markov chain by
  /// an iterative method aka "PageRank computation".
  /// The algorithm takes at most O(|E| * IterativeBFIMaxIterations) steps but
  /// typically converges faster.
  ///
  /// Decide whether we want to apply iterative inference for a given function.
  bool needIterativeInference() const;

  /// Apply an iterative post-processing to infer correct counts for irr loops.
  void applyIterativeInference();

  using ProbMatrixType = std::vector<std::vector<std::pair<size_t, Scaled64>>>;

  /// Run iterative inference for a probability matrix and initial frequencies.
  void iterativeInference(const ProbMatrixType &ProbMatrix,
                          const BitVector &Blocks,
                          std::vector<Scaled64> &Freq) const;

  /// Find all blocks to apply inference on, that is, reachable from the entry
  /// and backward reachable from exits along edges with positive probability.
  void findReachableBlocks(BitVector &Blocks) const;

  /// Build a matrix of probabilities with transitions (edges) between the
  /// blocks: ProbMatrix[I] holds pairs (J, P), where Pr[J -> I | J] = P
  void initTransitionProbabilities(const BitVector &Blocks,
                                   ProbMatrixType &ProbMatrix) const;

#ifndef NDEBUG
  /// Compute the discrepancy between current block frequencies and the
  /// probability matrix.
  Scaled64 discrepancy(const ProbMatrixType &ProbMatrix,
                       const std::vector<Scaled64> &Freq) const;
#endif

public:
  /// Construct an empty analysis; call calculate() before querying.
  BlockFrequencyInfoImpl() = default;

  /// Return the function this analysis was computed for, or null if none.
  /// @return The function this analysis was computed for, or null if none.
  const FunctionT *getFunction() const { return F; }

  /// Compute block frequencies for \p F using \p BPI and \p CI.
  /// @param F Function whose CFG is analyzed.
  /// @param BPI Branch probabilities for edges in \p F.
  /// @param CI Cycle information identifying loops and irreducible SCCs.
  void calculate(const FunctionT &F, const BranchProbabilityInfoT &BPI,
                 const CycleInfoT &CI);

  /// Inherit getEntryFreq from the base class.
  using BlockFrequencyInfoImplBase::getEntryFreq;

  /// Return the frequency of basic block \p BB.
  /// @param BB Block whose frequency is requested.
  /// @return The frequency of basic block \p BB.
  BlockFrequency getBlockFreq(const BlockT *BB) const {
    return BlockFrequencyInfoImplBase::getBlockFreq(getNode(BB));
  }

  /// Return the estimated profile count of \p BB in \p F, if available.
  /// @param F Function providing the entry count scale.
  /// @param BB Block whose profile count is requested.
  /// @return The estimated profile count, or nullopt if unavailable.
  std::optional<uint64_t> getBlockProfileCount(const Function &F,
                                               const BlockT *BB) const {
    return BlockFrequencyInfoImplBase::getBlockProfileCount(F, getNode(BB));
  }

  /// Scale \p Freq by \p F's entry count into an estimated profile count.
  /// @param F Function providing the entry count scale.
  /// @param Freq Relative block frequency to convert.
  /// @return The estimated profile count, or nullopt if unavailable.
  std::optional<uint64_t> getProfileCountFromFreq(const Function &F,
                                                  BlockFrequency Freq) const {
    return BlockFrequencyInfoImplBase::getProfileCountFromFreq(F, Freq);
  }

  /// Return true if \p BB is an irreducible loop header.
  /// @param BB Block to test.
  /// @return True if \p BB is an irreducible loop header.
  bool isIrrLoopHeader(const BlockT *BB) {
    return BlockFrequencyInfoImplBase::isIrrLoopHeader(getNode(BB));
  }

  /// Set the frequency of \p BB to \p Freq.
  /// @param BB Block whose frequency is updated.
  /// @param Freq New frequency to store.
  void setBlockFreq(const BlockT *BB, BlockFrequency Freq);

  /// Return the floating-point frequency of \p BB.
  /// @param BB Block whose floating frequency is requested.
  /// @return The floating-point frequency of \p BB.
  Scaled64 getFloatingBlockFreq(const BlockT *BB) const {
    return BlockFrequencyInfoImplBase::getFloatingBlockFreq(getNode(BB));
  }

  /// Return the branch probability info used by this analysis.
  /// @return The branch probability info used by this analysis.
  const BranchProbabilityInfoT &getBPI() const { return *BPI; }

  /// Print the frequencies for the current function.
  ///
  /// Prints the frequencies for the blocks in the current function.
  ///
  /// Blocks are printed in the natural iteration order of the function, rather
  /// than reverse post-order.  This provides two advantages:  writing -analyze
  /// tests is easier (since blocks come out in source order), and even
  /// unreachable blocks are printed.
  ///
  /// \a BlockFrequencyInfoImplBase::print() only knows reverse post-order, so
  /// we need to override it here.
  /// @param OS Output stream to write frequencies to.
  /// @return The stream \p OS after writing.
  raw_ostream &print(raw_ostream &OS) const override;

  /// Inherit dump from the base class.
  using BlockFrequencyInfoImplBase::dump;

  /// Assert that this analysis matches \p Other block-for-block.
  /// @param Other Other BlockFrequencyInfoImpl to compare against.
  void verifyMatch(BlockFrequencyInfoImpl<BT> &Other) const;
};

template <class BT>
void BlockFrequencyInfoImpl<BT>::calculate(const FunctionT &F,
                                           const BranchProbabilityInfoT &BPI,
                                           const CycleInfoT &CI) {
  // Save the parameters.
  this->BPI = &BPI;
  this->CI = &CI;
  this->F = &F;

  // Clean up left-over data structures.
  BlockFrequencyInfoImplBase::clear();
  RPOT.clear();
  Nodes.clear();

  LLVM_DEBUG(dbgs() << "\nblock-frequency: " << F.getName()
                    << "\n================="
                    << std::string(F.getName().size(), '=') << "\n");

  // Mass flows over a DAG: loops are packaged into pseudo-nodes, and backedges
  // accumulate as loop mass instead of being followed.

  // Number blocks in reverse post-order; BlockNode comparisons use it.
  initializeRPOT();
  // Group blocks into the loops BFI represents, marking irreducible regions.
  initializeLoops();

  // Deepest loop first, so each is packaged before its parent needs it.
  computeMassInLoops();
  computeMassInFunction();
  // Unpackage, scaling members by the loop's iterations and package mass.
  unwrapLoops();
  // Apply a post-processing step improving computed frequencies for functions
  // with irreducible loops.
  if (needIterativeInference())
    applyIterativeInference();
  finalizeMetrics();

  if (CheckBFIUnknownBlockQueries) {
    // To detect BFI queries for unknown blocks, add entries for unreachable
    // blocks, if any. This is to distinguish between known/existing unreachable
    // blocks and unknown blocks.
    for (const BlockT &BB : F)
      if (!getNode(&BB).isValid())
        setBlockFreq(&BB, BlockFrequency());
  }

  RPOT.clear();
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::setBlockFreq(const BlockT *BB,
                                              BlockFrequency Freq) {
  assert(BlockNumberEpoch == GraphTraits<const FunctionT *>::getNumberEpoch(F));
  unsigned BlockNumber = GraphTraits<const BlockT *>::getNumber(BB);
  if (Nodes.size() <= BlockNumber)
    Nodes.resize(GraphTraits<const FunctionT *>::getMaxNumber(F));
  BlockNode &Node = Nodes[BlockNumber];
  if (!Node.isValid()) {
    // If BB is a newly added block after BFI is done, we need to create a new
    // BlockNode for it assigned with a new index. The index can be determined
    // by the size of Freqs.
    Node = BlockNode(Freqs.size());
    Freqs.emplace_back();
  }
  BlockFrequencyInfoImplBase::setBlockFreq(Node, Freq);
}

template <class BT> void BlockFrequencyInfoImpl<BT>::initializeRPOT() {
  const BlockT *Entry = &F->front();
  RPOT.reserve(F->size());
  for (const BlockT *BB : post_order(Entry))
    RPOT.emplace_back(BB);
  std::reverse(RPOT.begin(), RPOT.end());

  assert(RPOT.size() - 1 <= BlockNode::getMaxIndex() &&
         "More nodes in function than Block Frequency Info supports");

  LLVM_DEBUG(dbgs() << "reverse-post-order-traversal\n");
  Nodes.resize(GraphTraits<const FunctionT *>::getMaxNumber(F));
  BlockNumberEpoch = GraphTraits<const FunctionT *>::getNumberEpoch(F);
  for (auto [Idx, Block] : enumerate(RPOT)) {
    BlockNode Node = BlockNode(Idx);
    LLVM_DEBUG(dbgs() << " - " << Idx << ": " << getBlockName(Node) << "\n");
    Nodes[GraphTraits<const BlockT *>::getNumber(Block)] = Node;
  }

  Working.reserve(RPOT.size());
  for (size_t Index = 0; Index < RPOT.size(); ++Index)
    Working.emplace_back(Index);
  Freqs.resize(RPOT.size());
}

template <class BT> void BlockFrequencyInfoImpl<BT>::initializeLoops() {
  LLVM_DEBUG(dbgs() << "loop-detection\n");

  LLVM_DEBUG(CI->print(dbgs()));

  // Whether \p C describes a loop for BFI. An entry of a cycle an edge
  // re-enters heads a loop the forest does not represent, because the cycle
  // absorbed it; which entry that is depends on the order the search found
  // them in. Represent none of them, so that equal entries stay equal, and
  // leave the region to the packaging computeIrreducibleMass does.
  auto hasLoop = [&](CycleRef C) {
    if (!CI->isReducible(C))
      return false;
    for (CycleRef A = CI->getParentCycle(C); A; A = CI->getParentCycle(A))
      if (!CI->isReducible(A) && CI->isEntry(A, CI->getHeader(C)))
        return false;
    return true;
  };

  // Visit loops top down and assign them an index.
  std::deque<std::pair<CycleRef, LoopData *>> Q;
  for (CycleRef C : CI->toplevel_cycles())
    Q.emplace_back(C, nullptr);
  if (Q.empty())
    return; // Early exit if there are no cycles.
  while (!Q.empty()) {
    CycleRef Cycle = Q.front().first;
    LoopData *Parent = Q.front().second;
    Q.pop_front();

    if (hasLoop(Cycle)) {
      BlockNode Header = getNode(CI->getHeader(Cycle));
      Loops.emplace_back(Parent, Header);

      Working[Header.Index].Loop = &Loops.back();
      LLVM_DEBUG(dbgs() << " - loop = " << getBlockName(Header) << "\n");
      Parent = &Loops.back();
    } else if (!CI->isReducible(Cycle)) {
      // No LoopData yet; ask computeIrreducibleMass to package the SCC
      // that contains this cycle.
      if (Parent)
        Parent->ContainsIrreducible = true;
      else
        TopContainsIrreducible = true;
    }

    for (CycleRef C : CI->children(Cycle))
      Q.emplace_back(C, Parent);
  }

  // Visit nodes in reverse post-order and add them to their deepest containing
  // loop.
  for (size_t Index = 0; Index < RPOT.size(); ++Index) {
    // Loop headers have already been mostly mapped.
    if (Working[Index].isLoopHeader()) {
      LoopData *ContainingLoop = Working[Index].getContainingLoop();
      if (ContainingLoop)
        ContainingLoop->Nodes.push_back(Index);
      continue;
    }

    CycleRef Cycle = CI->getCycle(RPOT[Index]);
    while (Cycle && !hasLoop(Cycle))
      Cycle = CI->getParentCycle(Cycle);
    if (!Cycle)
      continue;

    // Add this node to its containing loop's member list.
    BlockNode Header = getNode(CI->getHeader(Cycle));
    assert(Header.isValid());
    const auto &HeaderData = Working[Header.Index];
    assert(HeaderData.isLoopHeader());

    Working[Index].Loop = HeaderData.Loop;
    HeaderData.Loop->Nodes.push_back(Index);
    LLVM_DEBUG(dbgs() << " - loop = " << getBlockName(Header)
                      << ": member = " << getBlockName(Index) << "\n");
  }
}

template <class BT> void BlockFrequencyInfoImpl<BT>::computeMassInLoops() {
  // Visit loops with the deepest first, and the top-level loops last.
  // computeIrreducibleMass inserts each new loop immediately after *L.
  for (auto L = Loops.end(), B = Loops.begin(); L != B;) {
    --L;
    if (L->ContainsIrreducible)
      computeIrreducibleMass(&*L, std::next(L));
    computeMassInLoop(*L);
  }
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::computeMassInLoop(LoopData &Loop) {
  LLVM_DEBUG(dbgs() << "compute-mass-in-loop: " << getLoopName(Loop) << "\n");

  if (Loop.isIrreducible()) {
    LLVM_DEBUG(dbgs() << "isIrreducible = true\n");
    solveIrreducibleMass(Loop);
  } else {
    Working[Loop.getHeader().Index].getMass() = BlockMass::getFull();
    propagateMassToSuccessors(&Loop, Loop.getHeader());
    for (const BlockNode &M : Loop.members())
      propagateMassToSuccessors(&Loop, M);
  }

  computeLoopScale(Loop);
  packageLoop(Loop);
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::getSuccWeights(
    const BlockNode &Node,
    SmallVectorImpl<std::pair<BlockNode, uint64_t>> &Out) {
  Out.clear();
  if (auto *L = Working[Node.Index].getPackagedLoop()) {
    for (const auto &E : L->Exits)
      Out.emplace_back(Working[E.first.Index].getResolvedNode(),
                       E.second.getMass());
    return;
  }
  const BlockT *BB = getBlock(Node);
  for (auto It : enumerate(children<const BlockT *>(BB))) {
    BlockNode Succ = getNode(It.value());
    if (!Succ.isValid())
      continue;
    uint64_t W =
        getWeightFromBranchProb(BPI->getEdgeProbability(BB, It.index()));
    Out.emplace_back(Working[Succ.Index].getResolvedNode(),
                     std::max<uint64_t>(1, W));
  }
}

// Distribute an irreducible SCC's mass among its members, and record the
// exits and circulating mass computeLoopScale() needs. For the transition
// matrix restricted to SCC members, use power iteration to find an approximate
// solution.
template <class BT>
void BlockFrequencyInfoImpl<BT>::solveIrreducibleMass(LoopData &Loop) {
  const size_t N = Loop.Nodes.size();
  // Intra-SCC edges (src, dst) and exit edges (src, target), both in src order.
  SmallVector<std::tuple<uint32_t, uint32_t, Scaled64>> P;
  SmallVector<std::tuple<uint32_t, BlockNode, Scaled64>> Ex;
  SmallVector<std::pair<BlockNode, uint64_t>, 8> Succs;
  for (size_t I = 0; I != N; ++I) {
    getSuccWeights(Loop.Nodes[I], Succs);
    uint64_t Total = llvm::sum_of(llvm::make_second_range(Succs));
    if (!Total)
      continue;
    Scaled64 InvTotal = Scaled64::getInverse(Total);
    for (const auto &S : Succs) {
      Scaled64 Pr = Scaled64(S.second, 0) * InvTotal;
      // createIrreducibleLoop sorted Nodes, so a member's position in the
      // matrix is where it lands in that list.
      auto It = llvm::lower_bound(Loop.Nodes, S.first);
      if (It != Loop.Nodes.end() && *It == S.first)
        P.emplace_back(I, It - Loop.Nodes.begin(), Pr);
      else
        Ex.emplace_back(I, S.first, Pr);
    }
  }

  // irr_loop_header_weight is a measured block frequency, so pin the members
  // that carry one and let the rest settle around them.  Weights that are all
  // zero anchor no scale, so start from a uniform split instead.
  SmallVector<Scaled64> F(N), G(N);
  SmallVector<bool> Pinned(N, false);
  Scaled64 Sum;
  for (size_t I = 0; I != N; ++I)
    if (auto W = getBlock(Loop.Nodes[I])->getIrrLoopHeaderWeight()) {
      F[I] = Scaled64(*W, 0);
      Pinned[I] = true;
      Sum += F[I];
    }
  if (Sum.isZero()) {
    Pinned.assign(N, false);
    F.assign(N, Scaled64::getInverse(N));
    Sum = llvm::sum_of(F, Scaled64::getZero());
  }

  // A backstop, not a convergence criterion: a periodic SCC never settles.
  const unsigned MaxIterations = 16;
  // Mass leaks out of the SCC, so F decays geometrically.  Sum tracks the
  // decay; Ratio divides it out so Delta compares directions, not sizes.
  for (unsigned It = 0; It != MaxIterations; ++It) {
    G.assign(N, Scaled64::getZero());
    for (auto [I, J, Pr] : P)
      G[J] += F[I] * Pr;
    Scaled64 New;
    for (size_t I = 0; I != N; ++I) {
      if (Pinned[I])
        G[I] = F[I];
      New += G[I];
    }
    if (New.isZero())
      break; // nothing circulates; keep the uniform split
    Scaled64 Ratio = New / Sum;
    Scaled64 Delta;
    for (size_t I = 0; I != N; ++I) {
      Scaled64 Was = Ratio * F[I];
      Delta += G[I] >= Was ? G[I] - Was : Was - G[I];
      F[I] = G[I];
    }
    Sum = New;
    if (Delta < New * Scaled64(1, -32))
      break;
  }

  if (!Sum.isZero())
    for (auto &X : F)
      X = X / Sum;

  for (size_t I = 0; I != N; ++I)
    Working[Loop.Nodes[I].Index].getMass() = BlockMass(F[I].scale(UINT64_MAX));

  BlockMass TotalExit;
  for (auto [I, Succ, Pr] : Ex) {
    uint64_t M = (F[I] * Pr).scale(UINT64_MAX);
    Loop.Exits.emplace_back(Succ, BlockMass(M));
    TotalExit += BlockMass(M);
  }
  Loop.BackedgeMass = BlockMass::getFull() - TotalExit;
}

template <class BT> void BlockFrequencyInfoImpl<BT>::computeMassInFunction() {
  if (TopContainsIrreducible)
    computeIrreducibleMass(nullptr, Loops.begin());

  LLVM_DEBUG(dbgs() << "compute-mass-in-function\n");
  assert(!Working.empty() && "no blocks in function");
  assert(!Working[0].isLoopHeader() && "entry block is a loop header");

  Working[0].getMass() = BlockMass::getFull();
  for (size_t i = 0, n = RPOT.size(); i != n; ++i) {
    // Check for nodes that have been packaged.
    if (Working[i].isPackaged())
      continue;

    propagateMassToSuccessors(nullptr, BlockNode(i));
  }
}

template <class BT>
bool BlockFrequencyInfoImpl<BT>::needIterativeInference() const {
  if (!UseIterativeBFIInference)
    return false;
  if (!F->getFunction().hasProfileData())
    return false;
  // Apply iterative inference only if the function contains irreducible loops;
  // otherwise, computed block frequencies are reasonably correct.
  for (auto L = Loops.rbegin(), E = Loops.rend(); L != E; ++L) {
    if (L->isIrreducible())
      return true;
  }
  return false;
}

template <class BT> void BlockFrequencyInfoImpl<BT>::applyIterativeInference() {
  // Extract blocks for processing: a block is considered for inference iff it
  // can be reached from the entry by edges with a positive probability.
  // Non-processed blocks are assigned with the zero frequency and are ignored
  // in the computation
  BitVector ReachableBlocks;
  findReachableBlocks(ReachableBlocks);
  if (ReachableBlocks.none())
    return;

  // Extract initial frequencies for the reachable blocks
  auto Freq = std::vector<Scaled64>(ReachableBlocks.size());
  Scaled64 SumFreq;
  for (const BlockT &BB : *F) {
    unsigned Number = GraphTraits<const BlockT *>::getNumber(&BB);
    if (!ReachableBlocks[Number])
      continue;
    Freq[Number] = getFloatingBlockFreq(&BB);
    SumFreq += Freq[Number];
  }
  assert(!SumFreq.isZero() && "empty initial block frequencies");

  LLVM_DEBUG(dbgs() << "Applying iterative inference for " << F->getName()
                    << " with " << ReachableBlocks.count() << " blocks\n");

  // Normalizing frequencies so they sum up to 1.0
  for (auto &Value : Freq) {
    Value /= SumFreq;
  }

  // Setting up edge probabilities using sparse matrix representation:
  // ProbMatrix[I] holds a vector of pairs (J, P) where Pr[J -> I | J] = P
  ProbMatrixType ProbMatrix;
  initTransitionProbabilities(ReachableBlocks, ProbMatrix);

  // Run the propagation
  iterativeInference(ProbMatrix, ReachableBlocks, Freq);

  // Assign computed frequency values
  for (const BlockT &BB : *F) {
    auto Node = getNode(&BB);
    if (!Node.isValid())
      continue;
    unsigned Number = GraphTraits<const BlockT *>::getNumber(&BB);
    Freqs[Node.Index].Scaled =
        ReachableBlocks[Number] ? Freq[Number] : Scaled64::getZero();
  }
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::iterativeInference(
    const ProbMatrixType &ProbMatrix, const BitVector &Blocks,
    std::vector<Scaled64> &Freq) const {
  assert(0.0 < IterativeBFIPrecision && IterativeBFIPrecision < 1.0 &&
         "incorrectly specified precision");
  // Convert double precision to Scaled64
  const auto Precision =
      Scaled64::getInverse(static_cast<uint64_t>(1.0 / IterativeBFIPrecision));
  const size_t MaxIterations =
      IterativeBFIMaxIterationsPerBlock * Blocks.count();

#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "  Initial discrepancy = "
                    << discrepancy(ProbMatrix, Freq).toString() << "\n");
#endif

  // Successors[I] holds unique sucessors of the I-th block
  auto Successors = std::vector<std::vector<size_t>>(Freq.size());
  for (size_t I = 0; I < Freq.size(); I++) {
    for (const auto &Jump : ProbMatrix[I]) {
      Successors[Jump.first].push_back(I);
    }
  }

  // To speedup computation, we maintain a set of "active" blocks whose
  // frequencies need to be updated based on the incoming edges.
  // The set is dynamic and changes after every update. Initially all blocks
  // with a positive frequency are active
  auto IsActive = BitVector(Freq.size(), false);
  std::queue<size_t> ActiveSet;
  for (unsigned I : Blocks.set_bits()) {
    if (Freq[I] > 0) {
      ActiveSet.push(I);
      IsActive[I] = true;
    }
  }

  // Iterate over the blocks propagating frequencies
  size_t It = 0;
  while (It++ < MaxIterations && !ActiveSet.empty()) {
    size_t I = ActiveSet.front();
    ActiveSet.pop();
    IsActive[I] = false;

    // Compute a new frequency for the block: NewFreq := Freq \times ProbMatrix.
    // A special care is taken for self-edges that needs to be scaled by
    // (1.0 - SelfProb), where SelfProb is the sum of probabilities on the edges
    Scaled64 NewFreq;
    Scaled64 OneMinusSelfProb = Scaled64::getOne();
    for (const auto &Jump : ProbMatrix[I]) {
      if (Jump.first == I) {
        OneMinusSelfProb -= Jump.second;
      } else {
        NewFreq += Freq[Jump.first] * Jump.second;
      }
    }
    if (OneMinusSelfProb != Scaled64::getOne())
      NewFreq /= OneMinusSelfProb;

    // If the block's frequency has changed enough, then
    // make sure the block and its successors are in the active set
    auto Change = Freq[I] >= NewFreq ? Freq[I] - NewFreq : NewFreq - Freq[I];
    if (Change > Precision) {
      ActiveSet.push(I);
      IsActive[I] = true;
      for (size_t Succ : Successors[I]) {
        if (!IsActive[Succ]) {
          ActiveSet.push(Succ);
          IsActive[Succ] = true;
        }
      }
    }

    // Update the frequency for the block
    Freq[I] = NewFreq;
  }

  LLVM_DEBUG(dbgs() << "  Completed " << It << " inference iterations"
                    << format(" (%0.0f per block)", double(It) / Freq.size())
                    << "\n");
#ifndef NDEBUG
  LLVM_DEBUG(dbgs() << "  Final   discrepancy = "
                    << discrepancy(ProbMatrix, Freq).toString() << "\n");
#endif
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::findReachableBlocks(BitVector &Blocks) const {
  unsigned MaxNumber = GraphTraits<const FunctionT *>::getMaxNumber(F);
  auto number = [](const BlockT *BB) {
    return GraphTraits<const BlockT *>::getNumber(BB);
  };

  // Find all blocks to apply inference on, that is, reachable from the entry
  // along edges with non-zero probablities
  std::queue<const BlockT *> Queue;
  BitVector Reachable(MaxNumber);
  const BlockT *Entry = &F->front();
  Queue.push(Entry);
  Reachable.set(number(Entry));
  while (!Queue.empty()) {
    const BlockT *SrcBB = Queue.front();
    Queue.pop();
    for (auto It : enumerate(children<const BlockT *>(SrcBB))) {
      auto EP = BPI->getEdgeProbability(SrcBB, It.index());
      if (EP.isZero())
        continue;
      unsigned Number = number(It.value());
      if (!Reachable.test(Number)) {
        Reachable.set(Number);
        Queue.push(It.value());
      }
    }
  }

  // Find all blocks to apply inference on, that is, backward reachable from
  // the entry along (backward) edges with non-zero probablities
  BitVector InverseReachable(MaxNumber);
  for (const BlockT &BB : *F) {
    // An exit block is a block without any successors
    bool HasSucc = !llvm::children<const BlockT *>(&BB).empty();
    if (!HasSucc && Reachable.test(number(&BB))) {
      Queue.push(&BB);
      InverseReachable.set(number(&BB));
    }
  }
  while (!Queue.empty()) {
    const BlockT *SrcBB = Queue.front();
    Queue.pop();
    for (const BlockT *DstBB : inverse_children<const BlockT *>(SrcBB)) {
      auto EP = BPI->getEdgeProbability(DstBB, SrcBB);
      if (EP.isZero())
        continue;
      unsigned Number = number(DstBB);
      if (!InverseReachable.test(Number)) {
        InverseReachable.set(Number);
        Queue.push(DstBB);
      }
    }
  }

  // Collect the result
  Reachable &= InverseReachable;
  Blocks = std::move(Reachable);
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::initTransitionProbabilities(
    const BitVector &Blocks, ProbMatrixType &ProbMatrix) const {
  const size_t NumBlocks = Blocks.size();
  auto Succs = std::vector<std::vector<std::pair<size_t, Scaled64>>>(NumBlocks);
  auto SumProb = std::vector<Scaled64>(NumBlocks);

  // Find unique successors and corresponding probabilities for every block
  for (const BlockT &BB : *F) {
    size_t Src = GraphTraits<const BlockT *>::getNumber(&BB);
    if (!Blocks[Src])
      continue;
    SmallPtrSet<const BlockT *, 2> UniqueSuccs;
    for (auto It : enumerate(children<const BlockT *>(&BB))) {
      const BlockT *SI = It.value();
      size_t Dst = GraphTraits<const BlockT *>::getNumber(SI);
      // Ignore cold blocks
      if (!Blocks[Dst])
        continue;
      // Ignore parallel edges between BB and SI blocks
      if (!UniqueSuccs.insert(SI).second)
        continue;
      // Ignore jumps with zero probability
      auto EP = BPI->getEdgeProbability(&BB, It.index());
      if (EP.isZero())
        continue;

      auto EdgeProb =
          Scaled64::getFraction(EP.getNumerator(), EP.getDenominator());
      Succs[Src].push_back(std::make_pair(Dst, EdgeProb));
      SumProb[Src] += EdgeProb;
    }
  }

  // Add transitions for every jump with positive branch probability
  ProbMatrix = ProbMatrixType(NumBlocks);
  for (size_t Src = 0; Src < NumBlocks; Src++) {
    // Ignore blocks w/o successors
    if (Succs[Src].empty())
      continue;

    assert(!SumProb[Src].isZero() && "Zero sum probability of non-exit block");
    for (auto &Jump : Succs[Src]) {
      size_t Dst = Jump.first;
      Scaled64 Prob = Jump.second;
      ProbMatrix[Dst].push_back(std::make_pair(Src, Prob / SumProb[Src]));
    }
  }

  // Add transitions from sinks to the source
  size_t EntryIdx = GraphTraits<const BlockT *>::getNumber(&F->front());
  for (size_t Src = 0; Src < NumBlocks; Src++) {
    if (Blocks[Src] && Succs[Src].empty()) {
      ProbMatrix[EntryIdx].push_back(std::make_pair(Src, Scaled64::getOne()));
    }
  }
}

#ifndef NDEBUG
template <class BT>
BlockFrequencyInfoImplBase::Scaled64 BlockFrequencyInfoImpl<BT>::discrepancy(
    const ProbMatrixType &ProbMatrix, const std::vector<Scaled64> &Freq) const {
  size_t EntryIdx = GraphTraits<const BlockT *>::getNumber(&F->front());
  assert(Freq[EntryIdx] > 0 &&
         "Incorrectly computed frequency of the entry block");
  Scaled64 Discrepancy;
  for (size_t I = 0; I < ProbMatrix.size(); I++) {
    Scaled64 Sum;
    for (const auto &Jump : ProbMatrix[I]) {
      Sum += Freq[Jump.first] * Jump.second;
    }
    Discrepancy += Freq[I] >= Sum ? Freq[I] - Sum : Sum - Freq[I];
  }
  // Normalizing by the frequency of the entry block
  return Discrepancy / Freq[EntryIdx];
}
#endif

template <class BT>
void BlockFrequencyInfoImpl<BT>::computeIrreducibleMass(
    LoopData *OuterLoop, std::list<LoopData>::iterator Insert) {
  LLVM_DEBUG(dbgs() << "analyze-irreducible-in-";
             if (OuterLoop) dbgs()
             << "loop: " << getLoopName(*OuterLoop) << "\n";
             else dbgs() << "function\n");

  using namespace bfi_detail;

  auto addBlockEdges = [&](IrreducibleGraph &G, IrreducibleGraph::IrrNode &Irr,
                           const LoopData *OuterLoop) {
    const BlockT *BB = RPOT[Irr.Node.Index];
    for (const auto *Succ : children<const BlockT *>(BB))
      G.addEdge(Irr, getNode(Succ), OuterLoop);
  };
  IrreducibleGraph G(*this, OuterLoop, addBlockEdges);

  for (auto &L : analyzeIrreducible(G, OuterLoop, Insert))
    computeMassInLoop(L);

  if (!OuterLoop)
    return;

  // Drop the nodes the new packages absorbed.
  assert(OuterLoop->Exits.empty() && "unexpected exits before distribution");
  assert(OuterLoop->BackedgeMass.isEmpty() &&
         "unexpected backedge mass before distribution");
  auto O = OuterLoop->Nodes.begin() + 1;
  for (auto I = O, E = OuterLoop->Nodes.end(); I != E; ++I)
    if (!Working[I->Index].isPackaged())
      *O++ = *I;
  OuterLoop->Nodes.erase(O, OuterLoop->Nodes.end());
}

/// Convert a branch probability into an unscaled edge weight.
/// @param Prob Branch probability whose numerator becomes the weight.
/// @return The unscaled numerator of \p Prob.
inline uint32_t getWeightFromBranchProb(const BranchProbability Prob) {
  return Prob.getNumerator();
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::propagateMassToSuccessors(
    LoopData *OuterLoop, const BlockNode &Node) {
  LLVM_DEBUG(dbgs() << " - node: " << getBlockName(Node) << "\n");
  // Calculate probability for successors.
  Distribution Dist;
  if (auto *Loop = Working[Node.Index].getPackagedLoop()) {
    assert(Loop != OuterLoop && "Cannot propagate mass in a packaged loop");
    addLoopSuccessorsToDist(OuterLoop, *Loop, Dist);
  } else {
    const BlockT *BB = getBlock(Node);
    for (auto It : enumerate(children<const BlockT *>(BB)))
      addToDist(
          Dist, OuterLoop, Node, getNode(It.value()),
          getWeightFromBranchProb(BPI->getEdgeProbability(BB, It.index())));
  }

  // Distribute mass to successors, saving exit and backedge data in the
  // loop header.
  distributeMass(Node, OuterLoop, Dist);
}

template <class BT>
raw_ostream &BlockFrequencyInfoImpl<BT>::print(raw_ostream &OS) const {
  if (!F)
    return OS;
  OS << "block-frequency-info: " << F->getName() << "\n";
  for (const BlockT &BB : *F) {
    OS << " - " << bfi_detail::getBlockName(&BB) << ": float = ";
    getFloatingBlockFreq(&BB).print(OS, 5)
        << ", int = " << getBlockFreq(&BB).getFrequency();
    if (std::optional<uint64_t> ProfileCount =
        BlockFrequencyInfoImplBase::getBlockProfileCount(
            F->getFunction(), getNode(&BB)))
      OS << ", count = " << *ProfileCount;
    if (std::optional<uint64_t> IrrLoopHeaderWeight =
            BB.getIrrLoopHeaderWeight())
      OS << ", irr_loop_header_weight = " << *IrrLoopHeaderWeight;
    OS << "\n";
  }

  // Add an extra newline for readability.
  OS << "\n";
  return OS;
}

template <class BT>
void BlockFrequencyInfoImpl<BT>::verifyMatch(
    BlockFrequencyInfoImpl<BT> &Other) const {
  bool Match = true;
  // Gather blocks for numbers so that we can print names and determine whether
  // they still exist.
  SmallVector<const BlockT *> Blocks;
  Blocks.resize(GraphTraits<const FunctionT *>::getMaxNumber(F));
  for (const auto &BB : *F)
    Blocks[GraphTraits<const BlockT *>::getNumber(&BB)] = &BB;

  size_t MinSize = std::min(Nodes.size(), Other.Nodes.size());
  for (size_t i = 0; i < MinSize; ++i) {
    if (!Blocks[i])
      continue; // Block got deleted in the mean time, ignore.
    if (Nodes[i].isValid() != Other.Nodes[i].isValid()) {
      Match = false;
      dbgs() << "Block " << bfi_detail::getBlockName(Blocks[i])
             << " existence mismatch.\n";
    } else if (Nodes[i].isValid()) {
      const auto &Freq = Freqs[Nodes[i].Index];
      const auto &OtherFreq = Other.Freqs[Other.Nodes[i].Index];
      if (Freq.Integer != OtherFreq.Integer) {
        Match = false;
        dbgs() << "Freq mismatch: " << bfi_detail::getBlockName(Blocks[i])
               << " " << Freq.Integer << " vs " << OtherFreq.Integer << "\n";
      }
    }
  }
  // Block with higher numbers must not exist in either state.
  for (size_t i = MinSize; i < Nodes.size(); ++i) {
    if (Nodes[i].isValid()) {
      Match = false;
      dbgs() << "Block " << bfi_detail::getBlockName(Blocks[i])
             << " existence mismatch.\n";
    }
  }
  for (size_t i = MinSize; i < Other.Nodes.size(); ++i) {
    if (Other.Nodes[i].isValid()) {
      Match = false;
      dbgs() << "Block " << bfi_detail::getBlockName(Blocks[i])
             << " existence mismatch.\n";
    }
  }

  if (!Match) {
    dbgs() << "This\n";
    print(dbgs());
    dbgs() << "Other\n";
    Other.print(dbgs());
  }
  assert(Match && "BFI mismatch");
}

/// How block frequencies are rendered in DOT graph viewers.
enum GVDAGType {
  /// Do not render frequency information.
  GVDT_None,
  /// Show frequencies as fractions of the entry frequency.
  GVDT_Fraction,
  /// Show frequencies as raw integer values.
  GVDT_Integer,
  /// Show estimated profile counts.
  GVDT_Count
};

/// DOT GraphTraits helpers for block-frequency visualization.
///
/// Shared base for IR and Machine IR frequency DOT viewers. Formats node
/// labels and hot-edge/node attributes from a BlockFrequencyInfo analysis.
template <class BlockFrequencyInfoT, class BranchProbabilityInfoT>
struct BFIDOTGraphTraitsBase : public DefaultDOTGraphTraits {
  /// GraphTraits specialization for the frequency analysis.
  using GTraits = GraphTraits<BlockFrequencyInfoT *>;
  /// Reference type for a CFG node in the graph.
  using NodeRef = typename GTraits::NodeRef;
  /// Iterator over outgoing edges of a node.
  using EdgeIter = typename GTraits::ChildIteratorType;
  /// Iterator over all nodes in the graph.
  using NodeIter = typename GTraits::nodes_iterator;

  /// Highest block frequency seen while rendering, used for hot highlighting.
  uint64_t MaxFrequency = 0;

  /// Construct traits, optionally requesting the simple DOT style.
  /// @param isSimple When true, use the simplified DefaultDOTGraphTraits style.
  explicit BFIDOTGraphTraitsBase(bool isSimple = false)
      : DefaultDOTGraphTraits(isSimple) {}

  /// Return the function name used as the DOT graph title.
  /// @param G Frequency analysis whose function name is used.
  /// @return The function name used as the DOT graph title.
  static StringRef getGraphName(const BlockFrequencyInfoT *G) {
    return G->getFunction()->getName();
  }

  /// Return DOT attributes that highlight hot nodes in red.
  /// @param Node CFG node whose attributes are requested.
  /// @param Graph Frequency analysis providing block frequencies.
  /// @param HotPercentThreshold Percent of max frequency treated as hot; 0
  ///        disables highlighting.
  /// @return DOT attribute text for \p Node, or empty if not highlighted.
  std::string getNodeAttributes(NodeRef Node, const BlockFrequencyInfoT *Graph,
                                unsigned HotPercentThreshold = 0) {
    std::string Result;
    if (!HotPercentThreshold)
      return Result;

    // Compute MaxFrequency on the fly:
    if (!MaxFrequency) {
      for (NodeIter I = GTraits::nodes_begin(Graph),
                    E = GTraits::nodes_end(Graph);
           I != E; ++I) {
        NodeRef N = *I;
        MaxFrequency =
            std::max(MaxFrequency, Graph->getBlockFreq(N).getFrequency());
      }
    }
    BlockFrequency Freq = Graph->getBlockFreq(Node);
    BlockFrequency HotFreq =
        (BlockFrequency(MaxFrequency) *
         BranchProbability::getBranchProbability(HotPercentThreshold, 100));

    if (Freq < HotFreq)
      return Result;

    raw_string_ostream(Result) << "color=\"red\"";
    return Result;
  }

  /// Build the DOT label for \p Node according to \p GType.
  /// @param Node CFG node to label.
  /// @param Graph Frequency analysis providing frequencies and counts.
  /// @param GType How the frequency should be formatted in the label.
  /// @param layout_order Optional layout order appended to the name, or -1.
  /// @return The DOT label string for \p Node.
  std::string getNodeLabel(NodeRef Node, const BlockFrequencyInfoT *Graph,
                           GVDAGType GType, int layout_order = -1) {
    std::string Result;
    raw_string_ostream OS(Result);

    if (layout_order != -1)
      OS << Node->getName() << "[" << layout_order << "] : ";
    else
      OS << Node->getName() << " : ";
    switch (GType) {
    case GVDT_Fraction:
      OS << printBlockFreq(*Graph, *Node);
      break;
    case GVDT_Integer:
      OS << Graph->getBlockFreq(Node).getFrequency();
      break;
    case GVDT_Count: {
      auto Count = Graph->getBlockProfileCount(Node);
      if (Count)
        OS << *Count;
      else
        OS << "Unknown";
      break;
    }
    case GVDT_None:
      llvm_unreachable("If we are not supposed to render a graph we should "
                       "never reach this point.");
    }
    return Result;
  }

  /// Return DOT attributes for the successor edge at \p EI, including weight.
  /// @param Node Source CFG node of the edge.
  /// @param EI Iterator identifying the successor edge.
  /// @param BFI Frequency analysis used for hot-edge detection.
  /// @param BPI Branch probabilities used for the edge label; may be null.
  /// @param HotPercentThreshold Percent of max frequency treated as hot; 0
  ///        disables highlighting.
  /// @return DOT attribute text for the edge, or empty if \p BPI is null.
  std::string getEdgeAttributes(NodeRef Node, EdgeIter EI,
                                const BlockFrequencyInfoT *BFI,
                                const BranchProbabilityInfoT *BPI,
                                unsigned HotPercentThreshold = 0) {
    std::string Str;
    if (!BPI)
      return Str;

    unsigned SuccIdx = std::distance(succ_begin(Node), EI);
    BranchProbability BP = BPI->getEdgeProbability(Node, SuccIdx);
    uint32_t N = BP.getNumerator();
    uint32_t D = BP.getDenominator();
    double Percent = 100.0 * N / D;
    raw_string_ostream OS(Str);
    OS << format("label=\"%.1f%%\"", Percent);

    if (HotPercentThreshold) {
      BlockFrequency EFreq = BFI->getBlockFreq(Node) * BP;
      BlockFrequency HotFreq = BlockFrequency(MaxFrequency) *
                               BranchProbability(HotPercentThreshold, 100);

      if (EFreq >= HotFreq)
        OS << ",color=\"red\"";
    }
    return Str;
  }
};

} // end namespace llvm

#undef DEBUG_TYPE

#endif // LLVM_ANALYSIS_BLOCKFREQUENCYINFOIMPL_H
