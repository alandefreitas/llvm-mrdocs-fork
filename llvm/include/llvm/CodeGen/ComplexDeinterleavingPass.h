//===- ComplexDeinterleavingPass.h - Complex Deinterleaving Pass *- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This pass implements generation of target-specific intrinsics to support
// handling of complex number arithmetic and deinterleaving.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_COMPLEXDEINTERLEAVING_H
#define LLVM_CODEGEN_COMPLEXDEINTERLEAVING_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class TargetMachine;

/// New PM pass that generates target intrinsics for complex arithmetic.
///
/// Matches interleaved complex-number patterns and replaces them with
/// target-specific complex deinterleaving intrinsics when supported.
struct ComplexDeinterleavingPass
    : public OptionalPassInfoMixin<ComplexDeinterleavingPass> {
private:
  const TargetMachine *TM;

public:
  /// Construct a complex-deinterleaving pass for target machine \p TM.
  /// \param TM Target machine used to query complex-deinterleaving support.
  ComplexDeinterleavingPass(const TargetMachine &TM) : TM(&TM) {}

  /// Run complex deinterleaving on function \p F.
  /// \param F Function to transform.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved by this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

/// Kind of complex arithmetic or graph node used by complex deinterleaving.
enum class ComplexDeinterleavingOperation {
  /// Complex addition of interleaved real/imaginary pairs.
  CAdd,
  /// Partial complex multiply used while building a full multiply.
  CMulPartial,
  /// Complex dot-product reduction over interleaved vectors.
  CDot,
  /// Internal deinterleave of real and imaginary lanes (not for backends).
  Deinterleave,
  /// Internal splat of a scalar into a vector (not for backends).
  Splat,
  /// Internal symmetric real/imaginary pairwise operation (not for backends).
  Symmetric,
  /// Internal reduction PHI for loop-carried complex accumulators (not for
  /// backends).
  ReductionPHI,
  /// Internal complex reduction operation within a loop (not for backends).
  ReductionOperation,
  /// Internal select that participates in a complex reduction (not for
  /// backends).
  ReductionSelect,
  /// Internal single (non-paired) reduction value (not for backends).
  ReductionSingle
};

/// Rotation applied to a complex value in the complex plane.
enum class ComplexDeinterleavingRotation {
  /// No rotation (0 degrees).
  Rotation_0 = 0,
  /// Rotation by 90 degrees (multiply by \c i).
  Rotation_90 = 1,
  /// Rotation by 180 degrees (negate).
  Rotation_180 = 2,
  /// Rotation by 270 degrees (multiply by \c -i).
  Rotation_270 = 3,
};

} // namespace llvm

#endif // LLVM_CODEGEN_COMPLEXDEINTERLEAVING_H
