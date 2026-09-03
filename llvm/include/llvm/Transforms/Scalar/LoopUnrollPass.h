//===- LoopUnrollPass.h -----------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOOPUNROLLPASS_H
#define LLVM_TRANSFORMS_SCALAR_LOOPUNROLLPASS_H

#include "llvm/Analysis/LoopAnalysisManager.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/CommandLine.h"
#include <optional>

namespace llvm {

/// Command-line option to forget all SCEV loops when unrolling.
extern LLVM_ABI cl::opt<bool> ForgetSCEVInLoopUnroll;

class Function;
class Loop;
class LPMUpdater;

/// Loop unroll pass that only does full loop unrolling and peeling.
class LoopFullUnrollPass : public OptionalPassInfoMixin<LoopFullUnrollPass> {
  const int OptLevel;

  /// If false, use a cost model to determine whether unrolling of a loop is
  /// profitable. If true, only loops that explicitly request unrolling via
  /// metadata are considered. All other loops are skipped.
  const bool OnlyWhenForced;

  /// If true, forget all loops when unrolling. If false, forget top-most loop
  /// of the currently processed loops, which removes one entry at a time from
  /// the internal SCEV records. For large loops, the former is faster.
  const bool ForgetSCEV;

  /// If true, consider calls as inline candidates and defer unrolling so that
  /// LTO post-link inlining can consider them first.
  const bool PrepareForLTO;

public:
  /// Construct a full loop-unroll pass with the given options.
  /// @param OptLevel Optimization level used to tune unrolling decisions.
  /// @param OnlyWhenForced When true, only unroll loops that request it via
  /// metadata.
  /// @param ForgetSCEV When true, forget all loops in SCEV when unrolling.
  /// @param PrepareForLTO When true, defer unrolling of inline candidates for
  /// LTO.
  explicit LoopFullUnrollPass(int OptLevel = 2, bool OnlyWhenForced = false,
                              bool ForgetSCEV = false,
                              bool PrepareForLTO = false)
      : OptLevel(OptLevel), OnlyWhenForced(OnlyWhenForced),
        ForgetSCEV(ForgetSCEV), PrepareForLTO(PrepareForLTO) {}

  /// Run full loop unrolling and peeling over the loop.
  /// @param L Loop to unroll.
  /// @param AM Loop analysis manager providing analyses for the pass.
  /// @param AR Standard loop analyses available to the pass.
  /// @param U Loop pass manager updater for reporting loop structure changes.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Loop &L, LoopAnalysisManager &AM,
                                 LoopStandardAnalysisResults &AR,
                                 LPMUpdater &U);
};

/// Parameters controlling transforms performed by the LoopUnroll pass.
///
/// Each of the boolean parameters can be set to:
///      true - enabling the transformation.
///      false - disabling the transformation.
///      None - relying on a global default.
///
/// There is also OptLevel parameter, which is used for additional loop unroll
/// tuning.
///
/// Intended use is to create a default object, modify parameters with
/// additional setters and then pass it to LoopUnrollPass.
struct LoopUnrollOptions {
  /// Whether to allow partial unrolling; None uses the global default.
  std::optional<bool> AllowPartial;
  /// Whether to allow loop peeling; None uses the global default.
  std::optional<bool> AllowPeeling;
  /// Whether to allow unrolling of loops with a runtime trip count; None uses
  /// the global default.
  std::optional<bool> AllowRuntime;
  /// Whether to use a trip-count upper bound when unrolling; None uses the
  /// global default.
  std::optional<bool> AllowUpperBound;
  /// Whether to allow profile-based loop peeling; None uses the global
  /// default.
  std::optional<bool> AllowProfileBasedPeeling;
  /// Maximum full-unroll count; None uses the global default.
  std::optional<unsigned> FullUnrollMaxCount;
  /// Optimization level used to tune loop unrolling decisions.
  int OptLevel;

  /// Whether to unroll only loops that request it via metadata.
  ///
  /// If false, use a cost model to determine whether unrolling of a loop is
  /// profitable. If true, only loops that explicitly request unrolling via
  /// metadata are considered. All other loops are skipped.
  bool OnlyWhenForced;

  /// Whether to forget all loops in SCEV when unrolling.
  ///
  /// If true, forget all loops when unrolling. If false, forget top-most loop
  /// of the currently processed loops, which removes one entry at a time from
  /// the internal SCEV records. For large loops, the former is faster.
  const bool ForgetSCEV;

  /// If true, consider calls as inline candidates and defer unrolling so that
  /// LTO post-link inlining can consider them first.
  bool PrepareForLTO = false;

  /// Construct LoopUnroll options with the given defaults.
  /// @param OptLevel Optimization level used to tune unrolling decisions.
  /// @param OnlyWhenForced When true, only unroll loops that request it via
  /// metadata.
  /// @param ForgetSCEV When true, forget all loops in SCEV when unrolling.
  LoopUnrollOptions(int OptLevel = 2, bool OnlyWhenForced = false,
                    bool ForgetSCEV = false)
      : OptLevel(OptLevel), OnlyWhenForced(OnlyWhenForced),
        ForgetSCEV(ForgetSCEV) {}

  /// Enables or disables partial unrolling. When disabled only full unrolling
  /// is allowed.
  /// @param Partial True to allow partial unrolling, false to disable it.
  /// @return Reference to this options object for chaining.
  LoopUnrollOptions &setPartial(bool Partial) {
    AllowPartial = Partial;
    return *this;
  }

  /// Enables or disables unrolling of loops with runtime trip count.
  /// @param Runtime True to allow runtime-trip-count unrolling, false to
  /// disable it.
  /// @return Reference to this options object for chaining.
  LoopUnrollOptions &setRuntime(bool Runtime) {
    AllowRuntime = Runtime;
    return *this;
  }

  /// Enables or disables loop peeling.
  /// @param Peeling True to allow loop peeling, false to disable it.
  /// @return Reference to this options object for chaining.
  LoopUnrollOptions &setPeeling(bool Peeling) {
    AllowPeeling = Peeling;
    return *this;
  }

  /// Enables or disables the use of trip count upper bound
  /// in loop unrolling.
  /// @param UpperBound True to allow using a trip-count upper bound, false to
  /// disable it.
  /// @return Reference to this options object for chaining.
  LoopUnrollOptions &setUpperBound(bool UpperBound) {
    AllowUpperBound = UpperBound;
    return *this;
  }

  /// Sets the optimization level used to tune loop unrolling.
  /// @param O Optimization level used to tune unrolling decisions.
  /// @return Reference to this options object for chaining.
  LoopUnrollOptions &setOptLevel(int O) {
    OptLevel = O;
    return *this;
  }

  /// Enables or disables loop peeling based on profile information.
  /// @param O Non-zero to enable profile-based peeling.
  /// @return Reference to this options object for chaining.
  LoopUnrollOptions &setProfileBasedPeeling(int O) {
    AllowProfileBasedPeeling = O;
    return *this;
  }

  /// Sets the maximum full-unroll count.
  /// @param O Maximum full-unroll trip count.
  /// @return Reference to this options object for chaining.
  LoopUnrollOptions &setFullUnrollMaxCount(unsigned O) {
    FullUnrollMaxCount = O;
    return *this;
  }

  /// Sets whether to prepare for LTO by deferring unroll of inline candidates.
  /// @param V True to prepare for LTO by deferring unroll of inline
  /// candidates.
  /// @return Reference to this options object for chaining.
  LoopUnrollOptions &setPrepareForLTO(bool V) {
    PrepareForLTO = V;
    return *this;
  }
};

/// Loop unroll pass that supports both full and partial unrolling.
///
/// It is a function pass to have access to function and module analyses.
/// It will also put loops into canonical form (simplified and LCSSA).
class LoopUnrollPass : public OptionalPassInfoMixin<LoopUnrollPass> {
  LoopUnrollOptions UnrollOpts;

public:
  /// Construct a LoopUnroll pass with the given options.
  ///
  /// This uses the target information (or flags) to control the thresholds for
  /// different unrolling stategies but supports all of them.
  /// @param UnrollOpts Configuration controlling which unroll transforms run.
  explicit LoopUnrollPass(LoopUnrollOptions UnrollOpts = {})
      : UnrollOpts(UnrollOpts) {}

  /// Run full and partial loop unrolling over the function.
  /// @param F Function whose loops may be unrolled.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
  /// Print this pass's pipeline representation to \p OS.
  /// @param OS Stream to write the pipeline string to.
  /// @param MapClassName2PassName Maps class names to pass names.
  LLVM_ABI void
  printPipeline(raw_ostream &OS,
                function_ref<StringRef(StringRef)> MapClassName2PassName);
};

} // end namespace llvm

#endif // LLVM_TRANSFORMS_SCALAR_LOOPUNROLLPASS_H
