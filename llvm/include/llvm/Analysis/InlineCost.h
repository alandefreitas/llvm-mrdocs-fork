//===- InlineCost.h - Cost analysis for inliner -----------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements heuristics for inlining decisions.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_ANALYSIS_INLINECOST_H
#define LLVM_ANALYSIS_INLINECOST_H

#include "llvm/ADT/APInt.h"
#include "llvm/ADT/STLFunctionalExtras.h"
#include "llvm/Analysis/InlineModelFeatureMaps.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/Compiler.h"
#include <cassert>
#include <climits>
#include <optional>

namespace llvm {
class AssumptionCache;
class OptimizationRemarkEmitter;
class BlockFrequencyInfo;
class CallBase;
class DataLayout;
class Function;
class ProfileSummaryInfo;
class TargetTransformInfo;
class TargetLibraryInfo;
class EphemeralValuesCache;

/// Thresholds and magic constants used by inline cost analysis.
namespace InlineConstants {
// Various thresholds used by inline cost analysis.
/// Use when optsize (-Os) is specified.
const int OptSizeThreshold = 50;

/// Use when minsize (-Oz) is specified.
const int OptMinSizeThreshold = 5;

/// Use when -O3 is specified.
const int OptAggressiveThreshold = 250;

// Various magic constants used to adjust heuristics.
/// Return the cost assigned to a single instruction during inlining.
/// @return The cost assigned to a single instruction during inlining.
LLVM_ABI int getInstrCost();
/// Threshold used when estimating the benefit of inlining an indirect call.
const int IndirectCallThreshold = 100;
/// Extra cost charged for each loop when the caller is optimized for minsize.
const int LoopPenalty = 25;
/// Extra cost charged when inlining a callee that uses coldcc.
const int ColdccPenalty = 2000;
/// Do not inline functions which allocate this many bytes on the stack
/// when the caller is recursive.
const unsigned TotalAllocaSizeRecursiveCaller = 1024;
/// Do not inline dynamic allocas that have been constant propagated to be
/// static allocas above this amount in bytes.
const uint64_t MaxSimplifiedDynamicAllocaToInline = 65536;

/// Function attribute that multiplies the computed inline cost of a callsite.
const char FunctionInlineCostMultiplierAttributeName[] =
    "function-inline-cost-multiplier";

/// Function attribute that caps the callee stack size allowed when inlining.
const char MaxInlineStackSizeAttributeName[] = "inline-max-stacksize";
} // namespace InlineConstants

/// Pair of size cost and estimated cycle savings from cost-benefit analysis.
class CostBenefitPair {
public:
  /// Construct a cost-benefit pair from the given size and savings.
  /// @param Cost Estimated size cost of inlining.
  /// @param Benefit Estimated cycle savings from inlining.
  CostBenefitPair(APInt Cost, APInt Benefit)
      : Cost(std::move(Cost)), Benefit(std::move(Benefit)) {}

  /// Return the estimated size cost of inlining.
  /// @return The estimated size cost of inlining.
  const APInt &getCost() const { return Cost; }

  /// Return the estimated cycle savings from inlining.
  /// @return The estimated cycle savings from inlining.
  const APInt &getBenefit() const { return Benefit; }

private:
  APInt Cost;
  APInt Benefit;
};

/// Represents the cost of inlining a function.
///
/// This supports special values for functions which should "always" or
/// "never" be inlined. Otherwise, the cost represents a unitless amount;
/// smaller values increase the likelihood of the function being inlined.
///
/// Objects of this type also provide the adjusted threshold for inlining
/// based on the information available for a particular callsite. They can be
/// directly tested to determine if inlining should occur given the cost and
/// threshold for this cost metric.
class InlineCost {
  enum SentinelValues { AlwaysInlineCost = INT_MIN, NeverInlineCost = INT_MAX };

  /// The estimated cost of inlining this callsite.
  int Cost = 0;

  /// The adjusted threshold against which this cost was computed.
  int Threshold = 0;

  /// The amount of StaticBonus that has been applied.
  int StaticBonusApplied = 0;

  /// Must be set for Always and Never instances.
  const char *Reason = nullptr;

  /// The cost-benefit pair computed by cost-benefit analysis.
  std::optional<CostBenefitPair> CostBenefit;

  // Trivial constructor, interesting logic in the factory functions below.
  InlineCost(int Cost, int Threshold, int StaticBonusApplied,
             const char *Reason = nullptr,
             std::optional<CostBenefitPair> CostBenefit = std::nullopt)
      : Cost(Cost), Threshold(Threshold),
        StaticBonusApplied(StaticBonusApplied), Reason(Reason),
        CostBenefit(CostBenefit) {
    assert((isVariable() || Reason) &&
           "Reason must be provided for Never or Always");
  }

public:
  /// Create an InlineCost with a variable cost and threshold.
  /// @param Cost Estimated inlining cost; must not be a sentinel value.
  /// @param Threshold Adjusted threshold against which \p Cost is compared.
  /// @param StaticBonus Amount of static bonus already applied to the cost.
  /// @return An InlineCost with the given variable cost and threshold.
  static InlineCost get(int Cost, int Threshold, int StaticBonus = 0) {
    assert(Cost > AlwaysInlineCost && "Cost crosses sentinel value");
    assert(Cost < NeverInlineCost && "Cost crosses sentinel value");
    return InlineCost(Cost, Threshold, StaticBonus);
  }
  /// Create an InlineCost that always recommends inlining.
  /// @param Reason Human-readable explanation for the always-inline decision.
  /// @param CostBenefit Optional cost-benefit pair associated with the
  /// decision.
  /// @return An InlineCost that always recommends inlining.
  static InlineCost
  getAlways(const char *Reason,
            std::optional<CostBenefitPair> CostBenefit = std::nullopt) {
    return InlineCost(AlwaysInlineCost, 0, 0, Reason, CostBenefit);
  }
  /// Create an InlineCost that never recommends inlining.
  /// @param Reason Human-readable explanation for the never-inline decision.
  /// @param CostBenefit Optional cost-benefit pair associated with the
  /// decision.
  /// @return An InlineCost that never recommends inlining.
  static InlineCost
  getNever(const char *Reason,
           std::optional<CostBenefitPair> CostBenefit = std::nullopt) {
    return InlineCost(NeverInlineCost, 0, 0, Reason, CostBenefit);
  }

  /// Test whether the inline cost is low enough for inlining.
  /// @return True if the inline cost is low enough for inlining.
  explicit operator bool() const { return Cost < Threshold; }

  /// Return true if this cost is the always-inline sentinel.
  /// @return True if this cost is the always-inline sentinel.
  bool isAlways() const { return Cost == AlwaysInlineCost; }
  /// Return true if this cost is the never-inline sentinel.
  /// @return True if this cost is the never-inline sentinel.
  bool isNever() const { return Cost == NeverInlineCost; }
  /// Return true if this cost is a finite estimate rather than a sentinel.
  /// @return True if this cost is a finite estimate rather than a sentinel.
  bool isVariable() const { return !isAlways() && !isNever(); }

  /// Get the inline cost estimate.
  /// It is an error to call this on an "always" or "never" InlineCost.
  /// @return The estimated inlining cost.
  int getCost() const {
    assert(isVariable() && "Invalid access of InlineCost");
    return Cost;
  }

  /// Get the threshold against which the cost was computed
  /// @return The adjusted threshold against which the cost was computed.
  int getThreshold() const {
    assert(isVariable() && "Invalid access of InlineCost");
    return Threshold;
  }

  /// Get the amount of StaticBonus applied.
  /// @return The amount of static bonus applied to the cost.
  int getStaticBonusApplied() const {
    assert(isVariable() && "Invalid access of InlineCost");
    return StaticBonusApplied;
  }

  /// Get the cost-benefit pair which was computed by cost-benefit analysis
  /// @return The cost-benefit pair from cost-benefit analysis, if available.
  std::optional<CostBenefitPair> getCostBenefit() const { return CostBenefit; }

  /// Get the reason of Always or Never.
  /// @return The reason string for an Always or Never decision.
  const char *getReason() const {
    assert((Reason || isVariable()) &&
           "InlineCost reason must be set for Always or Never");
    return Reason;
  }

  /// Get the cost delta from the threshold for inlining.
  /// Only valid if the cost is of the variable kind. Returns a negative
  /// value if the cost is too high to inline.
  /// @return The threshold minus cost; negative if too expensive to inline.
  int getCostDelta() const { return Threshold - getCost(); }
};

/// InlineResult is basically true or false. For false results the message
/// describes a reason.
class InlineResult {
  const char *Message = nullptr;
  InlineResult(const char *Message = nullptr) : Message(Message) {}

public:
  /// Create a successful inlining result.
  /// @return A successful InlineResult.
  static InlineResult success() { return {}; }
  /// Create a failed inlining result with the given reason.
  /// @param Reason Human-readable explanation of why inlining cannot occur.
  /// @return A failed InlineResult with \p Reason.
  static InlineResult failure(const char *Reason) {
    return InlineResult(Reason);
  }
  /// Return true if inlining is allowed by this result.
  /// @return True if inlining is allowed by this result.
  bool isSuccess() const { return Message == nullptr; }
  /// Return the reason string for a failed inlining decision.
  ///
  /// It is an error to call this on a successful InlineResult.
  /// @return The human-readable reason for the failed inlining decision.
  const char *getFailureReason() const {
    assert(!isSuccess() &&
           "getFailureReason should only be called in failure cases");
    return Message;
  }
};

/// Parameters that tune thresholds used by inline cost analysis.
///
/// The inline cost analysis decides the condition to apply a threshold and
/// applies it. Otherwise, DefaultThreshold is used. If a threshold is Optional,
/// it is applied only when it has a valid value. Typically, users of inline
/// cost analysis obtain an InlineParams object through one of the
/// \c getInlineParams methods and pass it to \c getInlineCost. Some specialized
/// versions of inliner (such as the pre-inliner) might have custom logic to
/// compute \c InlineParams object.

struct InlineParams {
  /// The default threshold to start with for a callee.
  int DefaultThreshold = -1;

  /// Threshold to use for callees with inline hint.
  std::optional<int> HintThreshold;

  /// Threshold to use for callees with inline hint, when the caller is
  /// optimized for size.
  std::optional<int> OptSizeHintThreshold;

  /// Threshold to use for cold callees.
  std::optional<int> ColdThreshold;

  /// Threshold to use when the caller is optimized for size.
  std::optional<int> OptSizeThreshold;

  /// Threshold to use when the caller is optimized for minsize.
  std::optional<int> OptMinSizeThreshold;

  /// Threshold to use when the callsite is considered hot.
  std::optional<int> HotCallSiteThreshold;

  /// Threshold to use when the callsite is considered hot relative to function
  /// entry.
  std::optional<int> LocallyHotCallSiteThreshold;

  /// Threshold to use when the callsite is considered cold.
  std::optional<int> ColdCallSiteThreshold;

  /// Compute inline cost even when the cost has exceeded the threshold.
  std::optional<bool> ComputeFullInlineCost;

  /// Indicate whether we should allow inline deferral.
  std::optional<bool> EnableDeferral;

  /// Indicate whether we allow inlining for recursive call.
  std::optional<bool> AllowRecursiveCall = false;
};

/// Parse a function string attribute on \p CB as a decimal integer.
/// @param CB Call site whose function attributes are inspected.
/// @param AttrKind Name of the string attribute to parse.
/// @return The parsed integer, or std::nullopt if the attribute is missing
/// or not a valid integer.
LLVM_ABI std::optional<int> getStringFnAttrAsInt(CallBase &CB,
                                                 StringRef AttrKind);

/// Generate the parameters to tune the inline cost analysis based only on the
/// commandline options.
/// @return InlineParams derived from command-line options.
LLVM_ABI InlineParams getInlineParams();

/// Generate the parameters to tune the inline cost analysis based on command
/// line options.
///
/// If -inline-threshold option is not explicitly passed, \p Threshold is used
/// as the default threshold.
/// @param Threshold Default inline threshold when -inline-threshold is unset.
/// @return InlineParams with \p Threshold as the default when unset.
LLVM_ABI InlineParams getInlineParams(int Threshold);

/// Generate the parameters to tune the inline cost analysis based on command
/// line options.
///
/// If -inline-threshold option is not explicitly passed, the default threshold
/// is computed from \p OptLevel.
/// An \p OptLevel value above 3 is considered an aggressive optimization mode.
/// Optimization for size is handled via separate thresholds for
/// optsize/minsize, rather than changes to the default threshold.
/// @param OptLevel Optimization level used to pick the default threshold.
/// @return InlineParams with a default threshold derived from \p OptLevel.
LLVM_ABI InlineParams getInlineParamsFromOptLevel(unsigned OptLevel);

/// Return the cost associated with a callsite, including parameter passing
/// and the call/return instruction.
/// @param TTI Target transform info used for the call penalty.
/// @param Call Call site whose setup cost is estimated.
/// @param DL Data layout used to size byval arguments.
/// @return The estimated cost of the call setup and parameter passing.
LLVM_ABI int getCallsiteCost(const TargetTransformInfo &TTI,
                             const CallBase &Call, const DataLayout &DL);

/// Get an InlineCost object representing the cost of inlining this
/// callsite.
///
/// Note that a default threshold is passed into this function. This threshold
/// could be modified based on callsite's properties and only costs below this
/// new threshold are computed with any accuracy. The new threshold can be
/// used to bound the computation necessary to determine whether the cost is
/// sufficiently low to warrant inlining.
///
/// Also note that calling this function *dynamically* computes the cost of
/// inlining the callsite. It is an expensive, heavyweight call.
/// @param Call Call site whose inlining cost is computed.
/// @param Params Thresholds and knobs that tune the cost analysis.
/// @param CalleeTTI Target transform info for the callee.
/// @param GetAssumptionCache Callback returning the assumption cache for a
/// function.
/// @param GetTLI Callback returning target library info for a function.
/// @param GetBFI Optional callback returning block frequency info, or null.
/// @param PSI Optional profile summary used for hot/cold callsite thresholds.
/// @param ORE Optional remark emitter for inlining diagnostics.
/// @param GetEphValuesCache Optional callback returning the ephemeral values
/// cache for a function.
/// @return The computed InlineCost for \p Call.
LLVM_ABI InlineCost getInlineCost(
    CallBase &Call, const InlineParams &Params, TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
    ProfileSummaryInfo *PSI = nullptr, OptimizationRemarkEmitter *ORE = nullptr,
    function_ref<EphemeralValuesCache &(Function &)> GetEphValuesCache =
        nullptr);

/// Get an InlineCost with the callee explicitly specified.
///
/// This allows you to calculate the cost of inlining a function via a
/// pointer. This behaves exactly as the version with no explicit callee
/// parameter in all other respects.
/// @param Call Call site whose inlining cost is computed.
/// @param Callee Callee to consider; may differ from the called function.
/// @param Params Thresholds and knobs that tune the cost analysis.
/// @param CalleeTTI Target transform info for the callee.
/// @param GetAssumptionCache Callback returning the assumption cache for a
/// function.
/// @param GetTLI Callback returning target library info for a function.
/// @param GetBFI Optional callback returning block frequency info, or null.
/// @param PSI Optional profile summary used for hot/cold callsite thresholds.
/// @param ORE Optional remark emitter for inlining diagnostics.
/// @param GetEphValuesCache Optional callback returning the ephemeral values
/// cache for a function.
/// @return The computed InlineCost for inlining \p Callee at \p Call.
LLVM_ABI InlineCost getInlineCost(
    CallBase &Call, Function *Callee, const InlineParams &Params,
    TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
    ProfileSummaryInfo *PSI = nullptr, OptimizationRemarkEmitter *ORE = nullptr,
    function_ref<EphemeralValuesCache &(Function &)> GetEphValuesCache =
        nullptr);

/// Decide inlining from user directives and viability, without cost modeling.
///
/// Returns InlineResult::success() if the call site should be always inlined
/// because of user directives, and the inlining is viable. Returns
/// InlineResult::failure() if the inlining may never happen because of user
/// directives or incompatibilities detectable without needing callee traversal.
/// Otherwise returns std::nullopt, meaning that inlining should be decided
/// based on other criteria (e.g. cost modeling).
/// @param Call Call site whose attributes are inspected.
/// @param Callee Callee function, or null for an indirect call.
/// @param CalleeTTI Target transform info used to check inline compatibility.
/// @param GetTLI Callback returning target library info for a function.
/// @return Always/never decision from directives, or std::nullopt to defer to
/// cost modeling.
LLVM_ABI std::optional<InlineResult> getAttributeBasedInliningDecision(
    CallBase &Call, Function *Callee, TargetTransformInfo &CalleeTTI,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI);

/// Get the cost estimate ignoring thresholds.
///
/// This is similar to getInlineCost when passed
/// InlineParams::ComputeFullInlineCost, or a non-null ORE. It uses default
/// InlineParams otherwise.
/// Contrary to getInlineCost, which makes a threshold-based final evaluation of
/// should/shouldn't inline, captured in InlineResult, getInliningCostEstimate
/// returns:
/// - std::nullopt, if the inlining cannot happen (is illegal)
/// - an integer, representing the cost.
/// @param Call Call site whose inlining cost is estimated.
/// @param CalleeTTI Target transform info for the callee.
/// @param GetAssumptionCache Callback returning the assumption cache for a
/// function.
/// @param GetBFI Optional callback returning block frequency info, or null.
/// @param GetTLI Optional callback returning target library info, or null.
/// @param PSI Optional profile summary used for hot/cold callsite thresholds.
/// @param ORE Optional remark emitter for inlining diagnostics.
/// @return The estimated cost, or std::nullopt if inlining is illegal.
LLVM_ABI std::optional<int> getInliningCostEstimate(
    CallBase &Call, TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI = nullptr,
    ProfileSummaryInfo *PSI = nullptr,
    OptimizationRemarkEmitter *ORE = nullptr);

/// Get the expanded cost features. The features are returned unconditionally,
/// even if inlining is impossible.
/// @param Call Call site whose cost features are collected.
/// @param CalleeTTI Target transform info for the callee.
/// @param GetAssumptionCache Callback returning the assumption cache for a
/// function.
/// @param GetBFI Optional callback returning block frequency info, or null.
/// @param GetTLI Optional callback returning target library info, or null.
/// @param PSI Optional profile summary used for hot/cold callsite thresholds.
/// @param ORE Optional remark emitter for inlining diagnostics.
/// @return The expanded cost features for \p Call.
LLVM_ABI std::optional<InlineCostFeatures> getInliningCostFeatures(
    CallBase &Call, TargetTransformInfo &CalleeTTI,
    function_ref<AssumptionCache &(Function &)> GetAssumptionCache,
    function_ref<BlockFrequencyInfo &(Function &)> GetBFI = nullptr,
    function_ref<const TargetLibraryInfo &(Function &)> GetTLI = nullptr,
    ProfileSummaryInfo *PSI = nullptr,
    OptimizationRemarkEmitter *ORE = nullptr);

/// Check if it is mechanically possible to inline the function \p Callee, based
/// on the contents of the function.
///
/// See also \p CanInlineCallSite as an additional precondition necessary to
/// perform a valid inline in a particular use context.
/// @param Callee Function whose body is checked for mechanical inlinability.
/// @return Success if \p Callee is mechanically inlinable; otherwise failure
/// with a reason.
LLVM_ABI InlineResult isInlineViable(Function &Callee);

/// Pass that prints per-instruction inline cost annotations for debugging.
///
/// This pass is used to annotate instructions during the inline process for
/// debugging and analysis. The main purpose of the pass is to see and test
/// inliner's decisions when creating new optimizations to InlineCost.
struct InlineCostAnnotationPrinterPass
    : RequiredPassInfoMixin<InlineCostAnnotationPrinterPass> {
  /// Stream that receives the inline-cost annotation dump.
  raw_ostream &OS;

public:
  /// Construct a printer that writes annotations to \p OS.
  /// @param OS Stream that receives the annotation output.
  explicit InlineCostAnnotationPrinterPass(raw_ostream &OS) : OS(OS) {}
  /// Print inline-cost annotations for every viable call in \p F.
  /// @param F Function whose call sites are analyzed and printed.
  /// @param FAM Function analysis manager providing supporting analyses.
  /// @return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &FAM);
};
} // namespace llvm

#endif
