//===- LoopPassManager.h - Loop pass management -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// This header provides classes for managing a pipeline of passes over loops
/// in LLVM IR.
///
/// The primary loop pass pipeline is managed in a very particular way to
/// provide a set of core guarantees:
/// 1) Loops are, where possible, in simplified form.
/// 2) Loops are *always* in LCSSA form.
/// 3) A collection of Loop-specific analysis results are available:
///    - LoopInfo
///    - DominatorTree
///    - ScalarEvolution
///    - AAManager
/// 4) All loop passes preserve #1 (where possible), #2, and #3.
/// 5) Loop passes run over each loop in the loop nest from the innermost to
///    the outermost. Specifically, all inner loops are processed before
///    passes run over outer loops. When running the pipeline across an inner
///    loop creates new inner loops, those are added and processed in this
///    order as well.
///
/// This process is designed to facilitate transformations which simplify,
/// reduce, and remove loops. For passes which are more oriented towards
/// optimizing loops, especially optimizing loop *nests* instead of single
/// loops in isolation, this framework is less interesting.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H
#define LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H

#include "llvm/ADT/PriorityWorklist.h"
#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/LoopNestAnalysis.h"
#include "llvm/IR/PassInstrumentation.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Transforms/Utils/LCSSA.h"
#include "llvm/Transforms/Utils/LoopSimplify.h"
#include "llvm/Transforms/Utils/LoopUtils.h"
#include <memory>

namespace llvm {

// Forward declarations of an update tracking API used in the pass manager.
class LPMUpdater;
class PassInstrumentation;

namespace {

/// Detects whether \c PassT provides a loop \c run method (not loop-nest).
template <typename PassT>
using HasRunOnLoopT = decltype(std::declval<PassT>().run(
    std::declval<Loop &>(), std::declval<LoopAnalysisManager &>(),
    std::declval<LoopStandardAnalysisResults &>(),
    std::declval<LPMUpdater &>()));

} // namespace

// Explicit specialization and instantiation declarations for the pass manager.
// See the comments on the definition of the specialization for details on how
// it differs from the primary template.
/// Specialized pass manager that runs a mixed pipeline of loop and loop-nest
/// passes over a loop nest.
template <>
class PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
                  LPMUpdater &>
    : public RequiredPassInfoMixin<
          PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
                      LPMUpdater &>> {
public:
  /// Construct an empty loop pass manager.
  explicit PassManager() = default;

  // FIXME: These are equivalent to the default move constructor/move
  // assignment. However, using = default triggers linker errors due to the
  // explicit instantiations below. Find a way to use the default and remove the
  // duplicated code here.
  /// Move-construct a loop pass manager, transferring its pass pipeline.
  /// @param Arg Pass manager to move from.
  PassManager(PassManager &&Arg)
      : IsLoopNestPass(std::move(Arg.IsLoopNestPass)),
        LoopPasses(std::move(Arg.LoopPasses)),
        LoopNestPasses(std::move(Arg.LoopNestPasses)) {}

  /// Move-assign a loop pass manager, transferring its pass pipeline.
  /// @param RHS Pass manager to move from.
  /// @return A reference to this pass manager.
  PassManager &operator=(PassManager &&RHS) {
    IsLoopNestPass = std::move(RHS.IsLoopNestPass);
    LoopPasses = std::move(RHS.LoopPasses);
    LoopNestPasses = std::move(RHS.LoopNestPasses);
    return *this;
  }

  /// Run all contained loop and loop-nest passes over \p L.
  /// @param L Loop to run passes over.
  /// @param AM Loop analysis manager propagated to each pass.
  /// @param AR Standard loop analysis results available to each pass.
  /// @param U Updater used to revise the loop nest worklist.
  /// @return The analyses preserved after running all passes.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);

  /// Print the names of passes in this manager as a comma-separated pipeline.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
  /// Add either a loop pass or a loop-nest pass to the pass manager. Append \p
  /// Pass to the list of loop passes if it has a dedicated \fn run() method for
  /// loops and to the list of loop-nest passes if the \fn run() method is for
  /// loop-nests instead. Also append whether \p Pass is loop-nest pass or not
  /// to the end of \var IsLoopNestPass so we can easily identify the types of
  /// passes in the pass manager later.
  /// @param Pass Loop or loop-nest pass to append; moved into type-erased
  /// storage.
  template <typename PassT> LLVM_ATTRIBUTE_MINSIZE void addPass(PassT &&Pass) {
    if constexpr (is_detected<HasRunOnLoopT, PassT>::value) {
      using LoopPassModelT =
          detail::PassModel<Loop, PassT, LoopAnalysisManager,
                            LoopStandardAnalysisResults &, LPMUpdater &>;
      IsLoopNestPass.push_back(false);
      LoopPasses.push_back(LoopPassModelT::create(std::move(Pass)));
    } else {
      using LoopNestPassModelT =
          detail::PassModel<LoopNest, PassT, LoopAnalysisManager,
                            LoopStandardAnalysisResults &, LPMUpdater &>;
      IsLoopNestPass.push_back(true);
      LoopNestPasses.push_back(LoopNestPassModelT::create(std::move(Pass)));
    }
  }

  /// Returns whether this pass manager contains no passes.
  /// @return True if this manager has no passes.
  bool isEmpty() const { return LoopPasses.empty() && LoopNestPasses.empty(); }

  /// Returns the number of loop (non-nest) passes in this manager.
  /// @return The number of loop passes.
  size_t getNumLoopPasses() const { return LoopPasses.size(); }
  /// Returns the number of loop-nest passes in this manager.
  /// @return The number of loop-nest passes.
  size_t getNumLoopNestPasses() const { return LoopNestPasses.size(); }

protected:
  /// Type-erased loop pass concept stored in this manager.
  using LoopPassConceptT =
      detail::PassConcept<Loop, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;
  /// Type-erased loop-nest pass concept stored in this manager.
  using LoopNestPassConceptT =
      detail::PassConcept<LoopNest, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;

  /// Parallel to the pass lists: true when the corresponding pass is loop-nest.
  BitVector IsLoopNestPass;
  /// Sequence of type-erased loop passes managed by this pass manager.
  std::vector<LoopPassConceptT::unique_ptr> LoopPasses;
  /// Sequence of type-erased loop-nest passes managed by this pass manager.
  std::vector<LoopNestPassConceptT::unique_ptr> LoopNestPasses;

  /// Run a single loop or loop-nest pass with instrumentation.
  ///
  /// Returns `std::nullopt` if PassInstrumentation's BeforePass returns false.
  /// Otherwise, returns the preserved analyses of the pass.
  /// @param IR Loop or loop nest passed to the pass.
  /// @param Pass Type-erased pass to run.
  /// @param AM Loop analysis manager propagated to the pass.
  /// @param AR Standard loop analysis results available to the pass.
  /// @param U Updater used to revise the loop nest worklist.
  /// @param PI Instrumentation callbacks run before and after the pass.
  /// @return The pass's preserved analyses, or \c std::nullopt if skipped.
  template <typename IRUnitT, typename PassT>
  std::optional<PreservedAnalyses>
  runSinglePass(IRUnitT &IR, PassT &Pass, LoopAnalysisManager &AM,
                LoopStandardAnalysisResults &AR, LPMUpdater &U,
                PassInstrumentation &PI);

  /// Run the pipeline when it contains at least one loop-nest pass.
  /// @param L Loop whose nest is processed.
  /// @param AM Loop analysis manager propagated to each pass.
  /// @param AR Standard loop analysis results available to each pass.
  /// @param U Updater used to revise the loop nest worklist.
  /// @return The analyses preserved after running the pipeline.
  LLVM_ABI PreservedAnalyses
  runWithLoopNestPasses(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U);
  /// Run the pipeline when it contains only ordinary loop passes.
  /// @param L Loop to run passes over.
  /// @param AM Loop analysis manager propagated to each pass.
  /// @param AR Standard loop analysis results available to each pass.
  /// @param U Updater used to revise the loop nest worklist.
  /// @return The analyses preserved after running the pipeline.
  LLVM_ABI PreservedAnalyses
  runWithoutLoopNestPasses(Loop &L, LoopAnalysisManager &AM,
                           LoopStandardAnalysisResults &AR, LPMUpdater &U);

private:
  static const Loop &getLoopFromIR(Loop &L) { return L; }
  static const Loop &getLoopFromIR(LoopNest &LN) {
    return LN.getOutermostLoop();
  }
};

/// The Loop pass manager.
///
/// See the documentation for the PassManager template for details. It runs
/// a sequence of Loop passes over each Loop that the manager is run over. This
/// typedef serves as a convenient way to refer to this construct.
typedef PassManager<Loop, LoopAnalysisManager, LoopStandardAnalysisResults &,
                    LPMUpdater &>
    LoopPassManager;

/// A require-analysis pass specialization for loop transformations.
///
/// A partial specialization of the require analysis template pass to forward
/// the extra parameters from a transformation's run method to the
/// AnalysisManager's getResult.
template <typename AnalysisT>
struct RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                           LoopStandardAnalysisResults &, LPMUpdater &>
    : OptionalPassInfoMixin<
          RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                              LoopStandardAnalysisResults &, LPMUpdater &>> {
  /// Request \c AnalysisT for \p L and preserve all analyses.
  /// @param L Loop to request the analysis for.
  /// @param AM Loop analysis manager used to get the result.
  /// @param AR Standard loop analysis results forwarded to \c getResult.
  /// @param U Loop pass manager updater (unused).
  /// @return All analyses preserved.
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U) {
    (void)U;
    (void)AM.template getResult<AnalysisT>(L, AR);
    return PreservedAnalyses::all();
  }
  /// Print this pass as \c require<AnalysisName>.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps analysis class names to pass names.
  void printPipeline(raw_ostream &OS,
                     function_ref<StringRef(StringRef)> MapClassName2PassName) {
    auto ClassName = AnalysisT::name();
    auto PassName = MapClassName2PassName(ClassName);
    OS << "require<" << PassName << '>';
  }
};

/// An alias template to easily name a require analysis loop pass.
template <typename AnalysisT>
using RequireAnalysisLoopPass =
    RequireAnalysisPass<AnalysisT, Loop, LoopAnalysisManager,
                        LoopStandardAnalysisResults &, LPMUpdater &>;

class FunctionToLoopPassAdaptor;

/// This class provides an interface for updating the loop pass manager based
/// on mutations to the loop nest.
///
/// A reference to an instance of this class is passed as an argument to each
/// Loop pass, and Loop passes should use it to update LPM infrastructure if
/// they modify the loop nest structure.
///
/// \c LPMUpdater comes with two modes: the loop mode and the loop-nest mode. In
/// loop mode, all the loops in the function will be pushed into the worklist
/// and when new loops are added to the pipeline, their subloops are also
/// inserted recursively. On the other hand, in loop-nest mode, only top-level
/// loops are contained in the worklist and the addition of new (top-level)
/// loops will not trigger the addition of their subloops.
class LPMUpdater {
public:
  /// Query whether the current loop should be skipped after nest updates.
  ///
  /// This can be queried by loop passes which run other loop passes (like pass
  /// managers) to know whether the loop needs to be skipped due to updates to
  /// the loop nest.
  ///
  /// If this returns true, the loop object may have been deleted, so passes
  /// should take care not to touch the object.
  /// @return True if the current loop should be skipped.
  bool skipCurrentLoop() const { return SkipCurrentLoop; }

  /// Loop passes should use this method to indicate they have deleted a loop
  /// from the nest.
  ///
  /// Note that this loop must either be the current loop or a subloop of the
  /// current loop. This routine must be called prior to removing the loop from
  /// the loop nest.
  ///
  /// If this is called for the current loop, in addition to clearing any
  /// state, this routine will mark that the current loop should be skipped by
  /// the rest of the pass management infrastructure.
  /// @param L Loop that was deleted from the nest.
  /// @param Name Name of \p L, used when clearing analysis results.
  void markLoopAsDeleted(Loop &L, llvm::StringRef Name) {
    LAM.clear(L, Name);
    assert((&L == CurrentL || CurrentL->contains(&L)) &&
           "Cannot delete a loop outside of the "
           "subloop tree currently being processed.");
    if (&L == CurrentL)
      SkipCurrentLoop = true;
  }

  /// Set the parent of the current loop for debug-build nest checks.
  /// @param L Parent loop of the loop currently being processed, or null.
  void setParentLoop(Loop *L) {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS
    ParentL = L;
#endif
  }

  /// Loop passes should use this method to indicate they have added new child
  /// loops of the current loop.
  ///
  /// \p NewChildLoops must contain only the immediate children. Any nested
  /// loops within them will be visited in postorder as usual for the loop pass
  /// manager.
  /// @param NewChildLoops Immediate child loops added under the current loop.
  void addChildLoops(ArrayRef<Loop *> NewChildLoops) {
    assert(!LoopNestMode &&
           "Child loops should not be pushed in loop-nest mode.");
    // Insert ourselves back into the worklist first, as this loop should be
    // revisited after all the children have been processed.
    Worklist.insert(CurrentL);

#ifndef NDEBUG
    for (Loop *NewL : NewChildLoops)
      assert(NewL->getParentLoop() == CurrentL && "All of the new loops must "
                                                  "be immediate children of "
                                                  "the current loop!");
#endif

    appendLoopsToWorklist(NewChildLoops, Worklist);

    // Also skip further processing of the current loop--it will be revisited
    // after all of its newly added children are accounted for.
    SkipCurrentLoop = true;
  }

  /// Loop passes should use this method to indicate they have added new
  /// sibling loops to the current loop.
  ///
  /// \p NewSibLoops must only contain the immediate sibling loops. Any nested
  /// loops within them will be visited in postorder as usual for the loop pass
  /// manager.
  /// @param NewSibLoops Immediate sibling loops added beside the current loop.
  void addSiblingLoops(ArrayRef<Loop *> NewSibLoops) {
#if LLVM_ENABLE_ABI_BREAKING_CHECKS && !defined(NDEBUG)
    for (Loop *NewL : NewSibLoops)
      assert(NewL->getParentLoop() == ParentL &&
             "All of the new loops must be siblings of the current loop!");
#endif

    if (LoopNestMode)
      Worklist.insert(NewSibLoops);
    else
      appendLoopsToWorklist(NewSibLoops, Worklist);

    // No need to skip the current loop or revisit it, as sibling loops
    // shouldn't impact anything.
  }

  /// Restart the current loop.
  ///
  /// Loop passes should call this method to indicate the current loop has been
  /// sufficiently changed that it should be re-visited from the begining of
  /// the loop pass pipeline rather than continuing.
  void revisitCurrentLoop() {
    // Tell the currently in-flight pipeline to stop running.
    SkipCurrentLoop = true;

    // And insert ourselves back into the worklist.
    Worklist.insert(CurrentL);
  }

  /// Returns whether a loop-nest pass has modified the loop nest.
  /// @return True if a loop-nest pass has modified the loop nest.
  bool isLoopNestChanged() const {
    return LoopNestChanged;
  }

  /// Loopnest passes should use this method to indicate if the
  /// loopnest has been modified.
  /// @param Changed Whether the loop nest was modified.
  void markLoopNestChanged(bool Changed) {
    LoopNestChanged = Changed;
  }

private:
  friend class llvm::FunctionToLoopPassAdaptor;

  /// The \c FunctionToLoopPassAdaptor's worklist of loops to process.
  SmallPriorityWorklist<Loop *, 4> &Worklist;

  /// The analysis manager for use in the current loop nest.
  LoopAnalysisManager &LAM;

  Loop *CurrentL;
  bool SkipCurrentLoop;
  const bool LoopNestMode;
  bool LoopNestChanged;

#if LLVM_ENABLE_ABI_BREAKING_CHECKS
  // In debug builds we also track the parent loop to implement asserts even in
  // the face of loop deletion.
  Loop *ParentL;
#endif

  LPMUpdater(SmallPriorityWorklist<Loop *, 4> &Worklist,
             LoopAnalysisManager &LAM, bool LoopNestMode = false,
             bool LoopNestChanged = false)
      : Worklist(Worklist), LAM(LAM), LoopNestMode(LoopNestMode),
        LoopNestChanged(LoopNestChanged) {}
};

template <typename IRUnitT, typename PassT>
std::optional<PreservedAnalyses> LoopPassManager::runSinglePass(
    IRUnitT &IR, PassT &Pass, LoopAnalysisManager &AM,
    LoopStandardAnalysisResults &AR, LPMUpdater &U, PassInstrumentation &PI) {
  // Get the loop in case of Loop pass and outermost loop in case of LoopNest
  // pass which is to be passed to BeforePass and AfterPass call backs.
  const Loop &L = getLoopFromIR(IR);
  // Check the PassInstrumentation's BeforePass callbacks before running the
  // pass, skip its execution completely if asked to (callback returns false).
  if (!PI.runBeforePass<Loop>(*Pass, L))
    return std::nullopt;

  PreservedAnalyses PA = Pass->run(IR, AM, AR, U);

  // do not pass deleted Loop into the instrumentation
  if (U.skipCurrentLoop())
    PI.runAfterPassInvalidated<IRUnitT>(*Pass, PA);
  else
    PI.runAfterPass<Loop>(*Pass, L, PA);
  return PA;
}

/// Adaptor that maps from a function to its loops.
///
/// Designed to allow composition of a LoopPass(Manager) and a
/// FunctionPassManager. Note that if this pass is constructed with a \c
/// FunctionAnalysisManager it will run the \c LoopAnalysisManagerFunctionProxy
/// analysis prior to running the loop passes over the function to enable a \c
/// LoopAnalysisManager to be used within this run safely.
///
/// The adaptor comes with two modes: the loop mode and the loop-nest mode, and
/// the worklist updater lived inside will be in the same mode as the adaptor
/// (refer to the documentation of \c LPMUpdater for more detailed explanation).
/// Specifically, in loop mode, all loops in the function will be pushed into
/// the worklist and processed by \p Pass, while only top-level loops are
/// processed in loop-nest mode. Please refer to the various specializations of
/// \fn createLoopFunctionToLoopPassAdaptor to see when loop mode and loop-nest
/// mode are used.
class FunctionToLoopPassAdaptor
    : public RequiredPassInfoMixin<FunctionToLoopPassAdaptor> {
public:
  /// Type-erased loop pass concept used by this adaptor.
  using PassConceptT =
      detail::PassConcept<Loop, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;

  /// Construct an adaptor that runs \p Pass over loops in a function.
  /// @param Pass Type-erased loop pass to run.
  /// @param UseMemorySSA Whether to compute and preserve MemorySSA.
  /// @param LoopNestMode Whether to run in loop-nest mode.
  explicit FunctionToLoopPassAdaptor(PassConceptT::unique_ptr Pass,
                                     bool UseMemorySSA = false,
                                     bool LoopNestMode = false)
      : Pass(std::move(Pass)), UseMemorySSA(UseMemorySSA),
        LoopNestMode(LoopNestMode) {
    LoopCanonicalizationFPM.addPass(LoopSimplifyPass());
    LoopCanonicalizationFPM.addPass(LCSSAPass());
  }

  /// Runs the loop passes across every loop in the function.
  /// @param F Function whose loops are processed.
  /// @param AM Function analysis manager.
  /// @return The analyses preserved after running the loop passes.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Print this adaptor and its nested loop pass as a pipeline.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

  /// Returns whether this adaptor runs in loop-nest mode.
  /// @return True if this adaptor runs in loop-nest mode.
  bool isLoopNestMode() const { return LoopNestMode; }

private:
  PassConceptT::unique_ptr Pass;

  FunctionPassManager LoopCanonicalizationFPM;

  bool UseMemorySSA = false;
  const bool LoopNestMode;
};

/// A function to deduce a loop pass type and wrap it in the templated
/// adaptor.
///
/// If \p Pass is a loop pass, the returned adaptor will be in loop mode.
///
/// If \p Pass is a loop-nest pass, \p Pass will first be wrapped into a
/// \c LoopPassManager and the returned adaptor will be in loop-nest mode.
/// @param Pass Loop or loop-nest pass to wrap in the adaptor.
/// @param UseMemorySSA Whether the adaptor should enable MemorySSA.
/// @return A function-to-loop adaptor wrapping \p Pass.
template <typename LoopPassT>
inline FunctionToLoopPassAdaptor
createFunctionToLoopPassAdaptor(LoopPassT &&Pass, bool UseMemorySSA = false) {
  if constexpr (is_detected<HasRunOnLoopT, LoopPassT>::value) {
    using PassModelT =
        detail::PassModel<Loop, LoopPassT, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;
    return FunctionToLoopPassAdaptor(PassModelT::create(std::move(Pass)),
                                     UseMemorySSA, false);
  } else {
    LoopPassManager LPM;
    LPM.addPass(std::move(Pass));
    using PassModelT =
        detail::PassModel<Loop, LoopPassManager, LoopAnalysisManager,
                          LoopStandardAnalysisResults &, LPMUpdater &>;
    return FunctionToLoopPassAdaptor(PassModelT::create(std::move(LPM)),
                                     UseMemorySSA, true);
  }
}

/// If \p Pass is an instance of \c LoopPassManager, the returned adaptor will
/// be in loop-nest mode if the pass manager contains only loop-nest passes.
/// @param LPM Loop pass manager to wrap in the adaptor.
/// @param UseMemorySSA Whether the adaptor should enable MemorySSA.
/// @return A function-to-loop adaptor wrapping \p LPM.
template <>
inline FunctionToLoopPassAdaptor
createFunctionToLoopPassAdaptor<LoopPassManager>(LoopPassManager &&LPM,
                                                 bool UseMemorySSA) {
  // Check if LPM contains any loop pass and if it does not, returns an adaptor
  // in loop-nest mode.
  using PassModelT =
      detail::PassModel<Loop, LoopPassManager, LoopAnalysisManager,
                        LoopStandardAnalysisResults &, LPMUpdater &>;
  bool LoopNestMode = (LPM.getNumLoopPasses() == 0);
  return FunctionToLoopPassAdaptor(PassModelT::create(std::move(LPM)),
                                   UseMemorySSA, LoopNestMode);
}

/// Pass for printing a loop's contents as textual IR.
class PrintLoopPass : public RequiredPassInfoMixin<PrintLoopPass> {
  raw_ostream &OS;
  std::string Banner;

public:
  /// Construct a print pass that writes to \c dbgs() with an empty banner.
  LLVM_ABI PrintLoopPass();
  /// Construct a print pass that writes to \p OS with an optional \p Banner.
  /// @param OS Stream to write the loop IR to.
  /// @param Banner Optional banner printed before the loop IR.
  LLVM_ABI PrintLoopPass(raw_ostream &OS, const std::string &Banner = "");

  /// Print the loop as textual IR and preserve all analyses.
  /// @param L Loop whose IR is printed.
  /// @param AM Loop analysis manager (unused).
  /// @param AR Standard loop analysis results (unused).
  /// @param U Loop pass manager updater (unused).
  /// @return All analyses preserved.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR, LPMUpdater &U);
};
}

#endif // LLVM_TRANSFORMS_SCALAR_LOOPPASSMANAGER_H
