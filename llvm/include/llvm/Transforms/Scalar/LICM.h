//===- LICM.h - Loop Invariant Code Motion Pass -------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass performs loop invariant code motion, attempting to remove as much
// code from the body of a loop as possible.  It does this by either hoisting
// code into the preheader block, or by sinking code to the exit blocks if it is
// safe.  This pass also promotes must-aliased memory locations in the loop to
// live in registers, thus hoisting and sinking "invariant" loads and stores.
//
// This pass uses alias analysis for two purposes:
//
//  1. Moving loop invariant loads and calls out of loops.  If we can determine
//     that a load or call inside of a loop never aliases anything stored to,
//     we can hoist it or sink it like any other instruction.
//  2. Scalar Promotion of Memory - If there is a store instruction inside of
//     the loop, we try to move the store to happen AFTER the loop instead of
//     inside of the loop.  This can only happen if a few conditions are true:
//       A. The pointer stored through is loop invariant
//       B. There are no stores or loads in the loop which _may_ alias the
//          pointer.  There are no calls in the loop which mod/ref the pointer.
//     If these conditions are true, we can promote the loads and stores in the
//     loop of the pointer to use a temporary alloca'd variable.  We then use
//     the SSAUpdater to construct the appropriate SSA form for the value.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LICM_H
#define LLVM_TRANSFORMS_SCALAR_LICM_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"

namespace llvm {

class LPMUpdater;
class Loop;
class LoopNest;

/// Caps MemorySSA clobbering-call queries in LICM for compile-time.
extern LLVM_ABI cl::opt<unsigned> SetLicmMssaOptCap;
/// Caps loop memory accesses before LICM disables MemorySSA promotion.
extern LLVM_ABI cl::opt<unsigned> SetLicmMssaNoAccForPromotionCap;

/// Options controlling MemorySSA caps and speculation in LICM.
struct LICMOptions {
  /// Maximum MemorySSA clobbering-call queries before LICM backs off.
  unsigned MssaOptCap;
  /// Maximum memory accesses before scalar promotion is considered too costly.
  unsigned MssaNoAccForPromotionCap;
  /// Whether to hoist values that may not always execute.
  bool AllowSpeculation;

  /// Construct LICM options from the global command-line defaults.
  LICMOptions()
      : MssaOptCap(SetLicmMssaOptCap),
        MssaNoAccForPromotionCap(SetLicmMssaNoAccForPromotionCap),
        AllowSpeculation(true) {}

  /// Construct LICM options with explicit MemorySSA caps and speculation.
  /// @param MssaOptCap Max clobbering-call queries before giving up.
  /// @param MssaNoAccForPromotionCap Max memory accesses before promotion is
  /// considered too expensive.
  /// @param AllowSpeculation Whether to hoist values that may not always
  /// execute.
  LICMOptions(unsigned MssaOptCap, unsigned MssaNoAccForPromotionCap,
              bool AllowSpeculation)
      : MssaOptCap(MssaOptCap),
        MssaNoAccForPromotionCap(MssaNoAccForPromotionCap),
        AllowSpeculation(AllowSpeculation) {}
};

/// Performs Loop Invariant Code Motion Pass.
class LICMPass : public OptionalPassInfoMixin<LICMPass> {
  LICMOptions Opts;

public:
  /// Construct a LICM pass with explicit MemorySSA caps and speculation.
  /// @param MssaOptCap Max clobbering-call queries before giving up.
  /// @param MssaNoAccForPromotionCap Max memory accesses before promotion is
  /// considered too expensive.
  /// @param AllowSpeculation Whether to hoist values that may not always
  /// execute.
  LICMPass(unsigned MssaOptCap, unsigned MssaNoAccForPromotionCap,
           bool AllowSpeculation)
      : LICMPass(LICMOptions(MssaOptCap, MssaNoAccForPromotionCap,
                             AllowSpeculation)) {}
  /// Construct a LICM pass with the given options.
  /// @param Opts Configuration controlling MemorySSA caps and speculation.
  LICMPass(LICMOptions Opts) : Opts(Opts) {}

  /// Run loop invariant code motion over the loop.
  /// @param L Loop to transform.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};

/// Performs LoopNest Invariant Code Motion Pass.
class LNICMPass : public OptionalPassInfoMixin<LNICMPass> {
  LICMOptions Opts;

public:
  /// Construct a loop-nest LICM pass with explicit MemorySSA caps and
  /// speculation.
  /// @param MssaOptCap Max clobbering-call queries before giving up.
  /// @param MssaNoAccForPromotionCap Max memory accesses before promotion is
  /// considered too expensive.
  /// @param AllowSpeculation Whether to hoist values that may not always
  /// execute.
  LNICMPass(unsigned MssaOptCap, unsigned MssaNoAccForPromotionCap,
            bool AllowSpeculation)
      : LNICMPass(LICMOptions(MssaOptCap, MssaNoAccForPromotionCap,
                              AllowSpeculation)) {}
  /// Construct a loop-nest LICM pass with the given options.
  /// @param Opts Configuration controlling MemorySSA caps and speculation.
  LNICMPass(LICMOptions Opts) : Opts(Opts) {}

  /// Run loop-nest invariant code motion over the loop nest.
  /// @param L Loop nest to transform.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(LoopNest &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);

  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};
} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LICM_H
