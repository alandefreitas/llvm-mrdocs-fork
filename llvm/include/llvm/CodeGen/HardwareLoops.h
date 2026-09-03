//===- HardwareLoops.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Defines an IR pass for the creation of hardware loops.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_HARDWARELOOPS_H
#define LLVM_CODEGEN_HARDWARELOOPS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

/// Options that control how the hardware-loops pass inserts loop intrinsics.
struct HardwareLoopOptions {
  /// Optional per-iteration decrement applied to the hardware loop counter.
  std::optional<unsigned> Decrement;
  /// Optional bit width of the hardware loop counter type.
  std::optional<unsigned> Bitwidth;
  /// When set, force hardware-loop intrinsics even if the target declines.
  std::optional<bool> Force;
  /// When set, force the loop counter to be updated through a phi.
  std::optional<bool> ForcePhi;
  /// When set, allow nested hardware loops even if nesting is not profitable.
  std::optional<bool> ForceNested;
  /// When set, force generation of a hardware-loop guard intrinsic.
  std::optional<bool> ForceGuard;

  /// Set the hardware-loop decrement amount to \p Count.
  /// \param Count Value subtracted from the loop counter each iteration.
  /// \return Reference to this options object for chaining.
  HardwareLoopOptions &setDecrement(unsigned Count) {
    Decrement = Count;
    return *this;
  }
  /// Set the hardware-loop counter bit width to \p Width.
  /// \param Width Bit width of the integer type used for the loop counter.
  /// \return Reference to this options object for chaining.
  HardwareLoopOptions &setCounterBitwidth(unsigned Width) {
    Bitwidth = Width;
    return *this;
  }
  /// Set whether hardware-loop insertion is forced regardless of profitability.
  /// \param Force If true, insert hardware-loop intrinsics even when the
  ///        target would otherwise decline.
  /// \return Reference to this options object for chaining.
  HardwareLoopOptions &setForce(bool Force) {
    this->Force = Force;
    return *this;
  }
  /// Set whether the hardware-loop counter must be updated through a phi.
  /// \param Force If true, force a phi-updated counter instead of a target-
  ///        opaque decrement.
  /// \return Reference to this options object for chaining.
  HardwareLoopOptions &setForcePhi(bool Force) {
    ForcePhi = Force;
    return *this;
  }
  /// Set whether nested hardware loops are forced to be considered.
  /// \param Force If true, allow nested hardware loops even when nesting is
  ///        not normally legal or profitable.
  /// \return Reference to this options object for chaining.
  HardwareLoopOptions &setForceNested(bool Force) {
    ForceNested = Force;
    return *this;
  }
  /// Set whether a hardware-loop guard intrinsic must be generated.
  /// \param Force If true, force emission of a loop-guard intrinsic at entry.
  /// \return Reference to this options object for chaining.
  HardwareLoopOptions &setForceGuard(bool Force) {
    ForceGuard = Force;
    return *this;
  }
  /// Return whether a phi-updated hardware-loop counter is forced.
  /// \return True if \c ForcePhi is set and true; false if unset or false.
  bool getForcePhi() const {
    return ForcePhi.has_value() && ForcePhi.value();
  }
  /// Return whether nested hardware loops are forced.
  /// \return True if \c ForceNested is set and true; false if unset or false.
  bool getForceNested() const {
    return ForceNested.has_value() && ForceNested.value();
  }
  /// Return whether a hardware-loop guard intrinsic is forced.
  /// \return True if \c ForceGuard is set and true; false if unset or false.
  bool getForceGuard() const {
    return ForceGuard.has_value() && ForceGuard.value();
  }
};

/// New PM pass that inserts hardware-loop intrinsics into profitable loops.
class HardwareLoopsPass : public OptionalPassInfoMixin<HardwareLoopsPass> {
  HardwareLoopOptions Opts;

public:
  /// Construct a hardware-loops pass with the given \p Opts.
  /// \param Opts Configuration controlling counter shape and force flags.
  explicit HardwareLoopsPass(HardwareLoopOptions Opts = {})
    : Opts(Opts) { }

  /// Insert hardware-loop intrinsics into eligible loops in \p F.
  /// \param F Function whose loops are considered for conversion.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_HARDWARELOOPS_H
