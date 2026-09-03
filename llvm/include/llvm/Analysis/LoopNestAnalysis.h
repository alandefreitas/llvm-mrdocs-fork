//===- llvm/Analysis/LoopNestAnalysis.h ----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file defines the interface for the loop nest analysis.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_LOOPNESTANALYSIS_H
#define LLVM_ANALYSIS_LOOPNESTANALYSIS_H

#include "llvm/ADT/STLExtras.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

using LoopVectorTy = SmallVector<Loop *, 8>;

class LPMUpdater;

/// This class represents a loop nest and can be used to query its properties.
class LLVM_ABI LoopNest {
public:
  /// Vector of instructions that prevent perfect nesting.
  using InstrVectorTy = SmallVector<const Instruction *>;

  /// Construct a loop nest rooted by loop \p Root.
  /// @param Root Outermost loop of the nest.
  /// @param SE ScalarEvolution used to analyze the nest.
  LoopNest(Loop &Root, ScalarEvolution &SE);

  /// Deleted; a LoopNest requires a root loop and ScalarEvolution.
  LoopNest() = delete;

  /// Construct a LoopNest object.
  /// @param Root Outermost loop of the nest.
  /// @param SE ScalarEvolution used to analyze the nest.
  /// @return A unique_ptr owning the constructed LoopNest.
  static std::unique_ptr<LoopNest> getLoopNest(Loop &Root, ScalarEvolution &SE);

  /// Return true if the given loops \p OuterLoop and \p InnerLoop are
  /// perfectly nested with respect to each other, and false otherwise.
  /// Example:
  /// \code
  ///   for(i)
  ///     for(j)
  ///       for(k)
  /// \endcode
  /// arePerfectlyNested(loop_i, loop_j, SE) would return true.
  /// arePerfectlyNested(loop_j, loop_k, SE) would return true.
  /// arePerfectlyNested(loop_i, loop_k, SE) would return false.
  /// @param OuterLoop Candidate outer loop of the nest.
  /// @param InnerLoop Candidate inner loop of the nest.
  /// @param SE ScalarEvolution used to analyze nesting perfection.
  /// @return True if \p OuterLoop and \p InnerLoop are perfectly nested.
  static bool arePerfectlyNested(const Loop &OuterLoop, const Loop &InnerLoop,
                                 ScalarEvolution &SE);

  /// Return a vector of instructions that prevent the LoopNest given
  /// by loops \p OuterLoop and \p InnerLoop from being perfect.
  /// @param OuterLoop Outer loop of the candidate nest.
  /// @param InnerLoop Inner loop of the candidate nest.
  /// @param SE ScalarEvolution used to analyze nesting perfection.
  /// @return Instructions that prevent \p OuterLoop and \p InnerLoop from being
  /// perfectly nested.
  static InstrVectorTy getInterveningInstructions(const Loop &OuterLoop,
                                                  const Loop &InnerLoop,
                                                  ScalarEvolution &SE);

  /// Return the maximum nesting depth of the loop nest rooted by loop \p Root.
  /// For example given the loop nest:
  /// \code
  ///   for(i)     // loop at level 1 and Root of the nest
  ///     for(j)   // loop at level 2
  ///       <code>
  ///       for(k) // loop at level 3
  /// \endcode
  /// getMaxPerfectDepth(Loop_i) would return 2.
  /// @param Root Outermost loop of the nest.
  /// @param SE ScalarEvolution used to analyze nesting perfection.
  /// @return The maximum perfect nesting depth rooted at \p Root.
  static unsigned getMaxPerfectDepth(const Loop &Root, ScalarEvolution &SE);

  /// Skip empty single-successor blocks from \p From toward \p End.
  ///
  /// Recursively traverse all empty 'single successor' basic blocks of \p From
  /// (if there are any). When \p CheckUniquePred is set to true, check if
  /// each of the empty single successors has a unique predecessor. Return
  /// the last basic block found or \p End if it was reached during the search.
  /// @param From Starting basic block to traverse from.
  /// @param End Ending basic block that stops the search when reached.
  /// @param CheckUniquePred When true, require each empty successor to have a
  /// unique predecessor.
  /// @return The last empty block found, or \p End if it was reached.
  static const BasicBlock &skipEmptyBlockUntil(const BasicBlock *From,
                                               const BasicBlock *End,
                                               bool CheckUniquePred = false);

  /// Return the outermost loop in the loop nest.
  /// @return The outermost loop of this nest.
  Loop &getOutermostLoop() const { return *Loops.front(); }

  /// Return the unique innermost loop in the nest, or nullptr if none.
  ///
  /// The innermost loop returned is not necessarily perfectly nested.
  /// @return The unique innermost loop, or nullptr if there is none.
  Loop *getInnermostLoop() const {
    if (Loops.size() == 1)
      return Loops.back();

    // The loops in the 'Loops' vector have been collected in breadth first
    // order, therefore if the last 2 loops in it have the same nesting depth
    // there isn't a unique innermost loop in the nest.
    Loop *LastLoop = Loops.back();
    auto SecondLastLoopIter = ++Loops.rbegin();
    return (LastLoop->getLoopDepth() == (*SecondLastLoopIter)->getLoopDepth())
               ? nullptr
               : LastLoop;
  }

  /// Return the loop at the given \p Index.
  /// @param Index Zero-based index into the nest's breadth-first loop list.
  /// @return The loop at \p Index in the nest.
  Loop *getLoop(unsigned Index) const {
    assert(Index < Loops.size() && "Index is out of bounds");
    return Loops[Index];
  }

  /// Get the loop index of the given loop \p L.
  /// @param L Loop that must belong to this nest.
  /// @return The zero-based index of \p L in the nest's loop list.
  unsigned getLoopIndex(const Loop &L) const {
    for (unsigned I = 0; I < getNumLoops(); ++I)
      if (getLoop(I) == &L)
        return I;
    llvm_unreachable("Loop not in the loop nest");
  }

  /// Return the number of loops in the nest.
  /// @return The number of loops in this nest.
  size_t getNumLoops() const { return Loops.size(); }

  /// Get the loops in the nest.
  /// @return The loops in this nest, in breadth-first order.
  ArrayRef<Loop *> getLoops() const { return Loops; }

  /// Get the loops in the nest at the given \p Depth.
  /// @param Depth Loop depth of the loops to collect.
  /// @return The loops in this nest at \p Depth.
  LoopVectorTy getLoopsAtDepth(unsigned Depth) const {
    assert(Depth >= Loops.front()->getLoopDepth() &&
           Depth <= Loops.back()->getLoopDepth() && "Invalid depth");
    LoopVectorTy Result;
    for (unsigned I = 0; I < getNumLoops(); ++I) {
      Loop *L = getLoop(I);
      if (L->getLoopDepth() == Depth)
        Result.push_back(L);
      else if (L->getLoopDepth() > Depth)
        break;
    }
    return Result;
  }

  /// Retrieve perfect loop nests contained in this loop nest.
  ///
  /// For example, given the following nest containing 4 loops, this member
  /// function would return {{L1,L2},{L3,L4}}.
  /// \code
  ///   for(i) // L1
  ///     for(j) // L2
  ///       <code>
  ///       for(k) // L3
  ///         for(l) // L4
  /// \endcode
  /// @param SE ScalarEvolution used to analyze nesting perfection.
  /// @return Perfect sub-nests contained in this loop nest.
  SmallVector<LoopVectorTy, 4> getPerfectLoops(ScalarEvolution &SE) const;

  /// Return the loop nest depth (i.e. the loop depth of the 'deepest' loop)
  /// For example given the loop nest:
  /// \code
  ///   for(i)      // loop at level 1 and Root of the nest
  ///     for(j1)   // loop at level 2
  ///       for(k)  // loop at level 3
  ///     for(j2)   // loop at level 2
  /// \endcode
  /// getNestDepth() would return 3.
  /// @return The nesting depth from the outermost to the deepest loop.
  unsigned getNestDepth() const {
    int NestDepth =
        Loops.back()->getLoopDepth() - Loops.front()->getLoopDepth() + 1;
    assert(NestDepth > 0 && "Expecting NestDepth to be at least 1");
    return NestDepth;
  }

  /// Return the maximum perfect nesting depth.
  /// @return The maximum perfect nesting depth of this nest.
  unsigned getMaxPerfectDepth() const { return MaxPerfectDepth; }

  /// Return true if all loops in the loop nest are in simplify form.
  /// @return True if every loop in the nest is in simplify form.
  bool areAllLoopsSimplifyForm() const {
    return all_of(Loops, [](const Loop *L) { return L->isLoopSimplifyForm(); });
  }

  /// Return true if all loops in the loop nest are in rotated form.
  /// @return True if every loop in the nest is in rotated form.
  bool areAllLoopsRotatedForm() const {
    return all_of(Loops, [](const Loop *L) { return L->isRotatedForm(); });
  }

  /// Return the function to which the loop-nest belongs.
  /// @return The parent Function of the nest's outermost loop.
  Function *getParent() const {
    return Loops.front()->getHeader()->getParent();
  }

  /// Return the name of the outermost loop in the nest.
  /// @return The name of the outermost loop.
  StringRef getName() const { return Loops.front()->getName(); }

protected:
  /// Maximum perfect nesting depth level.
  const unsigned MaxPerfectDepth;
  /// Loops in the nest, in breadth-first order.
  LoopVectorTy Loops;

private:
  enum LoopNestEnum {
    PerfectLoopNest,
    ImperfectLoopNest,
    InvalidLoopStructure,
    OuterLoopLowerBoundUnknown
  };
  static LoopNestEnum analyzeLoopNestForPerfectNest(const Loop &OuterLoop,
                                                    const Loop &InnerLoop,
                                                    ScalarEvolution &SE);
};

/// Write loop nest \p LN to stream \p OS.
/// @param OS Output stream.
/// @param LN Loop nest to print.
/// @return A reference to \p OS.
LLVM_ABI raw_ostream &operator<<(raw_ostream &OS, const LoopNest &LN);

/// This analysis provides information for a loop nest. The analysis runs on
/// demand and can be initiated via AM.getResult<LoopNestAnalysis>.
class LoopNestAnalysis : public AnalysisInfoMixin<LoopNestAnalysis> {
  friend AnalysisInfoMixin<LoopNestAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Result of this analysis: a LoopNest for the analyzed loop.
  using Result = LoopNest;
  /// Run the analysis pass over loop \p L and produce a LoopNest.
  /// @param L Loop whose nest is analyzed.
  /// @param AM Loop analysis manager providing analyses.
  /// @param AR Standard loop analysis results used to build the nest.
  /// @return The LoopNest rooted at \p L.
  LLVM_ABI Result run(Loop &L, LoopAnalysisManager &AM,
                      LoopStandardAnalysisResults &AR);
};

/// Printer pass for the \c LoopNest results.
class LoopNestPrinterPass : public RequiredPassInfoMixin<LoopNestPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes LoopNest results to \p OS.
  /// @param OS Output stream for the printed analysis.
  explicit LoopNestPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print the LoopNest for loop \p L and return all analyses preserved.
  /// @param L Loop whose nest is printed.
  /// @param AM Loop analysis manager providing LoopNestAnalysis.
  /// @param AR Standard loop analysis results.
  /// @param U Loop pass manager updater (unused by the printer).
  /// @return All analyses, since the printer does not modify anything.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

} // namespace llvm

#endif // LLVM_ANALYSIS_LOOPNESTANALYSIS_H
