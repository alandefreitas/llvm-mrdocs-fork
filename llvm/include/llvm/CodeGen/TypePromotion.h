//===- TypePromotion.h ------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
/// \file
///
/// Defines an IR pass for type promotion.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CODEGEN_TYPEPROMOTION_H
#define LLVM_CODEGEN_TYPEPROMOTION_H

#include "llvm/IR/PassManager.h"

namespace llvm {

class Function;
class TargetMachine;

/// New PM pass that promotes small integer types that would otherwise be
/// promoted during legalization.
///
/// Works around SelectionDAG limitations for cyclic regions by promoting
/// non-wrapping or safely wrapping instruction trees rooted at icmp operands.
class TypePromotionPass : public OptionalPassInfoMixin<TypePromotionPass> {
private:
  const TargetMachine *TM;

public:
  /// Construct a TypePromotion pass for target machine \p TM.
  /// \param TM Target machine used when deciding which types to promote.
  TypePromotionPass(const TargetMachine &TM) : TM(&TM) {}
  /// Promote small integer types in \p F when profitable for the target.
  /// \param F Function whose values may be type-promoted.
  /// \param AM Function analysis manager providing required analyses.
  /// \return The set of analyses preserved after type promotion.
  LLVM_ABI PreservedAnalyses run(Function &F, FunctionAnalysisManager &AM);
};

} // end namespace llvm

#endif // LLVM_CODEGEN_TYPEPROMOTION_H
