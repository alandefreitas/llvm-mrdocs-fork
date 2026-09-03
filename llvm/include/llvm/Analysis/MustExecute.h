//===- MustExecute.h - Is an instruction known to execute--------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Contains a collection of routines for determining if a given instruction is
/// guaranteed to execute if a given point in control flow is reached. The most
/// common example is an instruction within a loop being provably executed if we
/// branch to the header of it's containing loop.
///
/// There are two interfaces available to determine if an instruction is
/// executed once a given point in the control flow is reached:
/// 1) A loop-centric one derived from LoopSafetyInfo.
/// 2) A "must be executed context"-based one implemented in the
///    MustBeExecutedContextExplorer.
/// Please refer to the class comments for more information.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MUSTEXECUTE_H
#define LLVM_ANALYSIS_MUSTEXECUTE_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/DenseSet.h"
#include "llvm/Analysis/InstructionPrecedenceTracking.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

namespace {
/// Function type that returns an analysis of type \c T for a Function.
template <typename T> using GetterTy = std::function<T *(const Function &F)>;
}

class BasicBlock;
class DominatorTree;
class Loop;
class LoopInfo;
class PostDominatorTree;
class raw_ostream;

/// Captures information about whether loop blocks may throw or exit abnormally.
///
/// It keep information for loop blocks may throw exception or otherwise
/// exit abnormally on any iteration of the loop which might actually execute
/// at runtime.  The primary way to consume this information is via
/// isGuaranteedToExecute below, but some callers bailout or fallback to
/// alternate reasoning if a loop contains any implicit control flow.
/// NOTE: LoopSafetyInfo contains cached information regarding loops and their
/// particular blocks. This information is only dropped on invocation of
/// computeLoopSafetyInfo. If the loop or any of its block is deleted, or if
/// any thrower instructions have been added or removed from them, or if the
/// control flow has changed, or in case of other meaningful modifications, the
/// LoopSafetyInfo needs to be recomputed. If a meaningful modifications to the
/// loop were made and the info wasn't recomputed properly, the behavior of all
/// methods except for computeLoopSafetyInfo is undefined.
class LoopSafetyInfo {
  // Used to update funclet bundle operands.
  DenseMap<BasicBlock *, ColorVector> BlockColors;

  // Cache whether (the start of) this block is guaranteed to execute if the
  // loop is entered.
  mutable DenseMap<const BasicBlock *, bool> GuaranteedToExecute;

protected:
  /// Computes block colors.
  /// \param CurLoop Loop whose block colors are computed.
  LLVM_ABI void computeBlockColors(const Loop *CurLoop);

public:
  /// Returns block colors map that is used to update funclet operand bundles.
  /// \returns The map from basic blocks to their funclet colors.
  LLVM_ABI const DenseMap<BasicBlock *, ColorVector> &getBlockColors() const;

  /// Copy colors of block \p Old into the block \p New.
  /// \param New Block that receives the copied colors.
  /// \param Old Block whose colors are copied.
  LLVM_ABI void copyColors(BasicBlock *New, BasicBlock *Old);

  /// Returns true iff the block \p BB potentially may throw exception. It can
  /// be false-positive in cases when we want to avoid complex analysis.
  /// \param BB Block to check for potentially throwing instructions.
  /// \returns True if \p BB may throw; may be a false positive.
  virtual bool blockMayThrow(const BasicBlock *BB) const = 0;

  /// Returns true iff any block of the loop for which this info is contains an
  /// instruction that may throw or otherwise exit abnormally.
  /// \returns True if any block in the loop may throw or exit abnormally.
  virtual bool anyBlockMayThrow() const = 0;

  /// Return true if we must reach the block \p BB under assumption that the
  /// loop \p CurLoop is entered.
  /// \param CurLoop Loop assumed to be entered.
  /// \param BB Block that must be reached on all paths through the loop.
  /// \param DT Dominator tree for the function containing \p CurLoop.
  /// \returns True if every path through \p CurLoop reaches \p BB.
  LLVM_ABI bool allLoopPathsLeadToBlock(const Loop *CurLoop,
                                        const BasicBlock *BB,
                                        const DominatorTree *DT) const;

  /// Implementation helper for \c allLoopPathsLeadToBlock.
  /// \param CurLoop Loop assumed to be entered.
  /// \param BB Block that must be reached on all paths through the loop.
  /// \param DT Dominator tree for the function containing \p CurLoop.
  /// \returns True if every path through \p CurLoop reaches \p BB.
  LLVM_ABI bool allLoopPathsLeadToBlockImpl(const Loop *CurLoop,
                                            const BasicBlock *BB,
                                            const DominatorTree *DT) const;

  /// Compute safety information for the given loop.
  ///
  /// Checks the loop body and header for the possibility of a may-throw
  /// exception. Updates safety information in this LoopSafetyInfo.
  /// Note: This is defined to clear and reinitialize an already initialized
  /// LoopSafetyInfo.  Some callers rely on this fact.
  /// \param CurLoop Loop for which safety information is computed.
  virtual void computeLoopSafetyInfo(const Loop *CurLoop) = 0;

  /// Returns true if the instruction in a loop is guaranteed to execute at
  /// least once (under the assumption that the loop is entered).
  /// \param Inst Instruction to check.
  /// \param DT Dominator tree for the function containing \p CurLoop.
  /// \param CurLoop Loop assumed to be entered.
  /// \returns True if \p Inst is guaranteed to execute at least once.
  virtual bool isGuaranteedToExecute(const Instruction &Inst,
                                     const DominatorTree *DT,
                                     const Loop *CurLoop) const = 0;

  /// Construct an empty loop safety info.
  LoopSafetyInfo() = default;

  /// Destroy the loop safety info.
  virtual ~LoopSafetyInfo() = default;
};


/// Simple and conservative implementation of LoopSafetyInfo that can give
/// false-positive answers to its queries in order to avoid complicated
/// analysis.
class LLVM_ABI SimpleLoopSafetyInfo : public LoopSafetyInfo {
  bool MayThrow = false;       // The current loop contains an instruction which
                               // may throw.
  bool HeaderMayThrow = false; // Same as previous, but specific to loop header

public:
  /// Returns true iff the block \p BB potentially may throw exception.
  /// \param BB Block to check for potentially throwing instructions.
  /// \returns True if \p BB may throw; may be a false positive.
  bool blockMayThrow(const BasicBlock *BB) const override;

  /// Returns true iff any block of the loop contains an instruction that may
  /// throw or otherwise exit abnormally.
  /// \returns True if any block in the loop may throw or exit abnormally.
  bool anyBlockMayThrow() const override;

  /// Compute safety information for the given loop.
  /// \param CurLoop Loop for which safety information is computed.
  void computeLoopSafetyInfo(const Loop *CurLoop) override;

  /// Returns true if \p Inst is guaranteed to execute at least once in the
  /// loop.
  /// \param Inst Instruction to check.
  /// \param DT Dominator tree for the function containing \p CurLoop.
  /// \param CurLoop Loop assumed to be entered.
  /// \returns True if \p Inst is guaranteed to execute at least once.
  bool isGuaranteedToExecute(const Instruction &Inst,
                             const DominatorTree *DT,
                             const Loop *CurLoop) const override;
};

/// Precise LoopSafetyInfo based on ImplicitControlFlowTracking.
///
/// This implementation of LoopSafetyInfo use ImplicitControlFlowTracking to
/// give precise answers on "may throw" queries. This implementation uses cache
/// that should be invalidated by calling the methods insertInstructionTo and
/// removeInstruction whenever we modify a basic block's contents by adding or
/// removing instructions.
class LLVM_ABI ICFLoopSafetyInfo : public LoopSafetyInfo {
  bool MayThrow = false;       // The current loop contains an instruction which
                               // may throw.
  // Contains information about implicit control flow in this loop's blocks.
  mutable ImplicitControlFlowTracking ICF;
  // Contains information about instruction that may possibly write memory.
  mutable MemoryWriteTracking MW;

public:
  /// Returns true iff the block \p BB potentially may throw exception.
  /// \param BB Block to check for potentially throwing instructions.
  /// \returns True if \p BB may throw; may be a false positive.
  bool blockMayThrow(const BasicBlock *BB) const override;

  /// Returns true iff any block of the loop contains an instruction that may
  /// throw or otherwise exit abnormally.
  /// \returns True if any block in the loop may throw or exit abnormally.
  bool anyBlockMayThrow() const override;

  /// Compute safety information for the given loop.
  /// \param CurLoop Loop for which safety information is computed.
  void computeLoopSafetyInfo(const Loop *CurLoop) override;

  /// Returns true if \p Inst is guaranteed to execute at least once in the
  /// loop.
  /// \param Inst Instruction to check.
  /// \param DT Dominator tree for the function containing \p CurLoop.
  /// \param CurLoop Loop assumed to be entered.
  /// \returns True if \p Inst is guaranteed to execute at least once.
  bool isGuaranteedToExecute(const Instruction &Inst,
                             const DominatorTree *DT,
                             const Loop *CurLoop) const override;

  /// Returns true if we could not execute a memory-modifying instruction before
  /// we enter \p BB under assumption that \p CurLoop is entered.
  /// \param BB Block entered after any prior memory writes are considered.
  /// \param CurLoop Loop assumed to be entered.
  /// \returns True if no memory write can execute before entering \p BB.
  bool doesNotWriteMemoryBefore(const BasicBlock *BB, const Loop *CurLoop)
      const;

  /// Returns true if we could not execute a memory-modifying instruction before
  /// we execute \p I under assumption that \p CurLoop is entered.
  /// \param I Instruction before which memory writes are considered.
  /// \param CurLoop Loop assumed to be entered.
  /// \returns True if no memory write can execute before \p I.
  bool doesNotWriteMemoryBefore(const Instruction &I, const Loop *CurLoop)
      const;

  /// Update caches for an instruction about to be inserted into a block.
  ///
  /// Inform the safety info that we are planning to insert a new instruction
  /// \p Inst into the basic block \p BB. It will make all cache updates to keep
  /// it correct after this insertion.
  /// \param Inst Instruction that will be inserted.
  /// \param BB Basic block that will contain \p Inst.
  void insertInstructionTo(const Instruction *Inst, const BasicBlock *BB);

  /// Inform safety info that we are planning to remove the instruction \p Inst
  /// from its block. It will make all cache updates to keep it correct after
  /// this removal.
  /// \param Inst Instruction that will be removed.
  void removeInstruction(const Instruction *Inst);
};

/// Return true if \p F may contain irreducible control flow.
/// \param F Function to inspect.
/// \param LI Loop info for \p F, or nullptr if unavailable.
/// \returns True if \p F may contain irreducible control flow.
LLVM_ABI bool mayContainIrreducibleControl(const Function &F,
                                           const LoopInfo *LI);

struct MustBeExecutedContextExplorer;

/// Direction in which a must-be-executed context is explored.
enum class ExplorationDirection {
  /// Explore toward predecessors / earlier instructions.
  BACKWARD = 0,
  /// Explore toward successors / later instructions.
  FORWARD = 1,
};

/// Must be executed iterators visit stretches of instructions that are
/// guaranteed to be executed together, potentially with other instruction
/// executed in-between.
///
/// Given the following code, and assuming all statements are single
/// instructions which transfer execution to the successor (see
/// isGuaranteedToTransferExecutionToSuccessor), there are two possible
/// outcomes. If we start the iterator at A, B, or E, we will visit only A, B,
/// and E. If we start at C or D, we will visit all instructions A-E.
///
/// \code
///   A;
///   B;
///   if (...) {
///     C;
///     D;
///   }
///   E;
/// \endcode
///
///
/// Below is the example extneded with instructions F and G. Now we assume F
/// might not transfer execution to it's successor G. As a result we get the
/// following visit sets:
///
/// Start Instruction   | Visit Set
/// A                   | A, B,       E, F
///    B                | A, B,       E, F
///       C             | A, B, C, D, E, F
///          D          | A, B, C, D, E, F
///             E       | A, B,       E, F
///                F    | A, B,       E, F
///                   G | A, B,       E, F, G
///
///
/// \code
///   A;
///   B;
///   if (...) {
///     C;
///     D;
///   }
///   E;
///   F;  // Might not transfer execution to its successor G.
///   G;
/// \endcode
///
///
/// A more complex example involving conditionals, loops, break, and continue
/// is shown below. We again assume all instructions will transmit control to
/// the successor and we assume we can prove the inner loop to be finite. We
/// omit non-trivial branch conditions as the exploration is oblivious to them.
/// Constant branches are assumed to be unconditional in the CFG. The resulting
/// visist sets are shown in the table below.
///
/// \code
///   A;
///   while (true) {
///     B;
///     if (...)
///       C;
///     if (...)
///       continue;
///     D;
///     if (...)
///       break;
///     do {
///       if (...)
///         continue;
///       E;
///     } while (...);
///     F;
///   }
///   G;
/// \endcode
///
/// Start Instruction    | Visit Set
/// A                    | A, B
///    B                 | A, B
///       C              | A, B, C
///          D           | A, B,    D
///             E        | A, B,    D, E, F
///                F     | A, B,    D,    F
///                   G  | A, B,    D,       G
///
///
/// Note that the examples show optimal visist sets but not necessarily the ones
/// derived by the explorer depending on the available CFG analyses (see
/// MustBeExecutedContextExplorer). Also note that we, depending on the options,
/// the visit set can contain instructions from other functions.
struct MustBeExecutedIterator {
  /// Value type exposed by this input iterator.
  typedef const Instruction *value_type;
  /// Type for distances between iterators.
  typedef std::ptrdiff_t difference_type;
  /// Pointer type for the iterated instruction.
  typedef const Instruction **pointer;
  /// Reference type for the iterated instruction.
  typedef const Instruction *&reference;
  /// Iterator category tag identifying this as an input iterator.
  typedef std::input_iterator_tag iterator_category;

  /// Explorer type that creates and drives this iterator.
  using ExplorerTy = MustBeExecutedContextExplorer;

  /// Copy-construct a must-be-executed iterator.
  /// \param Other Iterator to copy.
  MustBeExecutedIterator(const MustBeExecutedIterator &Other) = default;

  /// Move-construct a must-be-executed iterator.
  /// \param Other Iterator to move from.
  MustBeExecutedIterator(MustBeExecutedIterator &&Other)
      : Visited(std::move(Other.Visited)), Explorer(Other.Explorer),
        CurInst(Other.CurInst), Head(Other.Head), Tail(Other.Tail) {}

  /// Move-assign a must-be-executed iterator.
  /// \param Other Iterator to move from.
  /// \returns A reference to this iterator.
  MustBeExecutedIterator &operator=(MustBeExecutedIterator &&Other) {
    if (this != &Other) {
      std::swap(Visited, Other.Visited);
      std::swap(CurInst, Other.CurInst);
      std::swap(Head, Other.Head);
      std::swap(Tail, Other.Tail);
    }
    return *this;
  }

  /// Destroy the must-be-executed iterator.
  ~MustBeExecutedIterator() = default;

  /// Advance to the next instruction in the must-be-executed context.
  /// \returns A reference to this iterator.
  MustBeExecutedIterator &operator++() {
    CurInst = advance();
    return *this;
  }

  /// Advance to the next instruction, returning the prior iterator value.
  /// \param Unused Dummy parameter distinguishing post-increment.
  /// \returns A copy of the iterator before advancement.
  MustBeExecutedIterator operator++(int Unused) {
    MustBeExecutedIterator tmp(*this);
    operator++();
    return tmp;
  }

  /// Return true if both iterators point to the same position.
  ///
  /// The visit history is ignored for equality.
  /// \param Other Iterator to compare against.
  /// \returns True if both iterators expose the same position.
  bool operator==(const MustBeExecutedIterator &Other) const {
    return CurInst == Other.CurInst && Head == Other.Head && Tail == Other.Tail;
  }

  /// Return true if the iterators point to different positions.
  /// \param Other Iterator to compare against.
  /// \returns True if the iterators expose different positions.
  bool operator!=(const MustBeExecutedIterator &Other) const {
    return !(*this == Other);
  }

  /// Return the underlying instruction.
  /// \returns A reference to the current instruction pointer.
  const Instruction *&operator*() { return CurInst; }
  /// Return the instruction currently exposed by this iterator.
  /// \returns The current instruction, or nullptr at the end.
  const Instruction *getCurrentInst() const { return CurInst; }

  /// Return true if \p I was encountered by this iterator already.
  /// \param I Instruction to look up in the visit set.
  /// \returns True if \p I was already visited in either direction.
  bool count(const Instruction *I) const {
    return Visited.count({I, ExplorationDirection::FORWARD}) ||
           Visited.count({I, ExplorationDirection::BACKWARD});
  }

private:
  using VisitedSetTy =
      DenseSet<PointerIntPair<const Instruction *, 1, ExplorationDirection>>;

  /// Private constructors.
  LLVM_ABI MustBeExecutedIterator(ExplorerTy &Explorer, const Instruction *I);

  /// Reset the iterator to its initial state pointing at \p I.
  void reset(const Instruction *I);

  /// Reset the iterator to point at \p I, keep cached state.
  void resetInstruction(const Instruction *I);

  /// Try to advance one of the underlying positions (Head or Tail).
  ///
  /// \return The next instruction in the must be executed context, or nullptr
  ///         if none was found.
  LLVM_ABI const Instruction *advance();

  /// A set to track the visited instructions in order to deal with endless
  /// loops and recursion.
  VisitedSetTy Visited;

  /// A reference to the explorer that created this iterator.
  ExplorerTy &Explorer;

  /// The instruction we are currently exposing to the user. There is always an
  /// instruction that we know is executed with the given program point,
  /// initially the program point itself.
  const Instruction *CurInst;

  /// Two positions that mark the program points where this iterator will look
  /// for the next instruction. Note that the current instruction is either the
  /// one pointed to by Head, Tail, or both.
  const Instruction *Head, *Tail;

  friend struct MustBeExecutedContextExplorer;
};

/// Interface to explore must-be-executed contexts around program points.
///
/// A "must be executed context" for a given program point PP is the set of
/// instructions, potentially before and after PP, that are executed always when
/// PP is reached. The MustBeExecutedContextExplorer an interface to explore
/// "must be executed contexts" in a module through the use of
/// MustBeExecutedIterator.
///
/// The explorer exposes "must be executed iterators" that traverse the must be
/// executed context. There is little information sharing between iterators as
/// the expected use case involves few iterators for "far apart" instructions.
/// If that changes, we should consider caching more intermediate results.
struct MustBeExecutedContextExplorer {

  /// Construct an explorer with the given exploration options and analysis
  /// getters.
  ///
  /// In the description of the parameters we use PP to denote a program point
  /// for which the must be executed context is explored, or put differently,
  /// for which the MustBeExecutedIterator is created.
  ///
  /// \param ExploreInterBlock    Flag to indicate if instructions in blocks
  ///                             other than the parent of PP should be
  ///                             explored.
  /// \param ExploreCFGForward    Flag to indicate if instructions located after
  ///                             PP in the CFG, e.g., post-dominating PP,
  ///                             should be explored.
  /// \param ExploreCFGBackward   Flag to indicate if instructions located
  ///                             before PP in the CFG, e.g., dominating PP,
  ///                             should be explored.
  /// \param LIGetter             Getter for LoopInfo of a function.
  /// \param DTGetter             Getter for DominatorTree of a function.
  /// \param PDTGetter            Getter for PostDominatorTree of a function.
  MustBeExecutedContextExplorer(
      bool ExploreInterBlock, bool ExploreCFGForward, bool ExploreCFGBackward,
      GetterTy<const LoopInfo> LIGetter =
          [](const Function &) { return nullptr; },
      GetterTy<const DominatorTree> DTGetter =
          [](const Function &) { return nullptr; },
      GetterTy<const PostDominatorTree> PDTGetter =
          [](const Function &) { return nullptr; })
      : ExploreInterBlock(ExploreInterBlock),
        ExploreCFGForward(ExploreCFGForward),
        ExploreCFGBackward(ExploreCFGBackward), LIGetter(LIGetter),
        DTGetter(DTGetter), PDTGetter(PDTGetter), EndIterator(*this, nullptr) {}

  /// Iterator-based interface. \see MustBeExecutedIterator.
  ///{
  using iterator = MustBeExecutedIterator;
  /// Const iterator over a must-be-executed context.
  using const_iterator = const MustBeExecutedIterator;

  /// Return an iterator to explore the context around \p PP.
  /// \param PP Program point whose must-be-executed context is explored.
  /// \returns A mutable iterator positioned at the start of the context.
  iterator &begin(const Instruction *PP) {
    auto &It = InstructionIteratorMap[PP];
    if (!It)
      It.reset(new iterator(*this, PP));
    return *It;
  }

  /// Return an iterator to explore the cached context around \p PP.
  /// \param PP Program point whose cached must-be-executed context is explored.
  /// \returns A const iterator positioned at the start of the cached context.
  const_iterator &begin(const Instruction *PP) const {
    return *InstructionIteratorMap.find(PP)->second;
  }

  /// Return the universal end iterator for must-be-executed exploration.
  /// \returns The shared end iterator used by all explorations.
  iterator &end() { return EndIterator; }
  /// Return the universal end iterator (the program point is ignored).
  /// \param PP Unused program point for interface symmetry with \c begin.
  /// \returns The shared end iterator used by all explorations.
  iterator &end(const Instruction *PP) { return EndIterator; }

  /// Return the universal const end iterator for must-be-executed exploration.
  /// \returns The shared const end iterator used by all explorations.
  const_iterator &end() const { return EndIterator; }
  /// Return the universal const end iterator (the program point is ignored).
  /// \param PP Unused program point for interface symmetry with \c begin.
  /// \returns The shared const end iterator used by all explorations.
  const_iterator &end(const Instruction *PP) const { return EndIterator; }

  /// Return an iterator range to explore the context around \p PP.
  /// \param PP Program point whose must-be-executed context is explored.
  /// \returns An iterator range over the must-be-executed context of \p PP.
  llvm::iterator_range<iterator> range(const Instruction *PP) {
    return llvm::make_range(begin(PP), end(PP));
  }

  /// Return an iterator range to explore the cached context around \p PP.
  /// \param PP Program point whose cached must-be-executed context is explored.
  /// \returns A const iterator range over the must-be-executed context of \p PP.
  llvm::iterator_range<const_iterator> range(const Instruction *PP) const {
    return llvm::make_range(begin(PP), end(PP));
  }
  ///}

  /// Check \p Pred on all instructions in the context.
  ///
  /// This method will evaluate \p Pred and return
  /// true if \p Pred holds in every instruction.
  /// \param PP Program point whose must-be-executed context is checked.
  /// \param Pred Predicate evaluated for each instruction in the context.
  /// \returns True if \p Pred holds for every instruction in the context.
  bool checkForAllContext(const Instruction *PP,
                          function_ref<bool(const Instruction *)> Pred) {
    for (auto EIt = begin(PP), EEnd = end(PP); EIt != EEnd; ++EIt)
      if (!Pred(*EIt))
        return false;
    return true;
  }

  /// Helper to look for \p I in the context of \p PP.
  ///
  /// The context is expanded until \p I was found or no more expansion is
  /// possible.
  ///
  /// \param I Instruction to search for.
  /// \param PP Program point defining the context to search.
  /// \returns True, iff \p I was found.
  bool findInContextOf(const Instruction *I, const Instruction *PP) {
    auto EIt = begin(PP), EEnd = end(PP);
    return findInContextOf(I, EIt, EEnd);
  }

  /// Helper to look for \p I in the context defined by \p EIt and \p EEnd.
  ///
  /// The context is expanded until \p I was found or no more expansion is
  /// possible.
  ///
  /// \param I Instruction to search for.
  /// \param EIt Begin iterator for the context (expanded during the search).
  /// \param EEnd End iterator for the context.
  /// \returns True, iff \p I was found.
  bool findInContextOf(const Instruction *I, iterator &EIt, iterator &EEnd) {
    bool Found = EIt.count(I);
    while (!Found && EIt != EEnd)
      Found = (++EIt).getCurrentInst() == I;
    return Found;
  }

  /// Return the next instruction that is guaranteed to be executed after \p PP.
  ///
  /// \param It              The iterator that is used to traverse the must be
  ///                        executed context.
  /// \param PP              The program point for which the next instruction
  ///                        that is guaranteed to execute is determined.
  /// \returns The next must-execute instruction, or nullptr if none.
  LLVM_ABI const Instruction *
  getMustBeExecutedNextInstruction(MustBeExecutedIterator &It,
                                   const Instruction *PP);
  /// Return the previous instr. that is guaranteed to be executed before \p PP.
  ///
  /// \param It              The iterator that is used to traverse the must be
  ///                        executed context.
  /// \param PP              The program point for which the previous instr.
  ///                        that is guaranteed to execute is determined.
  /// \returns The previous must-execute instruction, or nullptr if none.
  LLVM_ABI const Instruction *
  getMustBeExecutedPrevInstruction(MustBeExecutedIterator &It,
                                   const Instruction *PP);

  /// Find the next join point from \p InitBB in forward direction.
  /// \param InitBB Basic block from which the forward search starts.
  /// \returns The join basic block, or nullptr if none was found.
  LLVM_ABI const BasicBlock *findForwardJoinPoint(const BasicBlock *InitBB);

  /// Find the next join point from \p InitBB in backward direction.
  /// \param InitBB Basic block from which the backward search starts.
  /// \returns The join basic block, or nullptr if none was found.
  LLVM_ABI const BasicBlock *findBackwardJoinPoint(const BasicBlock *InitBB);

  /// True if exploration may leave the parent block of the program point.
  const bool ExploreInterBlock;
  /// True if instructions after the program point in the CFG are explored.
  const bool ExploreCFGForward;
  /// True if instructions before the program point in the CFG are explored.
  const bool ExploreCFGBackward;

private:
  /// Getters for common CFG analyses: LoopInfo, DominatorTree, and
  /// PostDominatorTree.
  ///{
  GetterTy<const LoopInfo> LIGetter;
  GetterTy<const DominatorTree> DTGetter;
  GetterTy<const PostDominatorTree> PDTGetter;
  ///}

  /// Map to cache isGuaranteedToTransferExecutionToSuccessor results.
  DenseMap<const BasicBlock *, std::optional<bool>> BlockTransferMap;

  /// Map to cache containsIrreducibleCFG results.
  DenseMap<const Function *, std::optional<bool>> IrreducibleControlMap;

  /// Map from instructions to associated must be executed iterators.
  DenseMap<const Instruction *, std::unique_ptr<MustBeExecutedIterator>>
      InstructionIteratorMap;

  /// A unique end iterator.
  MustBeExecutedIterator EndIterator;
};

/// Printer pass for the must-execute analysis.
class MustExecutePrinterPass
    : public RequiredPassInfoMixin<MustExecutePrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// \param OS Output stream for the printed analysis.
  MustExecutePrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print must-execute information for \p F.
  /// \param F Function whose must-execute results are printed.
  /// \param AM Function analysis manager providing supporting analyses.
  /// \returns Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Printer pass for must-be-executed contexts.
class MustBeExecutedContextPrinterPass
    : public RequiredPassInfoMixin<MustBeExecutedContextPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes to \p OS.
  /// \param OS Output stream for the printed contexts.
  MustBeExecutedContextPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print must-be-executed contexts for module \p M.
  /// \param M Module whose must-be-executed contexts are printed.
  /// \param AM Module analysis manager providing supporting analyses.
  /// \returns Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);
};

} // namespace llvm

#endif
