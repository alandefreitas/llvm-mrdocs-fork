//===- SimplifyCFGOptions.h - Control structure for SimplifyCFG -*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// A set of parameters used to control the transforms in the SimplifyCFG pass.
// Options may change depending on the position in the optimization pipeline.
// For example, canonical form that includes switches and branches may later be
// replaced by lookup tables and selects.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_UTILS_SIMPLIFYCFGOPTIONS_H
#define LLVM_TRANSFORMS_UTILS_SIMPLIFYCFGOPTIONS_H

namespace llvm {

class AssumptionCache;

/// Options that control transforms performed by the SimplifyCFG pass.
///
/// Options may change depending on the position in the optimization pipeline.
/// For example, canonical form that includes switches and branches may later be
/// replaced by lookup tables and selects.
struct SimplifyCFGOptions {
  /// Maximum bonus instructions allowed when folding a branch to a common
  /// destination.
  int BonusInstThreshold = 1;
  /// Forward the switch condition into PHI operands when profitable.
  bool ForwardSwitchCondToPhi = false;
  /// Convert switch ranges into integer comparisons.
  bool ConvertSwitchRangeToICmp = false;
  /// Convert switches into arithmetic when profitable.
  bool ConvertSwitchToArithmetic = false;
  /// Convert switches into lookup tables when profitable.
  bool ConvertSwitchToLookupTable = false;
  /// Preserve canonical loop structure when simplifying.
  bool NeedCanonicalLoop = true;
  /// Hoist identical instructions from successor blocks.
  bool HoistCommonInsts = false;
  /// Hoist loads/stores when the target supports conditional faulting.
  bool HoistLoadsStoresWithCondFaulting = false;
  /// Sink identical instructions into a common successor.
  bool SinkCommonInsts = false;
  /// Simplify conditional branches.
  bool SimplifyCondBranch = true;
  /// Speculate (fold) blocks when the cost model allows.
  bool SpeculateBlocks = true;
  /// Speculate branches marked as unpredictable.
  bool SpeculateUnpredictables = false;

  /// Optional assumption cache used by SimplifyCFG transforms.
  AssumptionCache *AC = nullptr;

  // Support 'builder' pattern to set members by name at construction time.
  /// Set the bonus-instruction threshold used when folding branches.
  /// @param I Maximum bonus instructions allowed when folding.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &bonusInstThreshold(int I) {
    BonusInstThreshold = I;
    return *this;
  }
  /// Enable or disable forwarding switch conditions to PHI operands.
  /// @param B True to forward switch conditions into PHIs.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &forwardSwitchCondToPhi(bool B) {
    ForwardSwitchCondToPhi = B;
    return *this;
  }
  /// Enable or disable converting switch ranges to icmp.
  /// @param B True to convert switch ranges into integer comparisons.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &convertSwitchRangeToICmp(bool B) {
    ConvertSwitchRangeToICmp = B;
    return *this;
  }
  /// Enable or disable converting switches to arithmetic.
  /// @param B True to convert switches into arithmetic.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &convertSwitchToArithmetic(bool B) {
    ConvertSwitchToArithmetic = B;
    return *this;
  }
  /// Enable or disable converting switches to lookup tables.
  /// @param B True to convert switches into lookup tables.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &convertSwitchToLookupTable(bool B) {
    ConvertSwitchToLookupTable = B;
    return *this;
  }
  /// Enable or disable preserving canonical loop structure.
  /// @param B True to preserve canonical loop structure.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &needCanonicalLoops(bool B) {
    NeedCanonicalLoop = B;
    return *this;
  }
  /// Enable or disable hoisting common instructions from successors.
  /// @param B True to hoist identical instructions from successors.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &hoistCommonInsts(bool B) {
    HoistCommonInsts = B;
    return *this;
  }
  /// Enable or disable hoisting loads/stores with conditional faulting.
  /// @param B True to hoist loads/stores when conditional faulting is
  /// supported.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &hoistLoadsStoresWithCondFaulting(bool B) {
    HoistLoadsStoresWithCondFaulting = B;
    return *this;
  }
  /// Enable or disable sinking common instructions into a successor.
  /// @param B True to sink identical instructions into a common successor.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &sinkCommonInsts(bool B) {
    SinkCommonInsts = B;
    return *this;
  }
  /// Set the assumption cache used by SimplifyCFG transforms.
  /// @param Cache Assumption cache to consult, or nullptr.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &setAssumptionCache(AssumptionCache *Cache) {
    AC = Cache;
    return *this;
  }
  /// Enable or disable simplification of conditional branches.
  /// @param B True to simplify conditional branches.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &setSimplifyCondBranch(bool B) {
    SimplifyCondBranch = B;
    return *this;
  }

  /// Enable or disable speculative folding of blocks.
  /// @param B True to speculate (fold) blocks when profitable.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &speculateBlocks(bool B) {
    SpeculateBlocks = B;
    return *this;
  }
  /// Enable or disable speculation of unpredictable branches.
  /// @param B True to speculate branches marked as unpredictable.
  /// @return Reference to this options object for chaining.
  SimplifyCFGOptions &speculateUnpredictables(bool B) {
    SpeculateUnpredictables = B;
    return *this;
  }
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_UTILS_SIMPLIFYCFGOPTIONS_H
