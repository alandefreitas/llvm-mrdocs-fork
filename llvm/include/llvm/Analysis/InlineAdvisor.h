//===- InlineAdvisor.h - Inlining decision making abstraction -*- C++ ---*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
#ifndef LLVM_ANALYSIS_INLINEADVISOR_H
#define LLVM_ANALYSIS_INLINEADVISOR_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {
class BasicBlock;
class CallBase;
class Function;
class Module;
class OptimizationRemark;
class ImportedFunctionsInliningStatistics;
class OptimizationRemarkEmitter;
struct ReplayInlinerSettings;

/// There are 4 scenarios we can use the InlineAdvisor:
/// - Default - use manual heuristics.
///
/// - Release mode, the expected mode for production, day to day deployments.
/// In this mode, when building the compiler, we also compile a pre-trained ML
/// model to native code, and link it as a static library. This mode has low
/// overhead and no additional dependencies for the compiler runtime.
///
/// - Development mode, for training new models.
/// In this mode, we trade off runtime performance for flexibility. This mode
/// requires the TFLite library, and evaluates models dynamically. This mode
/// also permits generating training logs, for offline training.
///
/// - Dynamically load an advisor via a plugin (PluginInlineAdvisorAnalysis)
enum class InliningAdvisorMode : int {
  /// Use manual heuristics for inlining decisions.
  Default,
  /// Evaluate a compiled-in pre-trained ML model (production).
  Release,
  /// Evaluate models dynamically and optionally log training data.
  Development
};

/// Identifies which inline driver is constructing or using an advisor.
enum class InlinePass : int {
  /// Always-inliner pass driver.
  AlwaysInliner,
  /// CGSCC inliner pass driver.
  CGSCCInliner,
  /// Early inliner pass driver.
  EarlyInliner,
  /// Module inliner pass driver.
  ModuleInliner,
  /// ML-based inliner pass driver.
  MLInliner,
  /// Replay-based CGSCC inliner pass driver.
  ReplayCGSCCInliner,
  /// Replay-based sample-profile inliner pass driver.
  ReplaySampleProfileInliner,
  /// Sample-profile inliner pass driver.
  SampleProfileInliner,
};

/// Provides context on when an inline advisor is constructed in the pipeline
/// (e.g., link phase, inline driver).
struct InlineContext {
  /// Thin/full LTO phase in which the advisor is constructed.
  ThinOrFullLTOPhase LTOPhase;

  /// Inline driver pass that owns this advisor context.
  InlinePass Pass;
};

/// Annotate \p IC with a human-readable inline pass name string.
/// @param IC Inline context whose pass and LTO phase are annotated.
/// @return Annotated pass name for remarks and diagnostics.
LLVM_ABI std::string AnnotateInlinePassName(InlineContext IC);

class InlineAdvisor;
/// Captures state between an inlining decision and observing its impact.
///
/// Capture state between an inlining decision having had been made, and
/// its impact being observable. When collecting model training data, this
/// allows recording features/decisions/partial reward data sets.
///
/// Derivations of this type are expected to be tightly coupled with their
/// InliningAdvisors. The base type implements the minimal contractual
/// obligations.
class InlineAdvice {
public:
  /// Construct advice for call site \p CB with recommendation \p IsInliningRecommended.
  /// @param Advisor Owning inline advisor that produced this advice.
  /// @param CB Call site the advice applies to.
  /// @param ORE Remark emitter used to report inlining outcomes.
  /// @param IsInliningRecommended Whether inlining is recommended for \p CB.
  LLVM_ABI InlineAdvice(InlineAdvisor *Advisor, CallBase &CB,
                        OptimizationRemarkEmitter &ORE,
                        bool IsInliningRecommended);

  /// Deleted; InlineAdvice is not movable.
  /// @param Other Advice that would have been moved from.
  InlineAdvice(InlineAdvice &&Other) = delete;
  /// Deleted; InlineAdvice is not copyable.
  /// @param Other Advice that would have been copied from.
  InlineAdvice(const InlineAdvice &Other) = delete;
  /// Destroy advice after the inliner has recorded its decision.
  virtual ~InlineAdvice() {
    assert(Recorded && "InlineAdvice should have been informed of the "
                       "inliner's decision in all cases");
  }

  /// Exactly one of the record* APIs must be called. Implementers may extend
  /// behavior by implementing the corresponding record*Impl.
  ///
  /// Call after inlining succeeded, and did not result in deleting the callee.
  LLVM_ABI void recordInlining();

  /// Call after inlining succeeded, and results in the callee being
  /// delete-able, meaning, it has no more users, and will be cleaned up
  /// subsequently.
  LLVM_ABI void recordInliningWithCalleeDeleted();

  /// Call after the decision for a call site was to not inline.
  /// @param Result Why inlining was not performed.
  void recordUnsuccessfulInlining(const InlineResult &Result) {
    markRecorded();
    recordUnsuccessfulInliningImpl(Result);
  }

  /// Call to indicate inlining was not attempted.
  void recordUnattemptedInlining() {
    markRecorded();
    recordUnattemptedInliningImpl();
  }

  /// Get the inlining recommendation.
  /// @return True if inlining is recommended for the call site.
  bool isInliningRecommended() const { return IsInliningRecommended; }
  /// Return the original call site debug location.
  /// @return Debug location of the original call site.
  const DebugLoc &getOriginalCallSiteDebugLoc() const { return DLoc; }
  /// Return the original call site basic block.
  /// @return Basic block containing the original call site.
  const BasicBlock *getOriginalCallSiteBasicBlock() const { return Block; }

protected:
  /// Hook invoked when successful inlining did not delete the callee.
  virtual void recordInliningImpl() {}
  /// Hook invoked when successful inlining left the callee delete-able.
  virtual void recordInliningWithCalleeDeletedImpl() {}
  /// Hook invoked when inlining was decided against.
  /// @param Result Why inlining was not performed.
  virtual void recordUnsuccessfulInliningImpl(const InlineResult &Result) {}
  /// Hook invoked when inlining was not attempted.
  virtual void recordUnattemptedInliningImpl() {}

  /// Owning advisor that produced this advice.
  InlineAdvisor *const Advisor;
  /// Caller function before inlining.
  Function *const Caller;
  /// Callee function before inlining.
  Function *const Callee;

  // Capture the context of CB before inlining, as a successful inlining may
  // change that context, and we want to report success or failure in the
  // original context.
  /// Debug location of the original call site.
  const DebugLoc DLoc;
  /// Basic block containing the original call site.
  const BasicBlock *const Block;
  /// Remark emitter for reporting inlining outcomes.
  OptimizationRemarkEmitter &ORE;
  /// Whether this advice recommends inlining.
  const bool IsInliningRecommended;

private:
  void markRecorded() {
    assert(!Recorded && "Recording should happen exactly once");
    Recorded = true;
  }
  void recordInlineStatsIfNeeded();

  bool Recorded = false;
};

/// Default InlineAdvice that records outcomes using an optional InlineCost.
class LLVM_ABI DefaultInlineAdvice : public InlineAdvice {
public:
  /// Construct default advice for \p CB using optional cost \p OIC.
  /// @param Advisor Owning inline advisor that produced this advice.
  /// @param CB Call site the advice applies to.
  /// @param OIC Optional inline cost; presence implies a positive recommendation.
  /// @param ORE Remark emitter used to report inlining outcomes.
  /// @param EmitRemarks Whether to emit optimization remarks for outcomes.
  DefaultInlineAdvice(InlineAdvisor *Advisor, CallBase &CB,
                      std::optional<InlineCost> OIC,
                      OptimizationRemarkEmitter &ORE, bool EmitRemarks = true)
      : InlineAdvice(Advisor, CB, ORE, OIC.has_value()), OriginalCB(&CB),
        OIC(OIC), EmitRemarks(EmitRemarks) {}

private:
  void recordUnsuccessfulInliningImpl(const InlineResult &Result) override;
  void recordInliningWithCalleeDeletedImpl() override;
  void recordInliningImpl() override;

private:
  CallBase *const OriginalCB;
  std::optional<InlineCost> OIC;
  bool EmitRemarks;
};

/// Interface for deciding whether to inline a call site or not.
class LLVM_ABI InlineAdvisor {
public:
  /// Deleted; InlineAdvisor is not movable.
  /// @param Other Advisor that would have been moved from.
  InlineAdvisor(InlineAdvisor &&Other) = delete;
  /// Destroy the advisor and any owned statistics.
  virtual ~InlineAdvisor();

  /// Get an InlineAdvice recommending whether to inline call site \p CB.
  ///
  /// \p CB is assumed to be a direct call. \p FAM is assumed to
  /// be up-to-date wrt previous inlining decisions. \p MandatoryOnly indicates
  /// only mandatory (always-inline) call sites should be recommended - this
  /// allows the InlineAdvisor track such inlininings.
  /// Returns:
  /// - An InlineAdvice with the inlining recommendation.
  /// - Null when no recommendation is made (https://reviews.llvm.org/D110658).
  /// TODO: Consider removing the Null return scenario by incorporating the
  /// SampleProfile inliner into an InlineAdvisor
  /// @param CB Direct call site to advise on.
  /// @param MandatoryOnly If true, only recommend mandatory always-inline sites.
  /// @return Advice for \p CB, or null when no recommendation is made.
  std::unique_ptr<InlineAdvice> getAdvice(CallBase &CB,
                                          bool MandatoryOnly = false);

  /// Update advisor state when the Inliner pass is entered.
  ///
  /// This must be called when the Inliner pass is entered, to allow the
  /// InlineAdvisor update internal state, as result of function passes run
  /// between Inliner pass runs (for the same module).
  /// @param SCC Optional SCC being processed; may be null.
  virtual void onPassEntry(LazyCallGraph::SCC *SCC = nullptr) {}

  /// Prepare advisor state when the Inliner pass is exited.
  ///
  /// This must be called when the Inliner pass is exited, as function passes
  /// may be run subsequently. This allows an implementation of InlineAdvisor
  /// to prepare for a partial update, based on the optional SCC.
  /// @param SCC Optional SCC being processed; may be null.
  virtual void onPassExit(LazyCallGraph::SCC *SCC = nullptr) {}

  /// Print advisor state to \p OS.
  /// @param OS Output stream for the printed advisor state.
  virtual void print(raw_ostream &OS) const {
    OS << "Unimplemented InlineAdvisor print\n";
  }

  /// NOTE pass name is annotated only when inline advisor constructor provides InlineContext.
  /// @return Null-terminated annotated inline pass name.
  const char *getAnnotatedInlinePassName() const {
    return AnnotatedInlinePassName.c_str();
  }

protected:
  /// Construct an advisor for module \p M using analyses from \p FAM.
  /// @param M Module whose call sites are advised.
  /// @param FAM Function analysis manager providing analyses for advice.
  /// @param IC Optional pipeline context used to annotate the pass name.
  InlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                std::optional<InlineContext> IC = std::nullopt);
  /// Compute advice for call site \p CB.
  /// @param CB Direct call site to advise on.
  /// @return Advice recommending whether to inline \p CB.
  virtual std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) = 0;
  /// Compute mandatory (always/never) advice for call site \p CB.
  /// @param CB Call site to evaluate for mandatory inlining.
  /// @param Advice Whether mandatory inlining is recommended.
  /// @return Advice capturing the mandatory inlining decision.
  virtual std::unique_ptr<InlineAdvice> getMandatoryAdvice(CallBase &CB,
                                                           bool Advice);

  /// Module whose call sites are advised.
  Module &M;
  /// Function analysis manager providing analyses for advice.
  FunctionAnalysisManager &FAM;
  /// Optional pipeline context used when constructing this advisor.
  const std::optional<InlineContext> IC;
  /// Annotated inline pass name for remarks, when \p IC was provided.
  const std::string AnnotatedInlinePassName;
  /// Statistics for functions imported and then inlined.
  std::unique_ptr<ImportedFunctionsInliningStatistics> ImportedFunctionsStats;

  /// Kind of mandatory inlining constraint on a call site.
  enum class MandatoryInliningKind {
    /// Call site is not subject to a mandatory always/never constraint.
    NotMandatory,
    /// Call site must be inlined.
    Always,
    /// Call site must never be inlined.
    Never
  };

  /// Classify the mandatory inlining kind of call site \p CB.
  /// @param CB Call site to classify.
  /// @param FAM Function analysis manager providing analyses.
  /// @param ORE Remark emitter for mandatory-inlining diagnostics.
  /// @return Mandatory inlining kind for \p CB.
  static MandatoryInliningKind getMandatoryKind(CallBase &CB,
                                                FunctionAnalysisManager &FAM,
                                                OptimizationRemarkEmitter &ORE);

  /// Return the OptimizationRemarkEmitter for the caller of \p CB.
  /// @param CB Call site whose caller's remark emitter is requested.
  /// @return Remark emitter for the caller function.
  OptimizationRemarkEmitter &getCallerORE(CallBase &CB);

private:
  friend class InlineAdvice;
};

/// The default (manual heuristics) implementation of the InlineAdvisor.
///
/// This implementation does not need to keep state between inliner pass runs,
/// and is reusable as-is for inliner pass test scenarios, as well as for
/// regular use.
class LLVM_ABI DefaultInlineAdvisor : public InlineAdvisor {
public:
  /// Construct a default heuristic advisor for module \p M.
  /// @param M Module whose call sites are advised.
  /// @param FAM Function analysis manager providing analyses for advice.
  /// @param Params Inline cost/heuristic parameters.
  /// @param IC Pipeline context identifying the inline driver.
  DefaultInlineAdvisor(Module &M, FunctionAnalysisManager &FAM,
                       InlineParams Params, InlineContext IC)
      : InlineAdvisor(M, FAM, IC), Params(Params) {}

private:
  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;

  InlineParams Params;
};

/// Used for dynamically registering InlineAdvisors as plugins
///
/// An advisor plugin adds a new advisor at runtime by registering an instance
/// of PluginInlineAdvisorAnalysis in the current ModuleAnalysisManager.
/// For example, the following code dynamically registers a
/// DefaultInlineAdvisor:
///
/// namespace {
///
/// InlineAdvisor *defaultAdvisorFactory(Module &M,
///                                      FunctionAnalysisManager &FAM,
///                                      InlineParams Params,
///                                      InlineContext IC) {
///   return new DefaultInlineAdvisor(M, FAM, Params, IC);
/// }
///
/// } // namespace
///
/// extern "C" LLVM_ATTRIBUTE_WEAK ::llvm::PassPluginLibraryInfo
/// llvmGetPassPluginInfo() {
///   return {LLVM_PLUGIN_API_VERSION, "DynamicDefaultAdvisor",
///           LLVM_VERSION_STRING,
///           [](PassBuilder &PB) {
///             PB.registerAnalysisRegistrationCallback(
///                 [](ModuleAnalysisManager &MAM) {
///                   PluginInlineAdvisorAnalysis PA(defaultAdvisorFactory);
///                   MAM.registerPass([&] { return PA; });
///                 });
///           }};
/// }
///
/// A plugin must implement an AdvisorFactory and register it with a
/// PluginInlineAdvisorAnlysis to the provided ModuleAnalysisManager.
///
/// If such a plugin has been registered
/// InlineAdvisorAnalysis::Result::tryCreate will return the dynamically loaded
/// advisor.
///
class PluginInlineAdvisorAnalysis
    : public AnalysisInfoMixin<PluginInlineAdvisorAnalysis> {
public:
  /// Analysis key used to identify PluginInlineAdvisorAnalysis.
  LLVM_ABI static AnalysisKey Key;

  /// Factory that constructs a plugin InlineAdvisor for a module.
  typedef InlineAdvisor *(*AdvisorFactory)(Module &M,
                                           FunctionAnalysisManager &FAM,
                                           InlineParams Params,
                                           InlineContext IC);

  /// Construct analysis that exposes plugin factory \p Factory.
  /// @param Factory Non-null factory used to construct the plugin advisor.
  PluginInlineAdvisorAnalysis(AdvisorFactory Factory) : Factory(Factory) {
    assert(Factory != nullptr &&
           "The plugin advisor factory should not be a null pointer.");
  }

  /// Result holding the registered plugin advisor factory.
  struct Result {
    /// Factory used to construct the plugin InlineAdvisor.
    AdvisorFactory Factory;
  };

  /// Run the analysis and return the registered factory.
  /// @param M Module being analyzed (unused).
  /// @param MAM Module analysis manager (unused).
  /// @return Result containing the plugin advisor factory.
  Result run(Module &M, ModuleAnalysisManager &MAM) { return {Factory}; }
  /// Return the registered plugin advisor factory.
  /// @return Result containing the plugin advisor factory.
  Result getResult() { return {Factory}; }

private:
  AdvisorFactory Factory;
};

/// The InlineAdvisorAnalysis is a module pass because the InlineAdvisor
/// needs to capture state right before inlining commences over a module.
class InlineAdvisorAnalysis : public AnalysisInfoMixin<InlineAdvisorAnalysis> {
public:
  /// Analysis key used to identify InlineAdvisorAnalysis.
  LLVM_ABI static AnalysisKey Key;
  /// Construct an InlineAdvisorAnalysis.
  InlineAdvisorAnalysis() = default;
  /// Owns and lazily creates the module's InlineAdvisor.
  struct Result {
    /// Construct a result for module \p M under manager \p MAM.
    /// @param M Module whose advisor is managed.
    /// @param MAM Module analysis manager providing dependencies.
    Result(Module &M, ModuleAnalysisManager &MAM) : M(M), MAM(MAM) {}
    /// Invalidate this result unless it was preserved as a stateless analysis.
    /// @param M Module being invalidated (unused beyond the checker).
    /// @param PA Set of preserved analyses.
    /// @param Inv Invalidator for dependent analyses (unused).
    /// @return True if the result should be discarded.
    bool invalidate(Module &M, const PreservedAnalyses &PA,
                    ModuleAnalysisManager::Invalidator &Inv) {
      // Check whether the analysis has been explicitly invalidated. Otherwise,
      // it's stateless and remains preserved.
      auto PAC = PA.getChecker<InlineAdvisorAnalysis>();
      return !PAC.preservedWhenStateless();
    }
    /// Try to create an InlineAdvisor for the configured mode.
    /// @param Params Inline cost/heuristic parameters.
    /// @param Mode Advisor mode (default, release, or development).
    /// @param ReplaySettings Settings for replaying prior inlining decisions.
    /// @param IC Pipeline context identifying the inline driver.
    /// @return True if an advisor was successfully created.
    LLVM_ABI bool tryCreate(InlineParams Params, InliningAdvisorMode Mode,
                            const ReplayInlinerSettings &ReplaySettings,
                            InlineContext IC);
    /// Return the owned InlineAdvisor, if created.
    /// @return Pointer to the advisor, or null if none exists.
    InlineAdvisor *getAdvisor() const { return Advisor.get(); }

  private:
    Module &M;
    ModuleAnalysisManager &MAM;
    std::unique_ptr<InlineAdvisor> Advisor;
  };

  /// Run the analysis and construct a Result for module \p M.
  /// @param M Module whose advisor result is created.
  /// @param MAM Module analysis manager providing dependencies.
  /// @return Result managing the module InlineAdvisor.
  Result run(Module &M, ModuleAnalysisManager &MAM) { return Result(M, MAM); }

private:
  static bool initializeIR2VecVocabIfRequested(Module &M,
                                               ModuleAnalysisManager &MAM);
};

/// Printer pass for the InlineAdvisorAnalysis results.
class InlineAdvisorAnalysisPrinterPass
    : public RequiredPassInfoMixin<InlineAdvisorAnalysisPrinterPass> {
  raw_ostream &OS;

public:
  /// Construct a printer that writes advisor info to \p OS.
  /// @param OS Output stream for the printed advisor state.
  explicit InlineAdvisorAnalysisPrinterPass(raw_ostream &OS) : OS(OS) {}

  /// Print InlineAdvisorAnalysis results for module \p M.
  /// @param M Module whose advisor is printed.
  /// @param MAM Module analysis manager providing InlineAdvisorAnalysis.
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &MAM);

  /// Print InlineAdvisorAnalysis results for SCC \p InitialC.
  /// @param InitialC SCC whose advisor state is printed.
  /// @param AM CGSCC analysis manager providing analyses.
  /// @param CG Lazy call graph for the module.
  /// @param UR CGSCC update result (unused by the printer).
  /// @return Preserved analyses; this pass preserves all.
  LLVM_ABI PreservedAnalyses run(LazyCallGraph::SCC &InitialC,
                                 CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                 CGSCCUpdateResult &UR);
};

/// Create a release-mode (compiled-in model) InlineAdvisor for \p M.
/// @param M Module whose call sites are advised.
/// @param MAM Module analysis manager providing dependencies.
/// @param GetDefaultAdvice Fallback that returns whether to inline by default.
/// @return Owned release-mode advisor, or null if unavailable.
LLVM_ABI std::unique_ptr<InlineAdvisor>
getReleaseModeAdvisor(Module &M, ModuleAnalysisManager &MAM,
                      std::function<bool(CallBase &)> GetDefaultAdvice);

/// Create a development-mode (dynamic model) InlineAdvisor for \p M.
/// @param M Module whose call sites are advised.
/// @param MAM Module analysis manager providing dependencies.
/// @param GetDefaultAdvice Fallback that returns whether to inline by default.
/// @return Owned development-mode advisor, or null if unavailable.
LLVM_ABI std::unique_ptr<InlineAdvisor>
getDevelopmentModeAdvisor(Module &M, ModuleAnalysisManager &MAM,
                          std::function<bool(CallBase &)> GetDefaultAdvice);

// Default (manual policy) decision making helper APIs. Shared with the legacy
// pass manager inliner.

/// Return the inline cost when the inliner should attempt \p CB, else nullopt.
///
/// If we return the cost, we will emit an optimisation remark later
/// using that cost, so we won't do so from this function. Return std::nullopt
/// if inlining should not be attempted.
/// @param CB Call site to evaluate for inlining.
/// @param CalleeTTI Target transform info for the callee.
/// @param GetInlineCost Callback that computes the inline cost for \p CB.
/// @param ORE Remark emitter for deferral and related diagnostics.
/// @param EnableDeferral Whether inlining may be deferred to a later attempt.
/// @return Inline cost if inlining should be attempted; otherwise nullopt.
LLVM_ABI std::optional<InlineCost>
shouldInline(CallBase &CB, TargetTransformInfo &CalleeTTI,
             function_ref<InlineCost(CallBase &CB)> GetInlineCost,
             OptimizationRemarkEmitter &ORE, bool EnableDeferral = true);

/// Emit an ORE message that \p Callee was inlined into \p Caller.
/// @param ORE Remark emitter that receives the message.
/// @param DLoc Debug location of the original call site.
/// @param Block Basic block containing the original call site.
/// @param Callee Function that was inlined.
/// @param Caller Function that received the inlined body.
/// @param IsMandatory Whether the inlining was mandatory.
/// @param ExtraContext Optional callback to append extra remark context.
/// @param PassName Optional pass name override for the remark.
LLVM_ABI void
emitInlinedInto(OptimizationRemarkEmitter &ORE, DebugLoc DLoc,
                const BasicBlock *Block, const Function &Callee,
                const Function &Caller, bool IsMandatory,
                function_ref<void(OptimizationRemark &)> ExtraContext = {},
                const char *PassName = nullptr);

/// Emit an ORE inlined-into message based on default-heuristic cost \p IC.
/// @param ORE Remark emitter that receives the message.
/// @param DLoc Debug location of the original call site.
/// @param Block Basic block containing the original call site.
/// @param Callee Function that was inlined.
/// @param Caller Function that received the inlined body.
/// @param IC Inline cost used to format the remark.
/// @param ForProfileContext Whether the remark is for profile context.
/// @param PassName Optional pass name override for the remark.
LLVM_ABI void emitInlinedIntoBasedOnCost(
    OptimizationRemarkEmitter &ORE, DebugLoc DLoc, const BasicBlock *Block,
    const Function &Callee, const Function &Caller, const InlineCost &IC,
    bool ForProfileContext = false, const char *PassName = nullptr);

/// Add location info from \p DLoc to ORE message \p Remark.
/// @param Remark Optimization remark to annotate.
/// @param DLoc Debug location whose info is appended.
LLVM_ABI void addLocationToRemarks(OptimizationRemark &Remark, DebugLoc DLoc);

/// Set the inline-remark attribute on call site \p CB.
/// @param CB Call site that receives the inline-remark attribute.
/// @param Message Remark text stored on the attribute.
LLVM_ABI void setInlineRemark(CallBase &CB, StringRef Message);

/// Utility for extracting the inline cost message to a string.
/// @param IC Inline cost whose message is extracted.
/// @return String form of the inline cost message.
LLVM_ABI std::string inlineCostStr(const InlineCost &IC);
} // namespace llvm
#endif // LLVM_ANALYSIS_INLINEADVISOR_H
