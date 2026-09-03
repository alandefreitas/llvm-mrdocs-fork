//===- TargetPassConfig.h - Code Generation pass options --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
/// Target-Independent Code Generator Pass Configuration Options pass.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TARGETPASSCONFIG_H
#define LLVM_CODEGEN_TARGETPASSCONFIG_H

#include "llvm/Pass.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/Error.h"
#include <cassert>
#include <string>

namespace llvm {

class TargetMachine;
/// Opaque implementation details for \c TargetPassConfig.
class PassConfigImpl;
class CSEConfigBase;
class PassInstrumentationCallbacks;

/// Contains the legacy (old) pass manager infrastructure.
namespace legacy {

class PassManagerBase;

} // end namespace legacy

/// Alias for the legacy pass manager base class.
using legacy::PassManagerBase;

/// Discriminated union of Pass ID types.
///
/// The PassConfig API prefers dealing with IDs because they are safer and more
/// efficient. IDs decouple configuration from instantiation. This way, when a
/// pass is overriden, it isn't unnecessarily instantiated. It is also unsafe to
/// refer to a Pass pointer after adding it to a pass manager, which deletes
/// redundant pass instances.
///
/// However, it is convient to directly instantiate target passes with
/// non-default ctors. These often don't have a registered PassInfo. Rather than
/// force all target passes to implement the pass registry boilerplate, allow
/// the PassConfig API to handle either type.
///
/// AnalysisID is sadly char*, so PointerIntPair won't work.
class IdentifyingPassPtr {
  union {
    /// Pass analysis ID when this does not hold an instance.
    AnalysisID ID;
    /// Concrete pass instance when \c IsInstance is true.
    Pass *P;
  };
  bool IsInstance = false;

public:
  /// Construct an empty, invalid identifying pass pointer.
  IdentifyingPassPtr() : P(nullptr) {}
  /// Construct from a pass analysis ID.
  ///
  /// \param IDPtr Analysis ID identifying the pass.
  IdentifyingPassPtr(AnalysisID IDPtr) : ID(IDPtr) {}
  /// Construct from a concrete pass instance.
  ///
  /// \param InstancePtr Pass instance to wrap.
  IdentifyingPassPtr(Pass *InstancePtr) : P(InstancePtr), IsInstance(true) {}

  /// Return true if this holds a non-null pass ID or instance.
  ///
  /// \returns True if this pointer identifies a pass.
  bool isValid() const { return P; }
  /// Return true if this holds a concrete Pass instance rather than an ID.
  ///
  /// \returns True if this holds a Pass instance.
  bool isInstance() const { return IsInstance; }

  /// Return the analysis ID held by this object.
  ///
  /// Asserts that this does not hold a Pass instance.
  ///
  /// \returns The wrapped analysis ID.
  AnalysisID getID() const {
    assert(!IsInstance && "Not a Pass ID");
    return ID;
  }

  /// Return the Pass instance held by this object.
  ///
  /// Asserts that this holds a Pass instance.
  ///
  /// \returns The wrapped Pass instance.
  Pass *getInstance() const {
    assert(IsInstance && "Not a Pass Instance");
    return P;
  }
};


/// Target-Independent Code Generator Pass Configuration Options.
///
/// This is an ImmutablePass solely for the purpose of exposing CodeGen options
/// to the internals of other CodeGen passes.
class LLVM_ABI TargetPassConfig : public ImmutablePass {
private:
  PassManagerBase *PM = nullptr;
  AnalysisID StartBefore = nullptr;
  AnalysisID StartAfter = nullptr;
  AnalysisID StopBefore = nullptr;
  AnalysisID StopAfter = nullptr;

  unsigned StartBeforeInstanceNum = 0;
  unsigned StartBeforeCount = 0;

  unsigned StartAfterInstanceNum = 0;
  unsigned StartAfterCount = 0;

  unsigned StopBeforeInstanceNum = 0;
  unsigned StopBeforeCount = 0;

  unsigned StopAfterInstanceNum = 0;
  unsigned StopAfterCount = 0;

  bool Started = true;
  bool Stopped = false;
  bool AddingMachinePasses = false;
  bool DebugifyIsSafe = true;

  /// Set the StartAfter, StartBefore and StopAfter passes to allow running only
  /// a portion of the normal code-gen pass sequence.
  ///
  /// If the StartAfter and StartBefore pass ID is zero, then compilation will
  /// begin at the normal point; otherwise, clear the Started flag to indicate
  /// that passes should not be added until the starting pass is seen.  If the
  /// Stop pass ID is zero, then compilation will continue to the end.
  ///
  /// This function expects that at least one of the StartAfter or the
  /// StartBefore pass IDs is null.
  void setStartStopPasses();

protected:
  /// Target machine whose codegen pipeline is being configured.
  TargetMachine *TM;
  /// Internal data structures used by the pass configuration.
  PassConfigImpl *Impl = nullptr;
  /// True after all passes have been configured.
  bool Initialized = false;

  // Target Pass Options
  // Targets provide a default setting, user flags override.
  /// Whether machine IR verification is disabled for this target.
  bool DisableVerify = false;

  /// Default setting for -enable-tail-merge on this target.
  bool EnableTailMerge = true;

  /// Enable sinking of instructions in MachineSink where a computation can be
  /// folded into the addressing mode of a memory load/store instruction or
  /// replace a copy.
  bool EnableSinkAndFold = false;

  /// Require processing of functions such that callees are generated before
  /// callers.
  bool RequireCodeGenSCCOrder = false;

  /// Enable LoopTermFold immediately after LSR
  bool EnableLoopTermFold = false;

  /// Add the actual instruction selection passes. This does not include
  /// preparation passes on IR.
  ///
  /// \returns True if an error occurred while adding instruction selection
  /// passes.
  bool addCoreISelPasses();

public:
  /// Construct a pass configuration for \p TM that populates \p PM.
  ///
  /// \param TM Target machine being configured.
  /// \param PM Pass manager that receives the codegen passes.
  TargetPassConfig(TargetMachine &TM, PassManagerBase &PM);
  /// Construct a dummy pass configuration used for pass registration.
  TargetPassConfig();

  /// Destroy the pass configuration and its implementation data.
  ~TargetPassConfig() override;

  /// Pass identification, replacement for typeinfo.
  static char ID;

  /// Get the right type of TargetMachine for this target.
  ///
  /// \returns The target machine cast to \p TMC.
  template<typename TMC> TMC &getTM() const {
    return *static_cast<TMC*>(TM);
  }

  /// Mark this configuration as fully initialized.
  void setInitialized() { Initialized = true; }

  /// Return the codegen optimization level for this configuration.
  ///
  /// \returns The CodeGenOptLevel used by this configuration.
  CodeGenOptLevel getOptLevel() const;

  /// Returns true if one of the `-start-after`, `-start-before`, `-stop-after`
  /// or `-stop-before` options is set.
  ///
  /// \returns True if the codegen pipeline is limited by start/stop options.
  static bool hasLimitedCodeGenPipeline();

  /// Returns true if none of the `-stop-before` and `-stop-after` options is
  /// set.
  ///
  /// \returns True if the full codegen pipeline will run to completion.
  static bool willCompleteCodeGenPipeline();

  /// If hasLimitedCodeGenPipeline is true, this method returns
  /// a string with the name of the options that caused this
  /// pipeline to be limited.
  ///
  /// \returns Names of the options that limited the codegen pipeline.
  static std::string getLimitedCodeGenPipelineReason();

  /// Describes where a limited codegen pipeline starts and stops.
  struct StartStopInfo {
    /// True if compilation starts after \c StartPass rather than before it.
    bool StartAfter;
    /// True if compilation stops after \c StopPass rather than before it.
    bool StopAfter;
    /// 1-based instance number of \c StartPass to match, or 0 for the first.
    unsigned StartInstanceNum;
    /// 1-based instance number of \c StopPass to match, or 0 for the first.
    unsigned StopInstanceNum;
    /// Name of the pass where the limited pipeline starts.
    StringRef StartPass;
    /// Name of the pass where the limited pipeline stops.
    StringRef StopPass;
  };

  /// Returns pass name in `-stop-before` or `-stop-after`
  /// NOTE: New pass manager migration only
  ///
  /// \param PIC Pass instrumentation callbacks providing registered pass names.
  /// \returns Start/stop pass info for a limited codegen pipeline, or an error.
  static Expected<StartStopInfo>
  getStartStopInfo(PassInstrumentationCallbacks &PIC);

  /// Set whether machine IR verification is disabled.
  ///
  /// \param Disable If true, disable verification passes.
  void setDisableVerify(bool Disable) { setOpt(DisableVerify, Disable); }

  /// Return whether tail merging is enabled for this target.
  ///
  /// \returns True if tail merging is enabled.
  bool getEnableTailMerge() const { return EnableTailMerge; }
  /// Set whether tail merging is enabled.
  ///
  /// \param Enable If true, enable tail merging.
  void setEnableTailMerge(bool Enable) { setOpt(EnableTailMerge, Enable); }

  /// Return whether MachineSink sinking-and-folding is enabled.
  ///
  /// \returns True if MachineSink may sink and fold into addressing modes.
  bool getEnableSinkAndFold() const { return EnableSinkAndFold; }
  /// Set whether MachineSink may sink and fold into addressing modes.
  ///
  /// \param Enable If true, enable sink-and-fold.
  void setEnableSinkAndFold(bool Enable) { setOpt(EnableSinkAndFold, Enable); }

  /// Return true if functions must be processed with callees before callers.
  ///
  /// \returns True if codegen must process callees before callers.
  bool requiresCodeGenSCCOrder() const { return RequireCodeGenSCCOrder; }
  /// Set whether codegen must process callees before callers.
  ///
  /// \param Enable If true, require SCC order (defaults to true).
  void setRequiresCodeGenSCCOrder(bool Enable = true) {
    setOpt(RequireCodeGenSCCOrder, Enable);
  }

  /// Override a standard pipeline pass with a target-specific substitute.
  ///
  /// When passes are added to the standard pipeline at the point where
  /// StandardID is expected, add TargetID in its place.
  ///
  /// \param StandardID Pass ID expected by the standard pipeline.
  /// \param TargetID Pass to insert instead of \p StandardID.
  void substitutePass(AnalysisID StandardID, IdentifyingPassPtr TargetID);

  /// Insert InsertedPassID pass after TargetPassID pass.
  ///
  /// \param TargetPassID Pass after which the new pass is inserted.
  /// \param InsertedPassID Pass to insert.
  void insertPass(AnalysisID TargetPassID, IdentifyingPassPtr InsertedPassID);

  /// Allow the target to enable a specific standard pass by default.
  ///
  /// \param PassID Standard pass to enable.
  void enablePass(AnalysisID PassID) { substitutePass(PassID, PassID); }

  /// Allow the target to disable a specific standard pass by default.
  ///
  /// \param PassID Standard pass to disable.
  void disablePass(AnalysisID PassID) {
    substitutePass(PassID, IdentifyingPassPtr());
  }

  /// Return the pass substituted for StandardID by the target.
  /// If no substitution exists, return StandardID.
  ///
  /// \param StandardID Pass ID to look up.
  /// \returns The substituted pass, or \p StandardID if none is registered.
  IdentifyingPassPtr getPassSubstitution(AnalysisID StandardID) const;

  /// Return true if the pass has been substituted by the target or
  /// overridden on the command line.
  ///
  /// \param ID Pass ID to check.
  /// \returns True if the pass is substituted or overridden.
  bool isPassSubstitutedOrOverridden(AnalysisID ID) const;

  /// Return true if the optimized regalloc pipeline is enabled.
  ///
  /// \returns True if optimized register allocation is enabled.
  bool getOptimizeRegAlloc() const;

  /// Return true if the default global register allocator is in use and
  /// has not be overriden on the command line with '-regalloc=...'
  ///
  /// \returns True if the default register allocator is still in use.
  bool usingDefaultRegAlloc() const;

  /// Add all passes that lower LLVM IR to machine instructions.
  ///
  /// Adds IR based lowering and target specific optimization passes and finally
  /// the core instruction selection passes.
  /// \returns true if an error occurred, false otherwise.
  bool addISelPasses();

  /// Add common target configurable passes that perform LLVM IR to IR
  /// transforms following machine independent optimization.
  virtual void addIRPasses();

  /// Add passes to lower exception handling for the code generator.
  void addPassesToHandleExceptions();

  /// Add pass to prepare the LLVM IR for code generation. This should be done
  /// before exception handling preparation passes.
  virtual void addCodeGenPrepare();

  /// Add common passes that perform LLVM IR to IR transforms in preparation for
  /// instruction selection.
  virtual void addISelPrepare();

  /// addInstSelector - This method should install an instruction selector pass,
  /// which converts from LLVM code to machine instructions.
  ///
  /// \returns True on success; false if an error occurred while adding the
  /// pass.
  virtual bool addInstSelector() {
    return true;
  }

  /// This method should install an IR translator pass, which converts from
  /// LLVM code to machine instructions with possibly generic opcodes.
  ///
  /// \returns True on success; false if an error occurred while adding the
  /// pass.
  virtual bool addIRTranslator() { return true; }

  /// This method may be implemented by targets that want to run passes
  /// immediately before legalization.
  virtual void addPreLegalizeMachineIR() {}

  /// This method should install a legalize pass, which converts the instruction
  /// sequence into one that can be selected by the target.
  ///
  /// \returns True on success; false if an error occurred while adding the
  /// pass.
  virtual bool addLegalizeMachineIR() { return true; }

  /// This method may be implemented by targets that want to run passes
  /// immediately before the register bank selection.
  virtual void addPreRegBankSelect() {}

  /// This method should install a register bank selector pass, which
  /// assigns register banks to virtual registers without a register
  /// class or register banks.
  ///
  /// \returns True on success; false if an error occurred while adding the
  /// pass.
  virtual bool addRegBankSelect() { return true; }

  /// This method may be implemented by targets that want to run passes
  /// immediately before the (global) instruction selection.
  virtual void addPreGlobalInstructionSelect() {}

  /// Install a global instruction selector that constrains generic registers.
  ///
  /// Converts possibly generic instructions to fully target-specific
  /// instructions, thereby constraining all generic virtual registers to
  /// register classes.
  ///
  /// \returns True on success; false if an error occurred while adding the
  /// pass.
  virtual bool addGlobalInstructionSelect() { return true; }

  /// Add the complete, standard set of LLVM CodeGen passes.
  /// Fully developed targets will not generally override this.
  virtual void addMachinePasses();

  /// printAndVerify - Add a pass to dump then verify the machine function, if
  /// those steps are enabled.
  ///
  /// \param Banner Text prefixed to print/verify diagnostics.
  void printAndVerify(const std::string &Banner);

  /// Add a pass to print the machine function if printing is enabled.
  ///
  /// \param Banner Text prefixed to the printed machine function.
  void addPrintPass(const std::string &Banner);

  /// Add a pass to perform basic verification of the machine function if
  /// verification is enabled.
  ///
  /// \param Banner Text prefixed to verifier diagnostic messages.
  void addVerifyPass(const std::string &Banner);

  /// Add a pass to add synthesized debug info to the MIR.
  void addDebugifyPass();

  /// Add a pass to remove debug info from the MIR.
  void addStripDebugPass();

  /// Add a pass to check synthesized debug info for MIR.
  void addCheckDebugPass();

  /// Add standard passes before a pass that's about to be added. For example,
  /// the DebugifyMachineModulePass if it is enabled.
  ///
  /// \param AllowDebugify If true, allow adding the debugify pre-pass.
  void addMachinePrePasses(bool AllowDebugify = true);

  /// Add standard passes after a pass that has just been added. For example,
  /// the MachineVerifier if it is enabled.
  ///
  /// \param Banner Text prefixed to post-pass verifier diagnostics.
  void addMachinePostPasses(const std::string &Banner);

  /// Check whether or not GlobalISel should abort on error.
  /// When this is disabled, GlobalISel will fall back on SDISel instead of
  /// erroring out.
  ///
  /// \returns True if GlobalISel should abort on error rather than fall back.
  bool isGlobalISelAbortEnabled() const;

  /// Return whether to emit a diagnostic when GlobalISel falls back.
  ///
  /// Emits a diagnostic when GlobalISel failed and isGlobalISelAbortEnabled is
  /// false.
  ///
  /// \returns True if a diagnostic should be emitted on GlobalISel fallback.
  virtual bool reportDiagnosticWhenGlobalISelFallback() const;

  /// Returns the CSEConfig object to use for the current optimization level.
  ///
  /// \returns A CSEConfig appropriate for the current optimization level.
  virtual std::unique_ptr<CSEConfigBase> getCSEConfig() const;

protected:
  /// Assign a target option, verifying that configuration is still mutable.
  ///
  /// \param Opt Option reference to update.
  /// \param Val New value for \p Opt.
  void setOpt(bool &Opt, bool Val);

  /// Return true if register allocator is specified by -regalloc=override.
  ///
  /// \returns True if a custom register allocator was requested on the command
  /// line.
  bool isCustomizedRegAlloc();

  /// Methods with trivial inline returns are convenient points in the common
  /// codegen pass pipeline where targets may insert passes. Methods with
  /// out-of-line standard implementations are major CodeGen stages called by
  /// addMachinePasses. Some targets may override major stages when inserting
  /// passes is insufficient, but maintaining overriden stages is more work.
  ///

  /// addPreISelPasses - This method should add any "last minute" LLVM->LLVM
  /// passes (which are run just before instruction selector).
  ///
  /// \returns True on success; false if an error occurred while adding passes.
  virtual bool addPreISel() {
    return true;
  }

  /// addMachineSSAOptimization - Add standard passes that optimize machine
  /// instructions in SSA form.
  virtual void addMachineSSAOptimization();

  /// Add out-of-order ILP optimization passes while code is still in SSA form.
  ///
  /// These passes are run while the machine code is still in SSA form, so they
  /// can use MachineTraceMetrics to control their heuristics.
  ///
  /// All passes added here should preserve the MachineDominatorTree,
  /// MachineLoopInfo, and MachineTraceMetrics analyses.
  ///
  /// \returns True if any ILP optimization passes were added.
  virtual bool addILPOpts() {
    return false;
  }

  /// This method may be implemented by targets that want to run passes
  /// immediately before register allocation.
  virtual void addPreRegAlloc() { }

  /// createTargetRegisterAllocator - Create the register allocator pass for
  /// this target at the current optimization level.
  ///
  /// \param Optimized If true, create an optimizing register allocator.
  /// \returns The newly created register allocator pass.
  virtual FunctionPass *createTargetRegisterAllocator(bool Optimized);

  /// addFastRegAlloc - Add the minimum set of target-independent passes that
  /// are required for fast register allocation.
  virtual void addFastRegAlloc();

  /// addOptimizedRegAlloc - Add passes related to register allocation.
  /// CodeGenTargetMachineImpl provides standard regalloc passes for most
  /// targets.
  virtual void addOptimizedRegAlloc();

  /// Add passes after register allocation but before virtreg rewriting.
  ///
  /// These passes are added to the optimized register allocation pipeline after
  /// register allocation is complete, but before virtual registers are rewritten
  /// to physical registers.
  ///
  /// These passes must preserve VirtRegMap and LiveIntervals, and when running
  /// after RABasic or RAGreedy, they should take advantage of LiveRegMatrix.
  /// When these passes run, VirtRegMap contains legal physreg assignments for
  /// all virtual registers.
  ///
  /// Note if the target overloads addRegAssignAndRewriteOptimized, this may not
  /// be honored. This is also not generally used for the fast variant,
  /// where the allocation and rewriting are done in one pass.
  ///
  /// \returns True if any pre-rewrite passes were added.
  virtual bool addPreRewrite() {
    return false;
  }

  /// addPostFastRegAllocRewrite - Add passes to the optimized register
  /// allocation pipeline after fast register allocation is complete.
  ///
  /// \returns True if any post-fast-regalloc rewrite passes were added.
  virtual bool addPostFastRegAllocRewrite() { return false; }

  /// Add passes to be run immediately after virtual registers are rewritten
  /// to physical registers.
  virtual void addPostRewrite() { }

  /// This method may be implemented by targets that want to run passes after
  /// register allocation pass pipeline but before prolog-epilog insertion.
  virtual void addPostRegAlloc() { }

  /// Add passes that optimize machine instructions after register allocation.
  virtual void addMachineLateOptimization();

  /// This method may be implemented by targets that want to run passes after
  /// prolog-epilog insertion and before the second instruction scheduling pass.
  virtual void addPreSched2() { }

  /// addGCPasses - Add late codegen passes that analyze code for garbage
  /// collection. This should return true if GC info should be printed after
  /// these passes.
  ///
  /// \returns True if GC info should be printed after these passes.
  virtual bool addGCPasses();

  /// Add standard basic block placement passes.
  virtual void addBlockPlacement();

  /// This pass may be implemented by targets that want to run passes
  /// immediately before machine code is emitted.
  virtual void addPreEmitPass() { }

  /// This pass may be implemented by targets that want to run passes
  /// immediately after basic block sections are assigned.
  virtual void addPostBBSections() {}

  /// Targets may add passes immediately before machine code is emitted in this
  /// callback. This is called even later than `addPreEmitPass`.
  // FIXME: Rename `addPreEmitPass` to something more sensible given its actual
  // position and remove the `2` suffix here as this callback is what
  // `addPreEmitPass` *should* be but in reality isn't.
  virtual void addPreEmitPass2() {}

  /// Utilities for targets to add passes to the pass manager.
  ///

  /// Add a CodeGen pass at this point in the pipeline after checking overrides.
  /// Return the pass that was added, or zero if no pass was added.
  ///
  /// \param PassID Analysis ID of the pass to add.
  /// \returns The analysis ID of the pass that was added, or null if none.
  AnalysisID addPass(AnalysisID PassID);

  /// Add a pass to the PassManager if that pass is supposed to be run, as
  /// determined by the StartAfter and StopAfter options. Takes ownership of the
  /// pass.
  ///
  /// \param P Pass instance to add; ownership is transferred.
  void addPass(Pass *P);

  /// addMachinePasses helper to create the target-selected or overriden
  /// regalloc pass.
  ///
  /// \param Optimized If true, create an optimizing register allocator.
  /// \returns The register allocator pass selected for this target.
  virtual FunctionPass *createRegAllocPass(bool Optimized);

  /// Add core register allocator passes which do the actual register assignment
  /// and rewriting. \returns true if any passes were added.
  virtual bool addRegAssignAndRewriteFast();
  /// Add optimized register assignment and rewriting passes.
  ///
  /// \returns true if any passes were added.
  virtual bool addRegAssignAndRewriteOptimized();
};

/// Register codegen-related pass instrumentation callbacks.
///
/// \param PIC Callback registry to update.
/// \param TM Target machine providing codegen context.
LLVM_ABI void registerCodeGenCallback(PassInstrumentationCallbacks &PIC,
                                      TargetMachine &TM);

} // end namespace llvm

#endif // LLVM_CODEGEN_TARGETPASSCONFIG_H
