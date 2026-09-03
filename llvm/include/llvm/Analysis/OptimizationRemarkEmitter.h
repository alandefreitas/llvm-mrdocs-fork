//===- OptimizationRemarkEmitter.h - Optimization Diagnostic ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Optimization diagnostic interfaces.  It's packaged as an analysis pass so
// that by using this service passes become dependent on BFI as well.  BFI is
// used to compute the "hotness" of the diagnostic message.
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_OPTIMIZATIONREMARKEMITTER_H
#define LLVM_ANALYSIS_OPTIMIZATIONREMARKEMITTER_H

#include "llvm/Analysis/BlockFrequencyInfo.h"
#include "llvm/IR/DiagnosticInfo.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Pass.h"
#include "llvm/Support/Compiler.h"
#include <optional>

namespace llvm {

/// The optimization diagnostic interface.
///
/// It allows reporting when optimizations are performed and when they are not
/// along with the reasons for it.  Hotness information of the corresponding
/// code region can be included in the remark if DiagnosticsHotnessRequested is
/// enabled in the LLVM context.
class OptimizationRemarkEmitter {
public:
  /// Construct an emitter for \p F using the given block frequency info.
  /// @param F Function whose optimizations are diagnosed.
  /// @param BFI Block frequencies used to compute remark hotness, or null.
  OptimizationRemarkEmitter(const Function *F, BlockFrequencyInfo *BFI)
      : F(F), BFI(BFI) {}

  /// This variant can be used to generate ORE on demand (without the
  /// analysis pass).
  ///
  /// Note that this ctor has a very different cost depending on whether
  /// F->getContext().getDiagnosticsHotnessRequested() is on or not.  If it's off
  /// the operation is free.
  ///
  /// Whereas if DiagnosticsHotnessRequested is on, it is fairly expensive
  /// operation since BFI and all its required analyses are computed.  This is
  /// for example useful for CGSCC passes that can't use function analyses
  /// passes in the old PM.
  /// @param F Function whose optimizations are diagnosed.
  LLVM_ABI OptimizationRemarkEmitter(const Function *F);

  /// Move-construct from \p Arg.
  /// @param Arg Emitter to move from.
  OptimizationRemarkEmitter(OptimizationRemarkEmitter &&Arg)
      : F(Arg.F), BFI(Arg.BFI) {}

  /// Move-assign from \p RHS.
  /// @param RHS Emitter to move from.
  /// @return Reference to this emitter.
  OptimizationRemarkEmitter &operator=(OptimizationRemarkEmitter &&RHS) {
    F = RHS.F;
    BFI = RHS.BFI;
    return *this;
  }

  /// Handle invalidation events in the new pass manager.
  /// @param F Function being invalidated.
  /// @param PA Set of preserved analyses.
  /// @param Inv Invalidator for dependent analyses.
  /// @return True if this analysis result should be invalidated.
  LLVM_ABI bool invalidate(Function &F, const PreservedAnalyses &PA,
                           FunctionAnalysisManager::Invalidator &Inv);

  /// Return true iff at least *some* remarks are enabled.
  /// @return True if at least some remarks are enabled.
  bool enabled() const {
    return F->getContext().getLLVMRemarkStreamer() ||
           F->getContext().getDiagHandlerPtr()->isAnyRemarkEnabled();
  }

  /// Output the remark via the diagnostic handler and to the
  /// optimization record file.
  /// @param OptDiag Optimization remark to emit.
  LLVM_ABI void emit(DiagnosticInfoOptimizationBase &OptDiag);
  /// Also allow r-value for OptDiag to allow emitting a temporarily-constructed
  /// diagnostic.
  /// @param OptDiag Temporary optimization remark to emit.
  void emit(DiagnosticInfoOptimizationBase &&OptDiag) { emit(OptDiag); }

  /// Emit a remark built by \p RemarkBuilder if any remarks are enabled.
  ///
  /// Second argument is only used to restrict this to functions.
  /// @param RemarkBuilder Callable that returns a DiagnosticInfoOptimizationBase.
  /// @param Unused Unused SFINAE parameter used to constrain \p T.
  template <typename T>
  void emit(T RemarkBuilder, decltype(RemarkBuilder()) *Unused = nullptr) {
    // Avoid building the remark unless we know there are at least *some*
    // remarks enabled. We can't currently check whether remarks are requested
    // for the calling pass since that requires actually building the remark.

    if (enabled()) {
      auto R = RemarkBuilder();
      static_assert(
          std::is_base_of<DiagnosticInfoOptimizationBase, decltype(R)>::value,
          "the lambda passed to emit() must return a remark");
      emit((DiagnosticInfoOptimizationBase &)R);
    }
  }

  /// Whether we allow for extra compile-time budget to perform more
  /// analysis to produce fewer false positives.
  ///
  /// This is useful when reporting missed optimizations.  In this case we can
  /// use the extra analysis (1) to filter trivial false positives or (2) to
  /// provide more context so that non-trivial false positives can be quickly
  /// detected by the user.
  /// @param PassName Pass whose remarks are checked for enablement.
  /// @return True if extra analysis for \p PassName remarks is allowed.
  bool allowExtraAnalysis(StringRef PassName) const {
    return OptimizationRemarkEmitter::allowExtraAnalysis(*F, PassName);
  }
  /// Whether \p F's context allows extra analysis for \p PassName remarks.
  /// @param F Function whose LLVM context is queried.
  /// @param PassName Pass whose remarks are checked for enablement.
  /// @return True if extra analysis for \p PassName remarks is allowed.
  static bool allowExtraAnalysis(const Function &F, StringRef PassName) {
    return allowExtraAnalysis(F.getContext(), PassName);
  }
  /// Whether \p Ctx allows extra analysis for \p PassName remarks.
  /// @param Ctx LLVM context to query for remark enablement.
  /// @param PassName Pass whose remarks are checked for enablement.
  /// @return True if extra analysis for \p PassName remarks is allowed.
  static bool allowExtraAnalysis(LLVMContext &Ctx, StringRef PassName) {
    return Ctx.getLLVMRemarkStreamer() ||
           Ctx.getDiagHandlerPtr()->isAnyRemarkEnabled(PassName);
  }

private:
  const Function *F;

  BlockFrequencyInfo *BFI;

  /// If we generate BFI on demand, we need to free it when ORE is freed.
  std::unique_ptr<BlockFrequencyInfo> OwnedBFI;

  /// Compute hotness from IR value (currently assumed to be a block) if PGO is
  /// available.
  std::optional<uint64_t> computeHotness(const Value *V);

  /// Similar but use value from \p OptDiag and update hotness there.
  void computeHotness(DiagnosticInfoIROptimization &OptDiag);

  /// Only allow verbose messages if we know we're filtering by hotness
  /// (BFI is only set in this case).
  bool shouldEmitVerbose() { return BFI != nullptr; }

  OptimizationRemarkEmitter(const OptimizationRemarkEmitter &) = delete;
  void operator=(const OptimizationRemarkEmitter &) = delete;
};

/// Add a small namespace to avoid name clashes with the classes used in
/// the streaming interface.  We want these to be short for better
/// write/readability.
namespace ore {
/// Alias for a key-value argument in the optimization remark stream.
using NV = DiagnosticInfoOptimizationBase::Argument;
/// Alias for the verbose-remark stream marker.
using setIsVerbose = DiagnosticInfoOptimizationBase::setIsVerbose;
/// Alias for the record-only stream argument marker.
using setExtraArgs = DiagnosticInfoOptimizationBase::setExtraArgs;
}

/// OptimizationRemarkEmitter legacy analysis pass
///
/// Note that this pass shouldn't generally be marked as preserved by other
/// passes.  It's holding onto BFI, so if the pass does not preserve BFI, BFI
/// could be freed.
class LLVM_ABI OptimizationRemarkEmitterWrapperPass : public FunctionPass {
  std::unique_ptr<OptimizationRemarkEmitter> ORE;

public:
  /// Construct the legacy optimization remark emitter wrapper pass.
  OptimizationRemarkEmitterWrapperPass();

  /// Create an OptimizationRemarkEmitter for function \p F.
  /// @param F Function to analyze.
  /// @return False; this analysis does not modify the function.
  bool runOnFunction(Function &F) override;

  /// Declare required and preserved analyses for this pass.
  /// @param AU Analysis usage object to update.
  void getAnalysisUsage(AnalysisUsage &AU) const override;

  /// Return the OptimizationRemarkEmitter for the last analyzed function.
  /// @return The OptimizationRemarkEmitter for the last analyzed function.
  OptimizationRemarkEmitter &getORE() {
    assert(ORE && "pass not run yet");
    return *ORE;
  }

  /// Pass identification, replacement for typeid.
  static char ID;
};

/// Analysis pass that provides an OptimizationRemarkEmitter.
class OptimizationRemarkEmitterAnalysis
    : public AnalysisInfoMixin<OptimizationRemarkEmitterAnalysis> {
  friend AnalysisInfoMixin<OptimizationRemarkEmitterAnalysis>;
  LLVM_ABI static AnalysisKey Key;

public:
  /// Provide the result typedef for this analysis pass.
  typedef OptimizationRemarkEmitter Result;

  /// Run the analysis pass over a function and produce an ORE.
  /// @param F Function to analyze.
  /// @param AM Function analysis manager providing dependencies.
  /// @return An OptimizationRemarkEmitter for \p F.
  LLVM_ABI Result run(Function &F, FunctionAnalysisManager &AM);
};
} // namespace llvm
#endif // LLVM_ANALYSIS_OPTIMIZATIONREMARKEMITTER_H
