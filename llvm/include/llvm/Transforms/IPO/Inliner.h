//===- Inliner.h - Inliner pass and infrastructure --------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_IPO_INLINER_H
#define LLVM_TRANSFORMS_IPO_INLINER_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/Analysis/InlineAdvisor.h"
#include "llvm/Analysis/InlineCost.h"
#include "llvm/Analysis/LazyCallGraph.h"
#include "llvm/Analysis/Utils/ImportedFunctionsInliningStatistics.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"

namespace llvm {

/// The inliner pass for the new pass manager.
///
/// This pass wires together the inlining utilities and the inline cost
/// analysis into a CGSCC pass. It considers every call in every function in
/// the SCC and tries to inline if profitable. It can be tuned with a number of
/// parameters to control what cost model is used and what tradeoffs are made
/// when making the decision.
///
/// It should be noted that the legacy inliners do considerably more than this
/// inliner pass does. They provide logic for manually merging allocas, and
/// doing considerable DCE including the DCE of dead functions. This pass makes
/// every attempt to be simpler. DCE of functions requires complex reasoning
/// about comdat groups, etc. Instead, it is expected that other more focused
/// passes be composed to achieve the same end result.
class InlinerPass : public OptionalPassInfoMixin<InlinerPass> {
public:
  /// Construct an inliner pass.
  ///
  /// \param OnlyMandatory If true, only process always-inline / never-inline
  /// decisions and skip cost-model heuristics.
  /// \param LTOPhase Thin/full LTO phase in which this pass runs.
  InlinerPass(bool OnlyMandatory = false,
              ThinOrFullLTOPhase LTOPhase = ThinOrFullLTOPhase::None)
      : OnlyMandatory(OnlyMandatory), LTOPhase(LTOPhase) {}
  /// Move-construct an inliner pass.
  ///
  /// \param Arg Pass instance to move from.
  InlinerPass(InlinerPass &&Arg) = default;

  /// Run the inliner over SCC \p C.
  ///
  /// \param C The SCC whose call sites are considered for inlining.
  /// \param AM The CGSCC analysis manager.
  /// \param CG The lazy call graph.
  /// \param UR The CGSCC update result.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(LazyCallGraph::SCC &C,
                                 CGSCCAnalysisManager &AM, LazyCallGraph &CG,
                                 CGSCCUpdateResult &UR);

  /// Print this pass's pipeline representation to \p OS.
  ///
  /// \param OS Stream to write the pipeline string to.
  /// \param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  InlineAdvisor &getAdvisor(const ModuleAnalysisManagerCGSCCProxy::Result &MAM,
                            FunctionAnalysisManager &FAM, Module &M);
  std::unique_ptr<InlineAdvisor> OwnedAdvisor;
  const bool OnlyMandatory;
  const ThinOrFullLTOPhase LTOPhase;
};

/// Module pass wrapping the inliner with a module-wide advisor.
///
/// This works in conjunction with the InlineAdvisorAnalysis to facilitate
/// inlining decisions taking into account module-wide state, that need to keep
/// track of inter-inliner pass runs, for a given module. An InlineAdvisor is
/// configured and kept alive for the duration of the
/// ModuleInlinerWrapperPass::run.
class ModuleInlinerWrapperPass
    : public OptionalPassInfoMixin<ModuleInlinerWrapperPass> {
public:
  /// Construct a module inliner wrapper pass.
  ///
  /// \param Params Inline cost-model parameters for the advisor.
  /// \param MandatoryFirst If true, run a mandatory-only inliner before the
  /// full inliner.
  /// \param IC Pipeline context identifying the inline driver and LTO phase.
  /// \param Mode How the InlineAdvisor makes inlining decisions.
  /// \param MaxDevirtIterations Maximum times to repeat the SCC pipeline after
  /// detecting newly-devirtualized calls; zero disables the repeater.
  LLVM_ABI ModuleInlinerWrapperPass(
      InlineParams Params = getInlineParams(), bool MandatoryFirst = true,
      InlineContext IC = {},
      InliningAdvisorMode Mode = InliningAdvisorMode::Default,
      unsigned MaxDevirtIterations = 0);
  /// Move-construct a module inliner wrapper pass.
  ///
  /// \param Arg Pass instance to move from.
  ModuleInlinerWrapperPass(ModuleInlinerWrapperPass &&Arg) = default;

  /// Run the wrapped inliner pipeline over the given module.
  ///
  /// \param M Module whose call graph is processed for inlining.
  /// \param AM Module analysis manager providing analyses for the pass.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM);

  /// Allow adding more CGSCC passes, besides inlining. This should be called
  /// before run is called, as part of pass pipeline building.
  ///
  /// \return The CGSCC pass manager owned by this wrapper.
  CGSCCPassManager &getPM() { return PM; }

  /// Add a module pass that runs before the CGSCC passes.
  ///
  /// \param Pass Module pass to add; moved into the early module pipeline.
  template <class T> void addModulePass(T Pass) {
    MPM.addPass(std::move(Pass));
  }

  /// Add a module pass that runs after the CGSCC passes.
  ///
  /// \param Pass Module pass to add; moved into the late module pipeline.
  template <class T> void addLateModulePass(T Pass) {
    AfterCGMPM.addPass(std::move(Pass));
  }

  /// Print this pass's pipeline representation to \p OS.
  ///
  /// \param OS Stream to write the pipeline string to.
  /// \param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);

private:
  const InlineParams Params;
  const InlineContext IC;
  const InliningAdvisorMode Mode;
  const unsigned MaxDevirtIterations;
  // TODO: Clean this up so we only have one ModulePassManager.
  CGSCCPassManager PM;
  ModulePassManager MPM;
  ModulePassManager AfterCGMPM;
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_IPO_INLINER_H
