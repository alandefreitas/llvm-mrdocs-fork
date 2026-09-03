///===- MachineOptimizationRemarkEmitter.h - Opt Diagnostics -*- C++ -*----===//
///
/// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
/// See https://llvm.org/LICENSE.txt for license information.
/// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
///
///===---------------------------------------------------------------------===//
/// \file
/// Optimization diagnostic interfaces for machine passes.  It's packaged as an
/// analysis pass so that by using this service passes become dependent on MBFI
/// as well.  MBFI is used to compute the "hotness" of the diagnostic message.
///
///===---------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINEOPTIMIZATIONREMARKEMITTER_H
#define LLVM_CODEGEN_MACHINEOPTIMIZATIONREMARKEMITTER_H

#include "llvm/CodeGen/MachineFunctionPass.h"
#include "llvm/CodeGen/MachinePassManager.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {
class MachineBasicBlock;
class MachineBlockFrequencyInfo;
class MachineInstr;

/// Common features for diagnostics dealing with optimization remarks
/// that are used by machine passes.
class DiagnosticInfoMIROptimization : public DiagnosticInfoOptimizationBase {
public:
  /// Construct a MIR optimization remark of kind \p Kind.
  ///
  /// \param Kind Diagnostic kind
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param Loc Debug location for the remark
  /// \param MBB Machine basic block that the optimization operates in
  DiagnosticInfoMIROptimization(enum DiagnosticKind Kind, const char *PassName,
                                StringRef RemarkName,
                                const DiagnosticLocation &Loc,
                                const MachineBasicBlock *MBB)
      : DiagnosticInfoOptimizationBase(Kind, DS_Remark, PassName, RemarkName,
                                       MBB->getParent()->getFunction(), Loc),
        MBB(MBB) {}

  /// MI-specific kinds of diagnostic Arguments.
  struct MachineArgument : public DiagnosticInfoOptimizationBase::Argument {
    /// Print an entire MachineInstr.
    ///
    /// \param Key Argument key name
    /// \param MI Machine instruction to stringify
    LLVM_ABI MachineArgument(StringRef Key, const MachineInstr &MI);
  };

  /// Return true if \p DI is a MIR optimization remark.
  ///
  /// \param DI Diagnostic to test
  /// \return True if \p DI is a MIR optimization remark
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() >= DK_FirstMachineRemark &&
           DI->getKind() <= DK_LastMachineRemark;
  }

  /// Return the machine basic block this remark refers to.
  ///
  /// \return Machine basic block associated with this remark
  const MachineBasicBlock *getBlock() const { return MBB; }

private:
  const MachineBasicBlock *MBB;
};

/// Diagnostic information for applied optimization remarks.
class MachineOptimizationRemark : public DiagnosticInfoMIROptimization {
public:
  /// Construct an applied optimization remark for the given pass and block.
  ///
  /// \param PassName Name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass=, then the diagnostic will
  /// be emitted.
  /// \param RemarkName Textual identifier for the remark
  /// \param Loc Debug location for the remark
  /// \param MBB Machine basic block that the optimization operates in
  MachineOptimizationRemark(const char *PassName, StringRef RemarkName,
                            const DiagnosticLocation &Loc,
                            const MachineBasicBlock *MBB)
      : DiagnosticInfoMIROptimization(DK_MachineOptimizationRemark, PassName,
                                      RemarkName, Loc, MBB) {}

  /// Return true if \p DI is a MachineOptimizationRemark.
  ///
  /// \param DI Diagnostic to test
  /// \return True if \p DI is a MachineOptimizationRemark
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MachineOptimizationRemark;
  }

  /// Return true if this remark is enabled by -Rpass=.
  ///
  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  /// \return True if passed-optimization remarks are enabled for this pass
  bool isEnabled() const override {
    const Function &Fn = getFunction();
    LLVMContext &Ctx = Fn.getContext();
    return Ctx.getDiagHandlerPtr()->isPassedOptRemarkEnabled(getPassName());
  }
};

/// Diagnostic information for missed-optimization remarks.
class MachineOptimizationRemarkMissed : public DiagnosticInfoMIROptimization {
public:
  /// Construct a missed-optimization remark for the given pass and block.
  ///
  /// \param PassName Name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-missed=, then the
  /// diagnostic will be emitted.
  /// \param RemarkName Textual identifier for the remark
  /// \param Loc Debug location for the remark
  /// \param MBB Machine basic block that the optimization operates in
  MachineOptimizationRemarkMissed(const char *PassName, StringRef RemarkName,
                                  const DiagnosticLocation &Loc,
                                  const MachineBasicBlock *MBB)
      : DiagnosticInfoMIROptimization(DK_MachineOptimizationRemarkMissed,
                                      PassName, RemarkName, Loc, MBB) {}

  /// Return true if \p DI is a MachineOptimizationRemarkMissed.
  ///
  /// \param DI Diagnostic to test
  /// \return True if \p DI is a MachineOptimizationRemarkMissed
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MachineOptimizationRemarkMissed;
  }

  /// Return true if this remark is enabled by -Rpass-missed=.
  ///
  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  /// \return True if missed-optimization remarks are enabled for this pass
  bool isEnabled() const override {
    const Function &Fn = getFunction();
    LLVMContext &Ctx = Fn.getContext();
    return Ctx.getDiagHandlerPtr()->isMissedOptRemarkEnabled(getPassName());
  }
};

/// Diagnostic information for optimization analysis remarks.
class MachineOptimizationRemarkAnalysis : public DiagnosticInfoMIROptimization {
public:
  /// Construct an optimization analysis remark for the given pass and block.
  ///
  /// \param PassName Name of the pass emitting this diagnostic. If this name
  /// matches the regular expression given in -Rpass-analysis=, then the
  /// diagnostic will be emitted.
  /// \param RemarkName Textual identifier for the remark
  /// \param Loc Debug location for the remark
  /// \param MBB Machine basic block that the optimization operates in
  MachineOptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                                    const DiagnosticLocation &Loc,
                                    const MachineBasicBlock *MBB)
      : DiagnosticInfoMIROptimization(DK_MachineOptimizationRemarkAnalysis,
                                      PassName, RemarkName, Loc, MBB) {}

  /// Construct an analysis remark deriving location from instruction \p MI.
  ///
  /// \param PassName Name of the pass emitting this diagnostic
  /// \param RemarkName Textual identifier for the remark
  /// \param MI Instruction used to derive debug location and basic block
  MachineOptimizationRemarkAnalysis(const char *PassName, StringRef RemarkName,
                                    const MachineInstr *MI)
      : DiagnosticInfoMIROptimization(DK_MachineOptimizationRemarkAnalysis,
                                      PassName, RemarkName, MI->getDebugLoc(),
                                      MI->getParent()) {}

  /// Return true if \p DI is a MachineOptimizationRemarkAnalysis.
  ///
  /// \param DI Diagnostic to test
  /// \return True if \p DI is a MachineOptimizationRemarkAnalysis
  static bool classof(const DiagnosticInfo *DI) {
    return DI->getKind() == DK_MachineOptimizationRemarkAnalysis;
  }

  /// Return true if this remark is enabled by -Rpass-analysis=.
  ///
  /// \see DiagnosticInfoOptimizationBase::isEnabled.
  /// \return True if analysis remarks are enabled for this pass
  bool isEnabled() const override {
    const Function &Fn = getFunction();
    LLVMContext &Ctx = Fn.getContext();
    return Ctx.getDiagHandlerPtr()->isAnalysisRemarkEnabled(getPassName());
  }
};

namespace ore {
/// Alias for machine-instruction diagnostic arguments.
using MNV = DiagnosticInfoMIROptimization::MachineArgument;
}

/// The optimization diagnostic interface.
///
/// It allows reporting when optimizations are performed and when they are not
/// along with the reasons for it.  Hotness information of the corresponding
/// code region can be included in the remark if DiagnosticsHotnessRequested is
/// enabled in the LLVM context.
class MachineOptimizationRemarkEmitter {
public:
  /// Construct an emitter for \p MF using optional block frequencies \p MBFI.
  ///
  /// \param MF Machine function remarks are emitted for
  /// \param MBFI Block frequency info used for hotness, or null
  MachineOptimizationRemarkEmitter(MachineFunction &MF,
                                   MachineBlockFrequencyInfo *MBFI)
      : MF(MF), MBFI(MBFI) {}

  /// Move-construct an optimization remark emitter.
  ///
  /// \param Arg Emitter to move from
  MachineOptimizationRemarkEmitter(MachineOptimizationRemarkEmitter &&Arg) =
      default;

  /// Handle invalidation events in the new pass manager.
  ///
  /// \param MF Machine function whose analyses may be invalidated
  /// \param PA Set of analyses preserved by the invalidating pass
  /// \param Inv Invalidator used to invalidate dependent analyses
  /// \return True if this result should be invalidated because MBFI was
  /// invalidated
  LLVM_ABI bool invalidate(MachineFunction &MF, const PreservedAnalyses &PA,
                           MachineFunctionAnalysisManager::Invalidator &Inv);

  /// Emit an optimization remark.
  ///
  /// \param OptDiag Optimization diagnostic to emit
  LLVM_ABI void emit(DiagnosticInfoOptimizationBase &OptDiag);

  /// Whether we allow for extra compile-time budget to perform more
  /// analysis to be more informative.
  ///
  /// This is useful to enable additional missed optimizations to be reported
  /// that are normally too noisy.  In this mode, we can use the extra analysis
  /// (1) to filter trivial false positives or (2) to provide more context so
  /// that non-trivial false positives can be quickly detected by the user.
  ///
  /// \param PassName Name of the pass requesting extra analysis
  /// \return True if a remark streamer is active or remarks are enabled for
  /// \p PassName
  bool allowExtraAnalysis(StringRef PassName) const {
    return (
        MF.getFunction().getContext().getLLVMRemarkStreamer() ||
        MF.getFunction().getContext().getDiagHandlerPtr()->isAnyRemarkEnabled(
            PassName));
  }

  /// Take a lambda that returns a remark which will be emitted.  Second
  /// argument is only used to restrict this to functions.
  ///
  /// \param RemarkBuilder Callable that returns a remark to emit
  /// \param EnableIf Unused; SFINAE restriction to callable types
  template <typename T>
  void emit(T RemarkBuilder, decltype(RemarkBuilder()) *EnableIf = nullptr) {
    (void)EnableIf;
    // Avoid building the remark unless we know there are at least *some*
    // remarks enabled. We can't currently check whether remarks are requested
    // for the calling pass since that requires actually building the remark.

    if (MF.getFunction().getContext().getLLVMRemarkStreamer() ||
        MF.getFunction()
            .getContext()
            .getDiagHandlerPtr()
            ->isAnyRemarkEnabled()) {
      auto R = RemarkBuilder();
      emit((DiagnosticInfoOptimizationBase &)R);
    }
  }

  /// Return the machine block frequency info used for hotness, or null.
  ///
  /// \return Block frequency info, or null if hotness was not requested
  MachineBlockFrequencyInfo *getBFI() {
    return MBFI;
  }

private:
  MachineFunction &MF;

  /// MBFI is only set if hotness is requested.
  MachineBlockFrequencyInfo *MBFI;

  /// Compute hotness from IR value (currently assumed to be a block) if PGO is
  /// available.
  std::optional<uint64_t> computeHotness(const MachineBasicBlock &MBB);

  /// Similar but use value from \p OptDiag and update hotness there.
  void computeHotness(DiagnosticInfoMIROptimization &Remark);

  /// Only allow verbose messages if we know we're filtering by hotness
  /// (BFI is only set in this case).
  bool shouldEmitVerbose() { return MBFI != nullptr; }
};

/// The analysis pass
class MachineOptimizationRemarkEmitterAnalysis
    : public AnalysisInfoMixin<MachineOptimizationRemarkEmitterAnalysis> {
  friend AnalysisInfoMixin<MachineOptimizationRemarkEmitterAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  using Result = MachineOptimizationRemarkEmitter;
  /// Run the analysis pass over a machine function and produce an ORE.
  ///
  /// \param MF Machine function to analyze
  /// \param MFAM Machine function analysis manager
  /// \return Optimization remark emitter for \p MF
  LLVM_ABI Result run(MachineFunction &MF,
                      MachineFunctionAnalysisManager &MFAM);
};

/// The analysis pass
///
/// Note that this pass shouldn't generally be marked as preserved by other
/// passes.  It's holding onto BFI, so if the pass does not preserve BFI, BFI
/// could be freed.
class LLVM_ABI MachineOptimizationRemarkEmitterPass
    : public MachineFunctionPass {
  std::unique_ptr<MachineOptimizationRemarkEmitter> ORE;

public:
  /// Construct the machine optimization remark emitter pass.
  MachineOptimizationRemarkEmitterPass();

  /// Compute the optimization remark emitter for \p MF.
  ///
  /// \param MF Machine function to analyze
  /// \return False; this analysis does not modify the function
  bool runOnMachineFunction(MachineFunction &MF) override;

  /// Declare analyses required and preserved by this pass.
  ///
  /// \param AU Analysis usage object to update
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return the optimization remark emitter for the last run function.
  ///
  /// \return Reference to the emitter produced by the last run
  MachineOptimizationRemarkEmitter &getORE() {
    assert(ORE && "pass not run yet");
    return *ORE;
  }

  /// Pass identification, replacement for typeid.
  static char ID;
};
}

#endif
