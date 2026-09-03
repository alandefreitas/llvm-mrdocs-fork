//===- MLInlineAdvisor.h - ML - based InlineAdvisor factories ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_MLINLINEADVISOR_H
#define LLVM_ANALYSIS_MLINLINEADVISOR_H

#include "llvm/Analysis/FunctionPropertiesAnalysis.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/MLModelRunner.h"
#include "llvm/IR/PassManager.h"

#include <map>
#include <memory>
#include <optional>

namespace llvm {
class DiagnosticInfoOptimizationBase;
class Module;
class MLInlineAdvice;
class ProfileSummaryInfo;

/// InlineAdvisor that uses an ML model runner to decide whether to inline.
class LLVM_ABI MLInlineAdvisor : public InlineAdvisor {
public:
  /// Construct an ML-based advisor for module \p M.
  /// @param M Module whose call sites are advised.
  /// @param MAM Module analysis manager providing call graph and related analyses.
  /// @param GetModelRunner Factory that builds the model runner for the feature map.
  /// @param GetDefaultAdvice Fallback that returns the heuristic inlining decision.
  MLInlineAdvisor(Module &M, ModuleAnalysisManager &MAM,
                  std::function<std::unique_ptr<MLModelRunner>(
                      const std::vector<TensorSpec> &)>
                      GetModelRunner,
                  std::function<bool(CallBase &)> GetDefaultAdvice);

  /// Destroy the ML inline advisor.
  ~MLInlineAdvisor() override = default;

  /// Update advisor state when the Inliner pass is entered for \p SCC.
  /// @param SCC Optional SCC being processed; may be null.
  void onPassEntry(LazyCallGraph::SCC *SCC) override;
  /// Prepare advisor state when the Inliner pass exits \p SCC.
  /// @param SCC Optional SCC being processed; may be null.
  void onPassExit(LazyCallGraph::SCC *SCC) override;

  /// Return the cached IR size (instruction count) of function \p F.
  /// @param F Function whose IR size is requested.
  /// @return Total instruction count from cached function properties.
  int64_t getIRSize(Function &F) const {
    return getCachedFPI(F).TotalInstructionCount;
  }
  /// Update module-wide advisor state after a successful inlining of \p Advice.
  ///
  /// Invalidates caller analyses, refreshes cached FPI, and updates IR size and
  /// edge/node counts. May set ForceStop if size growth exceeds the threshold.
  /// @param Advice Advice describing the completed inlining.
  /// @param CalleeWasDeleted Whether the callee became delete-able after inlining.
  void onSuccessfulInlining(const MLInlineAdvice &Advice,
                            bool CalleeWasDeleted);

  /// Whether the advisor has stopped making ML decisions due to size growth.
  /// @return True if further ML-driven tracking and advice are disabled.
  bool isForcedToStop() const { return ForceStop; }
  /// Return the number of direct calls from \p F to defined functions.
  /// @param F Function whose local call count is requested.
  /// @return Count of direct calls to defined functions in \p F.
  int64_t getLocalCalls(Function &F);
  /// Return the model runner used to evaluate inlining decisions.
  /// @return Const reference to the owned ML model runner.
  const MLModelRunner &getModelRunner() const { return *ModelRunner; }
  /// Return (and cache) FunctionPropertiesInfo for function \p F.
  /// @param F Function whose properties are requested.
  /// @return Mutable reference to the cached function properties.
  FunctionPropertiesInfo &getCachedFPI(Function &F) const;
  /// Return the tensor feature map used by this advisor instance.
  /// @return Feature map describing model input tensors.
  const std::vector<TensorSpec> &getFeatureMap() const { return FeatureMap; };
  /// Return the default (pre-IR2Vec) feature map shared by ML advisors.
  /// @return Reference to the initial feature map of TensorSpecs.
  static const std::vector<TensorSpec> &getInitialFeatureMap();

protected:
  /// Compute ML or fallback advice for call site \p CB.
  /// @param CB Direct call site to advise on.
  /// @return Advice recommending whether to inline \p CB.
  std::unique_ptr<InlineAdvice> getAdviceImpl(CallBase &CB) override;

  /// Compute mandatory advice for call site \p CB, tracking ML state when needed.
  /// @param CB Call site to evaluate for mandatory inlining.
  /// @param Advice Whether mandatory inlining is recommended.
  /// @return Advice capturing the mandatory inlining decision.
  std::unique_ptr<InlineAdvice> getMandatoryAdvice(CallBase &CB,
                                                   bool Advice) override;

  /// Build ML advice that recommends mandatory inlining of call site \p CB.
  /// @param CB Call site that must be inlined.
  /// @return ML advice recommending inlining of \p CB.
  virtual std::unique_ptr<MLInlineAdvice> getMandatoryAdviceImpl(CallBase &CB);

  /// Evaluate the model and build ML advice for call site \p CB.
  /// @param CB Direct call site to advise on.
  /// @param ORE Remark emitter used to report inlining outcomes.
  /// @return ML advice based on the model evaluation for \p CB.
  virtual std::unique_ptr<MLInlineAdvice>
  getAdviceFromModel(CallBase &CB, OptimizationRemarkEmitter &ORE);

  /// Get the initial call-graph level of \p F, or 0 if introduced afterwards.
  ///
  /// TODO: should we keep this updated?
  /// @param F Function whose initial SCC height/level is requested.
  /// @return Initial level recorded at advisor construction, or 0.
  unsigned getInitialFunctionLevel(const Function &F) const;

  /// Model runner that evaluates inlining decisions from feature tensors.
  std::unique_ptr<MLModelRunner> ModelRunner;
  /// Fallback that returns the default heuristic inlining decision for a call.
  std::function<bool(CallBase &)> GetDefaultAdvice;
  /// Tensor feature map describing the inputs expected by \p ModelRunner.
  std::vector<TensorSpec> FeatureMap;

private:
  int64_t getModuleIRSize() const;
  std::unique_ptr<InlineAdvice>
  getSkipAdviceIfUnreachableCallsite(CallBase &CB);
  void print(raw_ostream &OS) const override;

  // Using std::map to benefit from its iterator / reference non-invalidating
  // semantics, which make it easy to use `getCachedFPI` results from multiple
  // calls without needing to copy to avoid invalidation effects.
  mutable std::map<const Function *, FunctionPropertiesInfo> FPICache;

  LazyCallGraph &CG;

  int64_t NodeCount = 0;
  int64_t EdgeCount = 0;
  int64_t EdgesOfLastSeenNodes = 0;
  const bool UseIR2Vec;

  std::map<const LazyCallGraph::Node *, unsigned> FunctionLevels;
  const int32_t InitialIRSize = 0;
  int32_t CurrentIRSize = 0;
  llvm::SmallPtrSet<const LazyCallGraph::Node *, 1> NodesInLastSCC;
  DenseSet<const LazyCallGraph::Node *> AllNodes;
  DenseSet<Function *> DeadFunctions;
  bool ForceStop = false;
  ProfileSummaryInfo &PSI;
};

/// InlineAdvice that tracks changes post inlining. For that reason, it only
/// overrides the "successful inlining" extension points.
class LLVM_ABI MLInlineAdvice : public InlineAdvice {
public:
  /// Construct ML advice for call site \p CB with recommendation \p Recommendation.
  /// @param Advisor Owning ML inline advisor that produced this advice.
  /// @param CB Call site the advice applies to.
  /// @param ORE Remark emitter used to report inlining outcomes.
  /// @param Recommendation Whether inlining is recommended for \p CB.
  MLInlineAdvice(MLInlineAdvisor *Advisor, CallBase &CB,
                 OptimizationRemarkEmitter &ORE, bool Recommendation);
  /// Destroy ML advice after the inliner has recorded its decision.
  ~MLInlineAdvice() override = default;

  /// Record a successful inlining that did not delete the callee.
  void recordInliningImpl() override;
  /// Record a successful inlining that left the callee delete-able.
  void recordInliningWithCalleeDeletedImpl() override;
  /// Record that inlining was attempted but unsuccessful.
  /// @param Result Why inlining was not performed.
  void recordUnsuccessfulInliningImpl(const InlineResult &Result) override;
  /// Record that inlining was not attempted for this call site.
  void recordUnattemptedInliningImpl() override;

  /// Return the caller function captured before inlining.
  /// @return Caller function of the original call site.
  Function *getCaller() const { return Caller; }
  /// Return the callee function captured before inlining.
  /// @return Callee function of the original call site.
  Function *getCallee() const { return Callee; }

  /// IR size of the caller before this inlining attempt.
  const int64_t CallerIRSize;
  /// IR size of the callee before this inlining attempt.
  const int64_t CalleeIRSize;
  /// Sum of local call edges from caller and callee before inlining.
  const int64_t CallerAndCalleeEdges;
  /// Finish updating the cached caller FunctionPropertiesInfo after inlining.
  /// @param FAM Function analysis manager used to complete the FPI update.
  void updateCachedCallerFPI(FunctionAnalysisManager &FAM) const;

private:
  void reportContextForRemark(DiagnosticInfoOptimizationBase &OR);
  MLInlineAdvisor *getAdvisor() const {
    return static_cast<MLInlineAdvisor *>(Advisor);
  };
  // Make a copy of the FPI of the caller right before inlining. If inlining
  // fails, we can just update the cache with that value.
  const FunctionPropertiesInfo PreInlineCallerFPI;
  std::optional<FunctionPropertiesUpdater> FPU;
};

} // namespace llvm

#endif // LLVM_ANALYSIS_MLINLINEADVISOR_H
