//===-- Float2Int.h - Demote floating point ops to work on integers -------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides the Float2Int pass, which aims to demote floating
// point operations to work on integers, where that is losslessly possible.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H
#define LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H

#include "llvm/ADT/EquivalenceClasses.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/PassManager.h"

namespace llvm {
class DominatorTree;
class Function;
class Instruction;
class LLVMContext;
class Type;
class Value;

/// Pass that demotes lossless floating-point operations to integers.
///
/// Walks from float-to-int roots (\c fptoui, \c fptosi, \c fcmp) back through
/// mappable FP arithmetic to integer-to-float sources, then rewrites those
/// graphs with equivalent integer operations when the conversion is exact.
class Float2IntPass : public OptionalPassInfoMixin<Float2IntPass> {
public:
  /// Run float-to-int demotion over the function.
  /// @param F Function whose floating-point operations may be demoted.
  /// @param AM Function analysis manager providing analyses for the pass.
  /// @return The set of analyses preserved after running this pass.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);

  /// Run float-to-int demotion using already-fetched analyses (legacy PM glue).
  /// @param F Function whose floating-point operations may be demoted.
  /// @param DT Dominator tree used to find conversion roots.
  /// @return True if the function was modified.
  LLVM_ABI bool runImpl(Function &F, const DominatorTree &DT);

private:
  void findRoots(Function &F, const DominatorTree &DT);
  void seen(Instruction *I, ConstantRange R);
  ConstantRange badRange();
  ConstantRange unknownRange();
  ConstantRange validateRange(ConstantRange R);
  std::optional<ConstantRange> calcRange(Instruction *I);
  void walkBackwards();
  void walkForwards();
  bool validateAndTransform(const DataLayout &DL);
  Value *convert(Instruction *I, Type *ToTy);
  void cleanup();

  MapVector<Instruction *, ConstantRange> SeenInsts;
  SmallSetVector<Instruction *, 8> Roots;
  EquivalenceClasses<Instruction *> ECs;
  MapVector<Instruction *, Value *> ConvertedInsts;
  LLVMContext *Ctx;
};
}
#endif // LLVM_TRANSFORMS_SCALAR_FLOAT2INT_H
