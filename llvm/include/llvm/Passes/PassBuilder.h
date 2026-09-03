//===- Parsing, selection, and construction of pass pipelines --*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Interfaces for registering analysis passes, producing common pass manager
/// configurations, and parsing of pass pipelines.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_PASSBUILDER_H
#define LLVM_PASSES_PASSBUILDER_H

#include "llvm/Analysis/CGSCCPassManager.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/RegAllocCommon.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Passes/OptimizationLevel.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/PGOOptions.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/IPO/Inliner.h"
#include "llvm/Transforms/IPO/ModuleInliner.h"
#include "llvm/Transforms/Scalar/LoopPassManager.h"
#include <optional>
#include <vector>

namespace llvm {
class StringRef;
class AAManager;
class TargetMachine;
class ModuleSummaryIndex;

/// Tunable parameters for passes in the default pipelines.
class PipelineTuningOptions {
public:
  /// Constructor sets pipeline tuning defaults based on cl::opts. Each option
  /// can be set in the PassBuilder when using a LLVM as a library.
  LLVM_ABI PipelineTuningOptions();

  /// Tuning option to set loop interleaving on/off, set based on opt level.
  bool LoopInterleaving;

  /// Tuning option to enable/disable loop vectorization, set based on opt
  /// level.
  bool LoopVectorization;

  /// Tuning option to enable/disable slp loop vectorization, set based on opt
  /// level.
  bool SLPVectorization;

  /// Tuning option to enable/disable loop unrolling. Its default value is true.
  bool LoopUnrolling;

  /// Tuning option to enable/disable loop interchange. Its default value is
  /// false.
  bool LoopInterchange;

  /// Tuning option to enable/disable loop fusion. Its default value is false.
  bool LoopFusion;

  /// Tuning option to forget all SCEV loops in LoopUnroll. Its default value
  /// is that of the flag: `-forget-scev-loop-unroll`.
  bool ForgetAllSCEVInLoopUnroll;

  /// Tuning option to cap the number of calls to retrive clobbering accesses in
  /// MemorySSA, in LICM.
  unsigned LicmMssaOptCap;

  /// Tuning option to disable promotion to scalars in LICM with MemorySSA, if
  /// the number of access is too large.
  unsigned LicmMssaNoAccForPromotionCap;

  /// Tuning option to enable/disable call graph profile. Its default value is
  /// that of the flag: `-enable-npm-call-graph-profile`.
  bool CallGraphProfile;

  /// Tuning option to enable the unified LTO pipeline.
  bool UnifiedLTO;

  /// Tuning option to enable/disable function merging. Its default value is
  /// false.
  bool MergeFunctions;

  /// Tuning option to override the default inliner threshold.
  int InlinerThreshold;

  /// Experimental option to eagerly invalidate more analyses.
  ///
  /// This has the potential to decrease max memory usage in exchange for more
  /// compile time. This may affect codegen due to either passes using analyses
  /// only when cached, or invalidating and recalculating an analysis that was
  /// stale/imprecise but still valid. Currently this invalidates all function
  /// analyses after various module->function or cgscc->function adaptors in the
  /// default pipelines.
  bool EagerlyInvalidateAnalyses;

  /// Tuning option to enable/disable speculative devirtualization.
  ///
  /// Its default value is false.
  bool DevirtualizeSpeculatively;
};

/// This class provides access to building LLVM's passes.
///
/// Its members provide the baseline state available to passes during their
/// construction. The \c PassRegistry.def file specifies how to construct all
/// of the built-in passes, and those may reference these members during
/// construction.
class PassBuilder {
  TargetMachine *TM;
  PipelineTuningOptions PTO;
  std::optional<PGOOptions> PGOOpt;
  PassInstrumentationCallbacks *PIC;
  IntrusiveRefCntPtr<vfs::FileSystem> FS;

public:
  /// A struct to capture parsed pass pipeline names.
  ///
  /// A pipeline is defined as a series of names, each of which may in itself
  /// recursively contain a nested pipeline. A name is either the name of a pass
  /// (e.g. "instcombine") or the name of a pipeline type (e.g. "cgscc"). If the
  /// name is the name of a pass, the InnerPipeline is empty, since passes
  /// cannot contain inner pipelines. See parsePassPipeline() for a more
  /// detailed description of the textual pipeline format.
  struct PipelineElement {
    /// Pass or pipeline type name (e.g. "instcombine" or "cgscc").
    StringRef Name;
    /// Nested pipeline elements when \c Name is a pipeline type; empty for passes.
    std::vector<PipelineElement> InnerPipeline;
  };

  /// Construct a pass builder with optional target and pipeline options.
  /// \param TM Optional target machine used when constructing target passes.
  /// \param PTO Tunable parameters for default pipelines.
  /// \param PGOOpt Optional profile-guided optimization options.
  /// \param PIC Optional pass instrumentation callbacks, or null.
  /// \param FS Virtual file system used when reading profile or remapping files.
  LLVM_ABI explicit PassBuilder(
      TargetMachine *TM = nullptr,
      PipelineTuningOptions PTO = PipelineTuningOptions(),
      std::optional<PGOOptions> PGOOpt = std::nullopt,
      PassInstrumentationCallbacks *PIC = nullptr,
      IntrusiveRefCntPtr<vfs::FileSystem> FS = vfs::getRealFileSystem());

  /// Cross register the analysis managers through their proxies.
  ///
  /// This is an interface that can be used to cross register each
  /// AnalysisManager with all the others analysis managers.
  /// \param LAM Loop analysis manager to cross-register.
  /// \param FAM Function analysis manager to cross-register.
  /// \param CGAM CGSCC analysis manager to cross-register.
  /// \param MAM Module analysis manager to cross-register.
  /// \param MFAM Optional machine-function analysis manager to cross-register.
  LLVM_ABI void
  crossRegisterProxies(LoopAnalysisManager &LAM, FunctionAnalysisManager &FAM,
                       CGSCCAnalysisManager &CGAM, ModuleAnalysisManager &MAM,
                       MachineFunctionAnalysisManager *MFAM = nullptr);

  /// Registers all available module analysis passes.
  ///
  /// This is an interface that can be used to populate a \c
  /// ModuleAnalysisManager with all registered module analyses. Callers can
  /// still manually register any additional analyses. Callers can also
  /// pre-register analyses and this will not override those.
  /// \param MAM Module analysis manager to populate.
  LLVM_ABI void registerModuleAnalyses(ModuleAnalysisManager &MAM);

  /// Registers all available CGSCC analysis passes.
  ///
  /// This is an interface that can be used to populate a \c CGSCCAnalysisManager
  /// with all registered CGSCC analyses. Callers can still manually register any
  /// additional analyses. Callers can also pre-register analyses and this will
  /// not override those.
  /// \param CGAM CGSCC analysis manager to populate.
  LLVM_ABI void registerCGSCCAnalyses(CGSCCAnalysisManager &CGAM);

  /// Registers all available function analysis passes.
  ///
  /// This is an interface that can be used to populate a \c
  /// FunctionAnalysisManager with all registered function analyses. Callers can
  /// still manually register any additional analyses. Callers can also
  /// pre-register analyses and this will not override those.
  /// \param FAM Function analysis manager to populate.
  LLVM_ABI void registerFunctionAnalyses(FunctionAnalysisManager &FAM);

  /// Registers all available loop analysis passes.
  ///
  /// This is an interface that can be used to populate a \c LoopAnalysisManager
  /// with all registered loop analyses. Callers can still manually register any
  /// additional analyses.
  /// \param LAM Loop analysis manager to populate.
  LLVM_ABI void registerLoopAnalyses(LoopAnalysisManager &LAM);

  /// Registers all available machine function analysis passes.
  ///
  /// This is an interface that can be used to populate a \c
  /// MachineFunctionAnalysisManager with all registered function analyses.
  /// Callers can still manually register any additional analyses. Callers can
  /// also pre-register analyses and this will not override those.
  /// \param MFAM Machine-function analysis manager to populate.
  LLVM_ABI void
  registerMachineFunctionAnalyses(MachineFunctionAnalysisManager &MFAM);

  /// Construct the core LLVM function canonicalization and simplification
  /// pipeline.
  ///
  /// This is a long pipeline and uses most of the per-function optimization
  /// passes in LLVM to canonicalize and simplify the IR. It is suitable to run
  /// repeatedly over the IR and is not expected to destroy important
  /// information about the semantics of the IR.
  ///
  /// Note that \p Level cannot be `O0` here. The pipelines produced are
  /// only intended for use when attempting to optimize code. If frontends
  /// require some transformations for semantic reasons, they should explicitly
  /// build them.
  ///
  /// \p Phase indicates the current ThinLTO phase.
  /// \param Level Optimization level; must not be \c O0.
  /// \param Phase Current ThinLTO or full LTO phase.
  /// \return A function pass manager with the simplification pipeline.
  LLVM_ABI FunctionPassManager buildFunctionSimplificationPipeline(
      OptimizationLevel Level, ThinOrFullLTOPhase Phase);

  /// Construct the core LLVM module canonicalization and simplification
  /// pipeline.
  ///
  /// This pipeline focuses on canonicalizing and simplifying the entire module
  /// of IR. Much like the function simplification pipeline above, it is
  /// suitable to run repeatedly over the IR and is not expected to destroy
  /// important information. It does, however, perform inlining and other
  /// heuristic based simplifications that are not strictly reversible.
  ///
  /// Note that \p Level cannot be `O0` here. The pipelines produced are
  /// only intended for use when attempting to optimize code. If frontends
  /// require some transformations for semantic reasons, they should explicitly
  /// build them.
  ///
  /// \p Phase indicates the current ThinLTO phase.
  /// \param Level Optimization level; must not be \c O0.
  /// \param Phase Current ThinLTO or full LTO phase.
  /// \return A module pass manager with the simplification pipeline.
  LLVM_ABI ModulePassManager buildModuleSimplificationPipeline(
      OptimizationLevel Level, ThinOrFullLTOPhase Phase);

  /// Construct the module pipeline that performs inlining as well as
  /// the inlining-driven cleanups.
  /// \param Level Optimization level controlling inliner thresholds.
  /// \param Phase Current ThinLTO or full LTO phase.
  /// \return A module inliner wrapper pass for \p Level and \p Phase.
  LLVM_ABI ModuleInlinerWrapperPass
  buildInlinerPipeline(OptimizationLevel Level, ThinOrFullLTOPhase Phase);

  /// Construct the module pipeline that performs inlining with
  /// module inliner pass.
  /// \param Level Optimization level controlling inliner thresholds.
  /// \param Phase Current ThinLTO or full LTO phase.
  /// \return A module pass manager with the module-inliner pipeline.
  LLVM_ABI ModulePassManager
  buildModuleInlinerPipeline(OptimizationLevel Level, ThinOrFullLTOPhase Phase);

  /// Construct the core LLVM module optimization pipeline.
  ///
  /// This pipeline focuses on optimizing the execution speed of the IR. It
  /// uses cost modeling and thresholds to balance code growth against runtime
  /// improvements. It includes vectorization and other information destroying
  /// transformations. It also cannot generally be run repeatedly on a module
  /// without potentially seriously regressing either runtime performance of
  /// the code or serious code size growth.
  ///
  /// Note that \p Level cannot be `O0` here. The pipelines produced are
  /// only intended for use when attempting to optimize code. If frontends
  /// require some transformations for semantic reasons, they should explicitly
  /// build them.
  /// \param Level Optimization level; must not be \c O0.
  /// \param LTOPhase Current ThinLTO or full LTO phase.
  /// \return A module pass manager with the optimization pipeline.
  LLVM_ABI ModulePassManager buildModuleOptimizationPipeline(
      OptimizationLevel Level, ThinOrFullLTOPhase LTOPhase);

  /// Build a per-module default optimization pipeline.
  ///
  /// This provides a good default optimization pipeline for per-module
  /// optimization and code generation without any link-time optimization. It
  /// typically correspond to frontend "-O[123]" options for optimization
  /// levels \c O1, \c O2 and \c O3 resp.
  /// \param Level Optimization level for the pipeline.
  /// \param Phase Current ThinLTO or full LTO phase, or \c None.
  /// \return A module pass manager with the per-module default pipeline.
  LLVM_ABI ModulePassManager buildPerModuleDefaultPipeline(
      OptimizationLevel Level,
      ThinOrFullLTOPhase Phase = ThinOrFullLTOPhase::None);

  /// Build a fat object default optimization pipeline.
  ///
  /// This builds a pipeline that runs the LTO/ThinLTO  pre-link pipeline, and
  /// emits a section containing the pre-link bitcode along side the object code
  /// generated in non-LTO compilation.
  /// \param Level Optimization level for the pipeline.
  /// \param ThinLTO When true, use the ThinLTO pre-link pipeline.
  /// \param EmitSummary When true, emit a module summary.
  /// \param Verify When true, verify IR after building the pipeline.
  /// \return A module pass manager with the fat LTO default pipeline.
  LLVM_ABI ModulePassManager buildFatLTODefaultPipeline(OptimizationLevel Level,
                                                        bool ThinLTO,
                                                        bool EmitSummary,
                                                        bool Verify = true);

  /// Build a pre-link, ThinLTO-targeting default optimization pipeline to
  /// a pass manager.
  ///
  /// This adds the pre-link optimizations tuned to prepare a module for
  /// a ThinLTO run. It works to minimize the IR which needs to be analyzed
  /// without making irreversible decisions which could be made better during
  /// the LTO run.
  /// \param Level Optimization level for the pipeline.
  /// \return A module pass manager with the ThinLTO pre-link pipeline.
  LLVM_ABI ModulePassManager
  buildThinLTOPreLinkDefaultPipeline(OptimizationLevel Level);

  /// Build a ThinLTO default optimization pipeline to a pass manager.
  ///
  /// This provides a good default optimization pipeline for link-time
  /// optimization and code generation. It is particularly tuned to fit well
  /// when IR coming into the LTO phase was first run through \c
  /// buildThinLTOPreLinkDefaultPipeline, and the two coordinate closely.
  /// \param Level Optimization level for the pipeline.
  /// \param ImportSummary Module summary index used for importing, or null.
  /// \return A module pass manager with the ThinLTO default pipeline.
  LLVM_ABI ModulePassManager buildThinLTODefaultPipeline(
      OptimizationLevel Level, const ModuleSummaryIndex *ImportSummary);

  /// Build a pre-link, LTO-targeting default optimization pipeline to a pass
  /// manager.
  ///
  /// This adds the pre-link optimizations tuned to work well with a later LTO
  /// run. It works to minimize the IR which needs to be analyzed without
  /// making irreversible decisions which could be made better during the LTO
  /// run.
  /// \param Level Optimization level for the pipeline.
  /// \return A module pass manager with the LTO pre-link pipeline.
  LLVM_ABI ModulePassManager
  buildLTOPreLinkDefaultPipeline(OptimizationLevel Level);

  /// Build an LTO default optimization pipeline to a pass manager.
  ///
  /// This provides a good default optimization pipeline for link-time
  /// optimization and code generation. It is particularly tuned to fit well
  /// when IR coming into the LTO phase was first run through \c
  /// buildLTOPreLinkDefaultPipeline, and the two coordinate closely.
  /// \param Level Optimization level for the pipeline.
  /// \param ExportSummary Module summary index for exporting, or null.
  /// \return A module pass manager with the LTO default pipeline.
  LLVM_ABI ModulePassManager buildLTODefaultPipeline(
      OptimizationLevel Level, ModuleSummaryIndex *ExportSummary);

  /// Build an O0 pipeline with the minimal semantically required passes.
  ///
  /// This should only be used for non-LTO and LTO pre-link pipelines.
  /// \param Level Optimization level; expected to be \c O0.
  /// \param Phase Current ThinLTO or full LTO phase, or \c None.
  /// \return A module pass manager with the O0 default pipeline.
  LLVM_ABI ModulePassManager
  buildO0DefaultPipeline(OptimizationLevel Level,
                         ThinOrFullLTOPhase Phase = ThinOrFullLTOPhase::None);

  /// Build the default `AAManager` with the default alias analysis pipeline
  /// registered.
  ///
  /// This also adds target-specific alias analyses registered via
  /// TargetMachine::registerDefaultAliasAnalyses().
  /// \return An AA manager with the default alias analysis pipeline.
  LLVM_ABI AAManager buildDefaultAAPipeline();

  /// Parse a textual pass pipeline description into a \c
  /// ModulePassManager.
  ///
  /// The format of the textual pass pipeline description looks something like:
  ///
  ///   module(function(instcombine,sroa),dce,cgscc(inliner,function(...)),...)
  ///
  /// Pass managers have ()s describing the nest structure of passes. All passes
  /// are comma separated. As a special shortcut, if the very first pass is not
  /// a module pass (as a module pass manager is), this will automatically form
  /// the shortest stack of pass managers that allow inserting that first pass.
  /// So, assuming function passes 'fpassN', CGSCC passes 'cgpassN', and loop
  /// passes 'lpassN', all of these are valid:
  ///
  ///   fpass1,fpass2,fpass3
  ///   cgpass1,cgpass2,cgpass3
  ///   lpass1,lpass2,lpass3
  ///
  /// And they are equivalent to the following (resp.):
  ///
  ///   module(function(fpass1,fpass2,fpass3))
  ///   module(cgscc(cgpass1,cgpass2,cgpass3))
  ///   module(function(loop(lpass1,lpass2,lpass3)))
  ///
  /// This shortcut is especially useful for debugging and testing small pass
  /// combinations.
  ///
  /// The sequence of passes aren't necessarily the exact same kind of pass.
  /// You can mix different levels implicitly if adaptor passes are defined to
  /// make them work. For example,
  ///
  ///   mpass1,fpass1,fpass2,mpass2,lpass1
  ///
  /// This pipeline uses only one pass manager: the top-level module manager.
  /// fpass1,fpass2 and lpass1 are added into the top-level module manager
  /// using only adaptor passes. No nested function/loop pass managers are
  /// added. The purpose is to allow easy pass testing when the user
  /// specifically want the pass to run under a adaptor directly. This is
  /// preferred when a pipeline is largely of one type, but one or just a few
  /// passes are of different types(See PassBuilder.cpp for examples).
  /// \param MPM Module pass manager to populate.
  /// \param PipelineText Textual pipeline description to parse.
  /// \return Success, or an error if the pipeline text cannot be parsed.
  LLVM_ABI Error parsePassPipeline(ModulePassManager &MPM,
                                   StringRef PipelineText);

  /// Parse a textual pass pipeline description into a CGSCC pass manager.
  ///
  /// Automatic deduction of an appropriate pass manager stack is not supported.
  /// For example, to insert a loop pass 'lpass' into a FunctionPassManager,
  /// this is the valid pipeline text:
  ///
  ///   function(lpass)
  /// \param CGPM CGSCC pass manager to populate.
  /// \param PipelineText Textual pipeline description to parse.
  /// \return Success, or an error if the pipeline text cannot be parsed.
  LLVM_ABI Error parsePassPipeline(CGSCCPassManager &CGPM,
                                   StringRef PipelineText);
  /// Parse a textual pass pipeline description into a function pass manager.
  ///
  /// Automatic deduction of an appropriate pass manager stack is not supported.
  /// \param FPM Function pass manager to populate.
  /// \param PipelineText Textual pipeline description to parse.
  /// \return Success, or an error if the pipeline text cannot be parsed.
  LLVM_ABI Error parsePassPipeline(FunctionPassManager &FPM,
                                   StringRef PipelineText);
  /// Parse a textual pass pipeline description into a loop pass manager.
  ///
  /// Automatic deduction of an appropriate pass manager stack is not supported.
  /// \param LPM Loop pass manager to populate.
  /// \param PipelineText Textual pipeline description to parse.
  /// \return Success, or an error if the pipeline text cannot be parsed.
  LLVM_ABI Error parsePassPipeline(LoopPassManager &LPM,
                                   StringRef PipelineText);

  /// Parse a textual MIR pipeline into a machine-function pass manager.
  ///
  /// The format of the textual machine pipeline is a comma separated list of
  /// machine pass names:
  ///
  ///   machine-funciton-pass,machine-module-pass,...
  ///
  /// There is no need to specify the pass nesting, and this function
  /// currently cannot handle the pass nesting.
  /// \param MFPM Machine-function pass manager to populate.
  /// \param PipelineText Textual MIR pipeline description to parse.
  /// \return Success, or an error if the pipeline text cannot be parsed.
  LLVM_ABI Error parsePassPipeline(MachineFunctionPassManager &MFPM,
                                   StringRef PipelineText);

  /// Parse a textual alias analysis pipeline into the provided AA manager.
  ///
  /// The format of the textual AA pipeline is a comma separated list of AA
  /// pass names:
  ///
  ///   basic-aa,globals-aa,...
  ///
  /// The AA manager is set up such that the provided alias analyses are tried
  /// in the order specified. See the \c AAManaager documentation for details
  /// about the logic used. This routine just provides the textual mapping
  /// between AA names and the analyses to register with the manager.
  ///
  /// Returns false if the text cannot be parsed cleanly. The specific state of
  /// the \p AA manager is unspecified if such an error is encountered and this
  /// returns false.
  /// \param AA Alias analysis manager to populate.
  /// \param PipelineText Textual AA pipeline description to parse.
  /// \return Success, or an error if the text cannot be parsed cleanly.
  LLVM_ABI Error parseAAPipeline(AAManager &AA, StringRef PipelineText);

  /// Parse RegAllocFilterName to get RegAllocFilterFunc.
  /// \param RegAllocFilterName Name of the register-allocation filter to parse.
  /// \return The matching filter function, or nullopt if the name is unknown.
  LLVM_ABI std::optional<RegAllocFilterFunc>
  parseRegAllocFilter(StringRef RegAllocFilterName);

  /// Print pass names.
  /// \param OS Stream to write pass names to.
  LLVM_ABI void printPassNames(raw_ostream &OS);

  /// Register a callback for a default optimizer pipeline extension
  /// point
  ///
  /// This extension point allows adding passes that perform peephole
  /// optimizations similar to the instruction combiner. These passes will be
  /// inserted after each instance of the instruction combiner pass.
  /// \param C Callback that receives a function pass manager and optimization level.
  void registerPeepholeEPCallback(
      const std::function<void(FunctionPassManager &, OptimizationLevel)> &C) {
    PeepholeEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension
  /// point
  ///
  /// This extension point allows adding late loop canonicalization and
  /// simplification passes. This is the last point in the loop optimization
  /// pipeline before loop deletion. Each pass added
  /// here must be an instance of LoopPass.
  /// This is the place to add passes that can remove loops, such as target-
  /// specific loop idiom recognition.
  /// \param C Callback that receives a loop pass manager and optimization level.
  void registerLateLoopOptimizationsEPCallback(
      const std::function<void(LoopPassManager &, OptimizationLevel)> &C) {
    LateLoopOptimizationsEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension
  /// point
  ///
  /// This extension point allows adding loop passes to the end of the loop
  /// optimizer.
  /// \param C Callback that receives a loop pass manager and optimization level.
  void registerLoopOptimizerEndEPCallback(
      const std::function<void(LoopPassManager &, OptimizationLevel)> &C) {
    LoopOptimizerEndEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension
  /// point
  ///
  /// This extension point allows adding optimization passes after most of the
  /// main optimizations, but before the last cleanup-ish optimizations.
  /// \param C Callback that receives a function pass manager and optimization level.
  void registerScalarOptimizerLateEPCallback(
      const std::function<void(FunctionPassManager &, OptimizationLevel)> &C) {
    ScalarOptimizerLateEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension
  /// point
  ///
  /// This extension point allows adding CallGraphSCC passes at the end of the
  /// main CallGraphSCC passes and before any function simplification passes run
  /// by CGPassManager.
  /// \param C Callback that receives a CGSCC pass manager and optimization level.
  void registerCGSCCOptimizerLateEPCallback(
      const std::function<void(CGSCCPassManager &, OptimizationLevel)> &C) {
    CGSCCOptimizerLateEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension
  /// point
  ///
  /// This extension point allows adding optimization passes before the
  /// vectorizer and other highly target specific optimization passes are
  /// executed.
  /// \param C Callback that receives a function pass manager and optimization level.
  void registerVectorizerStartEPCallback(
      const std::function<void(FunctionPassManager &, OptimizationLevel)> &C) {
    VectorizerStartEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension
  /// point
  ///
  /// This extension point allows adding optimization passes after the
  /// vectorizer and other highly target specific optimization passes are
  /// executed.
  /// \param C Callback that receives a function pass manager and optimization level.
  void registerVectorizerEndEPCallback(
      const std::function<void(FunctionPassManager &, OptimizationLevel)> &C) {
    VectorizerEndEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension point.
  ///
  /// This extension point allows adding optimization once at the start of the
  /// pipeline. This does not apply to 'backend' compiles (LTO and ThinLTO
  /// link-time pipelines).
  /// \param C Callback that receives a module pass manager and optimization level.
  void registerPipelineStartEPCallback(
      const std::function<void(ModulePassManager &, OptimizationLevel)> &C) {
    PipelineStartEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension point.
  ///
  /// This extension point allows adding optimization right after passes that do
  /// basic simplification of the input IR.
  /// \param C Callback that receives a module pass manager, optimization level, and LTO phase.
  void registerPipelineEarlySimplificationEPCallback(
      const std::function<void(ModulePassManager &, OptimizationLevel,
                               ThinOrFullLTOPhase)> &C) {
    PipelineEarlySimplificationEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension point
  ///
  /// This extension point allows adding optimizations before the function
  /// optimization pipeline.
  /// \param C Callback that receives a module pass manager, optimization level, and LTO phase.
  void registerOptimizerEarlyEPCallback(
      const std::function<void(ModulePassManager &, OptimizationLevel,
                               ThinOrFullLTOPhase Phase)> &C) {
    OptimizerEarlyEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension point
  ///
  /// This extension point allows adding optimizations at the very end of the
  /// function optimization pipeline.
  /// \param C Callback that receives a module pass manager, optimization level, and LTO phase.
  void registerOptimizerLastEPCallback(
      const std::function<void(ModulePassManager &, OptimizationLevel,
                               ThinOrFullLTOPhase)> &C) {
    OptimizerLastEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension point
  ///
  /// This extension point allows adding optimizations at the start of the full
  /// LTO pipeline.
  /// \param C Callback that receives a module pass manager and optimization level.
  void registerFullLinkTimeOptimizationEarlyEPCallback(
      const std::function<void(ModulePassManager &, OptimizationLevel)> &C) {
    FullLinkTimeOptimizationEarlyEPCallbacks.push_back(C);
  }

  /// Register a callback for a default optimizer pipeline extension point
  ///
  /// This extension point allows adding optimizations at the end of the full
  /// LTO pipeline.
  /// \param C Callback that receives a module pass manager and optimization level.
  void registerFullLinkTimeOptimizationLastEPCallback(
      const std::function<void(ModulePassManager &, OptimizationLevel)> &C) {
    FullLinkTimeOptimizationLastEPCallbacks.push_back(C);
  }

  /// Register a callback for parsing an AliasAnalysis Name to populate
  /// the given AAManager \p AA
  /// \param C Callback that maps an AA name onto registrations in an AAManager.
  void registerParseAACallback(
      const std::function<bool(StringRef Name, AAManager &AA)> &C) {
    AAParsingCallbacks.push_back(C);
  }

  /// Register a callback to register CGSCC analyses with this PassBuilder.
  ///
  /// Callees register their analyses with the given AnalysisManager objects.
  /// \param C Callback that registers analyses into a CGSCC analysis manager.
  void registerAnalysisRegistrationCallback(
      const std::function<void(CGSCCAnalysisManager &)> &C) {
    CGSCCAnalysisRegistrationCallbacks.push_back(C);
  }
  /// Register a callback to register function analyses with this PassBuilder.
  /// \param C Callback that registers analyses into a function analysis manager.
  void registerAnalysisRegistrationCallback(
      const std::function<void(FunctionAnalysisManager &)> &C) {
    FunctionAnalysisRegistrationCallbacks.push_back(C);
  }
  /// Register a callback to register loop analyses with this PassBuilder.
  /// \param C Callback that registers analyses into a loop analysis manager.
  void registerAnalysisRegistrationCallback(
      const std::function<void(LoopAnalysisManager &)> &C) {
    LoopAnalysisRegistrationCallbacks.push_back(C);
  }
  /// Register a callback to register module analyses with this PassBuilder.
  /// \param C Callback that registers analyses into a module analysis manager.
  void registerAnalysisRegistrationCallback(
      const std::function<void(ModuleAnalysisManager &)> &C) {
    ModuleAnalysisRegistrationCallbacks.push_back(C);
  }
  /// Register a callback to register machine-function analyses with this PassBuilder.
  /// \param C Callback that registers analyses into a machine-function analysis manager.
  void registerAnalysisRegistrationCallback(
      const std::function<void(MachineFunctionAnalysisManager &)> &C) {
    MachineFunctionAnalysisRegistrationCallbacks.push_back(C);
  }

  /// Register a CGSCC pipeline parsing callback with this pass builder.
  ///
  /// Using these callbacks, callers can parse both a single pass name, as well
  /// as entire sub-pipelines, and populate the PassManager instance
  /// accordingly.
  /// \param C Callback that parses a name into a CGSCC pass manager.
  void registerPipelineParsingCallback(
      const std::function<bool(StringRef Name, CGSCCPassManager &,
                               ArrayRef<PipelineElement>)> &C) {
    CGSCCPipelineParsingCallbacks.push_back(C);
  }
  /// Register a function pipeline parsing callback with this pass builder.
  /// \param C Callback that parses a name into a function pass manager.
  void registerPipelineParsingCallback(
      const std::function<bool(StringRef Name, FunctionPassManager &,
                               ArrayRef<PipelineElement>)> &C) {
    FunctionPipelineParsingCallbacks.push_back(C);
  }
  /// Register a loop pipeline parsing callback with this pass builder.
  /// \param C Callback that parses a name into a loop pass manager.
  void registerPipelineParsingCallback(
      const std::function<bool(StringRef Name, LoopPassManager &,
                               ArrayRef<PipelineElement>)> &C) {
    LoopPipelineParsingCallbacks.push_back(C);
  }
  /// Register a module pipeline parsing callback with this pass builder.
  /// \param C Callback that parses a name into a module pass manager.
  void registerPipelineParsingCallback(
      const std::function<bool(StringRef Name, ModulePassManager &,
                               ArrayRef<PipelineElement>)> &C) {
    ModulePipelineParsingCallbacks.push_back(C);
  }
  /// Register a machine-function pipeline parsing callback with this pass builder.
  /// \param C Callback that parses a name into a machine-function pass manager.
  void registerPipelineParsingCallback(
      const std::function<bool(StringRef Name, MachineFunctionPassManager &,
                               ArrayRef<PipelineElement>)> &C) {
    MachineFunctionPipelineParsingCallbacks.push_back(C);
  }

  /// Register callbacks to parse target specific filter field if regalloc pass
  /// needs it. E.g. AMDGPU requires regalloc passes can handle sgpr and vgpr
  /// separately.
  /// \param C Callback that maps a filter name to a RegAllocFilterFunc.
  void registerRegClassFilterParsingCallback(
      const std::function<RegAllocFilterFunc(StringRef)> &C) {
    RegClassFilterParsingCallbacks.push_back(C);
  }

  /// Register a callback for a top-level pipeline entry.
  ///
  /// If the PassManager type is not given at the top level of the pipeline
  /// text, this Callback should be used to determine the appropriate stack of
  /// PassManagers and populate the passed ModulePassManager.
  /// \param C Callback that populates a module pass manager from top-level elements.
  LLVM_ABI void registerParseTopLevelPipelineCallback(
      const std::function<bool(ModulePassManager &, ArrayRef<PipelineElement>)>
          &C);

  /// Add PGOInstrumenation passes for O0 only.
  /// \param MPM Module pass manager receiving the PGO instrumentation passes.
  /// \param RunProfileGen When true, generate a profile; otherwise use an existing one.
  /// \param IsCS When true, use context-sensitive PGO instrumentation.
  /// \param AtomicCounterUpdate When true, update profile counters atomically.
  /// \param ProfileFile Path to the profile file to read or write.
  /// \param ProfileRemappingFile Path to the profile remapping file, if any.
  LLVM_ABI void addPGOInstrPassesForO0(ModulePassManager &MPM,
                                       bool RunProfileGen, bool IsCS,
                                       bool AtomicCounterUpdate,
                                       std::string ProfileFile,
                                       std::string ProfileRemappingFile);

  /// Returns PIC. External libraries can use this to register pass
  /// instrumentation callbacks.
  /// \return The pass instrumentation callbacks, or null if none were provided.
  PassInstrumentationCallbacks *getPassInstrumentationCallbacks() const {
    return PIC;
  }

  /// Returns the virtual file system.
  /// \return The virtual file system used by this pass builder.
  IntrusiveRefCntPtr<vfs::FileSystem> getVirtualFileSystemPtr() const {
    return FS;
  }

  /// Invoke registered peephole extension-point callbacks.
  /// \param FPM Function pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void invokePeepholeEPCallbacks(FunctionPassManager &FPM,
                                          OptimizationLevel Level);
  /// Invoke registered late loop optimization extension-point callbacks.
  /// \param LPM Loop pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void invokeLateLoopOptimizationsEPCallbacks(LoopPassManager &LPM,
                                                       OptimizationLevel Level);
  /// Invoke registered loop-optimizer-end extension-point callbacks.
  /// \param LPM Loop pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void invokeLoopOptimizerEndEPCallbacks(LoopPassManager &LPM,
                                                  OptimizationLevel Level);
  /// Invoke registered late scalar optimizer extension-point callbacks.
  /// \param FPM Function pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void invokeScalarOptimizerLateEPCallbacks(FunctionPassManager &FPM,
                                                     OptimizationLevel Level);
  /// Invoke registered late CGSCC optimizer extension-point callbacks.
  /// \param CGPM CGSCC pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void invokeCGSCCOptimizerLateEPCallbacks(CGSCCPassManager &CGPM,
                                                    OptimizationLevel Level);
  /// Invoke registered vectorizer-start extension-point callbacks.
  /// \param FPM Function pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void invokeVectorizerStartEPCallbacks(FunctionPassManager &FPM,
                                                 OptimizationLevel Level);
  /// Invoke registered vectorizer-end extension-point callbacks.
  /// \param FPM Function pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void invokeVectorizerEndEPCallbacks(FunctionPassManager &FPM,
                                               OptimizationLevel Level);
  /// Invoke registered early optimizer extension-point callbacks.
  /// \param MPM Module pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  /// \param Phase Current ThinLTO or full LTO phase.
  LLVM_ABI void invokeOptimizerEarlyEPCallbacks(ModulePassManager &MPM,
                                                OptimizationLevel Level,
                                                ThinOrFullLTOPhase Phase);
  /// Invoke registered last optimizer extension-point callbacks.
  /// \param MPM Module pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  /// \param Phase Current ThinLTO or full LTO phase.
  LLVM_ABI void invokeOptimizerLastEPCallbacks(ModulePassManager &MPM,
                                               OptimizationLevel Level,
                                               ThinOrFullLTOPhase Phase);
  /// Invoke registered early full-LTO extension-point callbacks.
  /// \param MPM Module pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void
  invokeFullLinkTimeOptimizationEarlyEPCallbacks(ModulePassManager &MPM,
                                                 OptimizationLevel Level);
  /// Invoke registered last full-LTO extension-point callbacks.
  /// \param MPM Module pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void
  invokeFullLinkTimeOptimizationLastEPCallbacks(ModulePassManager &MPM,
                                                OptimizationLevel Level);
  /// Invoke registered pipeline-start extension-point callbacks.
  /// \param MPM Module pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  LLVM_ABI void invokePipelineStartEPCallbacks(ModulePassManager &MPM,
                                               OptimizationLevel Level);
  /// Invoke registered early simplification extension-point callbacks.
  /// \param MPM Module pass manager to extend.
  /// \param Level Optimization level passed to each callback.
  /// \param Phase Current ThinLTO or full LTO phase.
  LLVM_ABI void
  invokePipelineEarlySimplificationEPCallbacks(ModulePassManager &MPM,
                                               OptimizationLevel Level,
                                               ThinOrFullLTOPhase Phase);

  /// Return true if \p Name is \p PassName, optionally with `<...>` parameters.
  /// \param Name Candidate parameterized pass name from a textual pipeline.
  /// \param PassName Base pass name to match.
  /// \return True if \p Name is \p PassName, optionally with parameters.
  static bool checkParametrizedPassName(StringRef Name, StringRef PassName) {
    if (!Name.consume_front(PassName))
      return false;
    // normal pass name w/o parameters == default parameters
    if (Name.empty())
      return true;
    return Name.starts_with("<") && Name.ends_with(">");
  }

  /// This performs customized parsing of pass name with parameters.
  ///
  /// We do not need parametrization of passes in textual pipeline very often,
  /// yet on a rare occasion ability to specify parameters right there can be
  /// useful.
  ///
  /// \p Name - parameterized specification of a pass from a textual pipeline
  /// is a string in a form of :
  ///      PassName '<' parameter-list '>'
  ///
  /// Parameter list is being parsed by the parser callable argument, \p Parser,
  /// It takes a string-ref of parameters and returns either StringError or a
  /// parameter list in a form of a custom parameters type, all wrapped into
  /// Expected<> template class.
  ///
  /// \param Parser Callable that parses the parameter-list substring.
  /// \param Name Full parameterized pass specification from the pipeline text.
  /// \param PassName Base pass name expected as a prefix of \p Name.
  /// \return The parsed parameters on success, or a StringError on failure.
  template <typename ParametersParseCallableT>
  static auto parsePassParameters(ParametersParseCallableT &&Parser,
                                  StringRef Name, StringRef PassName)
      -> decltype(Parser(StringRef{})) {
    using ParametersT = typename decltype(Parser(StringRef{}))::value_type;

    StringRef Params = Name;
    if (!Params.consume_front(PassName)) {
      llvm_unreachable(
          "unable to strip pass name from parametrized pass specification");
    }
    if (!Params.empty() &&
        (!Params.consume_front("<") || !Params.consume_back(">"))) {
      llvm_unreachable("invalid format for parametrized pass name");
    }

    Expected<ParametersT> Result = Parser(Params);
    assert((Result || Result.template errorIsA<StringError>()) &&
           "Pass parameter parser can only return StringErrors.");
    return Result;
  }

  /// Handle passes only accept one bool-valued parameter.
  ///
  /// \return false when Params is empty.
  /// \param Params Parameter-list substring from a parameterized pass name.
  /// \param OptionName Name of the bool option to recognize.
  /// \param PassName Pass name used in diagnostic messages.
  LLVM_ABI static Expected<bool> parseSinglePassOption(StringRef Params,
                                                       StringRef OptionName,
                                                       StringRef PassName);

private:
  // O1 pass pipeline
  FunctionPassManager
  buildO1FunctionSimplificationPipeline(OptimizationLevel Level,
                                        ThinOrFullLTOPhase Phase);

  void addRequiredLTOPreLinkPasses(ModulePassManager &MPM);

  void addVectorPasses(OptimizationLevel Level, FunctionPassManager &FPM,
                       ThinOrFullLTOPhase LTOPhase);

  static std::optional<std::vector<PipelineElement>>
  parsePipelineText(StringRef Text);

  Error parseModulePass(ModulePassManager &MPM, const PipelineElement &E);
  Error parseCGSCCPass(CGSCCPassManager &CGPM, const PipelineElement &E);
  Error parseFunctionPass(FunctionPassManager &FPM, const PipelineElement &E);
  Error parseLoopPass(LoopPassManager &LPM, const PipelineElement &E);
  Error parseMachinePass(MachineFunctionPassManager &MFPM,
                         const PipelineElement &E);
  bool parseAAPassName(AAManager &AA, StringRef Name);

  Error parseMachinePassPipeline(MachineFunctionPassManager &MFPM,
                                 ArrayRef<PipelineElement> Pipeline);
  Error parseLoopPassPipeline(LoopPassManager &LPM,
                              ArrayRef<PipelineElement> Pipeline);
  Error parseFunctionPassPipeline(FunctionPassManager &FPM,
                                  ArrayRef<PipelineElement> Pipeline);
  Error parseCGSCCPassPipeline(CGSCCPassManager &CGPM,
                               ArrayRef<PipelineElement> Pipeline);
  Error parseModulePassPipeline(ModulePassManager &MPM,
                                ArrayRef<PipelineElement> Pipeline);

  // Adds passes to do pre-inlining and related cleanup passes before
  // profile instrumentation/matching (to enable better context sensitivity),
  // and for memprof to enable better matching with missing debug frames.
  void addPreInlinerPasses(ModulePassManager &MPM, OptimizationLevel Level,
                           ThinOrFullLTOPhase LTOPhase);

  void addPGOInstrPasses(ModulePassManager &MPM, OptimizationLevel Level,
                         bool RunProfileGen, bool IsCS,
                         bool AtomicCounterUpdate, std::string ProfileFile,
                         std::string ProfileRemappingFile);
  void addPostPGOLoopRotation(ModulePassManager &MPM, OptimizationLevel Level);

  bool isInstrumentedPGOUse() const;

  // Extension Point callbacks
  SmallVector<std::function<void(FunctionPassManager &, OptimizationLevel)>, 2>
      PeepholeEPCallbacks;
  SmallVector<std::function<void(LoopPassManager &, OptimizationLevel)>, 2>
      LateLoopOptimizationsEPCallbacks;
  SmallVector<std::function<void(LoopPassManager &, OptimizationLevel)>, 2>
      LoopOptimizerEndEPCallbacks;
  SmallVector<std::function<void(FunctionPassManager &, OptimizationLevel)>, 2>
      ScalarOptimizerLateEPCallbacks;
  SmallVector<std::function<void(CGSCCPassManager &, OptimizationLevel)>, 2>
      CGSCCOptimizerLateEPCallbacks;
  SmallVector<std::function<void(FunctionPassManager &, OptimizationLevel)>, 2>
      VectorizerStartEPCallbacks;
  SmallVector<std::function<void(FunctionPassManager &, OptimizationLevel)>, 2>
      VectorizerEndEPCallbacks;
  // Module callbacks
  SmallVector<std::function<void(ModulePassManager &, OptimizationLevel,
                                 ThinOrFullLTOPhase)>,
              2>
      OptimizerEarlyEPCallbacks;
  SmallVector<std::function<void(ModulePassManager &, OptimizationLevel,
                                 ThinOrFullLTOPhase)>,
              2>
      OptimizerLastEPCallbacks;
  SmallVector<std::function<void(ModulePassManager &, OptimizationLevel)>, 2>
      FullLinkTimeOptimizationEarlyEPCallbacks;
  SmallVector<std::function<void(ModulePassManager &, OptimizationLevel)>, 2>
      FullLinkTimeOptimizationLastEPCallbacks;
  SmallVector<std::function<void(ModulePassManager &, OptimizationLevel)>, 2>
      PipelineStartEPCallbacks;
  SmallVector<std::function<void(ModulePassManager &, OptimizationLevel,
                                 ThinOrFullLTOPhase)>,
              2>
      PipelineEarlySimplificationEPCallbacks;

  SmallVector<std::function<void(ModuleAnalysisManager &)>, 2>
      ModuleAnalysisRegistrationCallbacks;
  SmallVector<std::function<bool(StringRef, ModulePassManager &,
                                 ArrayRef<PipelineElement>)>,
              2>
      ModulePipelineParsingCallbacks;
  SmallVector<
      std::function<bool(ModulePassManager &, ArrayRef<PipelineElement>)>, 2>
      TopLevelPipelineParsingCallbacks;
  // CGSCC callbacks
  SmallVector<std::function<void(CGSCCAnalysisManager &)>, 2>
      CGSCCAnalysisRegistrationCallbacks;
  SmallVector<std::function<bool(StringRef, CGSCCPassManager &,
                                 ArrayRef<PipelineElement>)>,
              2>
      CGSCCPipelineParsingCallbacks;
  // Function callbacks
  SmallVector<std::function<void(FunctionAnalysisManager &)>, 2>
      FunctionAnalysisRegistrationCallbacks;
  SmallVector<std::function<bool(StringRef, FunctionPassManager &,
                                 ArrayRef<PipelineElement>)>,
              2>
      FunctionPipelineParsingCallbacks;
  // Loop callbacks
  SmallVector<std::function<void(LoopAnalysisManager &)>, 2>
      LoopAnalysisRegistrationCallbacks;
  SmallVector<std::function<bool(StringRef, LoopPassManager &,
                                 ArrayRef<PipelineElement>)>,
              2>
      LoopPipelineParsingCallbacks;
  // AA callbacks
  SmallVector<std::function<bool(StringRef Name, AAManager &AA)>, 2>
      AAParsingCallbacks;
  // Machine pass callbackcs
  SmallVector<std::function<void(MachineFunctionAnalysisManager &)>, 2>
      MachineFunctionAnalysisRegistrationCallbacks;
  SmallVector<std::function<bool(StringRef, MachineFunctionPassManager &,
                                 ArrayRef<PipelineElement>)>,
              2>
      MachineFunctionPipelineParsingCallbacks;
  // Callbacks to parse `filter` parameter in register allocation passes
  SmallVector<std::function<RegAllocFilterFunc(StringRef)>, 2>
      RegClassFilterParsingCallbacks;
};

/// Add require<> or invalidate<> utility passes for an analysis.
///
/// This utility template takes care of adding require<> and invalidate<>
/// passes for an analysis to a given \c PassManager. It is intended to be used
/// during parsing of a pass pipeline when parsing a single PipelineName.
/// When registering a new function analysis FancyAnalysis with the pass
/// pipeline name "fancy-analysis", a matching ParsePipelineCallback could look
/// like this:
///
/// static bool parseFunctionPipeline(StringRef Name, FunctionPassManager &FPM,
///                                   ArrayRef<PipelineElement> P) {
///   if (parseAnalysisUtilityPasses<FancyAnalysis>("fancy-analysis", Name,
///                                                 FPM))
///     return true;
///   return false;
/// }
/// \param AnalysisName Canonical analysis name to match inside require/invalidate.
/// \param PipelineName Full pipeline element name being parsed.
/// \param PM Pass manager that receives the require or invalidate pass.
/// \return True if \p PipelineName was a require or invalidate of \p AnalysisName.
template <typename AnalysisT, typename IRUnitT, typename AnalysisManagerT,
          typename... ExtraArgTs>
bool parseAnalysisUtilityPasses(
    StringRef AnalysisName, StringRef PipelineName,
    PassManager<IRUnitT, AnalysisManagerT, ExtraArgTs...> &PM) {
  if (!PipelineName.ends_with(">"))
    return false;
  // See if this is an invalidate<> pass name
  if (PipelineName.starts_with("invalidate<")) {
    PipelineName = PipelineName.substr(11, PipelineName.size() - 12);
    if (PipelineName != AnalysisName)
      return false;
    PM.addPass(InvalidateAnalysisPass<AnalysisT>());
    return true;
  }

  // See if this is a require<> pass name
  if (PipelineName.starts_with("require<")) {
    PipelineName = PipelineName.substr(8, PipelineName.size() - 9);
    if (PipelineName != AnalysisName)
      return false;
    PM.addPass(RequireAnalysisPass<AnalysisT, IRUnitT, AnalysisManagerT,
                                   ExtraArgTs...>());
    return true;
  }

  return false;
}

// These are special since they are only for testing purposes.

/// No-op module pass which does nothing.
struct NoOpModulePass : OptionalPassInfoMixin<NoOpModulePass> {
  /// Run this no-op pass on \p M.
  /// \param M Module to run on.
  /// \param AM Module analysis manager (unused).
  /// \return All analyses preserved.
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &AM) {
    return PreservedAnalyses::all();
  }
};

/// No-op module analysis.
class NoOpModuleAnalysis : public AnalysisInfoMixin<NoOpModuleAnalysis> {
  friend AnalysisInfoMixin<NoOpModuleAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Empty result produced by the no-op module analysis.
  struct Result {};
  /// Run this no-op analysis on \p M.
  /// \param M Module to analyze.
  /// \param AM Module analysis manager (unused).
  /// \return An empty analysis result.
  Result run(Module &M, ModuleAnalysisManager &AM) { return Result(); }
};

/// No-op CGSCC pass which does nothing.
struct NoOpCGSCCPass : OptionalPassInfoMixin<NoOpCGSCCPass> {
  /// Run this no-op pass on SCC \p C.
  /// \param C SCC to run on.
  /// \param AM CGSCC analysis manager (unused).
  /// \param CG Lazy call graph (unused).
  /// \param UR CGSCC update result (unused).
  /// \return All analyses preserved.
  PreservedAnalyses run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM,
                        LazyCallGraph &CG, CGSCCUpdateResult &UR) {
    return PreservedAnalyses::all();
  }
};

/// No-op CGSCC analysis.
class NoOpCGSCCAnalysis : public AnalysisInfoMixin<NoOpCGSCCAnalysis> {
  friend AnalysisInfoMixin<NoOpCGSCCAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Empty result produced by the no-op CGSCC analysis.
  struct Result {};
  /// Run this no-op analysis on SCC \p C.
  /// \param C SCC to analyze.
  /// \param AM CGSCC analysis manager (unused).
  /// \param G Lazy call graph (unused).
  /// \return An empty analysis result.
  Result run(LazyCallGraph::SCC &C, CGSCCAnalysisManager &AM, LazyCallGraph &G) {
    return Result();
  }
};

/// No-op function pass which does nothing.
struct NoOpFunctionPass : OptionalPassInfoMixin<NoOpFunctionPass> {
  /// Run this no-op pass on \p F.
  /// \param F Function to run on.
  /// \param AM Function analysis manager (unused).
  /// \return All analyses preserved.
  PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM) {
    return PreservedAnalyses::all();
  }
};

/// No-op function analysis.
class NoOpFunctionAnalysis : public AnalysisInfoMixin<NoOpFunctionAnalysis> {
  friend AnalysisInfoMixin<NoOpFunctionAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Empty result produced by the no-op function analysis.
  struct Result {};
  /// Run this no-op analysis on \p F.
  /// \param F Function to analyze.
  /// \param AM Function analysis manager (unused).
  /// \return An empty analysis result.
  Result run(Function &F, FunctionAnalysisManager &AM) { return Result(); }
};

/// No-op loop nest pass which does nothing.
struct NoOpLoopNestPass : OptionalPassInfoMixin<NoOpLoopNestPass> {
  /// Run this no-op pass on loop nest \p L.
  /// \param L Loop nest to run on.
  /// \param AM Loop analysis manager (unused).
  /// \param AR Standard loop analysis results (unused).
  /// \param U Loop pass manager updater (unused).
  /// \return All analyses preserved.
  PreservedAnalyses run(LoopNest &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U) {
    return PreservedAnalyses::all();
  }
};

/// No-op loop pass which does nothing.
struct NoOpLoopPass : OptionalPassInfoMixin<NoOpLoopPass> {
  /// Run this no-op pass on loop \p L.
  /// \param L Loop to run on.
  /// \param AM Loop analysis manager (unused).
  /// \param AR Standard loop analysis results (unused).
  /// \param U Loop pass manager updater (unused).
  /// \return All analyses preserved.
  PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                        LoopStandardAnalysisResults &AR, LPMUpdater &U) {
    return PreservedAnalyses::all();
  }
};

/// No-op machine function pass which does nothing.
struct NoOpMachineFunctionPass
    : public OptionalPassInfoMixin<NoOpMachineFunctionPass> {
  /// Run this no-op pass on \p MF.
  /// \param MF Machine function to run on.
  /// \param MFAM Machine-function analysis manager (unused).
  /// \return All analyses preserved.
  PreservedAnalyses run(MachineFunction &MF,
                        MachineFunctionAnalysisManager &MFAM) {
    return PreservedAnalyses::all();
  }
};

/// No-op loop analysis.
class NoOpLoopAnalysis : public AnalysisInfoMixin<NoOpLoopAnalysis> {
  friend AnalysisInfoMixin<NoOpLoopAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Empty result produced by the no-op loop analysis.
  struct Result {};
  /// Run this no-op analysis on loop \p L.
  /// \param L Loop to analyze.
  /// \param AM Loop analysis manager (unused).
  /// \param AR Standard loop analysis results (unused).
  /// \return An empty analysis result.
  Result run(Loop &L, LoopAnalysisManager &AM,
             LoopStandardAnalysisResults &AR) {
    return Result();
  }
};

/// Output format for printing pass pipelines.
enum class PrintPipelinePassesFormat {
  /// Print the pipeline as a flat textual list.
  Text,
  /// Print the pipeline as a nested tree.
  Tree,
};

/// Command-line parser for \c PrintPipelinePassesFormat option values.
struct PrintPipelinePassesFormatParser
    : public cl::parser<std::optional<PrintPipelinePassesFormat>> {
  /// Inherit the base option parser constructors.
  using cl::parser<std::optional<PrintPipelinePassesFormat>>::parser;
  /// Parse \p ArgValue into an optional \c PrintPipelinePassesFormat.
  /// \param O Command-line option being parsed.
  /// \param ArgName Name of the option argument.
  /// \param ArgValue Textual option value to parse.
  /// \param Val Destination for the parsed format, or nullopt on failure.
  /// \return False on success; true if \p ArgValue is invalid.
  LLVM_ABI bool parse(cl::Option &O, StringRef ArgName, StringRef ArgValue,
                      std::optional<PrintPipelinePassesFormat> &Val);
};

/// Common option used by multiple tools to print pipeline passes
LLVM_ABI extern cl::opt<std::optional<PrintPipelinePassesFormat>, false,
                        PrintPipelinePassesFormatParser>
    PrintPipelinePasses;

/// Print \p Pipeline to \p OS using the requested \p Format.
/// \param OS Stream to write the formatted pipeline to.
/// \param Pipeline Textual pipeline description to format.
/// \param Format Output format; defaults to flat text.
LLVM_ABI void printFormattedPipelinePasses(
    raw_ostream &OS, StringRef Pipeline,
    PrintPipelinePassesFormat Format = PrintPipelinePassesFormat::Text);
}

#endif
