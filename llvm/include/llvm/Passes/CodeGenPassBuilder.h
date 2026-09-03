//===- Construction of codegen pass pipelines ------------------*- C++ -*--===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Interfaces for producing common pass manager configurations.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_PASSES_CODEGENPASSBUILDER_H
#define LLVM_PASSES_CODEGENPASSBUILDER_H

#include "llvm/ADT/FunctionExtras.h"
#include "llvm/ADT/STLForwardCompat.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include "llvm/Target/CGPassBuilderOption.h"
#include "llvm/Target/TargetMachine.h"
#include <cassert>
#include <utility>

namespace llvm {

// FIXME: Dummy target independent passes definitions that have not yet been
// ported to new pass manager. Once they do, remove these.
#define DUMMY_FUNCTION_PASS(NAME, PASS_NAME)                                   \
  struct PASS_NAME : public OptionalPassInfoMixin<PASS_NAME> {                 \
    template <typename... Ts> PASS_NAME(Ts &&...) {}                           \
    PreservedAnalyses run(Function &, FunctionAnalysisManager &) {             \
      return PreservedAnalyses::all();                                         \
    }                                                                          \
  };
#define DUMMY_MACHINE_MODULE_PASS(NAME, PASS_NAME)                             \
  struct PASS_NAME : public OptionalPassInfoMixin<PASS_NAME> {                 \
    template <typename... Ts> PASS_NAME(Ts &&...) {}                           \
    PreservedAnalyses run(Module &, ModuleAnalysisManager &) {                 \
      return PreservedAnalyses::all();                                         \
    }                                                                          \
  };
#define DUMMY_MACHINE_FUNCTION_PASS(NAME, PASS_NAME)                           \
  struct PASS_NAME : public OptionalPassInfoMixin<PASS_NAME> {                 \
    template <typename... Ts> PASS_NAME(Ts &&...) {}                           \
    PreservedAnalyses run(MachineFunction &,                                   \
                          MachineFunctionAnalysisManager &) {                  \
      return PreservedAnalyses::all();                                         \
    }                                                                          \
  };
#include "llvm/Passes/MachinePassRegistry.def"

/// Wrapper holding module, function, and machine-function pass managers.
///
/// Used by CodeGenPassBuilder while assembling the codegen pipeline. Only
/// CodeGenPassBuilder may construct it.
class PassManagerWrapper {
private:
  PassManagerWrapper(ModulePassManager &ModulePM) : MPM(ModulePM) {};

  ModulePassManager &MPM;
  FunctionPassManager FPM;
  MachineFunctionPassManager MFPM;

  friend class CodeGenPassBuilder;
};

/// This class provides access to building LLVM's passes.
///
/// Its members provide the baseline state available to passes during their
/// construction. The \c MachinePassRegistry.def file specifies how to construct
/// all of the built-in passes, and those may reference these members during
/// construction.
///
/// Targets customize the pipeline by deriving from this class and overriding
/// the virtual add* hooks below: the add%Stage hooks replace a whole stage of
/// the pipeline, while the addPre%Stage / addPost%Stage hooks inject passes
/// around one. See addMachinePasses for how they fit together.
///
/// Dispatch is virtual rather than templated on the derived builder so that the
/// target-independent pipeline is emitted once for the whole build instead of
/// once per target.
class LLVM_ABI CodeGenPassBuilder {
public:
  /// Construct a codegen pass builder for \p TM.
  /// \param TM Target machine owning codegen options and the target.
  /// \param Opts Codegen pass builder options.
  /// \param PIC Optional pass instrumentation callbacks, or null.
  CodeGenPassBuilder(TargetMachine &TM, const CGPassBuilderOption &Opts,
                     PassInstrumentationCallbacks *PIC);
  /// Deleted copy constructor.
  /// \param Other Unused; copy construction is deleted.
  CodeGenPassBuilder(const CodeGenPassBuilder &Other) = delete;
  /// Deleted copy assignment operator.
  /// \param Other Unused; copy assignment is deleted.
  CodeGenPassBuilder &operator=(const CodeGenPassBuilder &Other) = delete;
  /// Destroy this codegen pass builder.
  virtual ~CodeGenPassBuilder();

  /// Build the complete codegen pipeline into \p MPM.
  /// \param MPM Module pass manager to populate.
  /// \param MAM Module analysis manager for the pipeline.
  /// \param Out Primary output stream for generated code.
  /// \param DwoOut Optional DWO output stream, or null.
  /// \param FileType Kind of codegen output file to emit.
  /// \param Ctx Machine-code context used during emission.
  /// \return Success, or an error if pipeline construction fails.
  Error buildPipeline(ModulePassManager &MPM, ModuleAnalysisManager &MAM,
                      raw_pwrite_stream &Out, raw_pwrite_stream *DwoOut,
                      CodeGenFileType FileType, MCContext &Ctx);

  /// Return the pass instrumentation callbacks, or null if none were provided.
  /// \return The pass instrumentation callbacks, or null if none were provided.
  PassInstrumentationCallbacks *getPassInstrumentationCallbacks() const {
    return PIC;
  }

protected:
  /// Detects whether \c PassT is a module pass via its run signature.
  template <typename PassT>
  using is_module_pass_t = decltype(std::declval<PassT &>().run(
      std::declval<Module &>(), std::declval<ModuleAnalysisManager &>()));

  /// Detects whether \c PassT is a function pass via its run signature.
  template <typename PassT>
  using is_function_pass_t = decltype(std::declval<PassT &>().run(
      std::declval<Function &>(), std::declval<FunctionAnalysisManager &>()));

  /// Detects whether \c PassT is a machine-function pass via its run signature.
  template <typename PassT>
  using is_machine_function_pass_t = decltype(std::declval<PassT &>().run(
      std::declval<MachineFunction &>(),
      std::declval<MachineFunctionAnalysisManager &>()));

  /// Add a function pass to the current function pipeline in \p PMW.
  /// \param Pass Pass instance to add.
  /// \param PMW Pass manager wrapper receiving the pass.
  /// \param Force When true, add even if start/stop filters would skip it.
  /// \param Name Pass name used for start/stop and disable filtering.
  template <typename PassT>
  void addFunctionPass(PassT &&Pass, PassManagerWrapper &PMW,
                       bool Force = false, StringRef Name = PassT::name()) {
    static_assert(is_detected<is_function_pass_t, PassT>::value &&
                  "Only function passes are supported.");
    if (!Force && !runBeforeAdding(Name))
      return;
    PMW.FPM.addPass(std::forward<PassT>(Pass));
  }

  /// Add a module pass to the module pipeline in \p PMW.
  /// \param Pass Pass instance to add.
  /// \param PMW Pass manager wrapper receiving the pass.
  /// \param Force When true, add even if start/stop filters would skip it.
  /// \param Name Pass name used for start/stop and disable filtering.
  template <typename PassT>
  void addModulePass(PassT &&Pass, PassManagerWrapper &PMW, bool Force = false,
                     StringRef Name = PassT::name()) {
    static_assert(is_detected<is_module_pass_t, PassT>::value &&
                  "Only module passes are suported.");
    assert(PMW.FPM.isEmpty() && PMW.MFPM.isEmpty() &&
           "You cannot insert a module pass without first flushing the current "
           "function pipelines to the module pipeline.");
    if (!Force && !runBeforeAdding(Name))
      return;
    PMW.MPM.addPass(std::forward<PassT>(Pass));
  }

  /// Add a machine-function pass to the current MF pipeline in \p PMW.
  /// \param Pass Pass instance to add.
  /// \param PMW Pass manager wrapper receiving the pass.
  /// \param Force When true, add even if start/stop filters would skip it.
  /// \param Name Pass name used for start/stop and disable filtering.
  template <typename PassT>
  void addMachineFunctionPass(PassT &&Pass, PassManagerWrapper &PMW,
                              bool Force = false,
                              StringRef Name = PassT::name()) {
    static_assert(is_detected<is_machine_function_pass_t, PassT>::value &&
                  "Only machine function passes are supported.");

    if (!Force && !runBeforeAdding(Name))
      return;
    PMW.MFPM.addPass(std::forward<PassT>(Pass));
    for (auto &C : AfterCallbacks)
      C(Name, PMW.MFPM);
  }

  /// Flush pending function and machine-function pipelines into the module PM.
  /// \param PMW Pass manager wrapper whose nested pipelines are flushed.
  /// \param FreeMachineFunctions When true, free machine functions after flush.
  void flushFPMsToMPM(PassManagerWrapper &PMW,
                      bool FreeMachineFunctions = false);

  /// Require that subsequent function passes be added in CGSCC order.
  /// \param PMW Pass manager wrapper; nested function pipelines must be empty.
  void requireCGSCCOrder(PassManagerWrapper &PMW) {
    assert(!AddInCGSCCOrder);
    assert(PMW.FPM.isEmpty() && PMW.MFPM.isEmpty() &&
           "Requiring CGSCC ordering requires flushing the current function "
           "pipelines to the MPM.");
    AddInCGSCCOrder = true;
  }

  /// Stop adding subsequent function passes in CGSCC order.
  /// \param PMW Pass manager wrapper; nested function pipelines must be empty.
  void stopAddingInCGSCCOrder(PassManagerWrapper &PMW) {
    assert(AddInCGSCCOrder);
    assert(PMW.FPM.isEmpty() && PMW.MFPM.isEmpty() &&
           "Stopping CGSCC ordering requires flushing the current function "
           "pipelines to the MPM.");
    AddInCGSCCOrder = false;
  }

  /// Target machine for this codegen pipeline.
  TargetMachine &TM;
  /// Options controlling codegen pass selection and behavior.
  CGPassBuilderOption Opt;
  /// Optional pass instrumentation callbacks, or null.
  PassInstrumentationCallbacks *PIC;

  /// Return the optimization level from the target machine.
  /// \return The optimization level configured on the target machine.
  CodeGenOptLevel getOptLevel() const { return TM.getOptLevel(); }

  /// Check whether or not GlobalISel should abort on error.
  /// When this is disabled, GlobalISel will fall back on SDISel instead of
  /// erroring out.
  /// \return True if GlobalISel should abort on error.
  bool isGlobalISelAbortEnabled() const {
    return TM.Options.GlobalISelAbort == GlobalISelAbortMode::Enable;
  }

  /// Return whether GlobalISel fallback should emit a diagnostic.
  ///
  /// In other words, it will emit a diagnostic when GlobalISel failed and
  /// isGlobalISelAbortEnabled is false.
  /// \return True if GlobalISel fallback should emit a diagnostic.
  bool reportDiagnosticWhenGlobalISelFallback() const {
    return TM.Options.GlobalISelAbort == GlobalISelAbortMode::DisableWithDiag;
  }

  /// Install an instruction selector that converts LLVM IR to machine code.
  /// \param PMW Pass manager wrapper receiving the selector pass.
  /// \return Success, or an error if the target does not provide a selector.
  virtual Error addInstSelector(PassManagerWrapper &PMW);

  /// Add GlobalMergePass before all IR passes.
  /// \param PMW Pass manager wrapper receiving the GlobalMerge pass.
  virtual void addGlobalMergePass(PassManagerWrapper &PMW) {}

  /// Add passes that optimize ILP for out-of-order targets.
  ///
  /// These passes are run while the machine code is still in SSA form, so they
  /// can use MachineTraceMetrics to control their heuristics.
  ///
  /// All passes added here should preserve the MachineDominatorTree,
  /// MachineLoopInfo, and MachineTraceMetrics analyses.
  /// \param PMW Pass manager wrapper receiving the ILP optimization passes.
  virtual void addILPOpts(PassManagerWrapper &PMW) {}

  /// Add passes immediately before register allocation.
  /// \param PMW Pass manager wrapper receiving the pre-regalloc passes.
  virtual void addPreRegAlloc(PassManagerWrapper &PMW) {}

  /// Add passes after optimized regalloc but before virtual-register rewrite.
  ///
  /// These passes must preserve VirtRegMap and LiveIntervals, and when running
  /// after RABasic or RAGreedy, they should take advantage of LiveRegMatrix.
  /// When these passes run, VirtRegMap contains legal physreg assignments for
  /// all virtual registers.
  ///
  /// Note if the target overloads addRegAssignAndRewriteOptimized, this may not
  /// be honored. This is also not generally used for the fast variant,
  /// where the allocation and rewriting are done in one pass.
  /// \param PMW Pass manager wrapper receiving the pre-rewrite passes.
  virtual void addPreRewrite(PassManagerWrapper &PMW) {}

  /// Add passes immediately after virtual registers are rewritten to physical.
  /// \param PMW Pass manager wrapper receiving the post-rewrite passes.
  virtual void addPostRewrite(PassManagerWrapper &PMW) {}

  /// Add passes after register allocation but before prolog-epilog insertion.
  /// \param PMW Pass manager wrapper receiving the post-regalloc passes.
  virtual void addPostRegAlloc(PassManagerWrapper &PMW) {}

  /// Add passes after prolog-epilog insertion and before the second scheduler.
  /// \param PMW Pass manager wrapper receiving the pre-sched2 passes.
  virtual void addPreSched2(PassManagerWrapper &PMW) {}

  /// Add passes immediately before machine code is emitted.
  /// \param PMW Pass manager wrapper receiving the pre-emit passes.
  virtual void addPreEmitPass(PassManagerWrapper &PMW) {}

  /// Add passes immediately before emission, later than `addPreEmitPass`.
  /// \param PMW Pass manager wrapper receiving the late pre-emit passes.
  // FIXME: Rename `addPreEmitPass` to something more sensible given its actual
  // position and remove the `2` suffix here as this callback is what
  // `addPreEmitPass` *should* be but in reality isn't.
  virtual void addPreEmitPass2(PassManagerWrapper &PMW) {}

  /// {{@ For GlobalISel
  ///

  /// Add last-minute LLVM IR passes just before instruction selection.
  /// \param PMW Pass manager wrapper receiving the pre-ISel IR passes.
  virtual void addPreISel(PassManagerWrapper &PMW) {}

  /// Install an IR translator from LLVM IR to possibly generic machine IR.
  /// \param PMW Pass manager wrapper receiving the IR translator pass.
  /// \return Success, or an error if the target does not provide a translator.
  virtual Error addIRTranslator(PassManagerWrapper &PMW);

  /// Add passes immediately before legalization.
  /// \param PMW Pass manager wrapper receiving the pre-legalize passes.
  virtual void addPreLegalizeMachineIR(PassManagerWrapper &PMW) {}

  /// Install a legalize pass for target-selectable instruction sequences.
  /// \param PMW Pass manager wrapper receiving the legalize pass.
  /// \return Success, or an error if the target does not provide a legalizer.
  virtual Error addLegalizeMachineIR(PassManagerWrapper &PMW);

  /// Add passes immediately before register bank selection.
  /// \param PMW Pass manager wrapper receiving the pre-regbankselect passes.
  virtual void addPreRegBankSelect(PassManagerWrapper &PMW) {}

  /// Install a register bank selector for unconstrained virtual registers.
  /// \param PMW Pass manager wrapper receiving the regbankselect pass.
  /// \return Success, or an error if the target does not provide regbankselect.
  virtual Error addRegBankSelect(PassManagerWrapper &PMW);

  /// Add passes immediately before global instruction selection.
  /// \param PMW Pass manager wrapper receiving the pre-GISel passes.
  virtual void addPreGlobalInstructionSelect(PassManagerWrapper &PMW) {}

  /// Install a global instruction selector that constrains generic vregs.
  ///
  /// Converts possibly generic instructions to fully target-specific
  /// instructions, thereby constraining all generic virtual registers to
  /// register classes.
  /// \param PMW Pass manager wrapper receiving the global ISel pass.
  /// \return Success, or an error if the target does not provide global ISel.
  virtual Error addGlobalInstructionSelect(PassManagerWrapper &PMW);
  /// @}}

  /// Add all passes that lower LLVM IR to the machine-instruction form.
  ///
  /// Adds IR-based lowering and target-specific optimization passes and finally
  /// the core instruction selection passes.
  /// \param PMW Pass manager wrapper receiving the ISel pipeline.
  void addISelPasses(PassManagerWrapper &PMW);

  /// Add the actual instruction selection passes, excluding IR preparation.
  /// \param PMW Pass manager wrapper receiving the core ISel passes.
  /// \return Success, or an error if adding the ISel passes fails.
  Error addCoreISelPasses(PassManagerWrapper &PMW);

  /// Add the complete, standard set of LLVM CodeGen passes.
  /// Fully developed targets will not generally override this.
  /// \param PMW Pass manager wrapper receiving the machine passes.
  /// \return Success, or an error if adding the machine passes fails.
  virtual Error addMachinePasses(PassManagerWrapper &PMW);

  /// Add passes to lower exception handling for the code generator.
  /// \param PMW Pass manager wrapper receiving the EH lowering passes.
  void addPassesToHandleExceptions(PassManagerWrapper &PMW);

  /// Add common IR-to-IR transforms after machine-independent optimization.
  /// \param PMW Pass manager wrapper receiving the IR codegen passes.
  virtual void addIRPasses(PassManagerWrapper &PMW);

  /// Add a pass to prepare LLVM IR for code generation before EH preparation.
  /// \param PMW Pass manager wrapper receiving the CodeGenPrepare pass.
  virtual void addCodeGenPrepare(PassManagerWrapper &PMW);

  /// Add common IR-to-IR transforms in preparation for instruction selection.
  /// \param PMW Pass manager wrapper receiving the ISel preparation passes.
  virtual void addISelPrepare(PassManagerWrapper &PMW);

  /// Methods with trivial inline returns are convenient points in the common
  /// codegen pass pipeline where targets may insert passes. Methods with
  /// out-of-line standard implementations are major CodeGen stages called by
  /// addMachinePasses. Some targets may override major stages when inserting
  /// passes is insufficient, but maintaining overriden stages is more work.
  ///

  /// Add standard passes that optimize machine instructions in SSA form.
  /// \param PMW Pass manager wrapper receiving the machine SSA opt passes.
  virtual void addMachineSSAOptimization(PassManagerWrapper &PMW);

  /// Add the minimum target-independent passes required for fast regalloc.
  /// \param PMW Pass manager wrapper receiving the fast regalloc passes.
  /// \return Success, or an error if adding the passes fails.
  virtual Error addFastRegAlloc(PassManagerWrapper &PMW);

  /// Add passes related to optimized register allocation.
  ///
  /// CodeGenTargetMachineImpl provides standard regalloc passes for most
  /// targets.
  /// \param PMW Pass manager wrapper receiving the optimized regalloc passes.
  /// \return Success, or an error if adding the passes fails.
  virtual Error addOptimizedRegAlloc(PassManagerWrapper &PMW);

  /// Add passes that optimize machine instructions after register allocation.
  /// \param PMW Pass manager wrapper receiving the late machine opt passes.
  virtual void addMachineLateOptimization(PassManagerWrapper &PMW);

  /// Add late codegen passes that analyze code for garbage collection.
  /// \param PMW Pass manager wrapper receiving the GC analysis passes.
  virtual void addGCPasses(PassManagerWrapper &PMW) {}

  /// Add standard basic block placement passes.
  /// \param PMW Pass manager wrapper receiving the block placement passes.
  virtual void addBlockPlacement(PassManagerWrapper &PMW);

  /// Add passes immediately after basic block sections are assigned.
  /// \param PMW Pass manager wrapper receiving the post-BB-sections passes.
  virtual void addPostBBSections(PassManagerWrapper &PMW) {}

  /// Add target-specific passes immediately before the AsmPrinter.
  /// \param PMW Pass manager wrapper receiving the pre-AsmPrinter passes.
  virtual void addAsmPrinterBegin(PassManagerWrapper &PMW);

  /// Add the target AsmPrinter that emits machine code or assembly.
  /// \param PMW Pass manager wrapper receiving the AsmPrinter pass.
  virtual void addAsmPrinter(PassManagerWrapper &PMW);

  /// Add target-specific passes immediately after the AsmPrinter.
  /// \param PMW Pass manager wrapper receiving the post-AsmPrinter passes.
  virtual void addAsmPrinterEnd(PassManagerWrapper &PMW);

  /// Utilities for targets to add passes to the pass manager.
  ///

  /// Create the register allocator pass for this target at the current opt level.
  /// \param PMW Pass manager wrapper receiving the register allocator.
  /// \param Optimized When true, select the optimized register allocator.
  virtual void addTargetRegisterAllocator(PassManagerWrapper &PMW,
                                          bool Optimized);

  /// Create the target-selected or overridden regalloc pass for addMachinePasses.
  /// \param PMW Pass manager wrapper receiving the register allocator.
  /// \param Optimized When true, select the optimized register allocator.
  void addRegAllocPass(PassManagerWrapper &PMW, bool Optimized);

  /// Add core regalloc passes that perform fast register assignment and rewrite.
  /// \param PMW Pass manager wrapper receiving the fast regassign/rewrite passes.
  /// \return Success, or an error if adding the passes fails.
  virtual Error addRegAssignAndRewriteFast(PassManagerWrapper &PMW);
  /// Add core regalloc passes that perform optimized assignment and rewrite.
  /// \param PMW Pass manager wrapper receiving the optimized regassign/rewrite passes.
  /// \return True if any passes were added, or an error on failure.
  virtual Expected<bool>
  addRegAssignAndRewriteOptimized(PassManagerWrapper &PMW);

  /// Allow the target to disable a specific pass by default.
  /// Backend can declare unwanted passes in constructor.
  template <typename... PassTs> void disablePass() {
    BeforeCallbacks.emplace_back(
        [](StringRef Name) { return ((Name != PassTs::name()) && ...); });
  }

  /// Insert InsertedPass pass after TargetPass pass.
  /// Only machine function passes are supported.
  /// \param Pass Pass instance to insert after \c TargetPassT.
  template <typename TargetPassT, typename InsertedPassT>
  void insertPass(InsertedPassT &&Pass) {
    AfterCallbacks.emplace_back(
        [&](StringRef Name, MachineFunctionPassManager &MFPM) mutable {
          if (Name == TargetPassT::name() &&
              runBeforeAdding(InsertedPassT::name())) {
            MFPM.addPass(std::forward<InsertedPassT>(Pass));
          }
        });
  }

private:
  bool runBeforeAdding(StringRef Name) {
    bool ShouldAdd = true;
    for (auto &C : BeforeCallbacks)
      ShouldAdd &= C(Name);
    return ShouldAdd;
  }

  void setStartStopPasses(const TargetPassConfig::StartStopInfo &Info);

  Error verifyStartStop(const TargetPassConfig::StartStopInfo &Info) const;

  SmallVector<llvm::unique_function<bool(StringRef)>, 4> BeforeCallbacks;
  SmallVector<
      llvm::unique_function<void(StringRef, MachineFunctionPassManager &)>, 4>
      AfterCallbacks;

  /// Helper variable for `-start-before/-start-after/-stop-before/-stop-after`
  bool Started = true;
  bool Stopped = true;
  bool AddInCGSCCOrder = false;
};

} // namespace llvm

#endif // LLVM_PASSES_CODEGENPASSBUILDER_H
