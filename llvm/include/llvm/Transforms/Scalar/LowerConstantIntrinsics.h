//===- LowerConstantIntrinsics.h - Lower constant int. pass -*- C++ -*-========//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// The header file for the LowerConstantIntrinsics pass as used by the new pass
/// manager.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_LOWERCONSTANTINTRINSICS_H
#define LLVM_TRANSFORMS_SCALAR_LOWERCONSTANTINTRINSICS_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class DominatorTree;
class Function;
class TargetLibraryInfo;

/// Lower remaining objectsize and is.constant intrinsic calls in a function.
///
/// Even when the argument has no known size or is not a constant respectively,
/// these intrinsics are folded to constants. The resulting constants are
/// propagated and conditional branches are resolved where possible.
/// @param F Function whose constant intrinsics may be lowered.
/// @param TLI Target library info used when lowering objectsize.
/// @param DT Optional dominator tree to keep updated, or nullptr.
/// @return True if any intrinsic was lowered.
LLVM_ABI bool lowerConstantIntrinsics(Function &F, const TargetLibraryInfo &TLI,
                                      DominatorTree *DT);

/// Pass that lowers remaining objectsize and is.constant intrinsics.
struct LowerConstantIntrinsicsPass
    : OptionalPassInfoMixin<LowerConstantIntrinsicsPass> {
public:
  /// Construct a pass that lowers constant intrinsics.
  explicit LowerConstantIntrinsicsPass() = default;

  /// Run the pass over the function.
  ///
  /// This will lower all remaining 'objectsize' and 'is.constant'`
  /// intrinsic calls in this function, even when the argument has no known
  /// size or is not a constant respectively. The resulting constant is
  /// propagated and conditional branches are resolved where possible.
  /// This complements the Instruction Simplification and
  /// Instruction Combination passes of the optimized pass chain.
  /// @param F Function whose constant intrinsics may be lowered.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};
}

#endif
