//===-- llvm/CodeGen/MachineCombinerPattern.h - Instruction pattern supported by
// combiner  ------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines instruction pattern supported by combiner
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_MACHINECOMBINERPATTERN_H
#define LLVM_CODEGEN_MACHINECOMBINERPATTERN_H

namespace llvm {

/// The combiner's goal may differ based on which pattern it is attempting
/// to optimize.
enum class CombinerObjective {
  /// The data dependency chain must be improved.
  MustReduceDepth,
  /// The register pressure must be reduced.
  MustReduceRegisterPressure,
  /// The critical path must not be lengthened.
  Default
};

/// These are instruction patterns matched by the machine combiner pass.
enum MachineCombinerPattern : unsigned {
  // These are commutative variants for reassociating a computation chain. See
  // the comments before getMachineCombinerPatterns() in TargetInstrInfo.cpp.
  /// Reassociate (A op X) op Y into A op (X op Y).
  REASSOC_AX_BY,
  /// Reassociate Y op (A op X) into (Y op X) op A.
  REASSOC_AX_YB,
  /// Reassociate (X op A) op Y into (X op Y) op A.
  REASSOC_XA_BY,
  /// Reassociate Y op (X op A) into (Y op X) op A.
  REASSOC_XA_YB,
  /// Rewrite an accumulator chain as a tree to increase ILP.
  ACC_CHAIN,

  /// First value reserved for target-specific combiner patterns.
  TARGET_PATTERN_START
};

} // end namespace llvm

#endif
